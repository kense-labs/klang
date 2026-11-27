#include "tcp_socket.h"
#include "application.h"
#include "../content_digest.h"
#include "graph.h"
#include "inference_host.h"
#include "memory_budget.h"
#include "tcp_policy.h"
#include "tcp_stream_host.h"

#include <fstream>
#include <limits>

namespace {
using namespace klang;
using namespace klang::cordis;

constexpr const char *stream_service = "tcp.stream";
constexpr const char *policy_service = "tcp.policy";
constexpr const char *inference_service = "inference";
thread_local string last_error;
thread_local bool fallback_error = false;

struct TcpPolicyContract {
    using Api = k_cordis_tcp_policy_api;
    static constexpr auto name = "TCP stream policy";
    static constexpr auto entry = K_CORDIS_TCP_POLICY_ENTRY;
    static constexpr uint32_t version = K_CORDIS_TCP_POLICY_ABI;
    static bool Supports(const Api &api) { return api.max_capacity != nullptr; }
};

[[noreturn]] void Fail(string code, string message, vector<string> chain = {}) {
    throw GraphError { { std::move(message) }, std::move(code), std::move(chain) };
}

template<typename Result, typename Run>
Result Report(Result failed, Run &&run) {
    last_error.clear();
    fallback_error = false;
    try {
        return run();
    } catch (const GraphError &e) {
        try { last_error = e.Data(); }
        catch (...) { fallback_error = true; }
    } catch (...) {
        fallback_error = true;
    }
    return failed;
}

string RequiredText(k_cordis_app_text text, const char *field) {
    if (!text.data || text.len <= 0 || (uint64_t)text.len > string().max_size())
        Fail("invalid_argument", cat("expected a nonempty Cordis ", field));
    string result(reinterpret_cast<const char *>(text.data), (size_t)text.len);
    if (result.find('\0') != string::npos)
        Fail("invalid_argument", cat("unexpected NUL in Cordis ", field));
    return result;
}

int64_t CopyText(string_view text, k_cordis_app_buffer out) {
    if (!text.empty() && out.data && out.len >= 0 && (uint64_t)out.len >= text.size())
        memcpy(out.data, text.data(), text.size());
    return (int64_t)text.size();
}

struct Application {
    struct Active {
        Graph graph;
        InferenceService inference;
        Service<TcpPolicyContract> policy;
        TcpStreamService service;
        string document;
    };
    Catalog catalog;
    string root;
    map<string, string> loaded_artifacts;
    unique_ptr<Active> active;
    vector<weak_ptr<const InferenceService::Generation>> retired_inference;
    uint64_t last_activation_reserved_bytes = 0;
    bool last_activation_measured = false;

    Application(const string &manifest, const char *root) : catalog(Catalog::Load(manifest, {})), root(root) {}

    template<typename Host>
    static void Start(Host &host, const Graph &graph, const string &name,
                      vector<shared_ptr<const void>> dependencies = {}) {
        const auto &provider = graph.providers.at(name);
        host.RegisterLibrary(provider.id, provider.artifact);
        try {
            host.Select(provider.id, std::move(dependencies));
        } catch (const AbiError &e) {
            Fail("incompatible_abi", e.msg, graph.DependencyChain(name));
        } catch (const CompileError &e) {
            Fail("activation_failed", e.msg, graph.DependencyChain(name));
        }
    }

    void Remember(const string &path, const string &digest, const Graph &graph, const string &name) {
        auto [known, inserted] = loaded_artifacts.try_emplace(path, digest);
        if (!inserted && known->second != digest)
            Fail("changed_input", "use a new artifact path for a different Cordis provider or runtime binary",
                 graph.DependencyChain(name));
    }

    static void LoadModel(const Graph &graph, const InferenceService::Generation &provider) {
        const auto &inputs = graph.providers.at(inference_service).inputs;
        auto model = inputs.find("model"), runtime = inputs.find("runtime");
        if (model == inputs.end() || runtime == inputs.end())
            Fail("inference_inputs", "inference requires locked model and runtime inputs",
                 graph.DependencyChain(inference_service));
        const auto &source = model->second;
        std::ifstream file(filesystem::path(source.path), std::ios::binary | std::ios::ate);
        auto size = static_cast<std::streamoff>(file.tellg());
        if (size <= 0 || size > K_INFERENCE_MAX_MODEL_BYTES)
            Fail("inference_model_size", "inference requires a nonempty model of at most 64 MiB",
                 graph.DependencyChain(inference_service));
        string bytes((size_t)size, '\0');
        file.seekg(0);
        if (!file.read(bytes.data(), (std::streamsize)bytes.size()) || file.peek() != EOF ||
            Sha256(bytes) != source.digest)
            Fail("changed_input", cat("locked model changed before loading: ", source.path),
                 graph.DependencyChain(inference_service));
        using Api = InferenceContract::Api;
        auto status = provider.Call<&Api::init_model>(reinterpret_cast<const uint8_t *>(bytes.data()),
                                                      bytes.size(), runtime->second.path.c_str());
        if (status != K_INFERENCE_OK) {
            auto message = provider.Call<&Api::model_error>();
            Fail("inference_model_failed", message ? message : "inference model initialization failed",
                 graph.DependencyChain(inference_service));
        }
    }

