// K compiler — load a program's source and import graph into one Ast.
#pragma once

namespace klang {

// Where the standard library lives (§11.1): an explicit --stdlib or
// K_STDLIB, else a `stdlib/` directory found by walking up from the
// executable (a release's, or the source tree's for a build inside it), else
// the one CMake was configured with (K_STDLIB_PATH: the source tree's, or
// a package's install location). A directory more than one of those name is
// in the list once, as the first named it.
inline vector<string> StdlibDirs(const string &stdlibdir, const string &exe) {
    vector<string> cands;
    if (!stdlibdir.empty()) cands.push_back(cat(stdlibdir, "/"));
    if (auto env = getenv("K_STDLIB")) cands.push_back(cat(env, "/"));
    auto exedir = DirOf(exe);
    cands.push_back(cat(exedir, "stdlib/"));
    cands.push_back(cat(exedir, "../stdlib/"));
    cands.push_back(cat(exedir, "../../stdlib/"));
    cands.push_back(cat(exedir, "../../../stdlib/"));
    #ifdef K_STDLIB_PATH
        if (*K_STDLIB_PATH) cands.push_back(cat(K_STDLIB_PATH, "/"));
    #endif
    cands.push_back("stdlib/");
    vector<string> dirs;
    set<string> seen;
    for (auto &dir : cands) {
        std::error_code ec;
        auto canon = filesystem::weakly_canonical(filesystem::path(dir), ec);
        // Normalized: its files' paths are what diagnostics and runtime errors print.
        if (seen.insert(ec ? dir : canon.string()).second)
            dirs.push_back(filesystem::path(dir).lexically_normal().generic_string());
    }
    return dirs;
}

struct ProgramLoadOptions {
    const vector<string> &stdlibdirs;
    string exedir;
    PackageFetchMode fetch;
};

// An error at an import in the file named `file`: over the whole `import ...;`.
inline Diagnostic ImportDiagnostic(const string &file, const Import &imp, const string &msg) {
    Diagnostic d;
    d.where = LocOf(imp.line);
    if (imp.end.line > 0) {
        d.where.endline = imp.end.line;
        d.where.endcol = imp.end.col;
    }
    d.message = msg;
    d.text = RenderDiagnostic(d, file, {});
    return d;
}

[[noreturn]] inline void ImportError(const string &file, const Import &imp, const string &msg) {
    auto d = ImportDiagnostic(file, imp, msg);
    throw CompileError { d.text, { d } };
}

// Parses a root file and, transitively, everything it imports (each file once).
// `import a.b;` resolves relative to the importing file's package root (§11.1):
// the root file's directory for the program's own files, a library's
// directory for its modules; then as a module of a package the root
// declared; then in the standard library. `import .a.b;` resolves relative to
// the importing file's directory. `import name from "source";` declares a
// package in the root file and is a requirement anywhere else, which the
// root's declaration must satisfy (docs/design/packages.md §4.3). A file's
// text comes from `sources` where that has it (SourceProvider, utils.h),
// else from disk. `exedir` is the compiler's directory, where a release
// keeps the packages compiled into it. An import that fails is reported
// at the import (Ast::Report), and the program goes on without what it
// would have loaded: the names it would have declared are unknown where
// they are used.
inline void ParseProgram(Ast &ast, const string &rootpath, const ProgramLoadOptions &options,
                         const SourceProvider &sources = {}) {
    // A sourced import as declared by the root file.
    struct DeclaredPackage {
        PackageSource source;
        int pkg = 0;   // Its index in Ast::packages.
        int line = 0;  // Where the root declared it.
    };
    auto available = [&](const string &path) {
        return (sources && sources(path)) || FileExists(path);
    };
    // The program is package 0; a standard library directory becomes a
    // package the first time a module in it is reached.
    auto rootdir = DirOf(rootpath);
    ast.packages.push_back(Package { PathStem(rootpath), rootdir, false });
    LoadSidecar(ast.packages[0], true);
    auto stdlibpkg = [&](const string &dir) {
        for (size_t i = 0; i < ast.packages.size(); i++)
            if (ast.packages[i].stdlib && ast.packages[i].root == dir) return (int)i;
        ast.packages.push_back(Package { "stdlib", dir, true });
        return (int)ast.packages.size() - 1;
    };
    map<string, DeclaredPackage> declared;
    // A package is its directory: every .k file under its root is its
    // own, and its native part is built in a directory named by that root.
    // Two packages cannot share one; the program can share its root with one.
    auto checkroot = [&](const string &name, const string &root) {
        for (size_t i = 1; i < ast.packages.size(); i++)
            if (!ast.packages[i].stdlib && ast.packages[i].root == root)
                throw CompileError { cat("the packages ", ast.packages[i].name, " and ", name,
                                         " have the same root, ", root,
                                         ": each package needs a directory of its own") };
    };
    // Loads a blessed package (§4.5) once, under its package name among the
    // declared ones: from where the compiler's build put it, or a release
    // beside the compiler, when it is compiled in, else from its source as a
    // declared package would be (a path in the table is relative to the
    // source tree the standard library is found in).
    auto loadblessed = [&](const BlessedEntry &be) -> int {
        auto it = declared.find(be.package);
        if (it != declared.end()) return it->second.pkg;
        auto builtin = BuiltinLookup(be.package);
        auto builtindir = builtin ? BuiltinSrcDir(*builtin, options.exedir) : string();
        PackageSource src;
        src.text = be.source;
        string root;
        if (builtin && PathExists(SourcePath(cat(builtindir, be.package)))) {
            src.ispath = true;
            src.dir = root = builtindir;
        } else if (IsPathSource(be.source)) {
            auto rel = be.source.rfind("./", 0) == 0 ? be.source.substr(2) : be.source;
            for (auto &dir : options.stdlibdirs) {
                auto cand = cat(dir, "../", rel);
                if (PathExists(SourcePath(cat(cand, "/", be.package)))) {
                    src.ispath = true;
                    src.dir = PackageDir(cand);
                    break;
                }
            }
            if (src.dir.empty())
                throw CompileError { cat("cannot find the blessed package ", be.package,
                                         " at ", be.source, " of the source tree") };
            root = PackageRoot(src, be.package, options.fetch);
        } else {
            src = ParsePackageSource(cat(be.source, "@", be.ref), "");
            root = PackageRoot(src, be.package, options.fetch);
        }
        checkroot(be.package, root);
        ast.packages.push_back(Package { be.package, root, false });
        auto &p = ast.packages.back();
        SetPackageCheckout(p, src);
        if (builtin && root == builtindir) p.builtin = BuiltinIndex(builtin);
        LoadSidecar(p, false);
        declared.emplace(be.package, DeclaredPackage { src, (int)ast.packages.size() - 1, 0 });
        return (int)ast.packages.size() - 1;
    };
    vector<pair<string, int>> queue = { { rootpath, 0 } };   // (path, its package).
    set<string> loaded = { rootpath };
    map<string, int> fileof;                 // Resolved path -> index in ast.sources.
    vector<vector<string>> importsof;        // Per source, the paths it imports.
    // An imported file's first import, where an error opening it is.
    map<string, pair<string, Import>> importedat;
    // The packages whose declaration, or blessed load, failed: what imports
    // them again was reported at that import.
    set<string> failedpkgs;
    while (!queue.empty()) {
        auto [path, pkg] = queue.back();
        queue.pop_back();
        auto contents = make_unique<string>();
        if (auto text = sources ? sources(path) : nullptr) {
            *contents = *text;
            StripBom(*contents);
        } else if (!LoadSource(path, *contents)) {
            auto it = importedat.find(path);
            if (it == importedat.end()) throw CompileError { cat("cannot open file: ", path) };
            ast.Report(ImportDiagnostic(it->second.first, it->second.second,
                                        cat("cannot open file: ", path)));
            continue;
        }
        auto fileidx = (int)ast.sources.size();
        ast.sources.emplace_back(path, std::move(contents));
        ast.filepkg.push_back(pkg);
        fileof[path] = fileidx;
        importsof.emplace_back();
        auto &[filename, source] = ast.sources.back();
        Parser parser(ast, filename, *source, fileidx);
        parser.ParseTop();
        for (auto &imp : parser.imports) {
            // The package this import declares or loads, where it does.
            string pkgname;
            try {
                auto imppkg = pkg;
                string imppath;
                if (!imp.source.empty()) {
                    if (failedpkgs.count(imp.path)) continue;
                    pkgname = imp.path;
                    PackageSource src;
                    try {
                        src = ParsePackageSource(imp.source, DirOf(path));
                    } catch (CompileError &e) {
                        ImportError(filename, imp, e.msg);
                    }
                    auto it = declared.find(imp.path);
                    if (fileidx == 0) {
                        // The root declares the package, and imports its entry.
                        if (it != declared.end())
                            ImportError(filename, imp, cat("the package ", imp.path,
                                                                " is declared twice"));
                        string root;
                        try {
                            root = PackageRoot(src, imp.path, options.fetch);
                            checkroot(imp.path, root);
                        } catch (CompileError &e) {
                            ImportError(filename, imp, e.msg);
                        }
                        ast.packages.push_back(Package { imp.path, root, false });
                        SetPackageCheckout(ast.packages.back(), src);
                        LoadSidecar(ast.packages.back(), false);
                        auto dp = DeclaredPackage { src, (int)ast.packages.size() - 1,
                                                    imp.line.line };
                        it = declared.emplace(imp.path, dp).first;
                    } else {
                        // A requirement: satisfied by the root's declaration, or
                        // by the blessed table where the root declared nothing.
                        if (it == declared.end())
                            if (auto be = BlessedLookup(imp.path); be && be->package == imp.path) {
                                loadblessed(*be);
                                it = declared.find(imp.path);
                            }
                    }
                    if (it == declared.end()) {
                        // A requirement the root does not satisfy: the error is
                        // the line to add there.
                        ImportError(filename, imp,
                                    cat(ast.packages[pkg].name, " needs the package ", imp.path,
                                        ": add\n    import ", imp.path, " from \"",
                                        SourceForRoot(src, rootdir), "\";\nto ", rootpath,
                                        " (packages are declared by the root file only)"));
                    } else if (fileidx != 0 && !SameSource(it->second.source, src)) {
                        ImportError(filename, imp,
                                    cat(ast.packages[pkg].name, " needs the package ", imp.path,
                                        " from ", src.text, ", which ", rootpath, ":",
                                        it->second.line, " declared from a different source, ",
                                        it->second.source.text));
                    } else if (fileidx != 0 && it->second.source.ref != src.ref) {
                        ast.Warn(LocOf(imp.line),
                                 cat(ast.packages[pkg].name, " needs the package ", imp.path,
                                     " at ", src.ref, ", which ", rootpath, ":", it->second.line,
                                     " declares at ", it->second.source.ref));
                    }
                    imppkg = it->second.pkg;
                    // What a package requires links after it (§5.5).
                    if (fileidx != 0 && pkg != imppkg) {
                        auto &req = ast.packages[pkg].required;
                        if (std::find(req.begin(), req.end(), imppkg) == req.end())
                            req.push_back(imppkg);
                    }
                    imppath = SourcePath(cat(ast.packages[imppkg].root, imp.path));
                } else if (imp.relative) {
                    imppath = SourcePath(cat(DirOf(path), imp.path));
                } else {
                    // A file under this package's root; a module of a package the
                    // root declared (`import pkg;` is its entry, `import pkg.sub;`
                    // its sub.k); a blessed name, its package loaded as if
                    // the root had declared it (§4.5); the standard library.
                    auto local = SourcePath(cat(ast.packages[pkg].root, imp.path));
                    auto first = imp.path.substr(0, imp.path.find('/'));
                    auto rest = imp.path.size() > first.size() ? imp.path.substr(first.size() + 1)
                                                               : first;
                    auto it = declared.find(first);
                    // A package's own name, from inside it, is its own file.
                    if (it != declared.end() && it->second.pkg == pkg) it = declared.end();
                    if (it == declared.end() && failedpkgs.count(first)) continue;
                    auto blessed = it == declared.end() && !available(local) ? BlessedLookup(first)
                                                                               : nullptr;
                    if (blessed) pkgname = blessed->package;
                    if (it != declared.end()) {
                        if (available(local))
                            ImportError(filename, imp,
                                        cat(imp.path, " is both a file of this package (", local,
                                            ") and the declared package ", first, "'s module"));
                        imppkg = it->second.pkg;
                        imppath = SourcePath(cat(ast.packages[imppkg].root, rest));
                    } else if (blessed) {
                        try {
                            imppkg = loadblessed(*blessed);
                        } catch (CompileError &e) {
                            ImportError(filename, imp, e.msg);
                        }
                        imppath = SourcePath(cat(ast.packages[imppkg].root, rest));
                    } else {
                        imppath = local;
                        if (!available(local)) {
                            for (auto &dir : options.stdlibdirs) {
                                auto cand = SourcePath(cat(dir, imp.path));
                                if (available(cand)) {
                                    imppath = cand;
                                    imppkg = stdlibpkg(dir);
                                    break;
                                }
                            }
                            // Not found: where it was looked for, the standard
                            // library being the directories that hold std.k.
                            if (imppath == local) {
                                auto name = imp.path;
                                for (auto &c : name) if (c == '/') c = '.';
                                auto msg = cat("cannot find the module ", name,
                                               ", looked for\n    ",
                                               ForwardSlashes(AbsolutePath(local)));
                                auto havestd = false;
                                for (auto &dir : options.stdlibdirs)
                                    if (FileExists(cat(dir, "std.k"))) {
                                        havestd = true;
                                        auto cand = cat(dir, imp.path, ".k");
                                        Append(msg, "\n    ", ForwardSlashes(AbsolutePath(cand)));
                                    }
                                if (!havestd) {
                                    Append(msg, "\nand found no standard library, looking in");
                                    for (auto &dir : options.stdlibdirs)
                                        Append(msg, "\n    ", ForwardSlashes(AbsolutePath(dir)));
                                    Append(msg, "\nset --stdlib or K_STDLIB to its directory");
                                }
                                ImportError(filename, imp, msg);
                            }
                        }
                    }
                }
                importsof[fileidx].push_back(imppath);
                ast.imports.push_back({ imp.line, imp.end, imppath });
                if (loaded.insert(imppath).second) {
                    queue.push_back({ imppath, imppkg });
                    importedat.emplace(imppath, make_pair(filename, imp));
                }
            } catch (CompileError &e) {
                if (e.diags.empty()) ast.Report(ImportDiagnostic(filename, imp, e.msg));
                for (auto &d : e.diags) ast.Report(d);
                if (!pkgname.empty() && !declared.count(pkgname)) failedpkgs.insert(pkgname);
            }
        }
    }
    // A namespace alias's target must be a namespace some file declares,
    // which only now are all known. Where an error came first, a file that
    // would have declared it may be the one that failed to load or parse,
    // so the target is not reported as well. An alias may take the name of
    // a namespace: it holds in its own file alone, which then reaches that
    // namespace only through an alias of another name.
    if (ast.errors.empty())
        for (auto &a : ast.nsaliases) {
            if (ast.FindNS(a.target)) continue;
            Diagnostic d;
            d.where = LocOf(a.targetline);
            d.where.endline = a.targetline.line;
            d.where.endcol = a.targetline.col + (int)a.target.size();
            d.message = cat("unknown namespace ", a.target, " in namespace alias ", a.name);
            ast.Report(d);
        }
    // Globals initialize in import order: everything a file imports runs before
    // the file's own, so a global initializer may name an imported global
    // (`let IDENT = Xf { ..., t: float3_0 }`). Files parse in worklist order,
    // which is not that, so rank them by a post-order walk of the import graph
    // and stable-sort the initialization list by rank. A file in an import
    // cycle is visited once, and its rank is then whatever the walk reached
    // first, which is all that order can mean there.
    vector<int> rank(ast.sources.size(), 0);
    vector<char> state(ast.sources.size(), 0);   // 0 unvisited, 1 on the stack, 2 ranked.
    auto next = 0;
    std::function<void(int)> visit = [&](int f) {
        if (state[f]) return;
        state[f] = 1;
        for (auto &p : importsof[f]) {
            auto it = fileof.find(p);
            if (it != fileof.end()) visit(it->second);
        }
        state[f] = 2;
        rank[f] = next++;
    };
    for (size_t i = 0; i < ast.sources.size(); i++) visit((int)i);
    std::stable_sort(ast.globals.begin(), ast.globals.end(),
                     [&](VarDecl *a, VarDecl *b) {
                         return rank[a->line.fileidx] < rank[b->line.fileidx];
                     });
    // A sidecar's dylibs entry names a library some extern block declares
    // (§7.10): the program's, one in any package it uses; a package's, one
    // of its own. A package's blocks are those of all its files, including
    // modules this program does not import, which only a scan of them finds
    // (PackageExternNames); it is made only for a name no parsed block has.
    vector<set<string>> scanned(ast.packages.size());
    vector<bool> isscanned(ast.packages.size());
    auto scannedlibs = [&](size_t p) -> const set<string> & {
        if (!isscanned[p]) {
            PackageExternNames(ast.packages[p], &scanned[p]);
            isscanned[p] = true;
        }
        return scanned[p];
    };
    for (size_t p = 0; p < ast.packages.size(); p++) {
        auto &pkg = ast.packages[p];
        for (auto &d : pkg.dylibs) {
            auto declared = false;
            for (auto sf : ast.functions)
                if (sf->dylib == d.name && (p == 0 || ast.filepkg[sf->line.fileidx] == (int)p))
                    declared = true;
            for (size_t q = 1; q < ast.packages.size() && !declared; q++)
                if ((p == 0 || q == p) && !ast.packages[q].stdlib && ast.packages[q].builtin < 0 &&
                    !ast.packages[q].root.empty() && scannedlibs(q).count(d.name))
                    declared = true;
            if (!declared)
                throw CompileError { cat(pkg.sidecar, ": dylibs maps ", d.name, ", which no "
                                         "extern block ", p == 0 ? "in the program" : "of the package",
                                         " names") };
        }
    }
}

}  // namespace klang
