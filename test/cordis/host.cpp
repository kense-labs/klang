#include "cordis/service.h"
#include "runtime/runtime_tasks.h"
#include "value.h"

#include <stdexcept>

using namespace klang;

struct ValueContract {
    using Api = k_cordis_test_value_api;
    static constexpr const char *name = "test value";
    static constexpr const char *entry = "k_cordis_test_value_v1";
    static constexpr uint32_t version = 1;
    static bool Supports(const Api &api) { return api.read != nullptr; }
};

using ValueService = cordis::Service<ValueContract>;

static void Require(bool ok, const string &message) {
    if (!ok) throw runtime_error(message);
}

static void CheckEvents(const char *log, const string &expected) {
    string actual;
    Require(LoadFile(log, actual), "cannot read Cordis events");
    Require(actual == expected, cat("unexpected Cordis events:\n", actual, "expected:\n", expected));
}

#ifdef K_CORDIS_TEST_DYNAMIC
struct PendingProviderCall {
    shared_ptr<const ValueService::Generation> provider;
    bool completed = false;
    int polls = 0, cancellations = 0;
};

static const k_task_ops provider_call_ops {
    [](void *frame) {
        auto &call = *static_cast<PendingProviderCall *>(frame);
        ++call.polls;
        return K_TASK_POLL_PENDING;
    },
    [](void *frame) { ++static_cast<PendingProviderCall *>(frame)->cancellations; },
    [](void *frame) { return static_cast<PendingProviderCall *>(frame)->completed; },
    [](void *frame, k_task_state state, bool started) {
        auto &call = *static_cast<PendingProviderCall *>(frame);
        Require(state == K_TASK_CANCELLED && started && call.completed, "provider call retired before completion");
        call.provider.reset();
    },
};

static void CheckRejected(ValueService &host, const char *id, string_view expected) {
    auto before = host.Pin();
    try {
        host.Select(id);
    } catch (const CompileError &error) {
        Require(error.msg.find(expected) != string::npos, error.msg);
        Require(host.Pin() == before, cat("failed selection replaced the active provider: ", id));
        return;
    }
    throw runtime_error(cat("unexpected activation of ", id));
}

static void TestReplacement(ValueService &host, const char *log, char **argv) {
    Require(host.RegisterLibrary("shared", argv[1]), "cannot register shared provider");
    Require(host.RegisterLibrary("failed", argv[2]), "cannot register failing provider");
    Require(host.RegisterLibrary("incompatible", argv[3]), "cannot register incompatible provider");
    Require(host.RegisterLibrary("wrong-service", argv[4]), "cannot register wrong service");

    auto held = host.Pin();
    Require(host.Select("shared") && !host.Select("shared"), "replacement did not take effect once");
    CheckEvents(log, "start linked\nstart shared\n");
    Require(held->id == "linked" && held->Call<&ValueContract::Api::read>() == 17,
            "the old request lost its linked provider");
    Require(host.Pin()->id == "shared" && host.Pin()->Call<&ValueContract::Api::read>() == 29,
            "the new request did not use the shared library");

    CheckRejected(host, "failed", "failed to start (status 9)");
    CheckRejected(host, "incompatible", "incompatible Cordis test value ABI");
    CheckRejected(host, "wrong-service", "has no k_cordis_test_value_v1 entry");
    CheckRejected(host, "unknown", "unknown Cordis test value provider");
    Require(host.Pin()->Call<&ValueContract::Api::read>() == 29, "rollback damaged the active provider");
    const string events = "start linked\nstart shared\nstart failed\nstop failed\n";
    CheckEvents(log, events);
    held.reset();
    CheckEvents(log, events + "stop linked\n");

    // The old component stays live through a task's cancellation and drain.
    PendingProviderCall call { host.Pin() };
    weak_ptr<const ValueService::Generation> retired = call.provider;
    k_task_scope scope {};
    k_task task {};
    Require(k_task_scope_init(&scope, 1) && k_task_init(&task, &call, &provider_call_ops) &&
            k_task_scope_submit(&scope, &task), "could not submit provider task");
    Require(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && call.polls == 1, "provider task did not suspend");
    Require(host.Select("linked") && host.Pin()->Call<&ValueContract::Api::read>() == 17,
            "could not replace a shared provider with a linked provider");
    const string replaced = events + "stop linked\nstart linked\n";
    k_task_scope_shutdown(&scope);
    Require(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && task.state == K_TASK_DRAINING &&
            k_task_scope_step(&scope) == K_TASK_STEP_PENDING, "provider task did not wait for completion");
    CheckEvents(log, replaced);
    Require(call.polls == 1 && call.cancellations == 1 && call.provider->Call<&ValueContract::Api::read>() == 29,
            "the retired provider is unavailable to its foreign completion");
    call.completed = true;
    Require(k_task_scope_step(&scope) == K_TASK_STEP_FINISHED && scope.count == 0,
            "the provider task did not drain");
    Require(retired.expired(), "the retired shared generation kept an owner");
    CheckEvents(log, replaced + "stop shared\n");
    host.Reset();
    CheckEvents(log, replaced + "stop shared\nstop linked\n");
}
#endif

int main(int argc, char **argv) {
    try {
        auto log = getenv("K_CORDIS_TEST_LOG");
        Require(log && *log, "missing Cordis fixture log path");
        remove(log);
        ValueService host;
        Require(host.RegisterStatic("linked", k_cordis_test_value_v1), "cannot register linked provider");
        Require(!host.RegisterStatic("linked", k_cordis_test_value_v1), "duplicate provider was accepted");
        Require(host.Select("linked") && !host.Select("linked"), "linked provider did not activate once");
        Require(host.Pin()->Call<&ValueContract::Api::read>() == 17, "linked provider returned the wrong value");

#ifdef K_CORDIS_TEST_DYNAMIC
        Require(argc == 5, "expected three value libraries and one diagnostics library");
        TestReplacement(host, log, argv);
#else
        Require(argc == 1, cat(argv[0], " takes no library paths"));
        auto held = host.Pin();
        host.Reset();
        Require(!host.Pin(), "shutdown left a provider active");
        CheckEvents(log, "start linked\n");
        Require(held->Call<&ValueContract::Api::read>() == 17, "shutdown retired an in-flight request");
        held.reset();
        CheckEvents(log, "start linked\nstop linked\n");
#endif
        return 0;
    } catch (const CompileError &error) {
        fprintf(stderr, "%s\n", error.msg.c_str());
    } catch (const std::exception &error) {
        fprintf(stderr, "%s\n", error.what());
    }
    return 1;
}
