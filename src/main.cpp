// K compiler — driver. Includes establish the project-wide header order.

#include "includes.h"
#include "build_paths.h"   // K_STDLIB_PATH
#include "utils.h"
#include "paths.h"
#include "small_vector.h"
#include "flat_map.h"
#include "lexer.h"
#include "ast.h"
#include "dump.h"
#include "clone.h"
#include "parser.h"
#include "resolve.h"
#include "builtins.h"
#include "gfx.h"
#include "typecheck.h"
#include "typecheck_types.h"
#include "typecheck_exprs.h"
#include "typecheck_expansion.h"
#include "typecheck_decorators.h"
#include "typecheck_flow.h"
#include "typecheck_calls.h"
#include "typecheck_builtins.h"
#include "typecheck_nodes.h"
#include "typecheck_task_liveness.h"
#include "typecheck_ownership.h"
#include "optimize.h"
#include "optimize_basecase.h"
#include "optimize_tre.h"
#include "optimize_loops.h"
#include "optimize_reassoc.h"
#include "bce.h"
#include "codegen.h"
#include "codegen_types.h"
#include "codegen_frames.h"
#include "codegen_values.h"
#include "codegen_construct.h"
#include "codegen_stmts.h"
#include "codegen_calls.h"
#include "codegen_builtins.h"
#include "codegen_render.h"
#include "codegen_task_flow.h"
#include "codegen_emit.h"
#include "codegen_stacks.h"
#include "codegen_merge.h"
#include "codegen_nodes.h"
#include "runtime_inline.h"
#include "subproc.h"
#include "shared_library.h"
#include "jit.h"
#include "progress.h"
#include "buildfile.h"
#include "json.h"
#include "toolchain.h"
#include "builtin.h"
#include "packages.h"
#include "program.h"
#include "build.h"
#include "native.h"
#include "format.h"
#include "lsp_rpc.h"
#include "lsp_analysis.h"
#include "cordis_diagnostics.h"
#include "lsp_workspace.h"
#include "lsp_features.h"
#include "lsp.h"

namespace klang {

// In version.cpp: the compiler's version, as the build found it.
const char *CompilerVersion();

string ExePath(const char *argv0);

// Parses a program's dump as a program of its own and requires that dumping
// that gives back the same text: what `k --dump` of the dump written to a
// file would print. `path` is where the dump's diagnostics say they are.
void CheckRoundtrip(const string &dumped, const string &path,
                    const ProgramLoadOptions &options) {
    Ast again;
    try {
        ParseProgram(again, path, options,
                     [&](const string &p) -> const string * { return p == path ? &dumped : nullptr; });
    } catch (CompileError &e) {
        throw CompileError { cat("the dump does not parse again:\n", e.msg) };
    }
    string redumped;
    again.Dump(redumped);
    if (redumped == dumped) return;
    // The first line that differs, which is where the two parses part.
    size_t pos = 0, line = 1;
    while (pos < dumped.size() && pos < redumped.size() && dumped[pos] == redumped[pos]) {
        if (dumped[pos] == '\n') line++;
        pos++;
    }
    auto lineat = [&](const string &s) {
        auto start = pos ? s.rfind('\n', pos - 1) : string::npos;
        start = start == string::npos ? 0 : start + 1;
        auto end = s.find('\n', pos);
        return s.substr(start, end == string::npos ? string::npos : end - start);
    };
    throw CompileError { cat("the dump does not roundtrip: its line ", line, " reads\n    ",
                             lineat(dumped), "\nand dumps again as\n    ", lineat(redumped)) };
}

void DumpTokens(const string &path) {
    string contents;
    if (!LoadSource(path, contents)) throw CompileError { cat("cannot open file: ", path) };
    Lexer lex(path, contents.c_str());
    string s;
    while (lex.tok != T_EOF) {
        Append(s, TName(lex.tok));
        switch (lex.tok) {
            case T_IDENT:  Append(s, " ", lex.attr); break;
            case T_INTLIT: Append(s, " ", lex.ival, " (", lex.attr, ")"); break;
            case T_FLTLIT: Append(s, " ", lex.fval); break;
            case T_STRLIT: s += " "; EscapeString(s, lex.sval, '"'); break;
            default: break;
        }
        s += "\n";
        lex.Next();
    }
    fputs(s.c_str(), stdout);
}

// The runtime C sources embedded into the compiler (runtime_inline.h), in the
// order a standalone program holds them (runtime.h has how they divide), and
// klang.h, the header native parts compile against (native.h).
static const char *runtimefiles[] = { "runtime.h", "runtime_impl.h", "runtime_threads.h",
                                      "runtime_ext.h", "runtime_os.h", "klang.h", "jit_c.h",
                                      "runtime_tasks.h" };

static string RuntimeSections(std::initializer_list<const char *> names) {
    string s;
    for (auto name : names)
        for (auto &rf : runtime_files)
            if (string_view(rf.name) == name)
                Append(s, "/* ==== ", rf.name, " ==== */\n", rf.text, "\n");
    return s;
}

// What a separately built program and its runtime object must agree on: a
// hash of the runtime's text, which names the runtime's entry point.
string RuntimeVersion() {
    uint64_t h = 14695981039346656037ull;
    for (auto &rf : runtime_files)
        for (auto c : string_view(rf.text)) h = (h ^ (uint8_t)c) * 1099511628211ull;
    char buf[17];
    snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)(h >> 16));
    return buf;
}

// The runtime object's C (k --emit-runtime): the runtime that programs
// written with -o declare extern, for any program this compiler writes.
string RuntimeObjectSource() {
    return cat("/* The K runtime, written by `k --emit-runtime`. Do not edit.\n"
               "   Compile it once, with the C compiler and flags of the programs it is\n"
               "   for, and link the object with every program `k -o` writes (all\n"
               "   but --standalone ones). It needs no -D: a program hands the runtime\n"
               "   its configuration as it starts. A program from another version of\n"
               "   the compiler fails to link against it, missing k_rt_start_<version>. */\n\n"
               "#define K_SEPARATE_RUNTIME 1\n#define K_RUNTIME_OBJECT 1\n"
               "#define K_NEED_THREADS 1\n#define K_RUNTIME_VERSION ", RuntimeVersion(),
               "\n\n", RuntimeSections({ "runtime.h", "runtime_impl.h", "runtime_threads.h",
                                          "runtime_ext.h", "runtime_os.h" }));
}

// Locates the src/runtime/ directory (only needed by --gen-runtime-header):
// an explicit env override, next to the executable, or relative to it in the
// source tree layout.
string FindRuntimeDir(const char *exe) {
    vector<string> cands;
    if (auto env = getenv("K_RUNTIME")) cands.push_back(cat(env, "/"));
    auto exedir = DirOf(exe);
    cands.push_back(cat(exedir, "runtime/"));
    cands.push_back(cat(exedir, "../../src/runtime/"));
    cands.push_back(cat(exedir, "../src/runtime/"));
    cands.push_back("src/runtime/");
    for (auto &dir : cands) {
        string probe;
        if (LoadFile(cat(dir, runtimefiles[0]), probe)) return dir;
    }
    throw CompileError { "cannot locate the runtime sources (src/runtime); "
                         "set K_RUNTIME" };
}

// Regenerates src/runtime_inline.h from src/runtime/, so the compiler binary
// is self-contained. Anyone editing the runtime re-runs this and rebuilds.
void GenRuntimeHeader(const char *exe) {
    auto dir = FindRuntimeDir(exe);
    string out = "// Generated by `k --gen-runtime-header` from src/runtime/. "
                 "Do not edit.\n#pragma once\n\nnamespace klang {\n\n"
                 "struct RuntimeFile {\n    const char *name;\n    string_view text;\n"
                 "};\n\ninline const RuntimeFile runtime_files[] = {\n";
    for (auto rf : runtimefiles) {
        string src;
        if (!LoadFile(cat(dir, rf), src))
            throw CompileError { cat("cannot read runtime file: ", dir, rf) };
        if (src.find(")KRT\"") != string::npos)
            throw CompileError { cat(rf, " contains the raw-string delimiter )KRT\"") };
        Append(out, "    { \"", rf, "\", string_view(\n");
        // Blank-line boundaries keep chunks stable when earlier text changes.
        // Older MSVC caps single literals at ~16K, so long paragraphs also split.
        size_t pos = 0;
        while (pos < src.size()) {
            auto end = std::min(pos + 8000, src.size());
            for (auto line = pos; line < end;) {
                auto nl = src.find('\n', line);
                if (nl == string::npos || nl >= end) break;
                if (src.find_first_not_of(" \t\r", line) >= nl) {
                    end = nl + 1;
                    break;
                }
                line = nl + 1;
            }
            if (end < src.size() && src[end - 1] != '\n') {
                auto nl = src.rfind('\n', end - 1);
                if (nl != string::npos && nl >= pos) end = nl + 1;
            }
            Append(out, "R\"KRT(", string_view(src).substr(pos, end - pos), ")KRT\"\n");
            pos = end;
        }
        if (src.empty()) out += "\"\"\n";
        out += "    ) },\n";
    }
    out += "};\n\n}  // namespace klang\n";
    auto path = cat(dir, "../runtime_inline.h");
    auto f = fopen(path.c_str(), "wb");
    if (!f) throw CompileError { cat("cannot write ", path) };
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    printf("wrote %s (%d bytes)\n", path.c_str(), (int)out.size());
}

// At the end of this file, past the system headers it needs.
int RunOnCompilerStack(const function<int()> &fn);

// What --multi-test prints on a line of its own on stdout and on stderr after
// each file, with the exit code a run on the file alone would have had and
// the file's name as given.
constexpr const char *MULTIMARK = "==== k --multi-test: exit";

