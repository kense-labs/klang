// Optional runner for a locked Cordis TypeScript project profile.
#include "graph.h"
#include "service.h"
#include "typescript_project.h"
#include "../content_digest.h"

namespace klang::cordis {
namespace {
namespace fs = filesystem;

struct Frontend {
    using Api = k_cordis_typescript_frontend_v1_api;
    static constexpr const char *name = "typescript-frontend";
    static constexpr const char *entry = K_CORDIS_TYPESCRIPT_FRONTEND_ENTRY;
    static constexpr uint32_t version = K_CORDIS_TYPESCRIPT_ABI;
    static bool Supports(const Api &api) { return api.configure && api.compile; }
};

struct Runtime {
    using Api = k_cordis_typescript_project_v1_api;
    static constexpr const char *name = "typescript-project";
    static constexpr const char *entry = K_CORDIS_TYPESCRIPT_PROJECT_ENTRY;
    static constexpr uint32_t version = K_CORDIS_TYPESCRIPT_ABI;
    static bool Supports(const Api &api) { return api.run != nullptr; }
};

constexpr auto usage = "usage: k-cordis-ts --manifest FILE --lock FILE [--root DIRECTORY] ENTRY\n";

struct Options { string root = ".", manifest, lock, entry; };

Options Arguments(int argc, char **argv) {
    Options opts;
    map<string_view, string *> flags {{"--root", &opts.root}, {"--manifest", &opts.manifest}, {"--lock", &opts.lock}};
    for (int i = 1; i < argc; ++i) {
        string_view arg(argv[i]);
        if (auto found = flags.find(arg); found != flags.end() && i + 1 < argc && *argv[i + 1]) {
            *found->second = argv[++i];
            flags.erase(found);
        } else if (!arg.empty() && !arg.starts_with('-') && opts.entry.empty()) {
            opts.entry = arg;
        } else {
            throw CompileError { usage };
        }
    }
    if (opts.entry.empty() || opts.manifest.empty() || opts.lock.empty()) throw CompileError { usage };
    return opts;
}

k_cordis_text Slice(string_view text) { return { text.data(), text.size() }; }
string_view View(k_cordis_text text) { return text.size ? string_view(text.data, text.size) : string_view(); }

struct Host {
    fs::path root;
    shared_ptr<const Service<Frontend>::Generation> frontend;
    map<string, string, less<>> sources;
    map<string, k_cordis_source_v1, less<>> aliases;
    string read_error, error;

    k_cordis_source_v1 Open(string_view name) {
        if (auto found = aliases.find(name); found != aliases.end()) return found->second;
        if (name.find_first_of("\\:?#") != string_view::npos || name.find('\0') != string_view::npos)
            throw CompileError { cat("invalid project module path: ", name) };
        auto relative = fs::path(name).lexically_normal();
        if (relative.empty() || relative.has_root_path() || *relative.begin() == "..")
            throw CompileError { cat("module path leaves the project root: ", name) };
        auto extension = relative.extension();
        if (extension != ".ts" && extension != ".mts" && extension != ".js" && extension != ".mjs")
            throw CompileError { cat("expected a .ts, .mts, .js or .mjs module: ", name) };
        error_code ec;
        auto physical = fs::canonical(root / relative, ec);
        if (ec == errc::no_such_file_or_directory && (extension == ".js" || extension == ".mjs")) {
            relative.replace_extension(extension == ".js" ? ".ts" : ".mts");
            physical = fs::canonical(root / relative, ec);
        }
        if (ec) throw CompileError { cat("cannot load module ", name, ": ", ec.message()) };
        auto local = physical.lexically_relative(root);
        if (local.empty() || *local.begin() == "..")
            throw CompileError { cat("module path leaves the project root: ", name) };
        auto path = local.generic_string();
        auto [source, inserted] = sources.try_emplace(std::move(path));
        if (inserted && (!fs::is_regular_file(physical, ec) || !LoadFile(physical.string(), source->second))) {
            sources.erase(source);
            throw CompileError { cat("cannot read module ", name) };
        }
        k_cordis_source_v1 module { Slice(source->first), Slice(source->second) };
        aliases.try_emplace(string(name), module);
        aliases.try_emplace(source->first, module);
        return module;
    }

