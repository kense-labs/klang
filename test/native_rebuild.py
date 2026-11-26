"""When a native part is built again: a program's own C edited after a build
is compiled again for both `k build` and `k run`, but not when the
program is named by another path, and an extern fn added to the program
without touching its C rebuilds the JIT library, whose symbol table names
every extern fn. Two C files of one name in different directories each have
an object of their own. --debug builds the program's own C for a debugger,
apart from its release build, and a package's C release. A library's symbol
table leaves out the extern fns of an example with a sidecar of its own and
of a directory its sidecar excludes.

A CMake package is configured again when its cmake_options change, including
after a configure that failed for want of one; a cmake_target its project
does not define is reported at configure; a target compiled as C++ that
publishes a C++ standard to what links it builds, for both runs; built again
with nothing changed, the files the compiler generates for CMake keep their
times; and a program's own CMake target builds whether the program is named
by a relative path, from its directory or by its absolute path."""
import json
import os
import subprocess
import tempfile
from pathlib import Path

import toolchain as tc


def check_native_rebuild(exe, base, *, jit):
    """Returns why a part was skipped, or None when every check ran; raises
    RuntimeError on a failure."""
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rebuild-", dir=base) as tmp:
        root = Path(tmp)
        env = dict(os.environ)
        env["K_HOME"] = str(root / "home")

        def run(argv, cwd=root):
            r = subprocess.run([str(a) for a in argv], cwd=cwd, env=env, capture_output=True,
                               text=True, errors="replace", timeout=600)
            return r.returncode, r.stdout, r.stderr

        def require(good, message):
            if not good:
                raise RuntimeError(f"native rebuild: {message}")

        def expect(argv, want, what, cwd=root):
            code, out, err = run(argv, cwd)
            require(code == 0 and (want is None or out.split() == want),
                    f"{what}: wanted {' '.join(want or ['success'])}, got exit {code}:\n"
                    f"{out}{err}")

        def expect_failure(argv, message, what):
            code, out, err = run(argv)
            require(code != 0 and message in out + err,
                    f"{what}: wanted a failure saying {message!r}, got exit {code}:\n{out}{err}")

        # The program's own C.
        def program(*externs):
            tc.write_text(root / "rebuild.k",
                          "import std;\n" +
                          "".join(f"extern fn {e}() -> i64;\n" for e in externs) +
                          "fn main() {\n" + "".join(f"    print({e}());\n" for e in externs) +
                          "}\n")

        def impl(a):
            tc.write_text(root / "rebuild_impl.c",
                          "#include <stdint.h>\n"
                          f"int64_t rebuild_a(void) {{ return {a}; }}\n"
                          "int64_t rebuild_b(void) { return 2; }\n")

        tc.write_text(root / "rebuild.build", 'c_sources = ["rebuild_impl.c"];\n')
        program("rebuild_a")
        impl(1)
        source = root / "rebuild.k"
        app = root / ("rebuild" + tc.EXE_SUFFIX)
        expect([exe, "build", "-o", app, source], None, "k build")
        expect([app], ["1"], "the built program")
        # Named by a path relative to the working directory, it is the same
        # program, whose C is built.
        code, out, err = run([exe, "build", "-o", app, source.name])
        require(code == 0 and "building the package rebuild's C" not in out + err,
                f"k build by a relative path built the program's C again:\n{out}{err}")
        impl(11)
        expect([exe, "build", "-o", app, source], None, "k build after editing the C")
        expect([app], ["11"], "the program built after editing the C")

        # Two C files of one name, in different directories.
        twin = root / "twin"
        for sub, value in (("src", 1), ("lib", 2)):
            (twin / sub).mkdir(parents=True)
            tc.write_text(twin / sub / "util.c", "#include <stdint.h>\n"
                          f"int64_t twin_{sub}(void) {{ return {value}; }}\n")
        tc.write_text(twin / "twin.build", 'c_sources = ["src/util.c", "lib/util.c"];\n')
        tc.write_text(twin / "twin.k", "import std;\nextern fn twin_src() -> i64;\n"
                      "extern fn twin_lib() -> i64;\n"
                      'fn main() { print(twin_src(), " ", twin_lib()); }\n')
        twinapp = root / ("twin_app" + tc.EXE_SUFFIX)
        expect([exe, "build", "-o", twinapp, twin / "twin.k"], None,
               "k build of two C files of one name")
        expect([twinapp], ["1", "2"], "the program built from two C files of one name")
        if jit:
            expect([exe, "run", twin / "twin.k"], ["1", "2"],
                   "k run of two C files of one name")

        # --debug compiles the program's own C for a debugger, apart from its
        # release build, and leaves a package's C a release build.
        rel = root / "pkgs" / "rel"
        rel.mkdir(parents=True)
        tc.write_text(rel / "rel.k", "namespace rel;\nextern fn rel_three() -> i64;\n"
                                         "fn f() -> i64 { rel_three() }\n")
        tc.write_text(rel / "rel.c", "#include <stdint.h>\nint64_t rel_three(void) { return 3; }\n")
        tc.write_text(rel / "rel.build", 'c_sources = ["rel.c"];\n')
        tc.write_text(root / "dbg.k", 'import std;\nimport rel from "./pkgs/rel";\n'
                                          "extern fn dbg_four() -> i64;\n"
                                          'fn main() { print(rel::f(), " ", dbg_four()); }\n')
        tc.write_text(root / "dbg_impl.c",
                      "#include <stdint.h>\nint64_t dbg_four(void) { return 4; }\n")
        tc.write_text(root / "dbg.build", 'c_sources = ["dbg_impl.c"];\n')
        dbgapp = root / ("dbg" + tc.EXE_SUFFIX)
        for flags, want in ((["--debug"], {"dbg_impl.c": True, "rel.c": False}),
                            ([], {"dbg_impl.c": False}),
                            (["--debug"], {})):
            how = " ".join(flags) or "release"
            code, out, err = run([exe, "build", "-v", *flags, "-o", dbgapp, root / "dbg.k"])
            require(code == 0, f"k build {how}:\n{out}{err}")
            expect([dbgapp], ["3", "4"], f"the program built {how}")
            # The C files the build compiled, each with whether it was for a
            # debugger, from the commands -v prints.
            got = {}
            for line in out.splitlines():
                words = line.split()
                if "-c" in words or "/c" in words:
                    for name in ("dbg_impl.c", "rel.c"):
                        if name in line:
                            got[name] = "-O0" in words or "/Od" in words
            require(got == want, f"k build {how} compiled {got} (true: for a debugger), "
                                 f"not {want}:\n{out}")
        if jit:
            expect([exe, "run", source], ["11"], "k run")
            impl(111)
            expect([exe, "run", source], ["111"], "k run after editing the C")
            program("rebuild_a", "rebuild_b")
            expect([exe, "run", source], ["111", "2"], "k run after adding an extern fn")

            # A library whose example has C of its own, and a directory its
            # sidecar excludes: their extern fns are none of the library's.
            nest = root / "pkgs" / "nest"
            for file, text in {
                    "nest.k": "namespace nest;\nextern fn nest_seven() -> i64;\n"
                                  "fn f(x: i64) -> i64 { nest_seven() * x }\n",
                    "nest.c": "#include <stdint.h>\nint64_t nest_seven(void) { return 7; }\n",
                    "nest.build": 'c_sources = ["nest.c"];\nexclude = ["tools"];\n',
                    "examples/demo/demo.k": "import std;\nextern fn demo_only() -> i64;\n"
                                                "fn main() { print(demo_only()); }\n",
                    "examples/demo/demo.c": "#include <stdint.h>\n"
                                            "int64_t demo_only(void) { return 1; }\n",
                    "examples/demo/demo.build": 'c_sources = ["demo.c"];\n',
                    "tools/gen.k": "extern fn tools_only() -> i64;\n"}.items():
                (nest / file).parent.mkdir(parents=True, exist_ok=True)
                tc.write_text(nest / file, text)
            user = root / "use_nest.k"
            tc.write_text(user, 'import std;\nimport nest from "./pkgs/nest";\n'
                                "fn main() { print(nest::f(7)); }\n")
            expect([exe, "run", user], ["49"], "k run of a library with an example of its own")

        code, out, err = run([exe, "toolchain", "--json"])
        try:
            info = json.loads(out)
        except ValueError:
            raise RuntimeError(f"native rebuild: k toolchain --json:\n{out}{err}")
        if not info.get("cmake"):
            return "no cmake"

        def package(name, lists, sources, klang):
            d = root / "pkgs" / name
            d.mkdir(parents=True, exist_ok=True)
            tc.write_text(d / "CMakeLists.txt", lists)
            for file, text in sources.items():
                tc.write_text(d / file, text)
            tc.write_text(d / f"{name}.k", klang)
            user = root / f"use_{name}.k"
            tc.write_text(user, f'import std;\nimport {name} from "./pkgs/{name}";\n'
                                f"fn main() {{ print({name}::f(7)); }}\n")
            return d / f"{name}.build", user, root / (f"use_{name}" + tc.EXE_SUFFIX)

        # A project that will not configure without an option.
        sidecar, user, app = package(
            "cfg",
            "cmake_minimum_required(VERSION 3.20)\nproject(cfg C)\n"
            "if(NOT DEFINED CFG_SCALE)\n"
            '    message(FATAL_ERROR "cfg needs CFG_SCALE")\n'
            "endif()\n"
            "add_library(cfg STATIC cfg.c)\n"
            "target_compile_definitions(cfg PRIVATE CFG_SCALE=${CFG_SCALE})\n",
            {"cfg.c": "#include <stdint.h>\n"
                      "int64_t cfg_scaled(int64_t x) { return CFG_SCALE * x; }\n"},
            "namespace cfg;\nextern fn cfg_scaled(x: i64) -> i64;\n"
            "fn f(x: i64) -> i64 { cfg_scaled(x) }\n")
        tc.write_text(sidecar, 'cmake_target = "cfg";\n')
        expect_failure([exe, "build", "-o", app, user], "cfg needs CFG_SCALE",
                       "k build of a package missing a CMake option")
        for scale in (2, 3):
            tc.write_text(sidecar, f'cmake_target = "cfg";\ncmake_options = ["CFG_SCALE={scale}"];\n')
            expect([exe, "build", "-o", app, user], None, f"k build with CFG_SCALE={scale}")
            expect([app], [str(7 * scale)], f"the program built with CFG_SCALE={scale}")
        tc.write_text(sidecar, 'cmake_target = "nosuch";\ncmake_options = ["CFG_SCALE=3"];\n')
        expect_failure([exe, "build", "-o", app, user],
                       "names the CMake target nosuch, which",
                       "k build of a package naming a target its project does not define")

        # A C++ target publishing the standard it needs.
        sidecar, user, app = package(
            "cxx",
            "cmake_minimum_required(VERSION 3.20)\nproject(cxx CXX)\n"
            "add_library(cxx STATIC cxx.cpp)\n"
            "target_compile_features(cxx PUBLIC cxx_std_20)\n",
            {"cxx.cpp": "#include <cstdint>\n"
                        "template <typename T> constexpr T twice(T x) { return x + x; }\n"
                        'extern "C" int64_t cxx_twice(int64_t x) noexcept { return twice(x); }\n'},
            "namespace cxx;\nextern fn cxx_twice(x: i64) -> i64;\n"
            "fn f(x: i64) -> i64 { cxx_twice(x) }\n")
        tc.write_text(sidecar, 'cmake_target = "cxx";\n')
        expect([exe, "build", "-o", app, user], None, "k build of a C++ package")
        expect([app], ["14"], "the program using a C++ package")
        if jit:
            expect([exe, "run", user], ["14"], "k run of a C++ package")

        # Built again with nothing changed, the files the compiler generates
        # for CMake keep their times, so that its build relinks nothing.
        def wrapper(d):
            lists = d / "wrapper" / "CMakeLists.txt"
            return lists.read_text() if lists.is_file() else ""

        built = [d for d in (root / "home" / "build" / "pkg" / "local").glob("*/*")
                 if "project(k_pkg_cxx " in wrapper(d)]
        require(len(built) == 1, f"the C++ package was built in {built}")
        generated = [built[0] / name for name in
                     ("k_symbols.c", "k_linkprobe.c", "wrapper/CMakeLists.txt",
                      "include/klang.h", f"link-{info['cc']['style']}.rsp")]
        if jit:
            generated += list(built[0].glob("k_jit_cxx.*"))
        times = {f: f.stat().st_mtime_ns for f in generated}
        if jit:
            expect([exe, "run", user], ["14"], "k run of a C++ package again")
        else:
            expect([exe, "build", "-o", app, user], None, "k build of a C++ package again")
        rewritten = [str(f.relative_to(built[0])) for f in generated
                     if f.stat().st_mtime_ns != times[f]]
        require(not rewritten, f"a build with nothing changed rewrote {rewritten}")

        # A program's own CMake target, the program named by a path relative
        # to the working directory, from its directory and by its absolute
        # path.
        own = root / "own"
        own.mkdir()
        tc.write_text(own / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                                              "project(ownlib C)\nadd_library(ownlib STATIC ownlib.c)\n")
        tc.write_text(own / "ownlib.c",
                      "#include <stdint.h>\nint64_t own_nine(void) { return 9; }\n")
        tc.write_text(own / "own.build", 'cmake_target = "ownlib";\n')
        tc.write_text(own / "own.k", "import std;\nextern fn own_nine() -> i64;\n"
                                         "fn main() { print(own_nine()); }\n")
        ownapp = root / ("own_app" + tc.EXE_SUFFIX)
        for path, cwd, how in (("own/own.k", root, "by a relative path"),
                               ("own.k", own, "from its directory"),
                               (own / "own.k", root, "by its absolute path")):
            expect([exe, "build", "-o", ownapp, path], None,
                   f"k build of a program with a CMake target of its own {how}", cwd=cwd)
            expect([ownapp], ["9"], f"the program with a CMake target of its own, built {how}")
    return None
