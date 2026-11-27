#include "cordis/inference.h"
#include "includes.h"
#include "runtime/runtime_tasks.h"

#include <chrono>

namespace {

void Log(const char *event) {
    if (auto file = std::fopen("task.events", "ab")) {
        std::fprintf(file, "%s\n", event);
        std::fclose(file);
    }
}

bool Released(const char *gate) {
    if (auto file = std::fopen(gate, "rb")) {
        std::fclose(file);
        return true;
    }
    return false;
}

}  // namespace

struct k_inference_request {
    float values[2];
    k_foreign_task_ready task {};
    std::mutex mutex;
    std::thread worker;
    k_task_waker wake {};
    bool cancelled = false, ready = false, published = false;

    void Work() {
        for (;;) {
            {
                std::lock_guard lock(mutex);
                if (Released(cancelled ? "release-drain" : "release-task")) {
                    ready = true;
                    k_task_wake(wake);
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    k_task_poll Poll(void *result) {
        std::lock_guard lock(mutex);
        if (ready) {
            *static_cast<k_inference_result *>(result) = { K_INFERENCE_OK, 2 };
            return K_TASK_POLL_READY;
        }
        if (!worker.joinable()) {
            worker = std::thread([this] { Work(); });
            Log("started");
        }
        return K_TASK_POLL_PENDING;
    }

    void Cancel() {
        std::lock_guard lock(mutex);
        cancelled = true;
        Log("cancelled");
    }

    bool Drain() {
        std::lock_guard lock(mutex);
        return !worker.joinable() || ready;
    }

    bool Arm(const k_task_waker *waker) {
        std::lock_guard lock(mutex);
        if (waker->signal) wake = *waker;
        return !ready;
    }

    void Finish(k_task_state state) {
        if (worker.joinable()) worker.join();
        published = state == K_TASK_SUCCEEDED;
        Log(published ? "completed" : "drained");
    }
};

extern "C" const k_cordis_inference_api *k_cordis_inference_v1() {
    static const k_cordis_inference_api api {
        K_CORDIS_INFERENCE_ABI, offsetof(k_cordis_inference_api, arena),
        [](void **) -> int32_t { return K_INFERENCE_OK; },
        [](void *) { Log("stopped"); },
        [](void *, const uint8_t *, size_t, const char *) -> int32_t { return K_INFERENCE_OK; },
        [](void *) -> const char * { return "gateway fixture"; },
        [](void *) { return k_inference_layout { { 1, 3, { 3 } }, { 1, 2, { 2 } } }; },
        [](void *, const float *input, size_t elements, k_inference_request **out) -> int32_t {
            if (elements != 3) return K_INFERENCE_INVALID_INPUT;
            *out = new k_inference_request { { input[0], input[2] } };
            return K_INFERENCE_OK;
        },
        [](void *, k_inference_request *request) -> const k_foreign_task * {
            request->task = {
                {
                    K_FOREIGN_TASK_ABI_VERSION, sizeof(k_foreign_task_ready), sizeof(k_inference_result), request,
                    [](void *frame, void *out) { return static_cast<k_inference_request *>(frame)->Poll(out); },
                    [](void *frame) { static_cast<k_inference_request *>(frame)->Cancel(); },
                    [](void *frame) { return static_cast<k_inference_request *>(frame)->Drain(); },
                    [](void *frame, k_task_state state, bool) { static_cast<k_inference_request *>(frame)->Finish(state); }
                },
                [](void *frame, const k_task_waker *waker) {
                    return static_cast<k_inference_request *>(frame)->Arm(waker);
                }
            };
            return &request->task.base;
        },
        [](void *, k_inference_request *request, float *out, size_t elements) -> uint32_t {
            if (!request->published || elements < 2) return 0;
            std::copy_n(request->values, 2, out);
            return 2;
        },
        [](void *, k_inference_request *request) { delete request; },
    };
    return &api;
}
