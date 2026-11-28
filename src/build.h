// K compiler — `k build` (docs/design/packages.md §6): the C the
// compiler wrote, the runtime object and the native modules' response files,
// compiled and linked into an executable by the toolchain of toolchain.h.
// Intermediates live under K_HOME; the executable lands beside the root
// source unless -o says otherwise.
#pragma once
#include "artifact_cache.h"
#include "build_cache.h"
#include "content_digest.h"

namespace klang {

// In main.cpp: the runtime's text hash and the runtime object's C.
string RuntimeVersion();
string RuntimeObjectSource();

struct BuildOptions {
    bool debug = false;       // --debug: K_DEBUG=1, debug information, no C optimization
    bool verbose = false;     // -v: print every command and what it prints
    bool dryrun = false;      // --dry-run: print the commands and run nothing
    bool standalone = false;  // --standalone: no runtime object
    bool shared = false;      // --shared: build a self-contained shared library
    bool offline = false;     // --offline: CMake uses dependencies already on disk
    string exe;               // -o: the executable/library; "" puts it beside the root source
    bool wasi = false;
    int opt_level = 2;        // C backend optimization tier (2 = -O2, 3 = -O3)
    bool lto = false;         // --lto: link-time optimization across the program and runtime
    bool native_cpu = false;  // --cpu=native: target the host CPU ISA
};

// K_HOME/build/prog/<stem>-<hash of the root's absolute path>/: where a
// program's C, objects and logs go.
inline string ProgramBuildDir(const string &rootfile) {
    auto abs = AbsolutePath(rootfile);
    #ifdef _WIN32
        for (auto &c : abs) c = (char)tolower((unsigned char)c);   // One directory per file, however spelled.
    #endif
    return cat(KlangHome(), "/build/prog/", PathStem(rootfile), "-", Hex8(abs));
}

// Runs the toolchain's commands, printing them as -v and --dry-run ask, and
// a failed one with what it printed.
struct Builder {
    const Toolchain &tc;
    const BuildOptions &opts;
    FILE *msgs;

    // The compile flags of both styles: optimization, or debug information
    // and none of it. Defines go into the C itself (main.cpp), never here.
    vector<string> CFlags() const {
        if (tc.style == "msvc") {
            vector<string> f = { "/nologo" };
            if (opts.debug) f.push_back("/Od"), f.push_back("/Zi");
            else {
                f.push_back("/O2");
                if (opts.opt_level >= 3) f.push_back("/Oi"), f.push_back("/Ot");
                if (opts.lto && !opts.wasi) f.push_back("/GL");
            }
            return f;
        }
        vector<string> f;
        if (opts.wasi) f = { "--target=wasm32-wasip1", "-mexec-model=command" };
        if (opts.debug) f.push_back("-O0"), f.push_back("-g");
        else {
            f.push_back(opts.opt_level >= 3 ? "-O3" : "-O2");
            if (opts.lto && !opts.wasi)
                f.push_back(tc.name == "clang" ? "-flto=thin" : "-flto");
            if (opts.native_cpu && !opts.wasi) {
                #if defined(__APPLE__) && defined(__aarch64__)
                    f.push_back("-mcpu=native");
                #else
                    f.push_back("-march=native");
                #endif
            }
        }
        return f;
    }

    // The system libraries a program links beyond the C library, in the cc
    // style: none on Windows, where they live in the C runtime.
    vector<string> SystemLibs() const {
        if (opts.wasi) return { "-lm" };
        #ifdef _WIN32
            return {};
        #else
            if (tc.style == "msvc") return {};
            vector<string> libs = { "-lm", "-pthread" };
            #ifdef __linux__
                // dlopen, for the runtime's dynamic externs: in libdl before
                // glibc 2.34, and in libc since, where the archive left
                // behind is empty.
                libs.push_back("-ldl");
            #endif
            return libs;
        #endif
    }

    // Runs `argv`, `what` being what it does ("compiling the runtime"), with
    // its output kept in `logfile`. False when it failed, after saying so.
    bool Run(const vector<string> &argv, const string &what, const string &logfile,
             bool track_progress = true) const {
        if (opts.dryrun || opts.verbose) fprintf(msgs, "%s\n", CommandLineText(argv).c_str());
        if (opts.dryrun) return true;
        fflush(msgs);
        ProcessResult r;
        {
            optional<ProgressStep> step;
            if (track_progress) step.emplace(what);
            r = RunProcess(argv, "", opts.verbose);
        }
        if (!logfile.empty()) WriteFileMakingDirs(logfile, r.output);
        if (r.started && r.code == 0) return true;
        auto msg = cat(what, " failed", r.started ? cat(" (exit ", r.code, ")") : string(), ":\n    ",
                       CommandLineText(argv), "\n");
        if (!opts.verbose) msg += r.output;
        if (!logfile.empty())
            Append(msg, msg.back() == '\n' ? "" : "\n", "(the output is in ", logfile, ")");
        ProgressPrint(msg);
        return false;
    }

