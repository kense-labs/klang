// Run adjacent requests through the real handler, without a reader thread or
// background analysis between them. Keep the compiler's normal include order.
#define main k_original_main
#include "../src/main.cpp"
#undef main

using namespace klang;

static void Require(bool ok, const string &message) {
    if (!ok) throw runtime_error(message);
}

static void Handle(Server &server, const string &method, const string &params, bool request = true) {
    Message m;
    m.method = method;
    m.id = request ? "1" : "";
    m.json = ParseJson("{\"params\":" + params + "}");
    Require(server.Handle(m), "server stopped at " + method);
}

static string DocumentParams(const string &uri, const string &text, bool change = false) {
    JsonOut j;
    j.BeginObject();
    j.Key("textDocument");
    j.BeginObject();
    j.Member("uri", uri);
    j.Member("version", change ? 2 : 1);
    if (!change) {
        j.Member("languageId", "klang");
        j.Member("text", text);
    }
    j.EndObject();
    if (change) {
        j.Key("contentChanges");
        j.BeginArray();
        j.BeginObject();
        j.Member("text", text);
        j.EndObject();
        j.EndArray();
    }
    j.EndObject();
    return j.s;
}

static string Position(const string &uri, int line) {
    JsonOut j;
    j.BeginObject();
    j.Key("textDocument");
    j.BeginObject();
    j.Member("uri", uri);
    j.EndObject();
    j.Key("position");
    j.BeginObject();
    j.Member("line", line);
    j.Member("character", 3);
    j.EndObject();
    j.EndObject();
    return j.s;
}

static string Item(Server &server, const string &uri, int line) {
    JsonOut j;
    Require(PrepareCallHierarchy(server.ws, ParseJson(Position(uri, line)), j), "cannot prepare item");
    auto items = ParseJson(j.s);
    Require(items.kind == Json::ARRAY && items.arr.size() == 1, "expected one item");
    return "{\"item\":" + j.s.substr(1, j.s.size() - 2) + "}";
}

static bool Graph(Server &server, const string &caller, const string &a, const string &b, bool changed) {
    auto calls = [&](const string &item, bool incoming, const string &name) {
        JsonOut j;
        auto params = ParseJson(item);
        auto ok = incoming ? IncomingCalls(server.ws, params, j) : OutgoingCalls(server.ws, params, j);
        Require(ok, "cannot resolve saved item");
        auto result = ParseJson(j.s);
        Require(result.kind == Json::ARRAY, "expected call array");
        if (name.empty()) return result.arr.empty();
        if (result.arr.size() != 1) return false;
        auto fn = result.arr[0].Get(incoming ? "from" : "to");
        return fn && JsonStr(fn->Get("name")) == name;
    };
    return calls(caller, false, changed ? "b" : "a") &&
           calls(a, true, changed ? "" : "caller") &&
           calls(b, true, changed ? "caller" : "");
}

static bool Published(const Root &root, const string &uri, const string &source, const string &message) {
    auto found = root.published.find(uri);
    if (found == root.published.end()) return false;
    for (auto &item : found->second) {
        auto json = ParseJson(item.json);
        if (json.Str("source") == source && json.Str("message").find(message) != string::npos) return true;
    }
    return false;
}

static void TestCordis(char **argv) {
    auto log = getenv("K_CORDIS_TEST_LOG");
    Require(log && *log, "missing Cordis fixture log path");
    filesystem::remove(log);
    auto events = [&]() {
        string text;
        Require(LoadFile(log, text), "missing Cordis fixture log");
        return text;
    };
    auto manifest = cat(argv[3], "/cordis-test.json");
    JsonOut config;
    config.BeginObject();
    config.Member("version", 1);
    config.Member("diagnostics", "a");
    config.Key("providers");
    config.BeginObject();
    const char *ids[] = { "a", "b", "failed", "incompatible" };
    for (int i = 0; i < 4; ++i) config.Member(ids[i], argv[4 + i]);
    config.Member("missing", "./cordis-test-no-library");
    config.EndObject();
    config.EndObject();
    auto f = fopen(manifest.c_str(), "wb");
    Require(f != nullptr, "cannot write Cordis fixture manifest");
    auto wrote = fwrite(config.s.data(), 1, config.s.size(), f);
    Require(fclose(f) == 0 && wrote == config.s.size(), "cannot finish Cordis fixture manifest");

    Server server(ExePath(argv[0]), argv[2]);
    server.ws.diagnostics = CordisDiagnostics::Load(manifest, ExePath(argv[0]));
    Handle(server, "initialize", "{\"capabilities\":{}}");
    auto program = cat(argv[3], "/cordis-test.k");
    auto uri = UriOfPath(CanonicalPath(program));
    Handle(server, "textDocument/didOpen",
           DocumentParams(uri, "fn main() { print(missing_value); }\n"), false);
    server.ws.Settle();
    auto root = server.ws.FindRoot(PathKey(program));
    Require(root && root->analysis, "Cordis fixture was not analyzed");
    auto has = [&](const string &id, const string &message) {
        return Published(*root, uri, id, message);
    };
    Require(has("cordis/a", "diagnostics from a") &&
            has("cordis/a", "observed compiler diagnostic") && has("klang", "missing_value"),
            "provider A did not receive the compiler diagnostics");
    auto select = [&](const string &id) {
        JsonOut params;
        params.BeginObject();
        params.Member("provider", id);
        params.EndObject();
        Handle(server, "klang/cordis/selectDiagnostics", params.s);
    };
    auto in_flight = server.ws.diagnostics.Pin();
    weak_ptr<const CordisDiagnosticProvider> retired = in_flight;
    select("b");
    Require(root->dirty && events() == "start a\nstart b\n", "replacement retired a pinned provider");
    auto &analysis = *root->analysis;
    auto from_a = CordisDiagnostics::Inspect(*in_flight, root->path, *analysis.ast, analysis.diags);
    Require(!from_a.empty() && from_a[0].message == "diagnostics from a",
            "a pinned request could not call the old provider after replacement");
    server.ws.Settle();
    Require(has("cordis/b", "diagnostics from b") && !has("cordis/a", "diagnostics from a"),
            "the next analysis did not use the replacement");
    in_flight.reset();
    Require(retired.expired() && events() == "start a\nstart b\nstop a\n",
            "the retired provider was not stopped after its last request");

    for (auto id : { "failed", "incompatible", "missing", "/not/an/allowed/provider" }) {
        auto before = server.ws.diagnostics.Pin();
        select(id);
        Require(server.ws.diagnostics.Pin() == before && !root->dirty &&
                has("cordis/b", "diagnostics from b"), cat("selection did not roll back for ", id));
    }
    Require(events() == "start a\nstart b\nstop a\nstart failed\nstop failed\n",
            "staged provider cleanup or ABI validation is wrong");
    select("builtin");
    server.ws.Settle();
    Require(!has("cordis/b", "diagnostics from b") && has("klang", "missing_value"),
            "switch to the built-in provider did not clear supplemental diagnostics");
    Require(events() == "start a\nstart b\nstop a\nstart failed\nstop failed\nstop b\n",
            "provider B was not stopped once");
}