// The options run, build and check share, for their help.
constexpr const char *COMPILEOPTIONS =
R"(  --stats               Print what the compile did: the declarations parsed,
                        the specializations, the optimizations, the bounds
                        checks removed and the files written.
  --color when          Color errors and warnings: auto (on a terminal, unless
                        NO_COLOR is set), always or never.
  --diagnostics json    Print the errors and warnings as JSON on stdout, and
                        the --stats lines on stderr.
  --offline             Fetch nothing: a package that is not fetched is an
                        error at its import, which k fetch resolves.
  -D NAME[=VALUE]       Define a C macro in the generated program (or -DNAME).
  --stdlib dir          The standard library's directory (or K_STDLIB);
                        found from where k is by default.
  --update              Fetch packages at a tag or branch again (a commit is
                        never fetched again).
  --stack-reserve size  Address space reserved per data stack, from 64K, with a
                        K, M, G or T suffix (default 2G).
  --stacks              Report the data stacks the program and each worker can
                        take, and the cap that puts on hardware_threads().
  --no-bce              Keep every bounds check.
  --bce-lines           Report the bounds checks elided and kept on each line.
  --specs               Print the specializations of each function.
)";

constexpr const char *DEBUGOPTION =
R"(  --debug               Overflow, range and tag checks that name the source
                        line they fail on.
)";

constexpr const char *CORDISOPTIONS =
R"(  --cordis-manifest file
                        Select trusted diagnostics providers for this invocation.
  --cordis-lock file    Verify the provider graph before activating it. Generate
                        the lock with k lsp --cordis-manifest file --cordis-graph.
                        Supply both options together.
)";

// The help on a command or topic, `k help` or `k --help`'s for ""
// or "help", and "" for none. Each starts with its usage lines and a blank
// line, which a command line without a file prints.
string HelpText(const string &topic) {
    if (topic.empty() || topic == "help") return
R"(usage: k <command> file.k [options]
       k file.k [options] [-- program args...]

Commands:
  run        Compile a program and run it in this process, which k
             file.k does too.
  build      Build an executable or shared library with a C compiler.
  check      Parse and typecheck a program, and run nothing.
  fetch      Fetch the packages a program declares.
  cache      Report cache size or prune unused build artifacts.
  cordis     Resolve a provider graph as a lock document.
  format     Rewrite K files in the standard layout.
  toolchain  Report the C compiler, cmake and git found, and K_HOME.
  version    Print the compiler's version (or --version).
  lsp        Serve an editor over the Language Server Protocol.
  completion Print a shell's completion script for k.
  help       Print this (or --help, -h); with a command, that command's help.

Common options:
  -O0, -O1, -O2, -O3
                  Inlining level (default -O2).
  --debug         Overflow, range and tag checks that name the source line
                  they fail on.
  --stats         Print what the compile did (declarations, bounds checks
                  removed, files written).
  --color when    Color errors and warnings: auto (on a terminal, unless
                  NO_COLOR is set), always or never.
  -- args...      What follows is the program's arguments.

Environment:
  K_HOME      Fetched packages, build intermediates and settings
                  (default ~/.klang).
  K_CC        The C compiler, where --cc is not given.
  K_STDLIB    The standard library, where --stdlib is not given.
  K_PROGRESS  plain or live: how progress shows on stderr.

k <command> --help, or k help <command>, lists a command's options.
k help c covers writing the C yourself (k -o out.c), and k help
internals the options for looking into the compiler and testing it. k
help --man prints all of the help as a man page.
)";
    if (topic == "run") return cat(
R"(usage: k run file.k [options] [-- program args...]
       k file.k [options] [-- program args...]

Compiles the program and runs it in this process with TinyCC, and exits with
its exit code. What follows -- is the program's arguments. A program that
starts threads needs k build.

Options:
  -O0, -O1, -O2, -O3    Inlining level (default -O2).
)", DEBUGOPTION, COMPILEOPTIONS,
R"(  --cc name|path        The C compiler for packages' native parts: msvc,
                        clang-cl, clang, gcc, cc or a path (or K_CC).
  -v                    Print each command that builds a native part, and
                        what it prints.
)");
    if (topic == "build") return cat(
R"(usage: k build file.k [options]

Builds an executable beside the source with a C compiler on this machine,
or a shared library with --shared (k toolchain says which), linking the
runtime and the native parts of the packages the program uses. Intermediates
go under K_HOME.

Options:
  -o exe                Where the executable goes.
  --target wasm32-wasip1
                        Build a WASI command module (.wasm). Set WASI_SDK_PATH
                        or pass --cc with the WASI SDK clang path.
  -O0, -O1, -O2, -O3    Inlining level (default -O2). The C compiler
                        optimizes at every level, unless --debug (-O3 uses -O3).
  --lto                 Enable link-time optimization across the program and runtime.
  --cpu=native          Tune native code generation for the host CPU.
  --debug               Overflow, range and tag checks that name the source
                        line they fail on, and the C built for a debugger.
)", COMPILEOPTIONS, CORDISOPTIONS,
R"(  --cc name|path        The C compiler: msvc, clang-cl, clang, gcc, cc or a
                        path (or K_CC; by default the first one found).
  --standalone          Compile the runtime into the program rather than link
                        the runtime object built once per C compiler.
  --shared              Build a self-contained shared library (.dll, .dylib or
                        .so). Its export fns and k_init/k_exit are the
                        host entry points; implies --standalone.
  --header out.h        With --shared: also write the host's header, declaring
                        the export fns and k_init/k_exit.
  -v                    Print each command the build runs and what it prints.
  --dry-run             Print the commands the build would run, and run none.
)");
    if (topic == "check") return cat(
R"(usage: k check file.k [options]

Parses and typechecks the program and what it imports, and writes and runs
nothing.

Options:
)", COMPILEOPTIONS, CORDISOPTIONS);
    if (topic == "fetch") return
R"(usage: k fetch [--update] file.k

Fetches the packages the program declares from git into K_HOME, and
compiles nothing. A run or a build fetches what is missing as well.

Options:
  --update              Fetch packages at a tag or branch again (a commit is
                        never fetched again).
)";
    if (topic == "format") return
R"(usage: k format [--check] file.k|dir...
       k format [-]

Rewrites each file in the standard layout, in place: indentation, the
spaces between tokens, blank lines and trailing commas, with the types
and =s of a run of declarations lined up in columns. Line breaks stay
where they are. A directory stands for the .k files under it, less
those in directories whose names start with a dot and in build trees. With
no files, or -, it formats standard input to standard output. A file that
does not parse is left as it is, with its errors.

Options:
  --check               Write nothing: list the files that are not formatted.

Exit status:
  0                     Every file is formatted (or now is).
  1                     --check found a file that is not.
  2                     A file does not parse or cannot be read or written,
                        or an option is wrong.
)";
    if (topic == "lsp") return
R"(usage: k lsp [--stdio] [--stdlib dir] [--cordis-manifest file]
       k lsp --cordis-manifest file --cordis-graph
       k lsp --probe

Serves an editor over the Language Server Protocol, on stdin and stdout:
diagnostics as files change, hover, go to definition, references and
rename, document and workspace symbols, semantic highlighting, signature
help, completion, inlay hints, folding and selection ranges, code
lenses, and formatting as k format does. It analyzes the programs whose entry files the editor configures
(the initializationOptions' entryFiles, with the standard library's
directory as stdlib) and every open file none of them imports; the
initializationOptions' commands are the client's own commands a code lens
may name (klang.run, klang.showInstantiations). It fetches nothing: a
package not fetched is an error at its import, which k fetch resolves.

Options:
  --probe               Print the protocol version and exit.
  --stdio               Serve on stdin and stdout, as it does anyway.
  --stdlib dir          The standard library's directory (or K_STDLIB).
  --cordis-manifest file
                        Select trusted native diagnostics providers for this server.
  --cordis-graph        Print the resolved graph or error as JSON without activation.
)";
    if (topic == "cordis") return
R"(usage: k cordis resolve service --manifest file [--provider id]

Prints the content-addressed provider graph as JSON. It reads local files
and resolves the selected service without loading provider code.

Options:
  --manifest file       Read the Cordis provider catalog.
  --provider id         Override the root service's selection.
)";
    if (topic == "cache") return
R"(usage: k cache stats [--json]
       k cache prune --budget bytes [--json]

Reports cache sizes under K_HOME. Prune applies one budget to managed
runtime and locked native package outputs, removing idle entries oldest first.

Options:
  --budget bytes        Maximum managed artifact bytes after pruning.
  --json                Report as JSON.
)";
    if (topic == "toolchain") return
R"(usage: k toolchain [--cc name|path] [--json] [--refresh]

Reports the C compiler a build would use, cmake, git, K_HOME and the
modules compiled into k. Exits with 1 when there is no C compiler.

Options:
  --cc name|path        Look for this C compiler: msvc, clang-cl, clang, gcc,
                        cc or a path (or K_CC).
  --json                Report as JSON.
  --refresh             Set up Visual Studio's environment again rather than
                        take the one kept from last time.
)";
    if (topic == "version") return
R"(usage: k version
       k --version

Prints the compiler's version and the runtime's: a hash of the runtime's
text, which the runtime that k --emit-runtime writes has to match.
)";
    if (topic == "completion") return
R"(usage: k completion bash|zsh|fish|powershell

Prints the script that completes k's commands, options and .k
files in that shell. To have it in every shell:

  bash        k completion bash > ~/.local/share/bash-completion/completions/k
  zsh         k completion zsh > "${fpath[1]}/_k"
  fish        k completion fish > ~/.config/fish/completions/k.fish
  powershell  k completion powershell >> $PROFILE

