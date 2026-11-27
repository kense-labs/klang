#include "inference.h"
#include "../includes.h"
#include "../utils.h"
#include "../shared_library.h"
#include "../runtime/runtime_tasks.h"

#include <onnxruntime_c_api.h>

#include <charconv>
#include <stdexcept>

using klang::CompileError;
using klang::SharedLibrary;

namespace {

const OrtApi &LoadApi(const SharedLibrary &runtime) {
    auto entry = reinterpret_cast<decltype(&OrtGetApiBase)>(runtime.Symbol("OrtGetApiBase"));
    if (!entry) throw std::runtime_error("the locked runtime has no OrtGetApiBase entry");
    auto base = entry();
    auto api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) throw std::runtime_error("the locked ONNX Runtime does not support the compiled C API version");
    return *api;
}

struct Model {
    // Keep the loaded library through the destruction of all its handles.
    SharedLibrary runtime;
    const OrtApi &api;
    std::unique_ptr<OrtEnv, decltype(OrtApi::ReleaseEnv)> env;
    std::unique_ptr<OrtSession, decltype(OrtApi::ReleaseSession)> session;
    std::unique_ptr<OrtMemoryInfo, decltype(OrtApi::ReleaseMemoryInfo)> memory;
    k_inference_layout layout {};
    std::string input_name, output_name;

    void Check(OrtStatus *status) const {
        if (!status) return;
        std::unique_ptr<OrtStatus, decltype(OrtApi::ReleaseStatus)> error(status, api.ReleaseStatus);
        throw std::runtime_error(api.GetErrorMessage(status));
    }

    template<typename T, typename Release, typename Create>
    auto CreateHandle(Release release, Create &&create) const {
        T *value = nullptr;
        auto status = create(&value);
        std::unique_ptr<T, Release> owned(value, release);
        Check(status);
        return owned;
    }

    k_inference_tensor Tensor(bool input) const {
        auto info = CreateHandle<OrtTypeInfo>(api.ReleaseTypeInfo, [&](auto out) {
            return input ? api.SessionGetInputTypeInfo(session.get(), 0, out)
                         : api.SessionGetOutputTypeInfo(session.get(), 0, out);
        });
        const OrtTensorTypeAndShapeInfo *shape = nullptr;
        Check(api.CastTypeInfoToTensorInfo(info.get(), &shape));
        if (!shape) throw std::runtime_error("the model input and output must be tensors");
        ONNXTensorElementDataType element;
        Check(api.GetTensorElementType(shape, &element));
        if (element != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            throw std::runtime_error("the model input and output must be float32");
        size_t rank;
        Check(api.GetDimensionsCount(shape, &rank));
        if (rank > K_INFERENCE_MAX_RANK) throw std::runtime_error("model tensor rank exceeds the ABI limit");
        k_inference_tensor result {};
        result.rank = (uint32_t)rank;
        result.elements = 1;
        if (rank) Check(api.GetDimensions(shape, result.dimensions, rank));
        for (size_t i = 0; i < rank; ++i) {
            auto axis = result.dimensions[i];
            if (axis <= 0 || axis > K_INFERENCE_MAX_ELEMENTS / result.elements)
                throw std::runtime_error("model shapes must be fixed, positive and within the tensor element limit");
            result.elements *= (uint32_t)axis;
        }
        return result;
    }

    std::string Name(OrtAllocator *allocator, bool input) const {
        auto name = CreateHandle<char>([allocator](char *p) { allocator->Free(allocator, p); }, [&](auto out) {
            return input ? api.SessionGetInputName(session.get(), 0, allocator, out)
                         : api.SessionGetOutputName(session.get(), 0, allocator, out);
        });
        return name.get();
    }

    Model(const char *path, const uint8_t *bytes, size_t length)
        : runtime(path), api(LoadApi(runtime)), env(nullptr, api.ReleaseEnv),
          session(nullptr, api.ReleaseSession), memory(nullptr, api.ReleaseMemoryInfo) {
        env = CreateHandle<OrtEnv>(api.ReleaseEnv, [&](auto out) {
            return api.CreateEnv(ORT_LOGGING_LEVEL_ERROR, "k-cordis", out);
        });
        Check(api.DisableTelemetryEvents(env.get()));
        auto options = CreateHandle<OrtSessionOptions>(api.ReleaseSessionOptions, [&](auto out) {
            return api.CreateSessionOptions(out);
        });
        Check(api.SetSessionExecutionMode(options.get(), ORT_SEQUENTIAL));
        Check(api.SetIntraOpNumThreads(options.get(), 1));
        Check(api.SetInterOpNumThreads(options.get(), 1));
        session = CreateHandle<OrtSession>(api.ReleaseSession, [&](auto out) {
            return api.CreateSessionFromArray(env.get(), bytes, length, options.get(), out);
        });
        size_t inputs, outputs;
        Check(api.SessionGetInputCount(session.get(), &inputs));
        Check(api.SessionGetOutputCount(session.get(), &outputs));
        if (inputs != 1 || outputs != 1)
            throw std::runtime_error("the inference ABI requires exactly one input and one output");
        layout.input = Tensor(true);
        layout.output = Tensor(false);
        OrtAllocator *allocator = nullptr;
        Check(api.GetAllocatorWithDefaultOptions(&allocator));
        input_name = Name(allocator, true);
        output_name = Name(allocator, false);
        memory = CreateHandle<OrtMemoryInfo>(api.ReleaseMemoryInfo, [&](auto out) {
            return api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, out);
        });
    }

