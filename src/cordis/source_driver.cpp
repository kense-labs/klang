// Optional Cordis build host. The compiler receives only the generated K tree.
#include "graph.h"
#include "service.h"
#include "source_transform.h"

#include "../content_digest.h"
#include "../json.h"
#include "../paths.h"
#include "../process.h"

#include <fstream>
#include <random>

namespace klang::cordis {
namespace {
namespace fs = filesystem;
constexpr const char *service_name = "source-transform";

struct Contract {
    using Api = k_cordis_source_transform_v1_api;
    static constexpr const char *name = service_name;
    static constexpr const char *entry = K_CORDIS_SOURCE_TRANSFORM_ENTRY;
    static constexpr uint32_t version = K_CORDIS_SOURCE_TRANSFORM_ABI;
    static constexpr size_t minimum_size = offsetof(Api, last_error);
    static bool Supports(const Api &api) { return api.transform != nullptr; }
};

[[noreturn]] void Fail(string message) { throw CompileError { std::move(message) }; }

struct Options {
    bool build = false;
    string compiler = "k", root = ".", manifest, lock, out, cc;
    vector<string> sources;
};

constexpr auto usage =
    "usage: k-cordis-source check|build --manifest FILE --lock FILE [--compiler K]\n"
    "       [--root DIRECTORY] [--input RELATIVE_FILE ...] [--cc COMPILER] [-o BUNDLE] ENTRY\n";

Options Arguments(int argc, char **argv) {
    Options opts;
    string_view action = argc > 1 ? argv[1] : "";
    if (action != "check" && action != "build") Fail(usage);
    opts.build = action == "build";
    map<string_view, string *> flags {
        { "--compiler", &opts.compiler }, { "--root", &opts.root }, { "--manifest", &opts.manifest },
        { "--lock", &opts.lock }, { "-o", &opts.out }, { "--cc", &opts.cc },
    };
    set<string_view> seen;
    string entry;
    for (int i = 2; i < argc; i++) {
        string_view arg = argv[i];
        if (arg == "--input") {
            if (++i == argc) Fail(usage);
            opts.sources.push_back(argv[i]);
        } else if (auto flag = flags.find(arg); flag != flags.end()) {
            if (!seen.insert(arg).second || ++i == argc || !*argv[i]) Fail(usage);
            *flag->second = argv[i];
        } else if (!arg.empty() && !arg.starts_with('-') && entry.empty()) {
            entry = arg;
        } else {
            Fail(usage);
        }
    }
    if (opts.manifest.empty() || opts.lock.empty() || entry.empty() ||
        (opts.build ? opts.out.empty() : !opts.out.empty() || !opts.cc.empty())) Fail(usage);
    opts.sources.insert(opts.sources.begin(), std::move(entry));
    return opts;
}

// These names are written below a private directory on each supported host.
string FoldPath(string name) {
    for (auto &ch : name) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return name;
}

string LogicalPath(string name) {
    auto lower = FoldPath(name);
    string_view remaining(lower);
    do {
        auto slash = remaining.find('/');
        auto part = remaining.substr(0, slash);
        auto stem = part.substr(0, part.find('.'));
        if (part.empty() || part.back() == '.' || !all_of(part.begin(), part.end(), [](char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
            }) || stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             stem.back() >= '0' && stem.back() <= '9'))
            Fail(cat("invalid relative source or artifact name: ", name));
        if (slash == string::npos) break;
        remaining.remove_prefix(slash + 1);
    } while (true);
    return name;
}

string Read(const fs::path &path) {
    string text;
    if (!fs::is_regular_file(path) || !LoadFile(path.string(), text))
        Fail(cat("cannot read input ", path.string()));
    return text;
}

void Write(const fs::path &path, string_view text) {
    fs::create_directories(path.parent_path());
    ofstream out(path, ios::binary);
    if (!text.empty()) out.write(text.data(), static_cast<streamsize>(text.size()));
    out.close();
    if (!out) Fail(cat("cannot write ", path.string()));
}

struct Input {
    string name;
    fs::path path;
    string text;
    string digest;
};

vector<Input> ProjectInputs(const fs::path &root, const vector<string> &names) {
    vector<Input> inputs;
    set<string> seen;
    for (const auto &name : names) {
        auto path = fs::canonical(root / LogicalPath(name));
        auto relative = path.lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..") Fail(cat("source is outside the project: ", name));
        if (!seen.insert(FoldPath(name)).second) Fail(cat("duplicate source input: ", name));
        auto text = Read(path);
        auto digest = Sha256(text);
        inputs.push_back({ name, std::move(path), std::move(text), std::move(digest) });
    }
    return inputs;
}

k_cordis_text Text(string_view value) { return { value.data(), value.size() }; }

string Copy(k_cordis_text value) {
    if (!value.size) return {};
    if (!value.data || value.size > string().max_size()) Fail("invalid source transform byte slice");
    return string(value.data, static_cast<size_t>(value.size));
}

using Files = map<string, string>;
struct Span {
    string generated;
    size_t offset, source, original, size;
};

struct Output {
    string entry;
    Files modules, artifacts;
    vector<Span> spans;
};

struct TransformDiagnostic : CompileError {
    string file;
    int32_t line, column, end_line, end_column;
};

struct FailedTransform {
    const vector<Input> &sources;
    optional<TransformDiagnostic> error;

