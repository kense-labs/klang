#pragma once

#include "foreign_task.h"
#include "inference.h"
#include "service.h"

namespace klang::cordis {

struct InferenceContract {
    using Api = k_cordis_inference_api;
    static constexpr auto name = "inference";
    static constexpr auto entry = K_CORDIS_INFERENCE_ENTRY;
    static constexpr uint32_t version = K_CORDIS_INFERENCE_ABI;
    static constexpr size_t minimum_size = offsetof(Api, arena);
    static bool Supports(const Api &api) {
        return api.init_model && api.model_error && api.layout && api.request &&
               api.task && api.output && api.close;
    }
};

using InferenceService = Service<InferenceContract>;

inline bool ReadInferenceArena(const InferenceService::Generation &provider, k_inference_arena &arena) {
    using Api = InferenceContract::Api;
    return provider.TryQuery<&Api::arena, offsetof(Api, arena)>(&arena);
}

class InferenceRequest {
    using Api = InferenceContract::Api;
    using Generation = InferenceService::Generation;
    shared_ptr<const Generation> provider;
    k_inference_request *request = nullptr;

public:
    explicit InferenceRequest(shared_ptr<const Generation> generation) : provider(std::move(generation)) {}
    InferenceRequest(const InferenceRequest &) = delete;
    InferenceRequest &operator=(const InferenceRequest &) = delete;
    ~InferenceRequest() {
        if (request) provider->Call<&Api::close>(request);
    }

    int32_t Open(span<const float> input) {
        auto status = provider->Call<&Api::request>(input.data(), input.size(), &request);
        return status == K_INFERENCE_OK && !request ? K_INFERENCE_FAILED : status;
    }
    const k_foreign_task *Task() const {
        return PinnedForeignTask<Generation>::Bind(provider, provider->Call<&Api::task>(request));
    }
    uint32_t Output(span<float> output) const {
        return provider->Call<&Api::output>(request, output.data(), output.size());
    }
};

}  // namespace klang::cordis
