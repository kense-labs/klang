"""Exercise the Linux cgroup hard limit with the installed inference gateway."""
import argparse
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import tempfile

from cordis_gateway import DONE, REQUEST, connect, request, setup
from cordis_inference import blob, classifier, integer, model, node, tensor, tensor_value


def pressure_model(rows):
    """Expand a runtime input before reducing it back to the wire shape."""
    def initializer(name, values):
        return (integer(1, len(values)) + integer(2, 7) + blob(8, name) +
                blob(9, struct.pack(f"<{len(values)}q", *values)))

    return model(
        blob(1, node("Expand", ["input", "shape"], "expanded")) +
        blob(1, node("ReduceSum", ["expanded", "axes"], "sums")) +
        blob(1, node("MatMul", ["sums", "weights"], "output")) +
        blob(2, "pressure") +
        blob(5, initializer("shape", [rows, 3])) + blob(5, initializer("axes", [0])) +
        blob(5, tensor("weights", [3, 2], [1, -1, 2, 1, -1, 2])) +
        blob(11, tensor_value("input", [1, 3])) + blob(12, tensor_value("output", [1, 2]))
    )


def memory_events(cgroup):
    return {key: int(value) for key, value in
            (line.split() for line in (cgroup / "memory.events").read_text().splitlines())}


def check(install, budget_mib):
    cgroup = Path("/sys/fs/cgroup")
    if os.getpid() != 1 or (cgroup / "memory.oom.group").read_text().strip() != "0":
        raise ValueError("run the test as PID 1 in a dedicated container with per-process OOM handling")
    limit = (cgroup / "memory.max").read_text().strip()
    if limit != str(budget_mib * 2**20) or (cgroup / "memory.swap.max").read_text().strip() != "0":
        raise ValueError("run this test in a dedicated Docker cgroup with the specified RAM limit and no swap")
    install = install.resolve()
    lib = install / "lib"
    # The intermediate alone touches twice the cgroup limit. Its ONNX
    # description stays small, so the initial load does not allocate it.
    rows = (2 * int(limit) + 11) // 12
    with tempfile.TemporaryDirectory(prefix="gateway-cgroup-") as work:
        root = Path(work)
        env = {**os.environ, "K_HOME": str(root / "home")}
        _, native = setup(install / "k", lib / "libk_cordis_application.so",
                          lib / "libk_cordis_onnx.so", lib / "libonnxruntime.so", root, env,
                          source_path=install / "examples/inference_gateway.k",
                          native_binary=install / "k-inference-gateway",
                          models={"a": classifier([0, 1]), "b": pressure_model(rows)})
        before = memory_events(cgroup)
        with subprocess.Popen([*native, "providers.json", "a.lock", "0", "1", "20000", "16", "1", "2",
                               "reload", "b", "b.lock", "--memory-budget-mib", str(budget_mib)],
                              cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as child:
            ready = ""
            try:
                if select.select([child.stdout], [], [], 30)[0]:
                    ready = child.stdout.readline().strip()
                assert ready.startswith("ready "), ready
                port = int(ready.split()[1])
                assert request(port) == (DONE, [2.0, 8.0])
                (root / "reload").touch()
                with connect(port, timeout=25) as peer:
                    peer.sendall(REQUEST)
                    try:
                        assert peer.recv(1) == b""
                    except ConnectionResetError:
                        pass
                code = child.wait(timeout=30)
            finally:
                if child.poll() is None:
                    child.kill()
                stdout, stderr = child.communicate(timeout=10)
                print(ready, stdout, stderr, sep="\n", end="")
        after = memory_events(cgroup)
        assert code == -signal.SIGKILL and after["oom_kill"] > before["oom_kill"], (code, before, after)
        assert "selected b" in stdout, stdout
        print(f"Kernel enforced {limit} bytes during inference after replacement (oom_kill {before['oom_kill']} -> {after['oom_kill']})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--memory-budget-mib", type=int, default=96)
    args = parser.parse_args()
    if not 64 <= args.memory_budget_mib <= 192:
        parser.error("test memory budget must be between 64 and 192 MiB")
    check(args.install, args.memory_budget_mib)