    static int32_t Report(void *opaque, const k_cordis_diagnostic_v1 *diagnostic) noexcept {
        auto &self = *static_cast<FailedTransform *>(opaque);
        if (!diagnostic || self.error) return 0;
        try {
            auto message = Copy(diagnostic->message);
            if (message.empty() || diagnostic->file_index < -1) return 0;
            string file;
            if (diagnostic->file_index >= 0) {
                auto index = static_cast<size_t>(diagnostic->file_index);
                if (index >= self.sources.size() || diagnostic->line < 1 || diagnostic->column < 1) return 0;
                file = self.sources[index].path.string();
            }
            self.error = TransformDiagnostic { { std::move(message) }, std::move(file), diagnostic->line,
                diagnostic->column, diagnostic->end_line, diagnostic->end_column };
            return 1;
        } catch (...) {
            return 0;
        }
    }
};

Files CopyFiles(const k_cordis_source_v1 *items, uint64_t count, bool modules) {
    if (count && !items) Fail("missing output files");
    Files files;
    set<string> seen;
    for (uint64_t i = 0; i < count; ++i) {
        auto name = LogicalPath(Copy(items[i].path));
        if (!seen.insert(FoldPath(name)).second) Fail(cat("duplicate output path: ", name));
        auto text = Copy(items[i].contents);
        if (modules && (!name.ends_with(".k") || text.find('\0') != string::npos))
            Fail(cat("generated source must be a K module: ", name));
        files.emplace(std::move(name), std::move(text));
    }
    return files;
}

bool Fits(size_t length, uint64_t start, uint64_t size) { return start <= length && size <= length - start; }

struct Capture {
    const vector<Input> &inputs;
    optional<Output> output;
    exception_ptr failure;

