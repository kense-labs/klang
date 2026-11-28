"""A package's native part, built into one directory under K_HOME for
every program that uses it: concurrent first builds compile its C once, the
others waiting for that build and linking what it made, and a warm build
compiles nothing. The controlled C compiler, runtime_cache_cc.c, holds the
first compile until the test releases it."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import threading
import time

import toolchain as tc

WAITING = "waiting for another klang building the package shared's C"


def check_native_cache(exe, cc, base):
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="native-cache-", dir=base) as tmp:
        root = Path(tmp)
        compiler = root / ("cache-cc" + tc.EXE_SUFFIX)
        ok, log = cc.compile(Path(__file__).with_name("runtime_cache_cc.c"), compiler)
        if not ok:
            raise RuntimeError(log)
        # The clang-cl copy spells the flags as MSVC does (runtime_cache.py).
        msvc = root / ("clang-cl" + tc.EXE_SUFFIX)
        shutil.copy2(compiler, msvc)
        for driver in (compiler, msvc):
            case = root / f"case-{driver.stem}"
            case.mkdir()
            _check_case(exe, driver, case)


def _check_case(exe, compiler, base):
    home = base / "home"
    # A live progress line on the pipe as well, which says what a build waits for.
    env = dict(os.environ, K_HOME=str(home), K_CACHE_TEST=str(base),
               K_PROGRESS="live", COLUMNS="200")
    pkg = base / "pkgs" / "shared"
    pkg.mkdir(parents=True)
    tc.write_text(pkg / "shared.k", "namespace shared;\nextern fn shared_one() -> i64;\n"
                                        "fn one() -> i64 { shared_one() }\n")
    tc.write_text(pkg / "shared.c", "/* What the controlled compiler compiles. */\n")
    tc.write_text(pkg / "shared.build", 'c_sources = ["shared.c"];\n')

    def start(name):
        source = base / f"{name}.k"
        tc.write_text(source, 'import std;\nimport shared from "./pkgs/shared";\n'
                              "fn main() { print(shared::one()); }\n")
        argv = [str(a) for a in (exe, "build", "--cc", compiler, "-o",
                                 base / (name + tc.EXE_SUFFIX), source)]
        p = subprocess.Popen(argv, cwd=base, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
        # What it printed so far, read as it comes.
        p.text = []

        def read():
            for chunk in iter(lambda: p.stdout.read1(4096), b""):
                p.text.append(tc.decode(chunk))
        p.reader = threading.Thread(target=read, daemon=True)
        p.reader.start()
        return p

    def require(good, message):
        if not good:
            raise RuntimeError(f"native part cache ({base.name}): {message}")

    def compiles():
        """The objects the controlled compiler was asked for, by its markers."""
        return [m.read_text() for m in base.glob("started-*")]

    def package_compiles():
        return [c for c in compiles() if "/build/pkg/" in c.replace("\\", "/")]

    def wait_for(predicate, message):
        deadline = time.monotonic() + 20
        while not predicate():
            require(time.monotonic() < deadline, message)
            time.sleep(0.01)

    workers = []
    try:
        workers.append(start("first"))
        wait_for(lambda: package_compiles(), "the package's C did not start compiling")
        for i in range(3):
            workers.append(start(f"concurrent_{i}"))
        # Each waits for the first build, unless it compiles the C itself.
        wait_for(lambda: len(package_compiles()) > 1 or
                 all(WAITING in "".join(w.text) for w in workers[1:]),
                 "the builders neither waited for the package nor compiled it")
        (base / "release").touch()
        for w in workers:
            w.wait(timeout=60)
            w.reader.join(timeout=60)
            require(w.returncode == 0, "".join(w.text))
    finally:
        (base / "release").touch()
        for w in workers:
            if w.poll() is None:
                w.kill()
            w.wait()
    built = package_compiles()
    require(len(built) == 1, f"concurrent builds compiled the package's C {len(built)} times")
    for name in ["first"] + [f"concurrent_{i}" for i in range(3)]:
        require((base / (name + tc.EXE_SUFFIX)).read_text() == "linked",
                f"{name} did not link")
    before = len(compiles())
    warm = start("warm")
    warm.wait(timeout=60)
    warm.reader.join(timeout=60)
    require(warm.returncode == 0, "".join(warm.text))
    require(len(compiles()) == before, "a warm build compiled again")


def check_locked_native_cache(exe, cc, base, *, jit):
    git = shutil.which("git")
    if not git:
        return "no git on PATH"
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="locked-native-", dir=base) as tmp:
        root = Path(tmp)
        compiler = root / ("cache-cc" + tc.EXE_SUFFIX)
        ok, log = cc.compile(Path(__file__).with_name("runtime_cache_cc.c"), compiler)
        if not ok:
            raise RuntimeError(log)
        _check_locked_native(exe, cc.cc, compiler, git, root, jit)
    return None


def _check_locked_native(exe, driver, compiler, git, root, jit):
    home = root / "home"
    env = dict(os.environ, K_HOME=str(home), K_CACHE_TEST=str(root), GIT_ALLOW_PROTOCOL="file")
    for name in ("CPATH", "C_INCLUDE_PATH", "CL", "_CL_", "CCC_OVERRIDE_OPTIONS"):
        env.pop(name, None)

    def run(argv, *, cwd=root, run_env=env, ok=True):
        result = subprocess.run(argv, cwd=cwd, env=run_env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace", timeout=60)
        if (result.returncode == 0) != ok or tc.sanitizer_failure(result.stdout):
            raise RuntimeError(f"{argv} (exit {result.returncode}):\n{result.stdout}")
        return result.stdout

    def require(good, message):
        if not good:
            raise RuntimeError(f"locked native cache: {message}")

    def wait_for(path, worker):
        deadline = time.monotonic() + 20
        while not path.exists() and worker.poll() is None and time.monotonic() < deadline:
            time.sleep(0.01)
        require(path.exists() and worker.poll() is None,
                f"worker did not reach {path.name} (exit {worker.poll()})")

    def repository(name, sidecar, source, cmake=None):
        repo = root / (name + "-source")
        repo.mkdir()
        tc.write_text(repo / f"{name}.k",
                      f"namespace {name};\nextern fn {name}_value() -> i64;\n")
        tc.write_text(repo / f"{name}.build", sidecar)
        tc.write_text(repo / "native.c", source)
        if cmake:
            tc.write_text(repo / "CMakeLists.txt", cmake)
        cmd = [git, "-c", "user.name=K", "-c", "user.email=k@example.test", "-c", "commit.gpgsign=false"]
        run(cmd + ["init", "-q", "-b", "main"], cwd=repo)
        run(cmd + ["add", "."], cwd=repo)
        run(cmd + ["commit", "-qm", "native package"], cwd=repo)
        revision = run(cmd + ["rev-parse", "HEAD"], cwd=repo).strip()
        bare = root / (name + ".git")
        run(cmd + ["clone", "-q", "--bare", repo, bare])
        run(cmd + ["config", "uploadpack.allowAnySHA1InWant", "true"], cwd=bare)
        return bare, f'import {name} from "{bare.as_uri()}@{revision}";\n'

    def project(name, package, imported):
        path = root / name / "main.k"
        path.parent.mkdir()
        tc.write_text(path, imported + f"fn main() {{ print({package}::{package}_value()); }}\n")
        return path

    def build(source, cc_path, *flags):
        exe_path = source.parent / ("app" + tc.EXE_SUFFIX)
        return [exe, "build", "--offline", "--cc", cc_path, "-o", exe_path, *flags, source], exe_path

    def cache(*args):
        return json.loads(run([exe, "cache", *args, "--json"]))

    def objects():
        return sorted(p for p in home.glob("build/native/*/output/native-*")
                      if p.suffix in (".o", ".obj"))

    def native_compiles():
        return [p for p in root.glob("started-*") if "/build/native/" in
                p.read_text().replace("\\", "/")]

    native_c = """#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
