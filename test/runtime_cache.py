"""The build driver's shared runtime cache, usage report and eviction."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

import toolchain as tc


def check_executable_cache(exe, base):
    if sys.platform != "darwin" or shutil.which("clang") != "/usr/bin/clang":
        return False
    if os.path.exists("/usr/local/include") or os.path.exists("/usr/local/lib"):
        return False
    sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True)
    if sdk.returncode or not sdk.stdout.strip():
        return False
    if any((Path(sdk.stdout.strip()) / "usr/local" / p).exists() for p in ("include", "lib")):
        return False
    with tempfile.TemporaryDirectory(prefix="executable cache ", dir=base) as tmp:
        root = Path(tmp)
        home = root / "home"
        source, app = root / "app.k", root / "app"
        env = dict(PATH="/usr/bin:/bin", K_HOME=str(home), K_PROGRESS="plain")
        cmd = [str(exe), "build", "--cc", "clang", "-O2", "-o", str(app), str(source)]

        def build(*args, hit=False, extra_env=None):
            r = subprocess.run(cmd + list(args), cwd=root, env=dict(env, **(extra_env or {})),
                               text=True, capture_output=True, timeout=60)
            output = r.stdout + r.stderr
            if r.returncode or ("up to date " in output) != hit:
                raise RuntimeError(f"executable cache: expected hit={hit}:\n{output}")

        def read_app():
            return subprocess.check_output([app], text=True, timeout=10)

        source.write_text('fn main() { print("first"); }\n')
        build()
        original = app.stat().st_mtime_ns
        build(hit=True)
        if app.stat().st_mtime_ns != original or read_app() != "first\n":
            raise RuntimeError("executable cache: an unchanged build changed the executable")
        build("--dry-run")
        build(hit=True)
        source.write_text('fn main() { print("second"); }\n')
        build()
        if read_app() != "second\n":
            raise RuntimeError("executable cache: a source edit kept the old executable")
        app.unlink()
        build()
        app.chmod(0o644)
        build()
        if read_app() != "second\n":
            raise RuntimeError("executable cache: the rebuilt executable did not run")
        runtime = next(home.glob("build/runtime/*/*/k_runtime.o"))
        os.utime(runtime, None)
        build()
        build(hit=True)
        with subprocess.Popen(cmd, cwd=root, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT) as worker:
            build(hit=True)
            output, _ = worker.communicate(timeout=60)
            if worker.returncode or b"up to date " not in output:
                raise RuntimeError(f"executable cache: concurrent hit:\n{tc.decode(output)}")
        source.write_text('fn main() { print("first"); }\n')
        other = root / "other.k"
        other.write_text('fn main() { print("other"); }\n')
        other_cmd = cmd[:-1] + [str(other)]
        with subprocess.Popen(other_cmd, cwd=root, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT) as worker:
            build()
            output, _ = worker.communicate(timeout=60)
            if worker.returncode:
                raise RuntimeError(f"executable cache: second root:\n{tc.decode(output)}")
        for command, expected in ((cmd, "first\n"), (other_cmd, "other\n")):
            subprocess.run(command, cwd=root, env=env, check=True, capture_output=True, timeout=60)
            if read_app() != expected:
                raise RuntimeError("executable cache: another root's binary was reused")
        build(extra_env={"CPATH": str(root)})
        return True


def check_runtime_cache(exe, cc, base):
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="runtime-cache-", dir=base) as tmp:
        root = Path(tmp)
        compiler = root / ("cache-cc" + tc.EXE_SUFFIX)
        ok, log = cc.compile(Path(__file__).with_name("runtime_cache_cc.c"), compiler)
        if not ok:
            raise RuntimeError(log)
        # A clang-cl basename selects MSVC flag spelling without importing
        # Visual Studio's environment. The probe understands both styles.
        msvc = root / ("clang-cl" + tc.EXE_SUFFIX)
        shutil.copy2(compiler, msvc)
        for driver in (compiler, msvc):
            for debug in (False, True):
                case = root / f"{driver.stem}-{'debug' if debug else 'release'}"
                case.mkdir()
                _check_case(exe, driver, case, debug)


def _check_case(exe, compiler, base, debug):
    home = base / "home"
    env = dict(os.environ, K_HOME=str(home), K_CACHE_TEST=str(base))
    for name in ("CPATH", "C_INCLUDE_PATH", "CL", "_CL_", "CCC_OVERRIDE_OPTIONS"):
        env.pop(name, None)
    flags = ["--debug"] if debug else []

    def command(name, extra=()):
        project = base / name
        project.mkdir(exist_ok=True)
        source = project / f"{name}.k"
        tc.write_text(source, 'fn main() { print("cache"); }\n')
        return [str(arg) for arg in (exe, "build", "--cc", compiler, *flags, *extra,
                                    "-o", project / (name + tc.EXE_SUFFIX), source)]

    def run(name, extra=(), extra_env=None):
        args = command(name, extra)
        return subprocess.run(args, cwd=Path(args[-1]).parent, env=dict(env, **(extra_env or {})),
                              capture_output=True, text=True, errors="replace", timeout=30)

    def start(name):
        args = command(name)
        return subprocess.Popen(args, cwd=Path(args[-1]).parent, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def require(good, message):
        if not good:
            raise RuntimeError(f"runtime cache ({base.name}): {message}")

    def cached_objects():
        return list(home.glob("build/runtime/*/*/k_runtime.o")) + \
               list(home.glob("build/runtime/*/*/k_runtime.obj"))

    def wait_for(predicate, message):
        deadline = time.monotonic() + 20
        while not predicate():
            require(time.monotonic() < deadline, message)
            time.sleep(0.01)

    def cache(*args):
        result = subprocess.run([str(exe), "cache", *args, "--json"], cwd=base, env=env,
                                capture_output=True, text=True, timeout=15)
        require(result.returncode == 0, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def entry_bytes(entry):
        return sum(p.stat().st_size for p in entry.rglob("*") if p.is_file() and not p.is_symlink())

    require(cache("stats")["total_bytes"] == 0 and not home.exists(),
            "stats created a cache directory")
    dry = run("dry", ["--dry-run"])
    require(dry.returncode == 0 and "k_runtime" in dry.stdout + dry.stderr,
            dry.stdout + dry.stderr)
    require(not (home / "build/runtime").exists(), "dry run changed the runtime cache")

    (base / "fail").touch()
    failed = run("failed")
    require(failed.returncode != 0 and
            "intentional runtime compile failure" in failed.stdout + failed.stderr,
            failed.stdout + failed.stderr)
    require(not cached_objects(), "a failed compile published an object")
    logs = list(home.glob("build/runtime/*/*/cc.log"))
    require(len(logs) == 1 and "intentional runtime compile failure" in logs[0].read_text(),
            "failed compile did not preserve its diagnostic log")
    (base / "fail").unlink()
    before = set(base.glob("started-*"))

    workers = []
    try:
        # The first retry leaves a partial object on disk while the other
        # builders start. Neither a cache hit nor a second writer may use it.
        workers.append(start("concurrent_0"))
        wait_for(lambda: set(base.glob("started-*")) != before,
                 "runtime compiler did not start")
        require(not cached_objects(), "an unfinished object became a cache hit")
        for i in range(1, 4):
            workers.append(start(f"concurrent_{i}"))
        wait_for(lambda: len(list(home.glob("build/prog/*/concurrent_*.c"))) == 4,
                 "concurrent builders did not generate their programs")
        (base / "release").touch()
        for worker in workers:
            output, _ = worker.communicate(timeout=30)
            require(worker.returncode == 0, tc.decode(output))
    finally:
        (base / "release").touch()
        for worker in workers:
            if worker.poll() is None:
                worker.kill()
            worker.communicate()

    require(len(set(base.glob("started-*")) - before) == 1,
            "concurrent builders compiled the same runtime more than once")
    objects = cached_objects()
    require(len(objects) == 1 and objects[0].read_text() == "complete",
            "runtime object was not published after compilation")
    for i in range(4):
        name = f"concurrent_{i}"
        require((base / name / (name + tc.EXE_SUFFIX)).read_text() == "linked",
                "a concurrent project did not link")
    stamp = objects[0].stat().st_mtime_ns
    warm = run("warm")
    require(warm.returncode == 0, warm.stdout + warm.stderr)
    require(objects[0].stat().st_mtime_ns == stamp and
            len(set(base.glob("started-*")) - before) == 1,
            "a warm build recompiled the runtime")

    variant_env = {"SDKROOT": str(base / "sdk")}
    alternate = run("alternate", extra_env=variant_env)
    require(alternate.returncode == 0, alternate.stdout + alternate.stderr)
    original = objects[0]
    alternatives = set(cached_objects()) - {original}
    require(len(alternatives) == 1, "a different SDK selection reused the cached runtime")
    alternate_object = alternatives.pop()

    legacy = home / "build/runtime/old-runtime/old-tool/k_runtime"
    package = home / "build/pkg/local/dependency/native.o"
    checkout = home / "pkg/locked/source.k"
    for path in (legacy, package, checkout):
        path.parent.mkdir(parents=True)
        path.write_bytes(b"keep")
    runtime_bytes = entry_bytes(original.parent.parent) + entry_bytes(alternate_object.parent.parent)
    if not tc.IS_WINDOWS:
        # A valid-looking entry name cannot redirect the pruner outside its store.
        linked_entry = home / "build/runtime" / ("v1-" + "0" * 64)
        linked_entry.symlink_to(checkout.parent, target_is_directory=True)

    stats = cache("stats")
    groups = stats["groups_bytes"]
    require(stats["managed_runtime_bytes"] == runtime_bytes and groups["runtime"] == runtime_bytes + 4,
            f"incorrect runtime accounting: {stats}")
    require(groups["native_packages"] == 4 and groups["git_checkouts"] == 4 and
            groups["project_builds"] > 0 and stats["total_bytes"] == sum(groups.values()),
            f"incorrect cache accounting: {stats}")

    (base / "pause-link").touch()
    with start("in_use") as worker:
        try:
            wait_for(lambda: (base / "link-started").exists(), "native link did not start")
            in_use = entry_bytes(original.parent.parent)
            report = cache("prune", "--budget", str(in_use))
            require(original.is_file() and not alternate_object.exists() and
                    report["reclaimed_bytes"] == runtime_bytes - in_use and
                    report["remaining_bytes"] == in_use and report["budget_met"],
                    f"did not meet budget with a live link: {report}")
            report = cache("prune", "--budget", "0")
            require(original.is_file() and report["reclaimed_bytes"] == 0 and
                    report["remaining_bytes"] == report["skipped_in_use_bytes"] == in_use and
                    not report["budget_met"], f"did not report the protected bytes: {report}")
        finally:
            (base / "finish-link").touch()
        output, _ = worker.communicate(timeout=30)
        require(worker.returncode == 0, tc.decode(output))

    rebuilt = run("rebuilt", ["--offline"], variant_env)
    require(rebuilt.returncode == 0 and alternate_object.read_text() == "complete",
            rebuilt.stdout + rebuilt.stderr)
    report = cache("prune", "--budget", "0")
    require(report["budget_met"] and report["remaining_bytes"] == 0 and
            report["reclaimed_bytes"] == report["before_bytes"],
            f"idle cache did not meet budget: {report}")
    require(all(p.read_bytes() == b"keep" for p in (legacy, package, checkout)),
            "prune changed data outside the managed cache")
