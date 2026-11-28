"""One C toolchain for a whole build: `k build` of a program using a CMake
package builds the package, the runtime object and the executable with the
toolchain `k toolchain` reports, and a JIT `k run` builds the package
with it too. On Windows, building a CMake package imports Visual Studio's
environment, which puts cl on PATH partway through; so cl starts off it here,
as it would in a plain shell."""
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import toolchain as tc

HERE = Path(__file__).resolve().parent


def check_native_toolchain(exe, base, *, jit):
    """Returns why it was skipped, or None when every check passed; raises
    RuntimeError on a failure."""
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="toolchain-", dir=base) as tmp:
        root = Path(tmp)
        shutil.copytree(HERE / "packages" / "pkgs" / "cmk", root / "pkgs" / "cmk")
        source = shutil.copy2(HERE / "packages" / "pkg_cmake_lib.k", root)
        home = root / "home"
        env = {k: v for k, v in os.environ.items() if k != "K_CC"}
        env["K_HOME"] = str(home)
        if tc.IS_WINDOWS:
            env["PATH"] = os.pathsep.join(
                d for d in env.get("PATH", "").split(os.pathsep)
                if not d or not (Path(d) / "cl.exe").is_file())

        def run(argv):
            r = subprocess.run([str(a) for a in argv], cwd=root, env=env, capture_output=True,
                               text=True, errors="replace", timeout=600)
            return r.returncode, r.stdout, r.stderr

        def require(good, message):
            if not good:
                raise RuntimeError(f"native toolchain: {message}")

        code, out, err = run([exe, "toolchain", "--json"])
        try:
            info = json.loads(out)
        except ValueError:
            raise RuntimeError(f"native toolchain: k toolchain --json:\n{out}{err}")
        if not info.get("cc"):
            return "no C compiler"
        if not info.get("cmake"):
            return "no cmake"
        want = {info["cc"]["id"]}

        def ids(pattern):
            return {p.name for p in home.glob(pattern) if p.is_dir()}

        app = root / ("app" + tc.EXE_SUFFIX)
        code, out, err = run([exe, "build", "-o", app, source])
        require(code == 0, f"k build:\n{out}{err}")
        code, out, err = run([app])
        require(code == 0 and out.split() == ["12", "-15"], f"the built program:\n{out}{err}")
        packages = ids("build/pkg/local/*/*")
        runtimes = list((home / "build/runtime").glob("v1-*/release/k_runtime.*"))
        runtimes = [p for p in runtimes if p.suffix in (".o", ".obj")]
        require(packages == want, f"k build used {sorted(packages)}, k toolchain reports {sorted(want)}")
        code, out, err = run([exe, "build", "--dry-run", "--cc", info["cc"]["path"], "-o", app, source])
        require(code == 0 and len(runtimes) == 1 and str(runtimes[0]) in out + err,
                f"the reported compiler did not select the same runtime object:\n{out}{err}")
        app_opt = root / ("app-opt" + tc.EXE_SUFFIX)
        code, out, err = run([exe, "build", "-O3", "--lto", "--cpu=native", "-o", app_opt, source])
        require(code == 0, f"k build -O3 --lto --cpu=native:\n{out}{err}")
        code, out, err = run([app_opt])
        require(code == 0 and out.split() == ["12", "-15"],
                f"the built -O3 --lto --cpu=native program:\n{out}{err}")
        if jit:
            code, out, err = run([exe, "run", source])
            require(code == 0 and out.split() == ["12", "-15"], f"k run:\n{out}{err}")
            packages = ids("build/pkg/local/*/*")
            require(packages == want,
                    f"k run used toolchains {sorted(packages - want)} for the package, "
                    f"where k toolchain reports {sorted(want)}")
    return None


def check_cmake_response_files(exe, base):
    """Build and run a CMake interface package through a cc-style response
    file. MinGW needs a separate check from the default Windows toolchain,
    which can be MSVC and does not consume backslashes in the same way."""
    cc = shutil.which("gcc" if tc.IS_WINDOWS else "cc")
    if not cc:
        return "no gcc on PATH" if tc.IS_WINDOWS else "no cc on PATH"
    if not shutil.which("cmake"):
        return "no cmake on PATH"
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cmake-response-", dir=base) as tmp:
        root = Path(tmp)
        shutil.copytree(HERE / "packages" / "pkgs" / "cmk", root / "pkgs" / "cmk")
        source = shutil.copy2(HERE / "packages" / "pkg_cmake_lib.k", root)
        app = root / ("app" + tc.EXE_SUFFIX)
        for name in ("home", "home with spaces"):
            env = dict(os.environ, K_HOME=str(root / name))
            for argv, want in (([exe, "build", "--cc", cc, "-o", app, source], None),
                               ([app], ["12", "-15"])):
                result = subprocess.run([str(a) for a in argv], cwd=root, env=env,
                                        capture_output=True, text=True, errors="replace", timeout=600)
                if result.returncode or (want is not None and result.stdout.split() != want):
                    raise RuntimeError(f"CMake response file ({name}): exit {result.returncode}\n"
                                       f"{result.stdout}{result.stderr}")
    return None