int64_t locked_value(void) {
    if (getenv("K_NATIVE_GATE")) {
        FILE *marker = fopen("jit-active", "w");
        if (!marker || fclose(marker)) return -1;
        for (int tries = 0; tries < 3000; ++tries) {
            FILE *done = fopen("jit-finish", "r");
            if (done) { fclose(done); return 23; }
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10000);
#endif
        }
        return -1;
    }
    return 23;
}
"""
    bare, imported = repository("locked", 'c_sources = ["native.c"];\n', native_c)
    first = project("first", "locked", imported)
    second = project("second", "locked", imported)
    run([exe, "fetch", first])
    bare.rename(root / "unavailable.git")
    first_build, _ = build(first, compiler)
    second_build, _ = build(second, compiler)
    run(first_build + ["--dry-run"])
    require(cache("stats")["managed_native_bytes"] == 0, "a dry run wrote managed output")

    (root / "fail").touch()
    failure = run(first_build, ok=False)
    require("intentional runtime compile failure" in failure and
            not list(home.glob("build/native/*/output/stamp")), "a failed native compile was published")
    (root / "fail").unlink()
    (root / "release").touch()
    run(first_build)
    require(len(objects()) == 1, "expected one compiled native object")
    active = objects()[0]
    before = len(native_compiles())
    run(second_build)
    require(objects() == [active] and len(native_compiles()) == before, "a second project recompiled the package")
    variant_env = dict(env, SDKROOT=str(root / "alternate-sdk"))
    run(second_build, run_env=variant_env)
    idle, = set(objects()) - {active}
    totals = cache("stats")
    require(totals["managed_native_bytes"] > 0 and totals["groups_bytes"]["git_checkouts"] > 0,
            f"expected native and checkout usage: {totals}")

    (root / "pause-link").touch()
    with subprocess.Popen(first_build, cwd=root, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True) as work:
        try:
            wait_for(root / "link-started", work)
            pruned = cache("prune", "--budget", "0")
            require(active.is_file() and not idle.exists() and pruned["reclaimed_bytes"] > 0 and
                    pruned["remaining_bytes"] == pruned["skipped_in_use_bytes"] > 0,
                    f"prune while linking: {pruned}")
        finally:
            (root / "finish-link").touch()
            text, _ = work.communicate(timeout=30)
        require(work.returncode == 0, text)
    (root / "pause-link").unlink()
    require(cache("prune", "--budget", "0")["budget_met"] and not objects(),
            "idle output still exists")
    run(first_build)
    require(active.is_file() and cache("stats")["groups_bytes"]["git_checkouts"] ==
            totals["groups_bytes"]["git_checkouts"], "offline build lost its checkout or output")

    real_build, app = build(first, driver)
    run(real_build)
    require(run([app]).strip() == "23", "rebuilt native executable returned the wrong value")
    if jit:
        ext = ".dll" if tc.IS_WINDOWS else ".dylib" if tc.IS_MACOS else ".so"
        command = [exe, "run", "--offline", "--cc", driver, first]
        with subprocess.Popen(command, cwd=root, env=dict(env, K_NATIVE_GATE="1"),
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as work:
            try:
                wait_for(root / "jit-active", work)
                library, = home.glob(f"build/native/*/output/k_jit_locked{ext}")
                report = cache("prune", "--budget", "0")
                require(library.is_file() and report["skipped_in_use_bytes"] > 0 and
                        report["remaining_bytes"] == report["skipped_in_use_bytes"],
                        f"prune during JIT use: {report}")
            finally:
                (root / "jit-finish").touch()
                text, _ = work.communicate(timeout=30)
            require(work.returncode == 0 and text.rstrip().endswith("23"), text)
        require(cache("prune", "--budget", "0")["budget_met"] and not library.exists(),
                "an exited JIT still holds its artifact")
        require(run(command).rstrip().endswith("23"), "offline JIT did not rebuild")

    if not shutil.which("cmake"):
        return
    payload = root / "payload"
    payload.mkdir()
    tc.write_text(payload / "CMakeLists.txt",
                  "cmake_minimum_required(VERSION 3.20)\nproject(payload C)\n"
                  "add_library(payload STATIC payload.c)\n")
    tc.write_text(payload / "payload.c", "#include <stdint.h>\nint64_t payload(void) { return 47; }\n")
    archive = root / "payload.tar.gz"
    with tarfile.open(archive, "w:gz") as output:
        output.add(payload, arcname="payload")
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    cmake = f"""cmake_minimum_required(VERSION 3.20)
project(cached C)
include(FetchContent)
FetchContent_Declare(payload URL "{archive.as_posix()}" URL_HASH SHA256={digest})
FetchContent_MakeAvailable(payload)
add_library(cached STATIC native.c)
target_link_libraries(cached PRIVATE payload)
"""
    bare, imported = repository("cmake_locked", 'cmake_target = "cached";\n',
                                "#include <stdint.h>\nint64_t payload(void);\n"
                                "int64_t cmake_locked_value(void) { return payload(); }\n", cmake)
    cmake_first = project("cmake_first", "cmake_locked", imported)
    cmake_second = project("cmake_second", "cmake_locked", imported)
    cmake_build, cmake_app = build(cmake_first, driver)
    run([arg for arg in cmake_build if arg != "--offline"])
    source, = home.glob("build/native/*/dependencies/payload-src/payload.c")
    stamp = source.parents[2] / "output" / "stamp"
    configured = stamp.parent / "cmake" / "klang-configured.txt"
    configured_without_offline = configured.read_text()
    command, other_app = build(cmake_second, driver)
    run(command)
    require(configured.read_text() == configured_without_offline and run([other_app]).strip() == "47",
            "a warm CMake build did not reuse the online artifact offline")
    bare.rename(root / "missing-cmake.git")
    archive.rename(root / "missing-payload.tar.gz")
    report = cache("prune", "--budget", "0")
    require(report["budget_met"] and source.is_file() and not stamp.exists(),
            f"CMake prune lost input or kept output: {report}")
    run(cmake_build)
    require(stamp.is_file() and run([cmake_app]).strip() == "47",
            "CMake did not rebuild offline from retained dependencies")

    # CMake's nominally static target may have a shared dependency. When
    # the executable needs a file in the entry, a completed link is not
    # the end of its use of that entry.
    cmake = """cmake_minimum_required(VERSION 3.20)