    static int32_t Emit(void *opaque, const k_cordis_source_result_v1 *result) noexcept {
        auto &self = *static_cast<Capture *>(opaque);
        if (self.failure) return 0;
        try {
            if (!result || self.output) Fail("expected one result from the source transform");
            Output out { LogicalPath(Copy(result->entry)), CopyFiles(result->modules, result->module_count, true),
                         CopyFiles(result->artifacts, result->artifact_count, false), {} };
            if (!out.modules.count(out.entry)) Fail("source transform did not emit the entry module");
            if (result->span_count && !result->spans) Fail("missing source spans");
            for (uint64_t i = 0; i < result->span_count; ++i) {
                const auto &span = result->spans[i];
                auto name = Copy(span.generated_path);
                const auto generated = out.modules.find(name);
                if (!span.size || generated == out.modules.end() || span.source_index >= self.inputs.size() ||
                    !Fits(generated->second.size(), span.generated_offset, span.size) ||
                    !Fits(self.inputs[static_cast<size_t>(span.source_index)].text.size(), span.source_offset, span.size))
                    Fail("source span refers outside the generated or original source");
                out.spans.push_back({ std::move(name), static_cast<size_t>(span.generated_offset),
                                      static_cast<size_t>(span.source_index), static_cast<size_t>(span.source_offset),
                                      static_cast<size_t>(span.size) });
            }
            sort(out.spans.begin(), out.spans.end(), [](const Span &a, const Span &b) {
                return a.generated == b.generated ? a.offset < b.offset : a.generated < b.generated;
            });
            for (size_t i = 1; i < out.spans.size(); ++i) {
                const auto &a = out.spans[i - 1];
                const auto &b = out.spans[i];
                if (a.generated == b.generated && b.offset < a.offset + a.size) Fail("overlapping source spans");
            }
            self.output = std::move(out);
            return 1;
        } catch (...) {
            self.failure = current_exception();
            return 0;
        }
    }
};

Output Transform(const Graph &graph, const vector<Input> &originals) {
    const auto &provider = graph.providers.at(service_name);
    if (provider.linked || graph.providers.size() != 1)
        throw GraphError { { "the source driver accepts one dynamic source transform in ABI v1" },
                           "unsupported_service", graph.DependencyChain(service_name) };
    vector<string> settings;
    vector<k_cordis_source_v1> sources, inputs;
    settings.reserve(provider.inputs.size());
    for (const auto &source : originals) sources.push_back({ Text(source.name), Text(source.text) });
    for (const auto &[name, input] : provider.inputs) {
        auto &text = settings.emplace_back(Read(input.path));
        if (Sha256(text) != input.digest) Fail(cat("tracked input changed while reading ", name));
        inputs.push_back({ Text(name), Text(text) });
    }
    try {
        Service<Contract> service;
        service.RegisterLibrary(provider.id, provider.artifact);
        service.Select(provider.id);
        const k_cordis_source_request_v1 request { sources.data(), sources.size(), inputs.data(), inputs.size() };
        Capture capture { originals, {}, {} };
        auto generation = service.Pin();
        auto status = generation->Call<&Contract::Api::transform>(&request, &Capture::Emit, &capture);
        if (capture.failure) rethrow_exception(capture.failure);
        if (status) {
            FailedTransform failure { originals, {} };
            if (generation->TryQuery<&Contract::Api::last_error, Contract::minimum_size>(
                    &FailedTransform::Report, &failure) && failure.error)
                throw std::move(*failure.error);
        }
        if (status || !capture.output) Fail(cat("source transform failed with status ", status));
        return std::move(*capture.output);
    } catch (const TransformDiagnostic &) {
        throw;
    } catch (const AbiError &e) {
        throw GraphError { e, "incompatible_abi", graph.DependencyChain(service_name) };
    } catch (const CompileError &e) {
        throw GraphError { e, "source_transform_failed", graph.DependencyChain(service_name) };
    }
}

struct Stage {
    fs::path path;
    explicit Stage(const fs::path &parent) {
        random_device next;
        for (int i = 0; i < 32; i++) {
            auto candidate = parent / cat(".k-source-", next(), "-", next());
            if (fs::create_directory(candidate)) {
#ifndef _WIN32
                error_code error;
                fs::permissions(candidate, fs::perms::owner_all, error);
                if (error) {
                    error_code ignored;
                    fs::remove(candidate, ignored);
                    Fail(cat("cannot protect the private source directory: ", error.message()));
                }
#endif
                path = std::move(candidate);
                return;
            }
        }
        Fail("cannot create a private source build directory");
    }
    ~Stage() {
        error_code error;
        if (!path.empty()) fs::remove_all(path, error);
    }
    Stage(const Stage &) = delete;
};

optional<size_t> Offset(string_view text, const Json *line, const Json *col) {
    auto position = [&](const Json *item) {
        return item && item->kind == Json::NUMBER && item->num >= 1 &&
               item->num <= text.size() + 1 && floor(item->num) == item->num;
    };
    if (!position(line) || !position(col)) return {};
    size_t start = 0;
    for (size_t row = 1; row < static_cast<size_t>(line->num); ++row) {
        auto next = text.find('\n', start);
        if (next == string::npos) return {};
        start = next + 1;
    }
    auto end = text.find('\n', start);
    if (end == string::npos) end = text.size();
    size_t column = static_cast<size_t>(col->num) - 1;
    if (column > end - start) return {};
    return start + column;
}

void Position(Json &issue, string_view text, size_t offset, const char *line, const char *col) {
    size_t row = 1, column = 1;
    for (char ch : text.substr(0, offset)) {
        if (ch == '\n') ++row, column = 1;
        else ++column;
    }
    for (auto [key, value] : { pair{line, row}, pair{col, column} }) {
        Json number;
        number.kind = Json::NUMBER;
        number.num = static_cast<double>(value);
        issue.obj.emplace_back(key, std::move(number));
    }
}

void Remap(Json &issue, const Output &output, const vector<Input> &inputs, const fs::path &modules) {
    if (issue.kind != Json::OBJECT) return;
    for (auto &[key, value] : issue.obj)
        if (key == "related")
            for (auto &related : value.arr) Remap(related, output, inputs, modules);
    auto file = issue.Str("file");
    if (file.empty()) return;
    fs::path relative = fs::path(file).lexically_normal();
    if (relative.is_absolute()) relative = relative.lexically_relative(modules);
    auto name = relative.generic_string();
    const auto module = output.modules.find(name);
    if (module == output.modules.end()) return;
    auto start = Offset(module->second, issue.Get("line"), issue.Get("col"));
    auto end = Offset(module->second, issue.Get("endLine"), issue.Get("endCol"));
    for (auto &[key, value] : issue.obj)
        if (key == "file") value.str = cat("source/", name);
    erase_if(issue.obj, [](const auto &item) { return item.first == "text"; });
    if (!start) return;
    for (const auto &span : output.spans) {
        if (span.generated != name || *start < span.offset || *start - span.offset >= span.size) continue;
        const auto &source = inputs[span.source];
        for (auto &[key, value] : issue.obj)
            if (key == "file") value.str = source.path.string();
        erase_if(issue.obj, [](const auto &item) {
            const auto &key = item.first;
            return key == "line" || key == "col" || key == "endLine" || key == "endCol";
        });
        Position(issue, source.text, span.original + *start - span.offset, "line", "col");
        if (end && *end >= *start && *end - span.offset <= span.size)
            Position(issue, source.text, span.original + *end - span.offset, "endLine", "endCol");
        break;
    }
}

void WriteJson(JsonOut &out, const Json &value) {
    switch (value.kind) {
        case Json::STRING: out.Str(value.str); break;
        case Json::NUMBER:
            if (abs(value.num) < 0x1p53 && floor(value.num) == value.num) out.Int(static_cast<int64_t>(value.num));
            else out.Raw(cat(value.num));
            break;
        case Json::BOOL: out.Bool(value.b); break;
        case Json::NUL: out.Null(); break;
        case Json::ARRAY:
            out.BeginArray();
            for (const auto &item : value.arr) WriteJson(out, item);
            out.EndArray();
            break;
        case Json::OBJECT:
            out.BeginObject();
            for (const auto &[key, item] : value.obj) {
                out.Key(key);
                WriteJson(out, item);
            }
            out.EndObject();
            break;
    }
}

string Receipt(const Graph &graph, const vector<Input> &inputs, const Output &output,
               string_view compiler, string_view program) {
    JsonOut out;
    out.BeginObject();
    out.Member("version", 1);
    out.Member("entry", output.entry);
    out.Member("compiler_sha256", compiler);
    out.Member("program_sha256", program);
    out.Key("cordis");
    out.Raw(graph.Document());
    out.Key("inputs");
    out.BeginObject();
    for (const auto &input : inputs) out.Member(input.name, input.digest);
    out.EndObject();
    auto files = [&](const char *key, const Files &contents) {
        out.Key(key);
        out.BeginObject();
        for (const auto &[name, text] : contents) out.Member(name, Sha256(text));
        out.EndObject();
    };
    files("modules", output.modules);
    files("artifacts", output.artifacts);
    out.Key("spans");
    out.BeginArray();
    for (const auto &span : output.spans) {
        out.BeginObject();
        out.Member("generated", span.generated);
        out.Member("offset", static_cast<int64_t>(span.offset));
        out.Member("source", inputs[span.source].name);
        out.Member("source_offset", static_cast<int64_t>(span.original));
        out.Member("size", static_cast<int64_t>(span.size));
        out.EndObject();
    }
    out.EndArray();
    out.EndObject();
    return out.s + "\n";
}

void RequireNew(const fs::path &path) {
    if (fs::symlink_status(path).type() != fs::file_type::not_found)
        Fail(cat("output already exists: ", path.string()));
}

int Run(const Options &opts) {
    auto executable = FindOnPath(opts.compiler);
    if (executable.empty()) Fail(cat("cannot find the K compiler: ", opts.compiler));
    auto compiler = fs::absolute(executable);
    auto compiler_identity = BuildPathIdentity(compiler.string());
    auto compiler_digest = opts.build ? Sha256File(compiler) : nullopt;
    if (compiler_identity.empty() || compiler_identity == "missing" || (opts.build && !compiler_digest))
        Fail("cannot read the K compiler");
    optional<fs::path> configured_home;
    if (auto home = getenv("K_HOME"); home && *home) configured_home = fs::absolute(home).lexically_normal();
    auto root = fs::canonical(opts.root);
    auto original = ProjectInputs(root, opts.sources);
    auto graph = Catalog::Load(opts.manifest, {}).Resolve(service_name);
    graph.VerifyLock(opts.lock);
    graph.VerifyInputs();
    auto verify = [&] {
        graph.VerifyInputs();
        graph.VerifyLock(opts.lock);
        for (const auto &input : original)
            if (fs::canonical(root / input.name) != input.path || Sha256File(input.path) != input.digest)
                Fail(cat("project source changed after capture: ", input.name));
        if (BuildPathIdentity(compiler.string()) != compiler_identity) Fail("K compiler changed during source build");
    };
    fs::path destination;
    if (opts.build) {
        destination = fs::absolute(opts.out).lexically_normal();
        if (destination.has_relative_path() && destination.filename().empty()) destination = destination.parent_path();
        RequireNew(destination);
        if (configured_home) {
            auto relative = fs::weakly_canonical(*configured_home).lexically_relative(fs::weakly_canonical(destination));
            if (!relative.empty() && *relative.begin() != "..") Fail("K_HOME must be outside the output bundle");
        }
        fs::create_directories(destination.parent_path());
    }
    auto output = Transform(graph, original);
    verify();

    Stage stage(opts.build ? destination.parent_path() : fs::temp_directory_path());
    const auto modules = stage.path / "source";
    for (const auto &[name, text] : output.modules) Write(modules / name, text);
    for (const auto &[name, text] : output.artifacts) Write(stage.path / "assets" / name, text);

#ifdef _WIN32
    constexpr auto program = "program.exe";
#else
    constexpr auto program = "program";
#endif
    auto staged_program = stage.path / program;
    vector<string> command { compiler.string(), opts.build ? "build" : "check", "--offline", "--diagnostics", "json",
                             cat("./", output.entry) };
    if (opts.build) {
        command.insert(command.end(), { "-o", staged_program.string() });
        if (!opts.cc.empty()) command.insert(command.end(), { "--cc", opts.cc.find_first_of("/\\") == string::npos
                                                            ? opts.cc : fs::absolute(opts.cc).string() });
    }
    const auto cache = stage.path / "cache";
    SetEnvVar("K_HOME", configured_home.value_or(cache).string());
    string log;
    auto process = RunProcess(command, modules.string(), false, &log);
    if (opts.build && process.code == 0) {
        auto built = cat("built ", staged_program.string(), log.ends_with("\r\n") ? "\r\n" : "\n");
        if (log.ends_with(built)) log.resize(log.size() - built.size());
    }
    if (!log.empty()) fwrite(log.data(), 1, log.size(), stderr);
    if (!process.started) Fail(std::move(process.output));
    if (process.output.empty()) Fail(cat("K compiler returned no diagnostics (status ", process.code, ")"));
    auto diagnostics = ParseJson(process.output);
    if (diagnostics.kind != Json::ARRAY) Fail("expected a JSON diagnostic array from K");
    for (auto &issue : diagnostics.arr) Remap(issue, output, original, modules);
    verify();

    if (opts.build && process.code == 0) {
        auto digest = Sha256File(staged_program);
        if (!digest) Fail("K did not produce a native program");
        if (!configured_home) fs::remove_all(cache);
        Write(stage.path / "source-build.json", Receipt(graph, original, output, *compiler_digest, *digest));
        RequireNew(destination);
        fs::rename(stage.path, destination);
        stage.path.clear();
        fprintf(stderr, "built %s\n", (destination / program).string().c_str());
    }
    JsonOut result;
    WriteJson(result, diagnostics);
    puts(result.s.c_str());
    return process.code < 0 ? 1 : process.code;
}

void Report(string_view message, const GraphError *error = nullptr, const TransformDiagnostic *diagnostic = nullptr) {
    JsonOut out;
    out.BeginArray();
    out.BeginObject();
    out.Member("severity", "error");
    out.Member("source", "cordis/source-transform");
    out.Member("message", message);
    if (diagnostic && !diagnostic->file.empty()) {
        out.Member("file", diagnostic->file);
        out.Member("line", diagnostic->line);
        out.Member("col", diagnostic->column);
        if (diagnostic->end_line > 0 && diagnostic->end_column > 0) {
            out.Member("endLine", diagnostic->end_line);
            out.Member("endCol", diagnostic->end_column);
        }
    }
    if (error) {
        out.Key("cordis");
        out.Raw(error->Data());
    }
    out.EndObject();
    out.EndArray();
    puts(out.s.c_str());
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
    } catch (const GraphError &error) {
        Report(error.msg, &error);
    } catch (const TransformDiagnostic &error) {
        Report(error.msg, nullptr, &error);
    } catch (const CompileError &error) {
        Report(error.msg);
    } catch (const exception &error) {
        Report(error.what());
    }
    return 1;
}