or, for the shell at hand, source <(k completion bash) (or zsh).
)";
    if (topic == "c") return cat(
R"(usage: k -o out.c file.k [options]
       k --emit-runtime runtime.c
       k --<package>-link msvc|cc

Writes the program's C, for a build of your own or a C host. It links with
the runtime that --emit-runtime writes, compiled once with the C compiler
and flags the programs use. k build --dry-run prints the commands for
the program at hand.

Options:
  -o out.c              Write the program's C rather than run it.
  --standalone          Write C that holds the whole runtime and builds alone.
  --header out.h        Also write a header for a C host, which supplies main,
                        starts the program through k_init and may end it
                        through k_exit.
  --include header.h    Have the C include this header (may be repeated).
  --jit                 Also run the program in this process.
  --emit-runtime out.c  Write the runtime's C.
  --<package>-link msvc|cc
                        Print the response file that links the native part of
                        a package compiled into k (--gfx-link cc).
  -O0, -O1, -O2, -O3    Inlining level (default -O2).
)", DEBUGOPTION, COMPILEOPTIONS);
    if (topic == "internals") return cat(
R"(usage: k [options] file.k

Options for looking into what the compiler does, and for its test suite.

Options:
  --tokens              Print the file's tokens.
  --parse               Stop after parsing.
  --check               Stop after typechecking, as k check does.
  --dump                Print the parsed program back as source.
  --dump-docs           Print the documentation comments of the program's
                        declarations, fields and variants.
  --dump-file out       Write that dump to a file, and go on.
  --roundtrip           Check that the dump parses and dumps back to itself.
  --multi-test file...  Compile each file as a run of its own would, ending what
                        each printed with a line on stdout and on stderr:
                        `)", MULTIMARK, R"( <code> <file>`.
                        A % in -o stands for each file's name.
  --bce-test            Verify the `// bce:elide` and `// bce:keep` annotations.
  --unsafe-no-rf-check  Leave out the checks after calls that a `return ...
                        from` can pass through: unsound, for pricing them only.
  --no-copy-elision     Copy every argument and result of a fixed struct over
                        256 bytes, which the C otherwise often shares: a
                        reference the same program must print the same with.
  --gen-runtime-header  Regenerate src/runtime_inline.h from src/runtime/.

k format --verify file.k|dir... formats each file in memory and
writes nothing: it checks the result as a format does, and that formatting
it again changes nothing, and skips a file that does not parse.
)");
    return "";
}

// The diagnostics as `--diagnostics json` prints them: an array with one
// object per error or warning, holding its severity, its message, where it
// is (the file as the compiler names it, its 1-based line and byte column,
// and one past its last byte as endLine and endCol, each left out where
// unknown), the related lines after the message the same way, and the text
// the terminal would have shown.
string DiagnosticsJson(const vector<Diagnostic> &diags, const Ast *ast,
                       const cordis::GraphError *cordis_error = nullptr) {
    string s = "[";
    auto where = [&](const DiagLoc &w) {
        if (!ast || w.fileidx < 0 || w.fileidx >= (int)ast->sources.size()) return;
        s += ", \"file\": ";
        JsonQuote(s, ast->sources[w.fileidx].first);
        Append(s, ", \"line\": ", w.line);
        if (w.col > 0) Append(s, ", \"col\": ", w.col);
        if (w.endline > 0) Append(s, ", \"endLine\": ", w.endline, ", \"endCol\": ", w.endcol);
    };
    for (size_t i = 0; i < diags.size(); i++) {
        auto &d = diags[i];
        Append(s, i ? ",\n  {" : "\n  {", "\"severity\": \"", d.warning ? "warning" : "error",
               "\", \"message\": ");
        JsonQuote(s, d.message);
        if (!d.source.empty()) {
            s += ", \"source\": ";
            JsonQuote(s, d.source);
        }
        if (cordis_error && i + 1 == diags.size()) Append(s, ", \"cordis\": ", cordis_error->Data());
        where(d.where);
        if (!d.related.empty()) {
            s += ", \"related\": [";
            for (size_t j = 0; j < d.related.size(); j++) {
                Append(s, j ? ", {" : "{", "\"message\": ");
                JsonQuote(s, d.related[j].message);
                where(d.related[j].where);
                s += "}";
            }
            s += "]";
        }
        s += ", \"text\": ";
        JsonQuote(s, d.text);
        s += "}";
    }
    s += diags.empty() ? "]\n" : "\n]\n";
    return s;
}

// A size with an optional K, M, G or T suffix (--stack-reserve).
bool ParseSize(string s, uint64_t &out) {
    if (!s.empty() && toupper((unsigned char)s.back()) == 'B') s.pop_back();
    if (s.empty()) return false;
    uint64_t mult = 1;
    switch (toupper((unsigned char)s.back())) {
        case 'K': mult = 1ull << 10; s.pop_back(); break;
        case 'M': mult = 1ull << 20; s.pop_back(); break;
        case 'G': mult = 1ull << 30; s.pop_back(); break;
        case 'T': mult = 1ull << 40; s.pop_back(); break;
    }
    if (s.empty() || s.size() > 18 || s.find_first_not_of("0123456789") != string::npos)
        return false;
    auto n = strtoull(s.c_str(), nullptr, 10);
    if (n > UINT64_MAX / mult) return false;
    out = n * mult;
    return true;
}

// The value of a runtime configuration macro as runtime.h spells its
// defaults and a -D may: an integer with C suffixes, or `(N << M)`.
bool ParseConfigValue(string s, uint64_t &out) {
    auto strip = [](string &t) {
        while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
        size_t i = 0;
        while (i < t.size() && isspace((unsigned char)t[i])) i++;
        t.erase(0, i);
    };
    strip(s);
    while (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        s = s.substr(1, s.size() - 2);
        strip(s);
    }
    auto num = [&](string t, uint64_t &v) {
        strip(t);
        while (!t.empty() && (tolower((unsigned char)t.back()) == 'u' ||
                              tolower((unsigned char)t.back()) == 'l'))
            t.pop_back();
        if (t.empty() || t.size() > 19 || t.find_first_not_of("0123456789") != string::npos)
            return false;
        v = strtoull(t.c_str(), nullptr, 10);
        return true;
    };
    if (auto sh = s.find("<<"); sh != string::npos) {
        uint64_t a, b;
        if (!num(s.substr(0, sh), a) || !num(s.substr(sh + 2), b) || b > 63) return false;
        out = a << b;
        return true;
    }
    return num(s, out);
}

// The data stack configuration the generated program compiles with, for
// the --stacks report: the runtime's defaults (runtime.h), then the -D
// given, --stack-reserve's among them. A -D the compiler cannot read keeps
// the default in the report; the C compiler sees it either way.
CodeGen::StackConfig StackConfigOf(const vector<string> &cdefines, bool wasi) {
    string_view runtime;
    for (auto &rf : runtime_files)
        if (string_view(rf.name) == "runtime.h") runtime = rf.text;
    auto value = [&](const char *name, uint64_t &out, const char *target_default = nullptr) {
        auto key = cat("#define ", target_default ? target_default : name, " ");
        if (auto at = runtime.find(key); at != string_view::npos) {
            auto line = runtime.substr(at + key.size());
            line = line.substr(0, line.find('\n'));
            if (auto c = line.find("/*"); c != string_view::npos) line = line.substr(0, c);
            ParseConfigValue(string(line), out);
        }
        for (auto &d : cdefines) {
            auto eq = d.find('=');
            if (d.substr(0, eq) == name)
                ParseConfigValue(eq == string::npos ? string("1") : d.substr(eq + 1), out);
        }
    };
    CodeGen::StackConfig cfg { 0, 0, 0, 0 };
    uint64_t maxstacks = 0;
    value("K_STACK_RESERVE", cfg.reserve, wasi ? "K_WASI_STACK_RESERVE" : nullptr);
    if (wasi) cfg.gap = 1;
    else value("K_STACK_GAP", cfg.gap);
    value("K_STACK_BUDGET", cfg.budget, wasi ? "K_WASI_STACK_BUDGET" : nullptr);
    value("K_MAX_STACKS", maxstacks);
    cfg.maxstacks = (int64_t)maxstacks;
    return cfg;
}

// The commands, in the order the help lists them.
static const char *const VERBS[] = { "run", "build", "check", "fetch", "cache", "cordis", "format", "toolchain",
                                     "version", "lsp", "completion", "help" };

// The edit distance between two words, for the command a mistyped one meant.
static size_t EditDistance(string_view a, string_view b) {
    vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) row[j] = j;
    for (size_t i = 1; i <= a.size(); i++) {
        auto diag = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); j++) {
            auto up = row[j];
            row[j] = min({ row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] != b[j - 1]) });
            diag = up;
        }
    }
    return row[b.size()];
}

// The options a command's help names, for the completion scripts: the
// first word of each spelling on the lines that start with an option.
static vector<string> HelpOptions(const string &topic) {
    vector<string> opts;
    auto text = HelpText(topic);
    for (size_t pos = 0; pos < text.size();) {
        auto e = text.find('\n', pos);
        if (e == string::npos) e = text.size();
        auto line = string_view(text).substr(pos, e - pos);
        pos = e + 1;
        if (line.substr(0, 3) != "  -") continue;
        auto head = line.substr(2, line.find("  ", 2) - 2);
        for (size_t p = 0; p < head.size();) {
            auto comma = head.find(", ", p);
            auto spelling = head.substr(p, comma == string_view::npos ? string_view::npos : comma - p);
            auto word = string(spelling.substr(0, spelling.find(' ')));
            if (word != "--" && word.size() > 1 && find(opts.begin(), opts.end(), word) == opts.end())
                opts.push_back(word);
            if (comma == string_view::npos) break;
            p = comma + 2;
        }
    }
    return opts;
}

