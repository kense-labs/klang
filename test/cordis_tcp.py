"""Run the real loopback provider through K's JIT and native backends."""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    exe, provider, library, build = map(lambda path: Path(path).resolve(), sys.argv[1:5])
    case = sys.argv[5] if len(sys.argv) == 6 else "read"
    with tempfile.TemporaryDirectory(prefix="cordis-tcp-", dir=build) as directory:
        check(exe, provider, library, Path(directory), case)


def check(exe, provider, library, base, case):
    (base / "lib").mkdir()
    if os.name == "nt":
        target = "tcp_fixture.dll"
    else:
        target = "libtcp_fixture.dylib" if sys.platform == "darwin" else "libtcp_fixture.so"
    shutil.copyfile(library, base / "lib" / target)
    source = base / f"tcp_{case}.k"
    shutil.copyfile(Path(__file__).with_name("cordis") / "fixtures" / source.name, source)
    native = source.with_suffix(".exe" if os.name == "nt" else "")
    env = dict(os.environ, K_CORDIS_TCP_READ_PROVIDER=str(provider))
    if case == "stream":
        expected = ["idle stream stopped polling and woke for a single TCP payload",
                    "bounded stream paused and resumed in FIFO order through EOF",
                    "small consumer slices preserved the complete TCP payload",
                    "stream delivered buffered data before the socket error",
                    "releasing a reader drained its in-flight socket operation",
                    "cancelled stream kept its retired provider through drain",
                    "unstarted stream released its socket"]
    else:
        expected = ["read byte", "read end of stream", "read socket error",
                    "cancelled and retired provider after drain", "released unstarted native operation",
                    "released pending operation on scope exit"]

    def invoke(*args):
        run = subprocess.run(args, cwd=base, env=env, capture_output=True, text=True, timeout=30)
        if run.returncode:
            raise RuntimeError(f"{args} exited {run.returncode}\n{run.stdout}{run.stderr}")
        return run.stdout.splitlines()

    jit = invoke(exe, "run", source)
    invoke(exe, "build", "-o", native, source)
    built = invoke(native)
    if jit != expected or built != expected:
        raise RuntimeError(f"Unexpected loopback output\nJIT {jit}\nNative {built}\nExpected {expected}")
    print(f"Cordis TCP {case} passed through K run and K build")


if __name__ == "__main__":
    main()
