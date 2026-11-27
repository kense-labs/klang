// The host pins a stream's reader and its pump independently. Releasing
// either owner can never unload the other's provider callbacks.
#pragma once

#include "foreign_task.h"
#include "service.h"
#include "tcp_stream.h"

namespace klang::cordis {

struct TcpStreamContract {
    using Api = k_cordis_tcp_stream_api;
    static constexpr auto name = "TCP stream";
    static constexpr auto entry = K_CORDIS_TCP_STREAM_ENTRY;
    static constexpr uint32_t version = K_CORDIS_TCP_STREAM_ABI;
    static bool Supports(const Api &api) { return api.open && api.pump && api.take && api.stats && api.close; }
};

using TcpStreamService = Service<TcpStreamContract>;

class TcpStreamReader {
    using Generation = TcpStreamService::Generation;
    using Api = TcpStreamContract::Api;
    shared_ptr<const Generation> provider;
    k_tcp_stream_reader *reader = nullptr;

    explicit TcpStreamReader(shared_ptr<const Generation> generation) : provider(std::move(generation)) {}

public:
    static unique_ptr<TcpStreamReader> Open(shared_ptr<const Generation> generation, uint64_t socket,
                                            uint32_t capacity) {
        if (!generation) return {};
        unique_ptr<TcpStreamReader> result(new (std::nothrow) TcpStreamReader(std::move(generation)));
        if (!result) return {};
        result->reader = result->provider->Call<&Api::open>(socket, capacity);
        if (!result->reader) return {};
        return result;
    }

    TcpStreamReader(const TcpStreamReader &) = delete;
    TcpStreamReader &operator=(const TcpStreamReader &) = delete;

    ~TcpStreamReader() {
        if (reader) provider->Call<&Api::close>(reader);
    }

    const k_foreign_task *Pump() const {
        return PinnedForeignTask<Generation>::Bind(provider, provider->Call<&Api::pump>(reader));
    }

    const string &ProviderId() const { return provider->id; }

    k_tcp_stream_read_result Take(span<uint8_t> destination) const {
        auto size = (uint32_t)std::min(destination.size(), (size_t)K_TCP_STREAM_CHUNK_BYTES);
        return provider->Call<&Api::take>(reader, destination.data(), size);
    }

    k_tcp_stream_stats Stats() const { return provider->Call<&Api::stats>(reader); }
};

}  // namespace klang::cordis