// What `k completion <shell>` prints: completion of the commands, each
// command's options as its help names them, the help's topics, and .k
// files and directories. "" for a shell it does not know.
static string CompletionScript(const string &shell) {
    vector<string> verbs(begin(VERBS), end(VERBS));
    vector<string> topics = verbs;
    topics.push_back("c");
    topics.push_back("internals");
    const vector<string> shells = { "bash", "zsh", "fish", "powershell" };
    // Each command's one-line summary, from the overview.
    map<string, string> summary;
    auto overview = HelpText("");
    for (auto &v : verbs) {
        auto at = overview.find(cat("\n  ", v, " "));
        if (at == string::npos) continue;
        auto start = overview.find_first_not_of(' ', at + 3 + v.size());
        auto end = overview.find('\n', start);
        summary[v] = overview.substr(start, end - start);
        // The lines it wraps onto, indented under it.
        while (overview.compare(end, 6, "\n     ") == 0) {
            auto next = overview.find_first_not_of(' ', end + 1);
            end = overview.find('\n', next);
            Append(summary[v], " ", overview.substr(next, end - next));
        }
    }
    // The options of the commands that take some; a file alone is a run.
    vector<pair<string, vector<string>>> optsof;
    for (auto &v : verbs)
        if (v != "help" && v != "completion" && v != "version") optsof.push_back({ v, HelpOptions(v) });
    auto words = [](const vector<string> &ws) {
        string s;
        for (auto &w : ws) Append(s, s.empty() ? "" : " ", w);
        return s;
    };
    auto runopts = words(HelpOptions("run"));
    string s;
    if (shell == "bash") {
        Append(s, "# k completion bash: source this, or install it as\n"
                  "# ~/.local/share/bash-completion/completions/k\n"
                  "_k() {\n"
                  "    local cur=${COMP_WORDS[COMP_CWORD]} verb= i opts\n"
                  "    for ((i = 1; i < COMP_CWORD; i++)); do\n"
                  "        case ${COMP_WORDS[i]} in -*) ;; *) verb=${COMP_WORDS[i]}; break ;; esac\n"
                  "    done\n"
                  "    case $verb in\n");
        for (auto &[v, opts] : optsof) Append(s, "        ", v, ") opts=\"", words(opts), "\" ;;\n");
        Append(s, "        help) COMPREPLY=($(compgen -W \"", words(topics), "\" -- \"$cur\")); return ;;\n"
                  "        completion) COMPREPLY=($(compgen -W \"", words(shells), "\" -- \"$cur\")); return ;;\n"
                  "        version) return ;;\n"
                  "        *) opts=\"", runopts, " --help --version\" ;;\n"
                  "    esac\n"
                  "    if [[ $cur == -* ]]; then COMPREPLY=($(compgen -W \"$opts\" -- \"$cur\")); return; fi\n"
                  "    COMPREPLY=()\n"
                  "    [[ -z $verb ]] && COMPREPLY=($(compgen -W \"", words(verbs), "\" -- \"$cur\"))\n"
                  "    COMPREPLY+=($(compgen -f -X '!*.k' -- \"$cur\") $(compgen -d -- \"$cur\"))\n"
                  "}\n"
                  "complete -o filenames -F _k k\n");
    } else if (shell == "zsh") {
        Append(s, "#compdef k\n"
                  "# k completion zsh: source this, or install it as _k in a\n"
                  "# directory on $fpath\n"
                  "_k() {\n"
                  "    local verb word\n"
                  "    for word in ${words[2,CURRENT-1]}; do\n"
                  "        [[ $word == -* ]] || { verb=$word; break }\n"
                  "    done\n"
                  "    local -a opts\n"
                  "    case $verb in\n");
        for (auto &[v, opts] : optsof) Append(s, "        ", v, ") opts=(", words(opts), ") ;;\n");
        Append(s, "        help) compadd -- ", words(topics), "; return ;;\n"
                  "        completion) compadd -- ", words(shells), "; return ;;\n"
                  "        version) return ;;\n"
                  "        *) opts=(", runopts, " --help --version) ;;\n"
                  "    esac\n"
                  "    if [[ $PREFIX == -* ]]; then compadd -- $opts; return; fi\n"
                  "    if [[ -z $verb ]]; then\n"
                  "        local -a commands\n"
                  "        commands=(\n");
        for (auto &v : verbs) {
            string d;
            for (auto c : summary[v]) { if (c == '\'') d += "'\\''"; else if (c == ':') d += "\\:"; else d += c; }
            Append(s, "            '", v, ":", d, "'\n");
        }
        Append(s, "        )\n"
                  "        _describe command commands\n"
                  "    fi\n"
                  "    _files -g '*.k'\n"
                  "}\n"
                  "if [[ $zsh_eval_context[-1] == loadautofunc ]]; then _k \"$@\"; else compdef _k k; fi\n");
    } else if (shell == "fish") {
        auto quote = [](const string &t) {
            string q = "'";
            for (auto c : t) { if (c == '\'' || c == '\\') q += '\\'; q += c; }
            return q + "'";
        };
        Append(s, "# k completion fish: install it as ~/.config/fish/completions/k.fish\n"
                  "complete -c k -f\n");
        for (auto &v : verbs)
            Append(s, "complete -c k -n __fish_use_subcommand -a ", v, " -d ", quote(summary[v]), "\n");
        auto flag = [&](const string &cond, const string &o) {
            Append(s, "complete -c k -n ", quote(cond), " ");
            if (o.rfind("--", 0) == 0) Append(s, "-l ", o.substr(2));
            else if (o.size() == 2) Append(s, "-s ", o.substr(1));
            else Append(s, "-o ", o.substr(1));
            s += "\n";
        };
        for (auto &[v, opts] : optsof)
            for (auto &o : opts) flag(cat("__fish_seen_subcommand_from ", v), o);
        string verbset;
        for (auto &v : verbs) Append(verbset, " ", v);
        for (auto &o : HelpOptions("run")) flag(cat("not __fish_seen_subcommand_from", verbset), o);
        Append(s, "complete -c k -n '__fish_seen_subcommand_from help' -a ", quote(words(topics)), "\n"
                  "complete -c k -n '__fish_seen_subcommand_from completion' -a ", quote(words(shells)), "\n"
                  "complete -c k -n 'not __fish_seen_subcommand_from help completion version toolchain lsp' "
                  "-a '(__fish_complete_suffix .k)'\n");
    } else if (shell == "powershell") {
        auto list = [](const vector<string> &ws) {
            string l = "@(";
            for (size_t i = 0; i < ws.size(); i++) Append(l, i ? ", " : "", "'", ws[i], "'");
            return l + ")";
        };
        Append(s, "# k completion powershell: add this to $PROFILE\n"
                  "Register-ArgumentCompleter -Native -CommandName k, k.exe -ScriptBlock {\n"
                  "    param($wordToComplete, $commandAst, $cursorPosition)\n"
                  "    $words = @($commandAst.CommandElements | Select-Object -Skip 1 | ForEach-Object { $_.ToString() })\n"
                  "    if ($wordToComplete -and $words.Count -gt 0) { $words = @($words | Select-Object -First ($words.Count - 1)) }\n"
                  "    $verb = $words | Where-Object { $_ -notlike '-*' } | Select-Object -First 1\n"
                  "    $opts = switch ($verb) {\n");
        for (auto &[v, opts] : optsof) Append(s, "        '", v, "' { ", list(opts), " }\n");
        auto runlist = HelpOptions("run");
        runlist.push_back("--help");
        runlist.push_back("--version");
        Append(s, "        default { ", list(runlist), " }\n"
                  "    }\n"
                  "    $candidates = if ($verb -eq 'help') { ", list(topics), " }\n"
                  "        elseif ($verb -eq 'completion') { ", list(shells), " }\n"
                  "        elseif ($verb -eq 'version') { @() }\n"
                  "        elseif ($wordToComplete -like '-*') { $opts }\n"
                  "        else {\n"
                  "            $dir = Split-Path $wordToComplete\n"
                  "            $files = @(Get-ChildItem -Path \"$wordToComplete*\" -ErrorAction SilentlyContinue |\n"
                  "                Where-Object { $_.PSIsContainer -or $_.Extension -eq '.k' } |\n"
                  "                ForEach-Object { if ($dir) { Join-Path $dir $_.Name } else { $_.Name } })\n"
                  "            if ($verb) { $files } else { ", list(verbs), " + $files }\n"
                  "        }\n"
                  "    $candidates | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n"
                  "        [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)\n"
                  "    }\n"
                  "}\n");
    }
    return s;
}

// What `k help --man` prints: the help of every command and topic as a
// man page, each kept as the terminal shows it.
static string ManPage() {
    auto roff = [](const string &text) {
        string s = ".nf\n";
        for (size_t pos = 0; pos < text.size();) {
            auto e = text.find('\n', pos);
            if (e == string::npos) e = text.size();
            auto line = string_view(text).substr(pos, e - pos);
            pos = e + 1;
            if (!line.empty() && (line[0] == '.' || line[0] == '\'')) s += "\\&";
            for (size_t i = 0; i < line.size(); i++) {
                auto c = line[i];
                if (c == '\\') s += "\\e";
                else if (c == '-') s += "\\-";
                else s += c;
                // troff takes `.`, `!` or `?` (past any closing `)]"'*`) followed by
                // spaces mid-line as the end of a sentence, and groff 1.25's -w style
                // warns about it, e.g. at "args...  What". `\&` keeps it a plain
                // character and prints nothing.
                if (c == '.' || c == '!' || c == '?') {
                    auto j = line.find_first_not_of(")]\"'*", i + 1);
                    if (j != string_view::npos && line[j] == ' ') s += "\\&";
                }
            }
            s += '\n';
        }
        return s + ".fi\n";
    };
    auto overview = HelpText("");
    auto body = overview.substr(overview.find("\n\n") + 2);
    string s;
    Append(s, ".TH K 1 \"\" \"k ", CompilerVersion(), "\" \"K Manual\"\n"
              ".SH NAME\n"
              "k \\- the K compiler, its build tool and its language server\n"
              ".SH SYNOPSIS\n", roff(overview.substr(0, overview.find("\n\n"))),
              ".SH DESCRIPTION\n", roff(body), ".SH COMMANDS\n");
    for (auto v : VERBS) {
        if (string_view(v) == "help") continue;
        Append(s, ".SS ", v, "\n", roff(HelpText(v)));
    }
    Append(s, ".SH WRITING THE C\n", roff(HelpText("c")),
              ".SH INTERNALS\n", roff(HelpText("internals")));
    return s;
}

static int RunCache(int argc, char **argv) {
    string_view action = argc > 2 ? argv[2] : "";
    bool json = false;
    optional<uintmax_t> budget;
    for (int i = 3; i < argc; ++i) {
        string_view arg = argv[i];
        if (arg == "--json") {
            json = true;
        } else if (arg == "--budget" && action == "prune" && !budget && i + 1 < argc) {
            string_view value = argv[++i];
            uintmax_t bytes;
            auto result = from_chars(value.data(), value.data() + value.size(), bytes);
            if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
                fprintf(stderr, "k cache prune expects a nonnegative integer byte budget\n");
                return 1;
            }
            budget = bytes;
        } else {
            fprintf(stderr, "unexpected cache argument %s (see k help cache)\n", argv[i]);
            return 1;
        }
    }
    if (action != "stats" && !(action == "prune" && budget)) {
        fputs(HelpText("cache").c_str(), stderr);
        return 1;
    }
    try {
        auto home = AbsolutePath(KlangHome());
        auto report = budget ? PruneArtifactCache(home, *budget, json) : CacheStats(home, json);
        fputs(report.c_str(), stdout);
        return 0;
    } catch (const CompileError &error) {
        PrintDiagnosticText(error.msg);
        return 1;
    }
}