    bool Select(const optional<string> &selection, const string &lock) {
        auto graph = catalog.Resolve(root, selection);
        graph.VerifyLock(lock);
        if (active && active->graph.identity == graph.identity) return false;
        for (const auto &[name, provider] : graph.providers) {
            if (provider.linked || (name != stream_service && name != policy_service && name != inference_service))
                Fail("unsupported_service", cat("the application host cannot activate ", name),
                     graph.DependencyChain(name));
        }
        for (const auto &[name, provider] : graph.providers) {
            Remember(provider.artifact, provider.digest, graph, name);
            if (name == inference_service)
                if (auto runtime = provider.inputs.find("runtime"); runtime != provider.inputs.end())
                    Remember(runtime->second.path, runtime->second.digest, graph, name);
        }

        auto staged = make_unique<Active>();
        map<string, shared_ptr<const void>> pins;
        for (const auto &name : graph.activation_order) {
            vector<shared_ptr<const void>> dependencies;
            for (const auto &[dependency, range] : graph.providers.at(name).requirements)
                dependencies.push_back(pins.at(dependency));
            if (name == policy_service) {
                Start(staged->policy, graph, name, std::move(dependencies));
                pins.emplace(name, staged->policy.Pin());
            } else if (name == inference_service) {
                Start(staged->inference, graph, name, std::move(dependencies));
                auto provider = staged->inference.Pin();
                LoadModel(graph, *provider);
                pins.emplace(name, std::move(provider));
            } else {
                Start(staged->service, graph, name, std::move(dependencies));
                pins.emplace(name, staged->service.Pin());
            }
        }
        graph.VerifyInputs();
        staged->document = graph.Document();
        staged->graph = std::move(graph);
        auto outgoing = active ? active->inference.Pin() : nullptr;
        auto incoming = staged->inference.Pin();
        auto previous = Workspace();
        k_inference_arena next {};
        auto measured = incoming && ReadInferenceArena(*incoming, next) &&
            (!outgoing || previous.active_measured) && previous.retired_count == previous.retired_measured;
        std::erase_if(retired_inference, [](const auto &entry) { return entry.expired(); });
        if (outgoing) retired_inference.emplace_back(outgoing);
        last_activation_reserved_bytes = measured ? next.reserved_bytes + previous.active.reserved_bytes +
            previous.retired_reserved_bytes : 0;
        last_activation_measured = measured;
        active.swap(staged);
        return true;
    }

    k_inference_workspace Workspace() const {
        k_inference_workspace result {};
        if (auto provider = active ? active->inference.Pin() : nullptr)
            result.active_measured = ReadInferenceArena(*provider, result.active);
        for (const auto &entry : retired_inference) {
            if (auto provider = entry.lock()) {
                ++result.retired_count;
                k_inference_arena arena {};
                if (ReadInferenceArena(*provider, arena)) {
                    ++result.retired_measured;
                    result.retired_reserved_bytes += arena.reserved_bytes;
                }
            }
        }
        result.last_activation_reserved_bytes = last_activation_reserved_bytes;
        result.last_activation_measured = last_activation_measured;
        return result;
    }

    void CheckCapacity(uint32_t capacity) const {
        if (auto policy = active->policy.Pin()) {
            auto limit = policy->Call<&TcpPolicyContract::Api::max_capacity>();
            if (capacity > limit)
                Fail("stream_capacity_denied", cat("Cordis TCP queue capacity ", capacity,
                     " exceeds the selected policy limit ", limit),
                     active->graph.DependencyChain(policy_service));
        }
    }
};

template<typename T, typename Handle> T *Object(Handle handle) {
    return reinterpret_cast<T *>((uintptr_t)handle.token);
}

template<typename Handle, typename T> Handle HandleOf(T *pointer) {
    return { (uint64_t)(uintptr_t)pointer };
}

k_cordis_app Open(k_cordis_app_text manifest, k_cordis_app_text lock, const char *root) {
    return Report(k_cordis_app {}, [&] {
        auto path = RequiredText(manifest, "manifest path");
        auto lock_path = RequiredText(lock, "lock path");
        auto app = make_unique<Application>(path, root);
        app->Select({}, lock_path);
        return HandleOf<k_cordis_app>(app.release());
    });
}

}  // namespace

