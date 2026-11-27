#pragma once

#include "../runtime/runtime_tasks.h"

#include <memory>
#include <new>
#include <utility>

namespace klang::cordis {

// A descriptor whose callbacks reside in a component must retain that
// generation through the return from finish. The driver owns this wrapper
// once the token crosses to K.
template<typename Generation> class PinnedForeignTask {
    std::shared_ptr<const Generation> pin;
    const k_foreign_task *provider;
    k_foreign_task_ready binding;

    PinnedForeignTask(std::shared_ptr<const Generation> pin, const k_foreign_task *provider)
        : pin(std::move(pin)), provider(provider), binding {
            {
                K_FOREIGN_TASK_ABI_VERSION, sizeof(k_foreign_task_ready), provider->result_size, this,
                [](void *frame, void *result) {
                    auto op = static_cast<PinnedForeignTask *>(frame)->provider;
                    return op->poll(op->frame, result);
                },
                [](void *frame) {
                    auto op = static_cast<PinnedForeignTask *>(frame)->provider;
                    if (op->request_cancel) op->request_cancel(op->frame);
                },
                [](void *frame) {
                    auto op = static_cast<PinnedForeignTask *>(frame)->provider;
                    return op->drain(op->frame);
                },
                [](void *frame, k_task_state state, bool started) {
                    std::unique_ptr<PinnedForeignTask> self(static_cast<PinnedForeignTask *>(frame));
                    auto op = self->provider;
                    op->finish(op->frame, state, started);
                },
            },
            [](void *frame, const k_task_waker *waker) {
                auto op = static_cast<PinnedForeignTask *>(frame)->provider;
                return k_foreign_task_arm(op, waker);
            },
        } {}

public:
    static const k_foreign_task *Bind(std::shared_ptr<const Generation> generation,
                                     const k_foreign_task *provider) {
        if (!generation || !k_foreign_task_get((uint64_t)(uintptr_t)provider)) return nullptr;
        // Keep the original pin while handling allocation failure too.
        auto owned = new (std::nothrow) PinnedForeignTask(generation, provider);
        if (owned) return &owned->binding.base;
        provider->finish(provider->frame, K_TASK_CANCELLED, false);
        return nullptr;
    }
};

}  // namespace klang::cordis