static int RunCordis(int argc, char **argv, const string &exe) {
    if (argc < 4 || string_view(argv[2]) != "resolve" || !*argv[3]) {
        fputs(HelpText("cordis").c_str(), stderr);
        return 1;
    }
    optional<string> manifest, provider;
    for (int i = 4; i < argc; ++i) {
        string_view arg = argv[i];
        if (arg == "--manifest" && !manifest && i + 1 < argc) manifest = argv[++i];
        else if (arg == "--provider" && !provider && i + 1 < argc) provider = argv[++i];
        else {
            fprintf(stderr, "unexpected Cordis argument %s (see k help cordis)\n", argv[i]);
            return 1;
        }
    }
    if (!manifest || manifest->empty() || (provider && provider->empty())) {
        fputs(HelpText("cordis").c_str(), stderr);
        return 1;
    }
    try {
        auto catalog = CordisDiagnostics::ReadCatalog(*manifest, exe);
        auto document = catalog.Resolve(argv[3], provider).Document();
        puts(document.c_str());
        return 0;
    } catch (const cordis::GraphError &error) {
        printf("{\"error\":%s}\n", error.Data().c_str());
        return 1;
    }
}

int Main(int argc, char **argv) {
    auto exe = ExePath(argv[0]);
    // The verb forms, `k run|build|check|fetch|format|toolchain|version|help ...`.
    // A first argument that is none of them is read as an option or a file,
    // as it always was, so `k foo.k` is `k run foo.k`.
    string verb;
    auto firstarg = 1;
    if (argc > 1) {
        string v = argv[1];
        for (auto known : VERBS)
            if (v == known) {
                verb = v;
                firstarg = 2;
            }
    }
    // Errors and warnings in color on a terminal (--color changes it).
    stderrcolor = StderrColor(false);
    // --help anywhere before a `--` is the help on the command given, or on
    // all of them when none is; --version likewise prints the version.
    for (int i = firstarg; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--") break;
        if (arg == "--help" || arg == "-h") {
            fputs(HelpText(verb).c_str(), stdout);
            return 0;
        }
        if (arg == "--version" && (verb.empty() || verb == "version")) verb = "version";
    }
    // A first word that is no command, no option and no file is a command
    // mistyped, not a program: `k biuld x.k`.
    if (verb.empty() && argc > 1 && argv[1][0] != '-') {
        string v = argv[1];
        if (v.find_first_of("/\\.") == string::npos && !PathExists(v)) {
            string near;
            for (auto known : VERBS)
                if (EditDistance(v, known) <= 2 && near.empty()) near = known;
            fprintf(stderr, "klang: unknown command %s%s (k help lists the commands)\n", v.c_str(),
                    near.empty() ? "" : cat(": did you mean ", near, "?").c_str());
            return 1;
        }
    }
    if (verb == "completion") {
        auto shell = argc > 2 ? string(argv[2]) : string();
        auto script = CompletionScript(shell);
        if (script.empty() || argc > 3) {
            fprintf(stderr, "k completion takes bash, zsh, fish or powershell%s\n",
                    shell.empty() ? "" : cat(", not ", shell).c_str());
            return 1;
        }
        fputs(script.c_str(), stdout);
        return 0;
    }
    if (verb == "help") {
        auto topic = argc > 2 ? string(argv[2]) : string();
        if (topic == "--man") {
            fputs(ManPage().c_str(), stdout);
            return 0;
        }
        auto help = HelpText(topic);
        if (help.empty()) {
            fprintf(stderr, "no help on %s: k help lists the commands\n", topic.c_str());
            return 1;
        }
        fputs(help.c_str(), stdout);
        return 0;
    }
    if (verb == "version") {
        printf("k %s (runtime %s)\n", CompilerVersion(), RuntimeVersion().c_str());
        return 0;
    }
    if (verb == "cache") return RunCache(argc, argv);
    if (verb == "cordis") return RunCordis(argc, argv, exe);
    if (verb == "lsp") return RunLsp(exe, vector<string>(argv + firstarg, argv + argc));
    if (verb == "format") return RunOnCompilerStack([&] {
        return RunFormat(vector<string>(argv + firstarg, argv + argc));
    });
    string outname, headername, stdlibdir, shaderfile, shadersource, dumpfile;
    optional<string> cordis_manifest, cordis_lock;
    auto dump = false, dumpdocs = false, tokens = false, parseonly = false, specs = false;
    auto nocgen = false;
    auto roundtrip = false, multitest = false;
    auto nobce = false, bcetest = false, bcelines = false, norfcheck = false;
    auto noelide = false;
    auto stacks = false;
    auto stats = false;
    auto forcejit = false, standalone = false, shared = false;
    auto lto = false, native_cpu = false;
    auto json = false, refresh = false, debug = false, verbose = false, dryrun = false;
    auto jsondiag = false;
    string ccrequest, target;
    // The inlining at its highest. The C compiler optimizes whatever the
    // level, unless --debug (Builder::CFlags).
    auto optlevel = 2;
    auto package_fetch = PackageFetchMode::FetchMissing;
    vector<string> cdefines, progargs, includenames, files;
    for (int i = firstarg; i < argc; i++) {
        string arg = argv[i];
        // Everything past `--` belongs to the program being run, not here.
        if (arg == "--") {
            for (int j = i + 1; j < argc; j++) progargs.push_back(argv[j]);
            break;
        }
        if (arg == "--dump") dump = true;
        else if (arg == "--tokens") tokens = true;
        else if (arg == "--parse") parseonly = true;
        else if (arg == "--roundtrip") roundtrip = true;
        else if (arg == "--dump-file" && i + 1 < argc) dumpfile = argv[++i];
        else if (arg == "--specs") specs = true;
        else if (arg == "--stacks") stacks = true;
        else if (arg == "--stack-reserve" && i + 1 < argc) {
            uint64_t size;
            // A whole page at least, 64 KB being the largest a platform
            // uses: the runtime rounds a reservation down to whole pages.
            if (!ParseSize(argv[++i], size) || size < (64ull << 10) || size > (1ull << 48)) {
                fprintf(stderr, "--stack-reserve takes a size from 64 KB to 256 TB (2^48, "
                                "k_spec.md 10.4), with an optional K, M, G or T suffix: %s\n",
                        argv[i]);
                return 1;
            }
            cdefines.push_back(cat("K_STACK_RESERVE=", size, "ull"));
        }
        else if (arg == "--check") nocgen = true;
        else if (arg == "--diagnostics" && i + 1 < argc) {
            string how = argv[++i];
            if (how == "json") jsondiag = true;
            else if (how != "text") {
                fprintf(stderr, "--diagnostics takes json or text, not %s\n", how.c_str());
                return 1;
            }
        }
        else if (arg == "--cordis-manifest" && i + 1 < argc) cordis_manifest = argv[++i];
        else if (arg == "--cordis-lock" && i + 1 < argc) cordis_lock = argv[++i];
        else if (arg == "--no-bce") nobce = true;
        else if (arg == "--bce-test") bcetest = true;
        else if (arg == "--bce-lines") bcelines = true;
        // Unsound; a measurement aid only (see CodeGen::norfcheck).
        else if (arg == "--unsafe-no-rf-check") norfcheck = true;
        // A reference for testing (see CodeGen::noelide).
        else if (arg == "--no-copy-elision") noelide = true;
        else if (arg == "--jit") forcejit = true;
        else if (arg == "--standalone") standalone = true;
        else if (arg == "--shared") shared = true;
        else if (arg == "--multi-test") multitest = true;
        else if (arg == "--gen-runtime-header") {
            try {
                GenRuntimeHeader(exe.c_str());
            } catch (CompileError &e) {
                PrintDiagnosticText(e.msg);
                return 1;
            }
            return 0;
        }
        else if (arg == "--emit-runtime" && i + 1 < argc) {
            auto path = argv[++i];
            auto src = RuntimeObjectSource();
            auto f = fopen(path, "wb");
            if (!f) {
                fprintf(stderr, "cannot write output file: %s\n", path);
                return 1;
            }
            fwrite(src.data(), 1, src.size(), f);
            fclose(f);
            printf("wrote %s (%d bytes)\n", path, (int)src.size());
            return 0;
        }
        // Hidden: what a shader compiles to, without a program around it.
        else if (arg == "--compile-shader" && i + 1 < argc) shaderfile = argv[++i];
        else if (arg == "--shader-source" && i + 1 < argc) shadersource = argv[++i];
        // `--<name>-link msvc|cc`: the response file of a blessed package
        // compiled into this compiler (builtin.h).
        else if (arg.size() > 7 && arg.rfind("--", 0) == 0 &&
                 arg.compare(arg.size() - 5, 5, "-link") == 0 && i + 1 < argc) {
            try {
                auto style = argv[++i];
                auto path = BuiltinLinkFile(arg.substr(2, arg.size() - 7), style, DirOf(exe));
                printf("%s\n", path.c_str());
            } catch (CompileError &e) {
                PrintDiagnosticText(e.msg);
                return 1;
            }
            return 0;
        }
        else if (arg == "-O0") optlevel = 0;
        else if (arg == "-O1") optlevel = 1;
        else if (arg == "-O2") optlevel = 2;
        else if (arg == "-O3") optlevel = 3;
        else if (arg == "--lto") lto = true;
        else if (arg == "--cpu=native") native_cpu = true;
        else if (arg == "-o" && i + 1 < argc) outname = argv[++i];
        else if (arg == "--header" && i + 1 < argc) headername = argv[++i];
        else if (arg == "--include" && i + 1 < argc) includenames.push_back(argv[++i]);
        else if (arg == "--stdlib" && i + 1 < argc) stdlibdir = argv[++i];
        else if (arg == "--cc" && i + 1 < argc) ccrequest = argv[++i];
        else if (arg == "--target" && i + 1 < argc) target = argv[++i];
        else if (arg == "--json") json = true;
        else if (arg == "--refresh") refresh = true;
        else if (arg == "--debug") {
            debug = true;
            cdefines.push_back("K_DEBUG=1");
        }
        else if (arg == "-v") verbose = true;
        else if (arg == "--stats") stats = true;
        else if (arg == "--color" && i + 1 < argc) {
            string when = argv[++i];
            if (when != "auto" && when != "always" && when != "never") {
                fprintf(stderr, "--color takes auto, always or never, not %s\n", when.c_str());
                return 1;
            }
            stderrcolor = when == "always" ? StderrColor(true) : when == "auto" && StderrColor(false);
        }
        else if (arg == "--dry-run") dryrun = true;
        else if (arg == "--update") {
            if (package_fetch != PackageFetchMode::Offline) package_fetch = PackageFetchMode::Update;
        }
        else if (arg == "--offline") package_fetch = PackageFetchMode::Offline;
        else if (arg == "--dump-docs") dumpdocs = true;
        // A -D lands in the generated C itself rather than on some backend's
        // command line, so a JIT run and a compiled one see the same source.
        else if (arg == "-D" && i + 1 < argc) cdefines.push_back(argv[++i]);
        else if (arg.rfind("-D", 0) == 0 && arg.size() > 2) cdefines.push_back(arg.substr(2));
        else if (!arg.empty() && arg[0] == '-') {
            fprintf(stderr, "unknown option: %s (k --help lists them)\n", arg.c_str());
            return 1;
        }
        else files.push_back(arg);
    }
    if (verbose || dryrun) ProgressPlain();
    const bool wasi = target == "wasm32-wasip1" || target == "wasm32-wasi";
    if (!target.empty() && (verb != "build" || (!wasi && target != "native"))) {
        fprintf(stderr, "k build supports --target native and --target wasm32-wasip1\n");
        return 1;
    }
    if (wasi && (shared || forcejit || norfcheck)) {
        fprintf(stderr, "the WASI target requires an executable with safe reference checks\n");
        return 1;
    }
    const bool locked_cordis = cordis_manifest.has_value() || cordis_lock.has_value();
    if (locked_cordis && ((verb != "check" && verb != "build") || !shaderfile.empty() ||
                          !cordis_manifest || cordis_manifest->empty() || !cordis_lock || cordis_lock->empty())) {
        fprintf(stderr, "k check and k build require --cordis-manifest and --cordis-lock together\n");
        return 1;
    }
    if (verb == "toolchain") {
        try {
            auto tools = FindTools(ccrequest, refresh);
            // The modules a program can import without a build: those of
            // the blessed packages compiled in.
            vector<string> builtin;
            for (auto &e : BlessedTable())
                if (BuiltinLookup(e.package)) builtin.push_back(e.import);
            fputs(ToolsReport(tools, json, builtin).c_str(), stdout);
            return tools.havecc ? 0 : 1;
        } catch (CompileError &e) {
            PrintDiagnosticText(e.msg);
            return 1;
        }
    }
    if (shared && verb != "build") {
        fprintf(stderr, "--shared is an option of k build\n");
        return 1;
    }
    if ((lto || native_cpu) && verb != "build") {
        fprintf(stderr, "--lto and --cpu=native are options of k build\n");
        return 1;
    }
    if (wasi && (lto || native_cpu)) {
        fprintf(stderr, "--lto and --cpu=native are not supported for the WASI target\n");
        return 1;
    }
    // What `k build` builds with; its -o names the executable or shared
    // library, and the C goes to the program's build directory (build.h).
    const bool offline = package_fetch == PackageFetchMode::Offline;
    BuildOptions buildopts;
    string builddir;
    if (verb == "run") {
        if (!outname.empty()) {
            fprintf(stderr, "k run takes no -o: it compiles and runs the program in this "
                            "process\n");
            return 1;
        }
        forcejit = true;
    } else if (verb == "check") {
        nocgen = true;
    } else if (verb == "fetch") {
        // Parsing is what fetches: every package the root declares is
        // fetched as its entry is reached.
        parseonly = true;
    } else if (verb == "build") {
        if (multitest || (!headername.empty() && !shared)) {
            fprintf(stderr, "k build takes one program, and --header only with --shared; "
                            "the flag form (-o out.c) writes the C for a C host\n");
            return 1;
        }
        if (shared || wasi) standalone = true;
        buildopts = BuildOptions { debug, verbose, dryrun, standalone, shared, offline, outname, wasi,
                                   optlevel, lto, native_cpu };
        outname.clear();
    }
    if (files.size() > 1 && !multitest) {
        fprintf(stderr, "multiple input files given\n");
        return 1;
    }
    if (!shaderfile.empty()) {
        try {
            DumpShader(shaderfile, shadersource);
        } catch (CompileError &e) {
            PrintDiagnosticText(e.msg);
            return 1;
        }
        return 0;
    }
    if (files.empty()) {
        auto help = HelpText(verb);
        fprintf(stderr, "%s\n%s\n", help.substr(0, help.find("\n\n") + 1).c_str(),
                verb.empty() ? "k --help lists the commands and common options."
                             : cat("k ", verb, " --help lists its options.").c_str());
        return 1;
    }
    // With no output file the program is compiled into this process and run,
    // which is what --jit asks for explicitly; a build writes the C to its
    // build directory instead.
    auto jit = verb != "build" && (forcejit || outname.empty());
    if (!headername.empty() && (jit || multitest)) {
        fprintf(stderr, "--header requires C output for a single input file\n");
        return 1;
    }
    // A header is for a C host, which supplies its own main and starts the
    // program through k_init (and may end it through k_exit).
    auto library = !headername.empty() || shared;
    if (multitest && (tokens || !dumpfile.empty() || !progargs.empty() ||
                      (!outname.empty() && outname.find('%') == string::npos) ||
                      (jit && !nocgen && !parseonly && !dump))) {
        fprintf(stderr, "--multi-test runs no programs and writes no dump files: it takes "
                        "--check, --parse, --dump or an -o with a %%, and no --tokens, "
                        "--dump-file, --jit or program arguments\n");
        return 1;
    }
    CordisDiagnostics diagnostics;
    if (locked_cordis) {
        try {
            diagnostics = CordisDiagnostics::Load(*cordis_manifest, exe, *cordis_lock);
        } catch (const cordis::GraphError &e) {
            if (jsondiag) {
                Diagnostic d;
                d.message = d.text = e.msg;
                d.source = "cordis";
                fputs(DiagnosticsJson({ d }, nullptr, &e).c_str(), stdout);
            } else {
                PrintDiagnosticText(e.msg);
            }
            return 1;
        }
    }
    const auto provider = diagnostics.Pin();
    const auto &cordis_graph = diagnostics.ActiveGraph();
    const auto cordis_document = cordis_graph ? cordis_graph->Document() : string();
    // The program shares stdout in JIT mode, and the diagnostics have it as
    // JSON; progress goes to stderr then.
    auto msgs = jit || jsondiag ? stderr : stdout;
    // The file being compiled, the C file it goes to, if any, and the
    // --include headers as that C file names them.
    string filename, outfile;
    vector<string> includes;
    // What a JIT run compiles and starts, once the compile produced it.
    string program, generated_c, generated_header;
    // The program's Ast outlives the compile: a build and a JIT run read
    // its packages, for the native parts the program needs (native.h).
    unique_ptr<Ast> astp;
    vector<int> nativepkgs;
    // The compile's diagnostics as data (--diagnostics json): the warnings
    // as they come, then the error's.
    vector<Diagnostic> found;
    auto write_c = [&](const string &source) {
        auto wrote = verb == "build" ? WriteFileIfChanged(outfile, source)
                                     : WriteFileMakingDirs(outfile, source);
        if (!wrote) throw CompileError { cat("cannot write output file: ", outfile) };
        if (stats) fprintf(msgs, "wrote %s (%d bytes)\n", outfile.c_str(), (int)source.size());
    };
    auto write_header = [&](const string &header) {
        if (!WriteFileMakingDirs(headername, header))
            throw CompileError { cat("cannot write export header: ", headername) };
        if (stats) fprintf(msgs, "wrote %s (%d bytes)\n", headername.c_str(), (int)header.size());
    };
    auto compile = [&]() -> int {
        if (tokens) {
            DumpTokens(filename);
            return 0;
        }
        astp = make_unique<Ast>();
        auto &ast = *astp;
        if (wasi) ast.pointer_bytes = 4;
        if (jsondiag || provider) ast.sink = [&](const Diagnostic &d) {
            if (!jsondiag) PrintDiagnosticText(d.text);
            found.push_back(d);
        };
        const auto stdlibdirs = StdlibDirs(stdlibdir, exe);
        const ProgramLoadOptions load { stdlibdirs, DirOf(exe), package_fetch };
        ParseProgram(ast, filename, load);
        // The parser reported what it recovered from as it went; nothing
        // further is asked of a program with a syntax error.
        if (!ast.errors.empty()) return 1;
        if (dumpdocs) {
            string s;
            DumpDocs(ast, s);
            fputs(s.c_str(), stdout);
            return 0;
        }
        if (dump) {
            // Dump is parse-level output: no name resolution or typecheck,
            // so parse-only test files can roundtrip, and every name shows
            // as written.
            string s;
            ast.Dump(s);
            fputs(s.c_str(), stdout);
            return 0;
        }
        // The dump as --dump prints it, taken before resolution.
        string dumped;
        if (roundtrip || !dumpfile.empty()) ast.Dump(dumped);
        if (!dumpfile.empty()) {
            auto f = fopen(dumpfile.c_str(), "wb");
            if (!f) throw CompileError { cat("cannot write dump file: ", dumpfile) };
            fwrite(dumped.data(), 1, dumped.size(), f);
            fclose(f);
        }
        ResolveTypeNames(ast);
        // As after the parse, which an unknown type name is part of.
        if (!ast.errors.empty()) return 1;
        // With --stats, printed by every run that gets this far, so that one
        // which goes on to fail says by itself which side of resolution it
        // failed on.
        if (stats) fprintf(msgs, "parsed ok: %d top-level declarations, %d file(s)\n",
                (int)ast.topdecls.size(), (int)ast.sources.size());
        // After resolution, so that a program that does not resolve fails as
        // it would without --roundtrip.
        if (roundtrip) {
            CheckRoundtrip(dumped, dumpfile.empty() ? cat(filename, ".dump") : dumpfile, load);
            if (stats) fprintf(msgs, "roundtrip ok: %d bytes of dump\n", (int)dumped.size());
        }
        if (parseonly) return 0;
        TypeCheckProgram(ast, library, nocgen);
        // The checker reported the errors it went on past, root by root.
        if (!ast.errors.empty()) return 1;
        Optimizer opt(ast, optlevel);
        if (specs) {
            string s;
            opt.DumpSpecs(s);
            fputs(s.c_str(), stdout);
        }
        if (stats) fprintf(msgs, "typechecked ok: %d specialization(s), %d struct/%d enum instance(s); "
                "optimized -O%d: %d inlined, %d base case(s), %d folded, %d propagated, "
                "%d tail loop(s), %d reassociated\n",
                (int)ast.fnspecs.size(), (int)ast.structinsts.size(),
                (int)ast.enuminsts.size(), optlevel, opt.inlined, opt.basecases,
                opt.folded, opt.propagated, opt.tailloops, opt.reassociated);
        BCE bce(ast);
        if (!nobce) {
            bce.RunAll();
            if (stats) fprintf(msgs, "bce: elided %d/%d index and %d/%d slice checks\n",
                    bce.idxelided, bce.idxtotal, bce.slelided, bce.sltotal);
            if (!bce.checkfreefails.empty()) throw bce.CheckfreeError();
        } else {
            for (auto sf : ast.functions)
                // Not for the standard library's checkfree helpers, or every
                // program that imports std would get the warning.
                if (sf->checkfree && !sf->isnested && !ast.StdlibFile(sf->line.fileidx)) {
                    ast.Warn(DiagLoc {}, "checkfree is not verified under --no-bce");
                    break;
                }
        }
        // Per-line outcomes, for comparing two builds of the pass.
        if (bcelines)
            for (auto &[where, counts] : bce.lineout)
                fprintf(msgs, "bce-line: %s:%d: %d elided, %d kept\n",
                        ast.sources[where.first].first.c_str(), where.second,
                        counts.first, counts.second);
        if (bcetest) {
            auto fails = bce.VerifyAnnotations();
            if (fails) {
                fprintf(stderr, "bce-test: %d annotation failure(s)\n", fails);
                return 1;
            }
            fprintf(msgs, "bce-test: all annotations verified\n");
        }
        // --stacks needs the generated program's bounds, so --check runs
        // the backend for them and writes nothing.
        if (nocgen && !stacks) return 0;
        // Sidecar headers share --include's position after generated types.
        // They belong to every loaded package, not just those with a native
        // part: a header-only adapter needs no separate compilation or loading.
        for (auto &pkg : ast.packages)
            includes.insert(includes.end(), pkg.cheaders.begin(), pkg.cheaders.end());
        // The extern-support runtime is written against the generated types,
        // so it goes in after them rather than up front.
        string_view runtime_ext_text;
        for (auto &rf : runtime_files)
            if (string_view(rf.name) == "runtime_ext.h") runtime_ext_text = rf.text;
        auto stackcfg = StackConfigOf(cdefines, wasi);
        CodeGen cg(ast, runtime_ext_text, norfcheck, library, shared,
                   stackcfg.maxstacks, noelide, wasi);
        if (stacks) cg.StackReport(msgs, stackcfg);
        // The packages whose native parts the program needs, which a JIT
        // run takes functions from and a build links: its own, when it has
        // one, and every package an extern fn it calls was declared in,
        // each before the packages it requires, which is the order a
        // linker resolves their archives in.
        nativepkgs = NativePackagesUsed(ast, cg.usedexterns);
        if (wasi) {
            if (cg.usesthreads) throw CompileError { "the WASI target does not support worker threads" };
            if (!cg.DlLibs().empty()) throw CompileError { "the WASI target does not support dynamic extern libraries" };
            for (auto pkg : nativepkgs) {
                auto &p = ast.packages[pkg];
                if (!p.cmake_target.empty())
                    throw CompileError { "the WASI target does not support CMake package builds" };
                if (p.builtin >= 0)
                    throw CompileError { "the WASI target does not support builtin native packages" };
            }
        }
        // Assemble: compiler-set feature defines, the embedded runtime, then
        // the generated program. A program built with a separate runtime
        // holds what runtime.h has of it and the extern support's
        // declarations; a standalone one holds all of it.
        auto assemble = [&](bool separate, bool embedded = false) {
            string out = cat("/* Generated by the K compiler from ", filename,
                             ". Do not edit.\n"
                             "   Names from the program carry a _g suffix, which keeps them clear of\n"
                             "   the C keywords, of the runtime's k_ names and of whatever this\n"
                             "   platform's headers declare; a namespaced name ns::x is ns_x_g followed\n"
                             "   by the namespace's length. An --include header names them that way.",
                             separate ? "\n   Link it with the runtime that `k --emit-runtime` "
                                        "writes. */\n\n#define K_SEPARATE_RUNTIME 1\n"
                                      + (embedded ? string() :
                                         "#define K_RUNTIME_VERSION " + RuntimeVersion() + "\n")
                                      : " */\n\n",
                             cg.predefs);
            if (wasi) out += "#define K_TARGET_WASI 1\n";
            // -D goes into the source rather than onto a backend's command
            // line, so both backends compile the same text.
            for (auto &d : cdefines) {
                auto eq = d.find('=');
                Append(out, "#define ", eq == string::npos ? d : d.substr(0, eq), " ",
                       eq == string::npos ? string("1") : d.substr(eq + 1), "\n");
            }
            if (embedded)
                Append(out, "#define K_EMBEDDED_JIT 1\n",
                       RuntimeSections({ "jit_c.h" }));
            auto tasks = cg.usestasks ? RuntimeSections({ "runtime_tasks.h" }) : string();
            if (cg.usestasks && !wasi)
                Append(out, "#ifndef K_NEED_TASK_WAIT\n#define K_NEED_TASK_WAIT 1\n#endif\n");
            if (separate)
                Append(out, RuntimeSections({ "runtime.h" }), tasks, cg.head,
                       RuntimeSections({ "runtime_ext.h" }));
            else
                Append(out, RuntimeSections({ "runtime.h", "runtime_impl.h", "runtime_threads.h" }),
                       tasks, cg.head, RuntimeSections({ "runtime_ext.h", "runtime_os.h" }));
            // A file is compiled from its own directory. The in-memory C is
            // compiled from the working directory even when this run also
            // writes a C file. Each variant needs its own header spelling.
            Append(out, "\n/* ---- includes ---- */\n");
            for (auto inc : includes) {
                if (FileExists(inc)) {
                    if (embedded) inc = AbsolutePath(inc);
                    else if (!outfile.empty()) inc = RelativeTo(inc, DirOf(outfile));
                }
                std::replace(inc.begin(), inc.end(), '\\', '/');
                Append(out, "#include \"", inc, "\"\n");
            }
            out += cg.result;
            if (embedded)
                Append(out, "\nvoid k_jit_cleanup(void) { free(k_stks); k_stks = NULL; "
                            "k_nstks = 0; }\n");
            if (!cordis_document.empty())
                Append(out, "\n#ifdef _WIN32\n__declspec(dllexport)\n"
                            "#elif defined(__GNUC__)\n__attribute__((used))\n#endif\n"
                            "const char k_cordis_build_graph[] = ", CodeGen::CStr(cordis_document), ";\n");
            return out;
        };
        if (!outfile.empty() && !nocgen) {
            auto source = assemble(!standalone);
            if (provider) generated_c = std::move(source);
            else write_c(source);
        }
        if (!headername.empty()) {
            auto header = cg.ExportHeader();
            if (provider) generated_header = std::move(header);
            else write_header(header);
        }
        if (jit && !nocgen) {
            // TinyCC's in-memory runner rejects a thread-local section, and
            // the runtime keeps each worker's data stacks in one.
            if (cg.usesthreads)
                throw CompileError { "JIT mode does not support threads yet (TinyCC cannot "
                                     "place thread-local storage in an in-memory run); "
                                     "compile with -o and a C compiler instead" };
            program = assemble(true, true);
        }
        return 0;
    };
    auto report_failure = [&](const CompileError &e) {
        if (!jsondiag) PrintDiagnosticText(e.msg);
        if (e.diags.empty()) {
            Diagnostic d;
            d.message = d.text = e.msg;
            found.push_back(std::move(d));
        } else {
            found.insert(found.end(), e.diags.begin(), e.diags.end());
        }
    };
    // The C toolchain everything this command builds is built with, found
    // when first needed: the native parts, the runtime object and the link
    // have to agree on it.
    Toolchain tc;
    string tcwhy;
    bool tclooked = false, havetc = false;
    auto findtc = [&] {
        if (!tclooked && wasi) {
            auto compiler = ccrequest;
            if (compiler.empty()) {
                if (auto sdk = getenv("WASI_SDK_PATH"); sdk && *sdk) {
                    #ifdef _WIN32
                        constexpr auto clang_name = "clang.exe";
                    #else
                        constexpr auto clang_name = "clang";
                    #endif
                    compiler = (filesystem::path(sdk) / "bin" / clang_name).string();
                }
            }
            if (!compiler.empty()) havetc = FindToolchain(compiler, tc, tcwhy);
            else tcwhy = "WASI SDK clang not found. Set WASI_SDK_PATH or pass --cc PATH.";
            if (havetc && tc.style != "cc") {
                tcwhy = "the WASI target requires WASI SDK clang, not an MSVC-style compiler";
                havetc = false;
            }
            // The same driver can produce host and WASI objects. Keep their
            // package directories and stamps distinct as well as their flags.
            if (havetc) tc.id += "-wasm32-wasip1";
        } else if (!tclooked) {
            havetc = FindToolchain(ccrequest, tc, tcwhy);
        }
        tclooked = true;
        if (!havetc) fprintf(stderr, "%s\n", tcwhy.c_str());
        return havetc;
    };
    auto run = [&](const string &file) -> int {
        filename = file;
        outfile = outname;
        if (multitest && !outfile.empty()) {
            auto base = file.substr(file.find_last_of("/\\") + 1);
            auto stem = base.substr(0, base.find_last_of('.'));
            for (auto pos = outfile.find('%'); pos != string::npos;
                 pos = outfile.find('%', pos + stem.size()))
                outfile.replace(pos, 1, stem);
        }
        includes = includenames;
        unique_ptr<FileLock> buildlock;
        if (verb == "build") {
            builddir = ProgramBuildDir(file);
            if (wasi) builddir += "/wasm32-wasip1";
            std::error_code ec;
            filesystem::create_directories(filesystem::path(builddir), ec);
            if (ec) {
                fprintf(stderr, "cannot create the build directory %s\n", builddir.c_str());
                return 1;
            }
            buildlock = make_unique<FileLock>(cat(builddir, "/build.lock"),
                                              "waiting for another build of this program");
            outfile = cat(builddir, "/", PathStem(file), ".c");
            // The C is written far from where the command was given, so a
            // header named relative to here is named relative to nowhere
            // else: absolute, and the C names it relative to itself.
            for (auto &inc : includes)
                if (FileExists(inc)) inc = AbsolutePath(inc);
        }
        program.clear();
        generated_c.clear();
        generated_header.clear();
        astp.reset();
        nativepkgs.clear();
        found.clear();
        optional<cordis::GraphError> graph_error;
        auto attempt = [&]() -> int {
            // The compile has a thread of its own. The program a JIT run
            // starts runs back on this one, the main thread, which a window
            // on macOS has to be made on, and whose stack the link reserves
            // as large as the compile thread's where the platform lets it
            // (CMakeLists.txt).
            auto code = 0;
            try {
                code = RunOnCompilerStack(compile);
            } catch (const CompileError &e) {
                if (!provider) throw;
                report_failure(e);
                code = 1;
            }
            if (provider && astp) {
                auto additions = CordisDiagnostics::Inspect(*provider, filename, *astp, found);
                cordis_graph->VerifyInputs();
                for (auto &added : additions) {
                    added.text = astp->DiagnosticText(added);
                    added.source = cat("cordis/", provider->id);
                    if (!jsondiag) PrintDiagnosticText(added.text);
                    if (!added.warning) code = 1;
                    found.push_back(std::move(added));
                }
            }
            if (code) return code;
            if (!generated_c.empty()) write_c(std::exchange(generated_c, {}));
            if (!generated_header.empty()) write_header(std::exchange(generated_header, {}));
            if (verb == "build") {
                if (nocgen) return 0;
                // The native parts first, so that the link can take their
                // response files: a builtin package's from the compiler's
                // build, the others' built now.
                if (!findtc()) return 1;
                Builder b { tc, buildopts, msgs };
                vector<string> rsps;
                vector<unique_ptr<FileLock>> native_leases;
                for (auto pkg : nativepkgs) {
                    auto &p = astp->packages[pkg];
                    if (p.builtin >= 0) {
                        rsps.push_back(BuiltinPackageLinkFile(builtin_packages[p.builtin],
                                                              tc.style, DirOf(exe)));
                        continue;
                    }
                    NativeBuild nb;
                    if (!BuildNativePart(*astp, pkg, filename, b, false, nb)) return 1;
                    rsps.push_back(nb.rsp);
                    if (nb.lease) native_leases.push_back(std::move(nb.lease));
                }
                return BuildExecutable(filename, outfile, builddir, tc, buildopts, msgs, rsps,
                                       astp->packages[0].output, cat(DirOf(exe), "native-runtime"),
                                       includes.empty() && cdefines.empty(),
                                       cordis_graph ? string_view(cordis_graph->identity) : string_view());
            }
            if (program.empty()) return code;
            // The native parts the program reaches hand the run their
            // functions by name: a builtin package's from the table compiled
            // in with it, the others' from libraries built now and loaded.
            vector<pair<string, const void *>> symbols;
            BuildOptions jitopts;
            jitopts.verbose = verbose;
            jitopts.offline = offline;
            for (auto pkg : nativepkgs) {
                auto &p = astp->packages[pkg];
                if (p.builtin >= 0) {
                    if (auto table = builtin_packages[p.builtin].symbols)
                        for (auto e = table(); e->name; e++) symbols.emplace_back(e->name, e->addr);
                    continue;
                }
                if (!findtc()) return 1;
                Builder b { tc, jitopts, msgs };
                NativeBuild nb;
                if (!BuildNativePart(*astp, pkg, filename, b, true, nb)) return 1;
                string err;
                if (!LoadSharedSymbols(nb.jitlib, symbols, err)) {
                    fprintf(stderr, "cannot load the package %s's library: %s\n", p.name.c_str(),
                            err.c_str());
                    return 1;
                }
                if (nb.lease) nb.lease->KeepUntilProcessExit();
            }
            // TinyCC's parser recurses as deep as the C nests, so TinyCC
            // compiles on a compile thread's stack as well: a Linux main
            // thread has only `ulimit -s`.
            TCCState *jit = nullptr;
            RunOnCompilerStack([&]() -> int {
                jit = CompileJit(program, JitLibPath(DirOf(exe)), symbols);
                return 0;
            });
            // The program shares this process, so its exit code becomes ours
            // and whatever it wrote is already on the same streams.
            fflush(msgs);
            return RunJit(jit, filename, progargs);
        };
        auto code = 0;
        try {
            code = attempt();
        } catch (const cordis::GraphError &e) {
            report_failure(e);
            found.back().source = "cordis";
            graph_error = e;
            code = 1;
        } catch (const CompileError &e) {
            report_failure(e);
            code = 1;
        }
        if (jsondiag)
            fputs(DiagnosticsJson(found, astp.get(), graph_error ? &*graph_error : nullptr).c_str(), stdout);
        return code;
    };
    if (!multitest) return run(files[0]);
    // Each file has an Ast and a compile thread of its own, which is all the
    // state a compile has. The mark goes after a line break of its own, so a
    // reader drops the one before it and gets what the file printed exactly.
    // Whatever is not a CompileError ends the process, as it would a run on
    // the file alone; a reader then has no mark for that file or any after.
    for (auto &file : files) {
        auto code = run(file);
        for (auto f : { stdout, stderr }) {
            fflush(f);
            fprintf(f, "\n%s %d %s\n", MULTIMARK, code, file.c_str());
            fflush(f);
        }
    }
    return 0;
}

}  // namespace klang