k_cordis_app k_cordis_app_open_v1(k_cordis_app_text manifest, k_cordis_app_text lock) {
    return Open(manifest, lock, stream_service);
}

k_cordis_app k_cordis_app_open_inference_v1(k_cordis_app_text manifest, k_cordis_app_text lock) {
    return Open(manifest, lock, inference_service);
}

int32_t k_cordis_app_require_memory_budget_v1(uint64_t bytes) {
    return Report(int32_t(0), [&]() -> int32_t {
        if (!bytes) Fail("invalid_argument", "the process memory budget must be positive");
        auto boundary = ProcessMemoryBoundary();
        if (!boundary.group_visible)
            Fail("memory_budget_unavailable", "the process needs a visible Linux cgroup v2 membership");
        if (!boundary.memory_max || *boundary.memory_max > bytes)
            Fail("memory_budget_insufficient", cat("expected a cgroup memory.max no larger than ", bytes,
                 " bytes, observed ", boundary.memory_max ? to_string(*boundary.memory_max) : "no finite limit"));
        if (!boundary.swap_disabled)
            Fail("memory_budget_swap_allowed", "expected memory.swap.max set to zero in the process cgroup hierarchy");
        return 1;
    });
}

int32_t k_cordis_app_select_v1(k_cordis_app app, k_cordis_app_text provider, k_cordis_app_text lock) {
    return Report(int32_t(-1), [&] {
        auto host = Object<Application>(app);
        if (!host) Fail("invalid_handle", "invalid Cordis application handle");
        auto id = RequiredText(provider, "provider ID");
        auto path = RequiredText(lock, "lock path");
        return (int32_t)host->Select(id, path);
    });
}

void k_cordis_app_close_v1(k_cordis_app app) { delete Object<Application>(app); }
int32_t k_cordis_app_ready_v1(k_cordis_app app) { return app.token != 0; }

int64_t k_cordis_app_error_v1(k_cordis_app_buffer out) {
    return CopyText(fallback_error ?
        string_view(R"({"code":"host_error","message":"Cordis application operation failed","chain":[],"candidates":[]})") :
        string_view(last_error), out);
}

int64_t k_cordis_app_graph_v1(k_cordis_app app, k_cordis_app_buffer out) {
    auto host = Object<Application>(app);
    return host ? CopyText(host->active->document, out) : 0;
}

int64_t k_cordis_app_provider_v1(k_cordis_app app, k_cordis_app_buffer out) {
    auto host = Object<Application>(app);
    return host ? CopyText(host->active->graph.providers.at(host->root).id, out) : 0;
}

int64_t k_cordis_app_reader_provider_v1(k_cordis_app_reader reader, k_cordis_app_buffer out) {
    auto stream = Object<TcpStreamReader>(reader);
    return stream ? CopyText(stream->ProviderId(), out) : 0;
}

k_cordis_app_reader k_cordis_app_adopt_v1(k_cordis_app app, k_cordis_app_socket socket, uint32_t capacity) {
    struct OwnedSocket {
        tcp::Socket value;
        ~OwnedSocket() { if (value != tcp::invalid_socket) tcp::Close(value); }
    } incoming { tcp::FromToken(socket.token) };
    return Report(k_cordis_app_reader {}, [&] {
        auto host = Object<Application>(app);
        if (!host || incoming.value == tcp::invalid_socket)
            Fail("invalid_handle", "expected a Cordis application and connected TCP socket");
        host->CheckCapacity(capacity);
        auto reader = TcpStreamReader::Open(host->active->service.Pin(), socket.token, capacity);
        if (!reader) Fail("stream_open_failed", "Cordis could not adopt the TCP socket");
        incoming.value = tcp::invalid_socket;
        return HandleOf<k_cordis_app_reader>(reader.release());
    });
}