    static int32_t Read(void *opaque, k_cordis_text path, k_cordis_source_v1 *result, k_cordis_text *error) {
        auto &self = *static_cast<Host *>(opaque);
        try {
            *result = self.Open(View(path));
            return 0;
        } catch (CompileError &failure) {
            self.read_error = std::move(failure.msg);
            *error = Slice(self.read_error);
        } catch (...) {
            *error = Slice("cannot read the requested project module");
        }
        return 1;
    }

    static int32_t Write(void *, int32_t stream, k_cordis_text text) {
        auto *file = stream == 1 ? stdout : stream == 2 ? stderr : nullptr;
        return file && (!text.size || fwrite(text.data, 1, text.size, file) == text.size) && fflush(file) == 0;
    }

    static int32_t Error(void *opaque, k_cordis_text text) {
        try {
            static_cast<Host *>(opaque)->error = View(text);
            return 1;
        } catch (...) {
            return 0;
        }
    }

    static int32_t Compile(void *opaque, k_cordis_source_v1 module, k_cordis_ts_emit_module_v1 emit, void *sink) {
        try {
            return static_cast<Host *>(opaque)->frontend->Call<&Frontend::Api::compile>(module, emit, sink);
        } catch (...) {
            return 1;
        }
    }
};

int Run(const Options &opts) {
    auto root = fs::canonical(opts.root);
    if (!fs::is_directory(root)) throw CompileError { "the TypeScript project root must be a directory" };
    auto graph = Catalog::Load(opts.manifest, {}).Resolve(Runtime::name);
    graph.VerifyLock(opts.lock);
    graph.VerifyInputs();
    const auto &runtime = graph.providers.at(Runtime::name);
    if (graph.providers.size() != 2 || !runtime.requirements.contains(Frontend::name))
        throw CompileError { "the TypeScript project ABI v1 requires a typescript-frontend dependency" };
    const auto &front = graph.providers.at(Frontend::name);
    auto compiler_input = front.inputs.find("compiler");
    if (compiler_input == front.inputs.end()) throw CompileError { "the TypeScript frontend requires a tracked compiler input" };
    const auto &input = compiler_input->second;
    string compiler;
    if (!LoadFile(input.path, compiler) || Sha256(compiler) != input.digest)
        throw CompileError { "cannot read the verified TypeScript compiler input" };

    Service<Frontend> frontend;
    frontend.RegisterLibrary(front.id, front.artifact);
    frontend.Select(front.id);
    Host host { std::move(root), frontend.Pin() };
    if (host.frontend->Call<&Frontend::Api::configure>(Slice(compiler), Host::Error, &host))
        throw CompileError { host.error.empty() ? "cannot initialize the TypeScript frontend" : std::move(host.error) };

    Service<Runtime> service;
    service.RegisterLibrary(runtime.id, runtime.artifact);
    service.Select(runtime.id, { host.frontend });
    graph.VerifyInputs();
    graph.VerifyLock(opts.lock);

    const k_cordis_ts_project_request_v1 request { Slice(opts.entry), &host, Host::Read, Host::Write, Host::Compile, Host::Error };
    if (service.Pin()->Call<&Runtime::Api::run>(&request))
        throw CompileError { host.error.empty() ? "the TypeScript project failed" : std::move(host.error) };
    return 0;
}

} // namespace
} // namespace klang::cordis

int main(int argc, char **argv) {
    using namespace klang;
    using namespace klang::cordis;
    if (argc == 2 && string_view(argv[1]) == "--help") {
        fputs(usage, stdout);
        return 0;
    }
    try {
        return Run(Arguments(argc, argv));
    } catch (const CompileError &failure) {
        fprintf(stderr, "%s\n", failure.msg.c_str());
    } catch (const exception &failure) {
        fprintf(stderr, "%s\n", failure.what());
    }
    return 1;
}