// The thread the compile runs on. The system headers come after the whole
// compiler, so that none of their macros reach it.
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <process.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <pthread.h>
    #include <unistd.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
#endif

namespace klang {

// The language server's streams carry bytes with Content-Length counts,
// which the text mode Windows gives them, translating line breaks, would
// put off.
void SetBinaryStdio() {
    #ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
    #endif
}

// The running executable's path with links resolved, so that what is found
// relative to the compiler (the standard library, the runtime sources) is found
// however it was started: by a path, through PATH or through a link to it.
// argv[0] where the system cannot say.
string ExePath(const char *argv0) {
    #ifdef _WIN32
        char path[4096];
        auto n = GetModuleFileNameA(nullptr, path, sizeof path);
        if (n && n < sizeof path) return string(path, n);
    #elif defined(__linux__)
        char path[4096];
        auto n = readlink("/proc/self/exe", path, sizeof path);
        if (n > 0 && (size_t)n < sizeof path) return string(path, (size_t)n);
    #elif defined(__APPLE__)
        char path[4096];
        uint32_t size = sizeof path;
        if (_NSGetExecutablePath(path, &size) == 0) {
            if (auto resolved = realpath(path, nullptr)) {
                string s = resolved;
                free(resolved);
                return s;
            }
        }
    #endif
    return argv0;
}

// The compile's native stack: address space reserved up front, of which only
// what a program's depth reaches is ever committed. The typechecker stops at
// stackfloor, STACKHEADROOM short of the end, which leaves room for the frames
// a body nests below its last check and for reporting the error.
#if defined(_MSC_VER) && !defined(NDEBUG)
constexpr size_t COMPILERSTACK = (size_t)128 << 20;
#else
constexpr size_t COMPILERSTACK = (size_t)64 << 20;
#endif
constexpr size_t STACKHEADROOM = (size_t)4 << 20;

struct CompilerThread {
    const function<int()> &fn;
    int code = 0;
    exception_ptr failure;