project(runtime_dep C)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/payload.c" [=[
#include <stdint.h>
int64_t payload(void) { return 61; }
]=])
add_library(runtimebits SHARED "${CMAKE_CURRENT_BINARY_DIR}/payload.c")
set_target_properties(runtimebits PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
    BUILD_WITH_INSTALL_NAME_DIR TRUE
    INSTALL_NAME_DIR "${CMAKE_CURRENT_BINARY_DIR}")
add_library(cached STATIC native.c)
target_link_libraries(cached PRIVATE runtimebits)
"""
    _, imported = repository("runtime_locked", 'cmake_target = "cached";\n',
                             "#include <stdint.h>\nint64_t payload(void);\n"
                             "int64_t runtime_locked_value(void) { return payload(); }\n", cmake)
    uses_shared = project("uses_shared", "runtime_locked", imported)
    command, app = build(uses_shared, driver)
    run([arg for arg in command if arg != "--offline"])
    ext = ".dll" if tc.IS_WINDOWS else ".dylib" if tc.IS_MACOS else ".so"
    library, = home.glob(f"build/native/*/output/**/*runtimebits{ext}")
    search = "PATH" if tc.IS_WINDOWS else "DYLD_LIBRARY_PATH" if tc.IS_MACOS else "LD_LIBRARY_PATH"
    runtime_env = dict(env)
    runtime_env[search] = os.pathsep.join(filter(None, [str(library.parent), env.get(search)]))
    require(run([app], run_env=runtime_env).strip() == "61", "shared dependency did not load")
    report = cache("prune", "--budget", "0")
    totals = cache("stats")
    require(report["budget_met"] and library.is_file() and totals["managed_native_bytes"] == 0 and
            totals["groups_bytes"]["native_packages"] >= library.stat().st_size,
            f"prune lost an AOT runtime dependency or miscounted its bytes: {report}, {totals}")
    require(run([app], run_env=runtime_env).strip() == "61", "prune broke the linked executable")