    // A system library by name, as the style's linker takes it.
    string LibFlag(const string &lib) const {
        return tc.style == "msvc" ? cat(lib, ".lib") : cat("-l", lib);
    }

    // Compiles the C file `src` to the object `obj`, with `extra` flags
    // (a package's includes and defines) after the usual ones.
    bool CompileObject(const string &src, const string &obj, const string &what,
                       const string &logfile, const vector<string> &extra = {},
                       bool track_progress = true) const {
        vector<string> argv = { tc.cc };
        for (auto &f : CFlags()) argv.push_back(f);
        for (auto &f : extra) argv.push_back(f);
        if (tc.style == "msvc") {
            argv.push_back("/c");
            argv.push_back(src);
            argv.push_back(cat("/Fo:", obj));
            if (opts.debug) argv.push_back(cat("/Fd:", obj, ".pdb"));
        } else {
            argv.push_back("-c");
            argv.push_back(src);
            argv.push_back("-o");
            argv.push_back(obj);
        }
        return Run(argv, what, logfile, track_progress);
    }

    // Links `objects` and the system libraries `libs` (LibFlag spellings)
    // into the shared library `out`, with `builddir` taking the
    // intermediates: a package's JIT library.
    bool LinkShared(const vector<string> &objects, const vector<string> &libs, const string &out,
                    const string &builddir, const string &logfile, const string &what) const {
        vector<string> argv = { tc.cc };
        if (tc.style == "msvc") {
            argv.push_back("/nologo");
            argv.push_back("/LD");
            argv.push_back(cat("/Fe:", out));
            argv.push_back(cat("/Fo:", builddir, "\\"));
            for (auto &o : objects) argv.push_back(o);
            for (auto &l : libs) argv.push_back(l);
        } else {
            argv.push_back("-shared");
            for (auto &o : objects) argv.push_back(o);
            argv.push_back("-o");
            argv.push_back(out);
            for (auto &l : libs) argv.push_back(l);
            for (auto &l : SystemLibs()) argv.push_back(l);
        }
        return Run(argv, what, logfile);
    }