    void Run() {
        stackfloor = StackPointer() - (COMPILERSTACK - STACKHEADROOM);
        try {
            code = fn();
        } catch (...) {
            failure = current_exception();
        }
    }
};

#ifdef _WIN32
static unsigned __stdcall CompilerThreadMain(void *t) {
    ((CompilerThread *)t)->Run();
    return 0;
}
#else
static void *CompilerThreadMain(void *t) {
    ((CompilerThread *)t)->Run();
    return nullptr;
}
#endif

// Runs fn on a thread with a COMPILERSTACK stack, and returns or throws here
// what it returned or threw. Where no such thread can be had, fn runs on this
// one, and nothing checks its depth.
int RunOnCompilerStack(const function<int()> &fn) {
    CompilerThread t { fn };
    #ifdef _WIN32
        // Without the flag, the size would be committed rather than reserved.
        auto h = (HANDLE)_beginthreadex(nullptr, (unsigned)COMPILERSTACK, CompilerThreadMain,
                                        &t, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (!h) return fn();
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
    #else
        pthread_attr_t attr;
        pthread_t th;
        if (pthread_attr_init(&attr)) return fn();
        auto failed = pthread_attr_setstacksize(&attr, COMPILERSTACK) ||
                      pthread_create(&th, &attr, CompilerThreadMain, &t);
        pthread_attr_destroy(&attr);
        if (failed) return fn();
        pthread_join(th, nullptr);
    #endif
    if (t.failure) rethrow_exception(t.failure);
    return t.code;
}

}  // namespace klang

int main(int argc, char **argv) {
    #ifdef _MSC_VER
        // Asserts and aborts go to stderr and exit, never a dialog, so failing
        // debug runs terminate cleanly under test automation.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    #endif
    return klang::Main(argc, argv);
}