k_cordis_app_task k_cordis_app_pump_v1(k_cordis_app_reader reader) {
    return Report(k_cordis_app_task {}, [&] {
        auto stream = Object<TcpStreamReader>(reader);
        if (!stream) Fail("invalid_handle", "invalid Cordis stream reader");
        auto task = stream->Pump();
        if (!task) Fail("stream_pump_failed", "Cordis could not start a stream pump");
        return HandleOf<k_cordis_app_task>(task);
    });
}

int32_t k_cordis_app_reader_ready_v1(k_cordis_app_reader reader) { return reader.token != 0; }
int32_t k_cordis_app_task_ready_v1(k_cordis_app_task task) { return task.token != 0; }

k_tcp_stream_read_result k_cordis_app_take_v1(k_cordis_app_reader reader, k_cordis_app_buffer out) {
    auto stream = Object<TcpStreamReader>(reader);
    if (!stream || !out.data || out.len <= 0 || (uint64_t)out.len > std::numeric_limits<size_t>::max())
        return { K_TCP_STREAM_ERROR, tcp::invalid_argument_error, 0 };
    return stream->Take({ out.data, (size_t)out.len });
}

k_tcp_stream_stats k_cordis_app_stats_v1(k_cordis_app_reader reader) {
    auto stream = Object<TcpStreamReader>(reader);
    return stream ? stream->Stats() : k_tcp_stream_stats {};
}

void k_cordis_app_close_reader_v1(k_cordis_app_reader reader) { delete Object<TcpStreamReader>(reader); }

void k_cordis_app_discard_task_v1(k_cordis_app_task task) {
    if (auto op = k_foreign_task_get(task.token)) op->finish(op->frame, K_TASK_CANCELLED, false);
}

k_inference_layout k_cordis_app_inference_layout_v1(k_cordis_app app) {
    auto host = Object<Application>(app);
    auto provider = host ? host->active->inference.Pin() : nullptr;
    return provider ? provider->Call<&InferenceContract::Api::layout>() : k_inference_layout {};
}

k_inference_workspace k_cordis_app_inference_workspace_v1(k_cordis_app app) {
    auto host = Object<Application>(app);
    return host ? host->Workspace() : k_inference_workspace {};
}

k_cordis_app_inference k_cordis_app_infer_v1(k_cordis_app app, k_cordis_app_input input) {
    return Report(k_cordis_app_inference {}, [&] {
        auto host = Object<Application>(app);
        auto provider = host ? host->active->inference.Pin() : nullptr;
        if (!provider) Fail("inference_unavailable", "the active Cordis graph has no inference service");
        auto chain = host->active->graph.DependencyChain(inference_service);
        if (!input.data || input.len <= 0 || (uint64_t)input.len > K_INFERENCE_MAX_ELEMENTS)
            Fail("inference_input", "expected a bounded, nonempty float32 tensor", chain);
        auto request = make_unique<InferenceRequest>(std::move(provider));
        auto status = request->Open({ input.data, (size_t)input.len });
        if (status == K_INFERENCE_BUSY) Fail("inference_busy", "the provider's request slot is in use", chain);
        if (status == K_INFERENCE_INVALID_INPUT)
            Fail("inference_input", "input element count differs from the model shape", chain);
        if (status != K_INFERENCE_OK) Fail("inference_failed", "the provider could not create a request", chain);
        return HandleOf<k_cordis_app_inference>(request.release());
    });
}

int32_t k_cordis_app_inference_ready_v1(k_cordis_app_inference request) { return request.token != 0; }

k_cordis_app_task k_cordis_app_inference_task_v1(k_cordis_app_inference request) {
    return Report(k_cordis_app_task {}, [&] {
        auto op = Object<InferenceRequest>(request);
        if (!op) Fail("invalid_handle", "invalid Cordis inference request");
        auto task = op->Task();
        if (!task) Fail("inference_task_failed", "the provider could not claim the inference task");
        return HandleOf<k_cordis_app_task>(task);
    });
}

int32_t k_cordis_app_inference_task_ready_v1(k_cordis_app_task task) { return task.token != 0; }
void k_cordis_app_discard_inference_task_v1(k_cordis_app_task task) { k_cordis_app_discard_task_v1(task); }

uint32_t k_cordis_app_inference_output_v1(k_cordis_app_inference request, k_cordis_app_output out) {
    auto op = Object<InferenceRequest>(request);
    if (!op || !out.data || out.len <= 0 || (uint64_t)out.len > std::numeric_limits<size_t>::max()) return 0;
    return op->Output({ out.data, (size_t)out.len });
}

void k_cordis_app_close_inference_v1(k_cordis_app_inference request) { delete Object<InferenceRequest>(request); }