int main(int argc, char **argv) {
    try {
        if (argc == 8 && string_view(argv[1]) == "--cordis") {
            TestCordis(argv);
            return 0;
        }
        Require(argc == 3, "usage: k_lsp_test STDLIB FIXTURE");
        const auto uri = UriOfPath(CanonicalPath(argv[2]));
        const string before = "fn a() -> i64 { 1 }\nfn b() -> i64 { 2 }\nfn caller() -> i64 { a() }\n";
        const string after = "fn a() -> i64 { 1 }\nfn b() -> i64 { 2 }\nfn caller() -> i64 { b() }\n";
        int failures = 0;
        // Each query must settle on its own, including a newly added caller.
        for (int first = 0; first < 3; first++) {
            Server server(ExePath(argv[0]), argv[1]);
            Handle(server, "initialize", "{\"capabilities\":{}}");
            Handle(server, "textDocument/didOpen", DocumentParams(uri, before), false);
            Handle(server, "textDocument/prepareCallHierarchy", Position(uri, 2));
            auto caller = Item(server, uri, 2), a = Item(server, uri, 0), b = Item(server, uri, 1);
            Require(Graph(server, caller, a, b, false), "initial graph is wrong");
            auto root = server.ws.FindRoot(PathKey(argv[2]));
            Require(root && root->analysis && !root->dirty, "initial analysis is absent or dirty");
            Handle(server, "textDocument/didChange", DocumentParams(uri, after, true), false);
            Require(root->dirty && root->analysis, "change did not retain dirty analysis");
            const string method = first == 0 ? "callHierarchy/outgoingCalls" : "callHierarchy/incomingCalls";
            Handle(server, method, first == 0 ? caller : first == 1 ? a : b);
            if (!Graph(server, caller, a, b, true) || root->dirty) {
                fprintf(stderr, "FAIL: stale graph after %s (%s)\n", method.c_str(),
                        first == 0 ? "caller" : first == 1 ? "a" : "b");
                failures++;
            }
            // The same saved items must also work after a document request.
            Handle(server, "textDocument/prepareCallHierarchy", Position(uri, 2));
            Require(Graph(server, caller, a, b, true) && !root->dirty, "settled graph is wrong");
        }
        // A workspace must select offline loading even when no CLI initialized it.
        Server server(ExePath(argv[0]), argv[1]);
        auto repo = UriOfPath(CanonicalPath(argv[2]) + ".missing.git");
        auto source = cat("import missing from \"", repo, "@lsp-test\";\nfn main() {}\n");
        Handle(server, "textDocument/didOpen", DocumentParams(uri, source), false);
        server.ws.Settle();
        auto root = server.ws.FindRoot(PathKey(argv[2]));
        Require(root && root->analysis, "offline analysis is absent");
        auto &diags = root->analysis->diags;
        Require(any_of(diags.begin(), diags.end(), [](const Diagnostic &d) {
            return d.message.find("--offline fetches nothing") != string::npos;
        }), "workspace did not report an offline package miss");
        return failures ? 1 : 0;
    } catch (const CompileError &e) {
        fprintf(stderr, "lsp test failed: %s\n", e.msg.c_str());
        return 1;
    } catch (const exception &e) {
        fprintf(stderr, "lsp test failed: %s\n", e.what());
        return 1;
    }
}