    auto RunOptions() const {
        return CreateHandle<OrtRunOptions>(api.ReleaseRunOptions, [&](auto out) { return api.CreateRunOptions(out); });
    }

    bool Arena(k_inference_arena &arena) const {
#if ORT_API_VERSION >= 23
        auto allocator = CreateHandle<OrtAllocator>(api.ReleaseAllocator, [&](auto out) {
            return api.CreateAllocator(session.get(), memory.get(), out);
        });
        auto stats = CreateHandle<OrtKeyValuePairs>(api.ReleaseKeyValuePairs, [&](auto out) {
            return api.AllocatorGetStats(allocator.get(), out);
        });
        auto read = [&](const char *key, uint64_t &value) {
            auto text = api.GetKeyValue(stats.get(), key);
            if (!text) return false;
            auto end = text + strlen(text);
            auto parsed = std::from_chars(text, end, value);
            return parsed.ec == std::errc() && parsed.ptr == end;
        };
        return read("TotalAllocated", arena.reserved_bytes) && read("InUse", arena.in_use_bytes) &&
               read("MaxInUse", arena.peak_in_use_bytes);
#else
        (void)arena;
        return false;
#endif
    }

    void Run(const OrtRunOptions *options, std::vector<float> &input, std::vector<float> &output) const {
        auto tensor = [&](const k_inference_tensor &shape, std::vector<float> &data) {
            return CreateHandle<OrtValue>(api.ReleaseValue, [&](auto out) {
                return api.CreateTensorWithDataAsOrtValue(memory.get(), data.data(), data.size() * sizeof(float),
                        shape.dimensions, shape.rank, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, out);
            });
        };
        auto in = tensor(layout.input, input);
        auto out = tensor(layout.output, output);
        const OrtValue *argument = in.get();
        OrtValue *result = out.get();
        const char *from = input_name.c_str(), *to = output_name.c_str();
        auto status = api.Run(session.get(), options, &from, &argument, 1, &to, 1, &result);
        if (result && result != out.get()) api.ReleaseValue(result);
        Check(status);
        if (result != out.get()) throw std::runtime_error("ONNX Runtime replaced a preallocated output");
    }
};

struct Provider {
    std::unique_ptr<Model> model;
    std::string error;
    bool busy = false;
};

}  // namespace

struct k_inference_request {
    Provider &provider;
    Model &model;
    std::vector<float> input, output;
    std::unique_ptr<OrtRunOptions, decltype(OrtApi::ReleaseRunOptions)> options;
    std::mutex mutex;
    std::thread worker;
    k_task_waker waker {};
    k_foreign_task_ready descriptor {};
    bool owner_open = true, claimed = false, task_live = false;
    bool done = false, cancelled = false, published = false;
    int32_t result = K_INFERENCE_FAILED;

    k_inference_request(Provider &provider, const float *data)
        : provider(provider), model(*provider.model), input(model.layout.input.elements),
          output(model.layout.output.elements), options(model.RunOptions()) {
        memcpy(input.data(), data, input.size() * sizeof(float));
    }

    ~k_inference_request() { provider.busy = false; }

    void Work() {
        int32_t completion = K_INFERENCE_OK;
        try { model.Run(options.get(), input, output); }
        catch (...) { completion = K_INFERENCE_FAILED; }
        std::lock_guard lock(mutex);
        result = completion;
        done = true;
        k_task_wake(waker);
    }

    k_task_poll Poll(void *destination) {
        std::lock_guard lock(mutex);
        if (!cancelled && !done && !worker.joinable()) {
            try { worker = std::thread([this] { Work(); }); }
            catch (...) { done = true; }
        }
        if (!done && worker.joinable()) return K_TASK_POLL_PENDING;
        k_inference_result value { cancelled ? K_INFERENCE_CANCELLED : result, 0 };
        if (value.status == K_INFERENCE_OK) value.written = model.layout.output.elements;
        memcpy(destination, &value, sizeof(value));
        return K_TASK_POLL_READY;
    }

    void Cancel() {
        std::lock_guard lock(mutex);
        if (cancelled) return;
        cancelled = true;
        if (worker.joinable() && !done) {
            auto &api = model.api;
            if (auto error = api.RunOptionsSetTerminate(options.get())) api.ReleaseStatus(error);
        }
    }