    // Compiles `src` and links it with `objects` and the response files
    // `rsps` into `exe`, with `builddir` taking the intermediates: an
    // executable, or with --shared a shared library, whose C holds the
    // runtime (a shared build is standalone) and whose package objects are
    // position independent already (native.h).
    bool Link(const string &src, const vector<string> &objects, const vector<string> &rsps,
              const string &exe, const string &builddir, const string &logfile,
              const vector<string> &extra = {}) const {
        vector<string> argv = { tc.cc };
        for (auto &f : CFlags()) argv.push_back(f);
        for (auto &f : extra) argv.push_back(f);
        if (tc.style == "msvc") {
            if (opts.shared) argv.push_back("/LD");
            argv.push_back(cat("/Fe:", exe));
            argv.push_back(cat("/Fo:", builddir, "\\"));
            if (opts.debug) argv.push_back(cat("/Fd:", builddir, "\\", PathStem(exe), ".pdb"));
            argv.push_back(src);
            for (auto &o : objects) argv.push_back(o);
            for (auto &r : rsps) argv.push_back(cat("@", r));
            if (!opts.debug && opts.lto && !opts.wasi) {
                argv.push_back("/link");
                argv.push_back("/LTCG");
            }
        } else {
            if (opts.shared) {
                // Not on Windows, where clang rejects the flag for its
                // target (the code is position independent regardless).
                #ifndef _WIN32
                    argv.push_back("-fPIC");
                #endif
                argv.push_back("-shared");
            }
            argv.push_back(src);
            for (auto &o : objects) argv.push_back(o);
            argv.push_back("-o");
            argv.push_back(exe);
            for (auto &r : rsps) argv.push_back(cat("@", r));
            for (auto &l : SystemLibs()) argv.push_back(l);
        }
        return Run(argv, cat("building ", exe), logfile);
    }
};

inline void AppendToolchainIdentity(string &input, const Builder &b) {
    auto driver = AbsolutePath(b.tc.cc);
    for (const auto &part : { driver, BuildPathIdentity(driver), b.tc.style,
                              b.tc.version, b.tc.banner })
        Append(input, part, '\0');
    for (const auto &flag : b.CFlags()) Append(input, flag, '\0');

    // Include environment options that change the target, driver selection
    // or system include search, even when the driver's banner is unchanged.
    for (auto name : { "PATH", "CPATH", "C_INCLUDE_PATH", "COMPILER_PATH", "GCC_EXEC_PREFIX",
                       "SDKROOT", "DEVELOPER_DIR", "TOOLCHAINS", "MACOSX_DEPLOYMENT_TARGET",
                       "CCC_OVERRIDE_OPTIONS", "CLANG_CONFIG_FILE_SYSTEM_DIR",
                       "CLANG_CONFIG_FILE_USER_DIR", "SOURCE_DATE_EPOCH", "CL", "_CL_",
                       "INCLUDE", "VCToolsInstallDir", "WindowsSdkDir", "WindowsSDKVersion",
                       "UniversalCRTSdkDir", "UCRTVersion", "VSCMD_ARG_TGT_ARCH" }) {
        auto value = getenv(name);
        Append(input, name, '=', value ? "1" : "0", value ? value : "", '\0');
    }
    // Include overrides may contain paths relative to the invocation. Scope
    // them to that directory if their meaning can vary between projects.
    if (getenv("CPATH") || getenv("C_INCLUDE_PATH") || getenv("CL") || getenv("_CL_") ||
        getenv("CCC_OVERRIDE_OPTIONS"))
        Append(input, AbsolutePath("."), '\0');
    #ifdef __APPLE__
        auto sdk = MacSdkPath();
        if (auto override = getenv("SDKROOT"); override && *override) sdk = AbsolutePath(override);
        if (!sdk.empty())
            Append(input, sdk, '\0', BuildPathIdentity(sdk), '\0',
                   BuildPathIdentity(cat(sdk, "/SDKSettings.json")), '\0');
    #endif
}

inline filesystem::path RuntimeEntryPath(const Builder &b) {
    static const auto source = Sha256(RuntimeObjectSource());
    auto input = source + '\0';
    AppendToolchainIdentity(input, b);
    return (filesystem::path(KlangHome()) / "build" / "runtime" /
            cat(RuntimeCachePrefix, Sha256(input))).lexically_normal().make_preferred();
}

// The macOS release can supply the optimized runtime for its Apple Clang
// profiles. Validate the object and the effective preprocessor input on this
// host, including the SDK headers. An incompatible seed is a cache miss.
inline bool SeedRuntimeObject([[maybe_unused]] const Builder &b, [[maybe_unused]] const string &dir,
                              [[maybe_unused]] const string &src, [[maybe_unused]] const string &tmp) {
    #ifdef __APPLE__
        if (dir.empty() || b.opts.debug || (b.tc.cc != "/usr/bin/cc" && b.tc.cc != "/usr/bin/clang") ||
            !b.tc.banner.starts_with("Apple clang version ") || b.CFlags() != vector<string>{ "-O2" })
            return false;
        for (auto name : { "CCC_OVERRIDE_OPTIONS", "CLANG_CONFIG_FILE_SYSTEM_DIR", "CLANG_CONFIG_FILE_USER_DIR",
                           "COMPILER_PATH", "TOOLCHAINS" })
            if (getenv(name)) return false;
        auto target = getenv("MACOSX_DEPLOYMENT_TARGET");
        if (target && string_view(target) != "13.3") return false;
        auto prefix = cat(dir, "/", target ? "13.3" : "default");
        string stamp, object;
        if (!LoadFile(prefix + ".stamp", stamp) || !LoadFile(prefix + ".o", object) || object.empty()) return false;
        auto probe = [&](vector<string> argv) {
            if (b.opts.verbose) {
                fprintf(b.msgs, "%s\n", CommandLineText(argv).c_str());
                fflush(b.msgs);
            }
            return RunProcess(argv);
        };
        // The driver plan also covers options that leave preprocessed C
        // unchanged, such as the effective target and codegen defaults.
        auto plan = probe({ b.tc.cc, "-O2", "-###", "-c", "-x", "c", "-", "-o", "-",
                            "-fdebug-compilation-dir=/", "-fcoverage-compilation-dir=/" });
        if (!plan.started || plan.code) return false;
        auto header = cat("k-native-runtime-v1\n", Sha256(plan.output), "\n", Sha256(object), "\n");
        if (!stamp.starts_with(header) || stamp.size() != header.size() + 65) return false;
        auto input = probe({ b.tc.cc, "-O2", "-E", "-P", src });
        if (!input.started || input.code || stamp != cat(header, Sha256(input.output), "\n")) return false;
        return WriteFileMakingDirs(tmp, object);
    #else
        return false;
    #endif
}

// The caller holds the entry's shared lease through publication and linking.
// The inner build lock serializes writers when the cache is cold.
inline string RuntimeObject(const Builder &b, const string &obj, const string &seed_dir) {
    if (PathExists(obj) && !b.opts.dryrun) return obj;
    auto dir = filesystem::path(obj).parent_path().string();
    auto src = cat(dir, "/k_runtime.c");
    auto log = cat(dir, "/cc.log");
    if (b.opts.dryrun) {
        b.CompileObject(src, obj, "compiling the runtime", log);
        return obj;
    }
    std::error_code ec;
    filesystem::create_directories(filesystem::path(dir), ec);
    if (ec) throw CompileError { cat("cannot create ", dir, ": ", ec.message()) };
    // Every first build shares these paths. Wait before writing the source
    // or invoking the compiler, then recheck what the previous builder left.
    FileLock lock(cat(dir, "/build.lock"), "waiting for another klang compiling the runtime");
    if (PathExists(obj)) return obj;
    if (!WriteFileMakingDirs(src, RuntimeObjectSource()))
        throw CompileError { cat("cannot write ", src) };
    // A failed or interrupted compiler may leave a partial object. Only a
    // successful compile publishes the name readers use, by atomic rename.
    auto tmp = cat(dir, "/k_runtime.tmp.", b.tc.style == "msvc" ? "obj" : "o");
    filesystem::remove(filesystem::path(tmp), ec);
    if (ec) throw CompileError { cat("cannot remove ", tmp, ": ", ec.message()) };
    auto seeded = SeedRuntimeObject(b, seed_dir, src, tmp);
    if (!seeded) {
        ProgressStep step(cat("compiling the runtime for ", b.tc.desc), true);
        if (!b.CompileObject(src, tmp, "compiling the runtime", log)) return "";
    }
    ec = RenamePath(tmp, obj);
    if (ec) throw CompileError { cat("cannot publish ", obj, ": ", ec.message()) };
    if (seeded) ProgressPrint("using packaged runtime object\n");
    return obj;
}

// What `k build` does once the compiler has written the program's C to
// `cfile` in `builddir`: finds the runtime object for the toolchain `tc`,
// which built the native parts whose response files are `nativersps`
// (native.h and builtin.h, found by the caller) in link order, a package's
// before the packages it requires and the program's own C first of all, and
// links the executable, or with --shared the shared library (lib<stem>.so,
// lib<stem>.dylib or <stem>.dll), named by -o, the program's sidecar
// (`output`) or the root file. The exit code.
inline int BuildExecutable(const string &rootfile, const string &cfile, const string &builddir,
                           const Toolchain &tc, const BuildOptions &opts, FILE *msgs,
                           const vector<string> &nativersps, const string &output,
                           const string &seed_dir,
                           [[maybe_unused]] bool uses_system_headers,
                           [[maybe_unused]] string_view cordis_identity = {}) {
    Builder b { tc, opts, msgs };
    string runtime;
    optional<FileLock> runtime_lease;
    if (!opts.standalone) {
        auto entry = RuntimeEntryPath(b);
        runtime = (entry / (opts.debug ? "debug" : "release") /
                   (tc.style == "msvc" ? "k_runtime.obj" : "k_runtime.o"))
                      .lexically_normal().make_preferred().string();
        if (!opts.dryrun) {
            std::error_code ec;
            filesystem::create_directories(entry.parent_path(), ec);
            if (ec) throw CompileError { cat("cannot create ", entry.parent_path().string(), ": ", ec.message()) };
            runtime_lease.emplace(ArtifactLeasePath(entry), "waiting for runtime cache pruning",
                                  FileLock::Mode::Shared);
        }
    }
    auto exe = opts.exe;
    if (exe.empty()) {
        auto stem = output.empty() ? PathStem(rootfile) : output;
        if (opts.wasi) {
            exe = cat(DirOf(rootfile), stem, stem.ends_with(".wasm") ? "" : ".wasm");
        } else if (opts.shared) {
            #ifdef _WIN32
                exe = cat(DirOf(rootfile), stem, ".dll");
            #elif defined(__APPLE__)
                exe = cat(DirOf(rootfile), "lib", stem, ".dylib");
            #else
                exe = cat(DirOf(rootfile), "lib", stem, ".so");
            #endif
        } else {
            exe = cat(DirOf(rootfile), stem);
            #ifdef _WIN32
                if (exe.size() < 4 || exe.compare(exe.size() - 4, 4, ".exe") != 0) exe += ".exe";
            #endif
        }
    }
    if (!opts.dryrun) {
        std::error_code ec;
        auto exedirpath = filesystem::path(exe).parent_path();
        if (!exedirpath.empty()) filesystem::create_directories(exedirpath, ec);
    }
    vector<string> extra;
    auto input = cfile;
    auto destination = exe;
    optional<PendingExecutable> wasm_output;
    if (opts.wasi) {
        // The C stack grows toward address zero. Keep a page out of the
        // address space so a wrap just below zero traps in linear memory.
        extra = { "-Wl,--stack-first,-z,stack-size=2097152,--max-memory=4294901760" };
        if (!opts.dryrun) {
            wasm_output.emplace(exe);
            if (wasm_output->path.empty()) throw CompileError { cat("cannot stage ", exe) };
            destination = wasm_output->path;
        }
    }
    #ifdef __APPLE__
        ExecutableCache cache;
        optional<PendingExecutable> pending;
        if (uses_system_headers && nativersps.empty() && !runtime.empty() && !opts.shared &&
            !opts.debug && !opts.dryrun)
            cache = ExecutableCache::Open(tc, builddir, cfile, runtime, exe,
                                          b.CFlags(), b.SystemLibs(), cordis_identity);
        if (!cache.dir.empty()) {
            if (cache.Hit()) {
                fprintf(msgs, "up to date %s\n", exe.c_str());
                return 0;
            }
            std::error_code ec;
            for (auto name : { "exe.inputs", "exe.headers", "exe.link-inputs", "exe.app.o" }) {
                filesystem::remove(filesystem::path(cache.Path(name)), ec);
                if (ec) {
                    cache = {};
                    break;
                }
            }
            if (!cache.dir.empty()) {
                pending.emplace(cache.inputs.front());
                if (pending->path.empty()) cache = {};
                else destination = pending->path;
            }
        }
        if (!cache.dir.empty()) {
            extra = cache.LinkFlags();
            auto compile_flags = cache.CompileFlags();
            if (PathExists(runtime)) {
                extra.insert(extra.end(), compile_flags.begin(), compile_flags.end());
            } else {
                // A cold runtime and the application's C are independent.
                // Overlap their compiles and link only after both succeeded.
                string built_runtime;
                exception_ptr failure;
                thread worker([&] {
                    try { built_runtime = RuntimeObject(b, runtime, seed_dir); }
                    catch (...) { failure = current_exception(); }
                });
                struct WorkerJoin {
                    thread &t;
                    ~WorkerJoin() { if (t.joinable()) t.join(); }
                } worker_guard { worker };
                input = cache.Path("exe.app.o");
                compile_flags.push_back("-pthread");
                auto compiled = b.CompileObject(cfile, input, cat("compiling ", cfile),
                                                cat(builddir, "/cc.log"), compile_flags, false);
                worker.join();
                if (!compiled || built_runtime.empty() || failure) {
                    cache.RemoveReports();
                    if (failure) rethrow_exception(failure);
                    return 1;
                }
            }
        }
    #endif
    vector<string> objects;
    if (!runtime.empty()) {
        auto obj = RuntimeObject(b, runtime, seed_dir);
        if (obj.empty()) return 1;
        objects.push_back(std::move(obj));
    }
    auto linked = b.Link(input, objects, nativersps, destination, builddir,
                         cat(builddir, "/cc.log"), extra);
    #ifdef __APPLE__
        if (!cache.dir.empty()) {
            if (linked) cache.Save(pending->Publish());
            cache.RemoveReports();
        }
    #endif
    if (!linked) return 1;
    if (wasm_output) {
        constexpr string_view magic("\0asm\x01\0\0\0", 8);
        char header[magic.size()];
        auto file = fopen(destination.c_str(), "rb");
        auto valid = file && fread(header, 1, sizeof header, file) == sizeof header &&
                     string_view(header, sizeof header) == magic;
        if (file) fclose(file);
        if (!valid)
            throw CompileError { "the WASI toolchain did not produce a WebAssembly executable" };
        wasm_output->Publish();
    }
    if (!opts.dryrun) fprintf(msgs, "built %s\n", exe.c_str());
    return 0;
}

}  // namespace klang
