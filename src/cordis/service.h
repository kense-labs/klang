// Activation and retirement for one versioned native service.
#pragma once

#include "../includes.h"
#include "../utils.h"
#include "../shared_library.h"

namespace klang::cordis {

struct AbiError : CompileError {};

// Contract names the ABI version, entry symbol, and required service methods.
// Hosts serialize registration, selection, and pin acquisition. A request
// keeps its pin until it finishes using the service. The last pin runs stop
// on its releasing thread, before releasing the native library.
template<typename Contract> class Service {
    using Api = typename Contract::Api;
    using Entry = const Api *(*)();
    template<auto Method> using Member = integral_constant<decltype(Method), Method>;
    static constexpr size_t minimum_size = [] {
        if constexpr (requires { Contract::minimum_size; }) return Contract::minimum_size;
        else return sizeof(Api);
    }();

    struct Descriptor {
        const Api *api;
        shared_ptr<SharedLibrary> library;
    };

    struct Registration {
        string origin;
        function<Descriptor()> open;
    };

public:
    class Generation {
        friend class Service;

        vector<shared_ptr<const void>> dependencies;
        Descriptor descriptor;
        void *state = nullptr;
        bool started = false;

        Generation(string name, Descriptor descriptor, vector<shared_ptr<const void>> dependencies)
            : dependencies(std::move(dependencies)), descriptor(std::move(descriptor)), id(std::move(name)) {}

    public:
        const string id;

        ~Generation() {
            if (started) descriptor.api->stop(state);
        }

        Generation(const Generation &) = delete;
        Generation &operator=(const Generation &) = delete;

        template<auto Method, typename... Args> decltype(auto) Call(Args &&...args) const {
            static_assert(!is_same_v<Member<Method>, Member<&Api::start>> &&
                          !is_same_v<Member<Method>, Member<&Api::stop>>,
                          "the service owns generation startup and teardown");
            return (descriptor.api->*Method)(state, std::forward<Args>(args)...);
        }

        template<auto Method, size_t Offset, typename... Args> bool TryQuery(Args &&...args) const {
            const auto *api = descriptor.api;
            return api->struct_size >= Offset + sizeof(api->*Method) && (api->*Method) &&
                   Call<Method>(std::forward<Args>(args)...);
        }
    };

    Service() = default;
    Service(Service &&) = default;
    Service &operator=(Service &&) = default;

    bool RegisterStatic(const string &id, Entry entry) {
        if (!entry) throw CompileError { cat("missing entry for Cordis ", Contract::name, " provider ", id) };
        return providers.try_emplace(id, Registration { id, [entry] { return Descriptor { entry(), {} }; } }).second;
    }

    bool RegisterLibrary(const string &id, const string &path) {
        return providers.try_emplace(id, Registration { path, [path] {
            auto library = make_shared<SharedLibrary>(path);
            auto entry = reinterpret_cast<Entry>(library->Symbol(Contract::entry));
            if (!entry) throw AbiError { { cat(path, " has no ", Contract::entry, " entry") } };
            return Descriptor { entry(), std::move(library) };
        } }).second;
    }

    shared_ptr<const Generation> Pin() const { return active; }
    void Reset() { active.reset(); }

    // A dependent can outlive its host's active graph. Keep the dependencies
    // pinned until after this generation has stopped and released its code.
    bool Select(const string &id, vector<shared_ptr<const void>> dependencies = {}) {
        if (active && active->id == id) return false;
        auto target = providers.find(id);
        if (target == providers.end())
            throw CompileError { cat("unknown Cordis ", Contract::name, " provider ", id) };

        auto descriptor = target->second.open();
        auto api = descriptor.api;
        if (!api || api->abi_version != Contract::version || api->struct_size < minimum_size ||
            !api->start || !api->stop || !Contract::Supports(*api))
            throw AbiError { { cat("incompatible Cordis ", Contract::name, " ABI in ", target->second.origin) } };

        auto staged = shared_ptr<Generation>(new Generation(id, std::move(descriptor), std::move(dependencies)));
        staged->started = true;
        auto status = api->start(&staged->state);
        if (status) throw CompileError { cat("Cordis ", Contract::name, " provider ", id,
                                             " failed to start (status ", status, ")") };
        active.swap(staged);
        return true;
    }

private:
    map<string, Registration> providers;
    shared_ptr<Generation> active;
};

}  // namespace klang::cordis