    bool Drain() {
        std::lock_guard lock(mutex);
        return !worker.joinable() || done;
    }

    bool Arm(const k_task_waker &next) {
        std::lock_guard lock(mutex);
        if (next.signal) waker = next;
        return worker.joinable() && !done;
    }

    void Finish(k_task_state state) {
        if (worker.joinable()) worker.join();
        waker = {};
        task_live = false;
        published = state == K_TASK_SUCCEEDED && result == K_INFERENCE_OK && !cancelled;
        if (!owner_open) delete this;
    }

    void Close() {
        owner_open = false;
        if (task_live) Cancel();
        else delete this;
    }

    const k_foreign_task *Task() {
        if (claimed) return nullptr;
        claimed = task_live = true;
        descriptor = {
            {
                K_FOREIGN_TASK_ABI_VERSION, sizeof(k_foreign_task_ready), sizeof(k_inference_result), this,
                [](void *frame, void *out) { return static_cast<k_inference_request *>(frame)->Poll(out); },
                [](void *frame) { static_cast<k_inference_request *>(frame)->Cancel(); },
                [](void *frame) { return static_cast<k_inference_request *>(frame)->Drain(); },
                [](void *frame, k_task_state state, bool) { static_cast<k_inference_request *>(frame)->Finish(state); },
            },
            [](void *frame, const k_task_waker *waker) { return static_cast<k_inference_request *>(frame)->Arm(*waker); },
        };
        return &descriptor.base;
    }
};

namespace {

int32_t Start(void **state) {
    *state = new (std::nothrow) Provider;
    return *state ? K_INFERENCE_OK : K_INFERENCE_FAILED;
}

void Stop(void *state) { delete static_cast<Provider *>(state); }

void SetError(Provider &provider, const char *message) {
    try { provider.error = message; }
    catch (...) { provider.error.clear(); }
}

int32_t InitModel(void *state, const uint8_t *data, size_t length, const char *runtime) {
    auto &provider = *static_cast<Provider *>(state);
    if (provider.model || !data || !length || length > K_INFERENCE_MAX_MODEL_BYTES || !runtime || !*runtime) {
        SetError(provider, "expected one bounded ONNX model and a locked ONNX Runtime library");
        return K_INFERENCE_INVALID_INPUT;
    }
    try {
        provider.model = std::make_unique<Model>(runtime, data, length);
        return K_INFERENCE_OK;
    } catch (const CompileError &error) {
        SetError(provider, error.msg.c_str());
    } catch (const std::exception &error) {
        SetError(provider, error.what());
    } catch (...) {
        provider.error.clear();
    }
    return K_INFERENCE_FAILED;
}

const char *ModelError(void *state) {
    const auto &error = static_cast<const Provider *>(state)->error;
    return error.empty() ? "ONNX Runtime initialization failed" : error.c_str();
}

k_inference_layout Layout(void *state) {
    auto &provider = *static_cast<Provider *>(state);
    return provider.model ? provider.model->layout : k_inference_layout {};
}

int32_t Request(void *state, const float *input, size_t elements, k_inference_request **out) {
    auto &provider = *static_cast<Provider *>(state);
    *out = nullptr;
    if (!provider.model || !input || elements != provider.model->layout.input.elements)
        return K_INFERENCE_INVALID_INPUT;
    if (provider.busy) return K_INFERENCE_BUSY;
    try {
        *out = new k_inference_request(provider, input);
        provider.busy = true;
        return K_INFERENCE_OK;
    } catch (...) {
        return K_INFERENCE_FAILED;
    }
}

const k_foreign_task *Task(void *, k_inference_request *request) { return request->Task(); }

uint32_t Output(void *, k_inference_request *request, float *out, size_t elements) {
    if (!request->published || !out || elements < request->output.size()) return 0;
    memcpy(out, request->output.data(), request->output.size() * sizeof(float));
    return (uint32_t)request->output.size();
}

void Close(void *, k_inference_request *request) { request->Close(); }

int32_t Arena(void *state, k_inference_arena *out) {
    const auto &provider = *static_cast<const Provider *>(state);
    if (!provider.model || !out) return 0;
    try {
        k_inference_arena arena {};
        if (provider.model->Arena(arena)) {
            *out = arena;
            return 1;
        }
    } catch (...) {
        // Telemetry must remain optional even when the runtime returns an error.
    }
    return 0;
}

}  // namespace

const k_cordis_inference_api *k_cordis_inference_v1(void) {
    static const k_cordis_inference_api api {
        K_CORDIS_INFERENCE_ABI, sizeof(k_cordis_inference_api), Start, Stop,
        InitModel, ModelError, Layout, Request, Task, Output, Close, Arena,
    };
    return &api;
}
