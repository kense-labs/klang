"""Verify actual Linux OOM rollback from a packaged, supervised gateway."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
from queue import Queue
import signal
import socket
import subprocess
import tempfile
from threading import Thread
import time

from cordis_gateway import DONE, REQUEST, connect, request, setup
from cordis_gateway_memory import memory_events, pressure_model
from cordis_inference import classifier


@contextmanager
def launch(command, root, env):
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as diagnostics, subprocess.Popen(
            command, cwd=root, env=env, text=True, stdout=subprocess.PIPE, stderr=diagnostics) as process:
        lines = Queue()

        def read():
            for line in process.stdout:
                lines.put(line.strip())
            lines.put(None)

        reader = Thread(target=read, daemon=True)
        reader.start()
        try:
            yield process, lambda: lines.get(timeout=30)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            reader.join(timeout=5)
            diagnostics.seek(0)
            print(diagnostics.read(), end="", flush=True)


def expect_pressure(port):
    with connect(port, timeout=25) as peer:
        peer.sendall(REQUEST)
        try:
            assert peer.recv(1) == b""
        except ConnectionResetError:
            pass


def check(install, group):
    install, group = install.resolve(), group.resolve()
    maximum = int((group / "memory.max").read_text().strip())
    if (not 64 * 2**20 <= maximum <= 192 * 2**20 or maximum % 2**20 or
            (group / "memory.swap.max").read_text().strip() != "0" or
            (group / "memory.oom.group").read_text().strip() != "1" or
            (group / "cgroup.procs").read_text().strip()):
        raise ValueError("expected an empty dedicated worker cgroup with 64–192 MiB and no swap")
    budget = maximum // 2**20
    lib = install / "lib"
    with tempfile.TemporaryDirectory(prefix="cordis-supervisor-") as work:
        root = Path(work)
        env = {**os.environ, "K_HOME": str(root / "home")}
        setup(install / "k", lib / "libk_cordis_application.so", lib / "libk_cordis_onnx.so",
              lib / "libonnxruntime.so", root, env, source_path=install / "examples/inference_gateway.k",
              native_binary=install / "k-inference-gateway",
              models={"a": classifier([0, 1]), "b": pressure_model((2 * maximum + 11) // 12)})
        with socket.socket() as free_port:
            free_port.bind(("127.0.0.1", 0))
            port = free_port.getsockname()[1]
        command = [install / "k-inference-supervisor", "--worker-cgroup", group, "--memory-budget-mib", str(budget)]
        rollout = ["--reload", "reload", "b", "b.lock", "--rollback-file", "rolled-back"]
        stable_args = ["--", "providers.json", "a.lock", str(port), "1", "20000", "16", "1", "0"]
        ready = f"ready {port}"
        marker = root / "reload"
        state = root / "rolled-back"
        before = memory_events(group)["oom_kill"]
        with launch([*command, *rollout, *stable_args], root, env) as (process, line):
            assert line() == ready
            occupied = subprocess.run([*command, *stable_args], cwd=root, env=env,
                                      capture_output=True, text=True, timeout=10)
            assert occupied.returncode != 0 and "worker cgroup must be empty" in occupied.stderr, occupied
            assert request(port) == (DONE, [2.0, 8.0])
            marker.touch()
            expect_pressure(port)
            assert line() == "selected b"
            assert line() == ready
            assert request(port) == (DONE, [2.0, 8.0])
            after_rollback = memory_events(group)["oom_kill"]
            assert state.is_file() and after_rollback > before
            process.terminate()
            assert process.wait(timeout=10) == 128 + signal.SIGTERM
        assert not (group / "cgroup.procs").read_text().strip()
        print(f"OOM rollback restored stable predictions on port {port}")

        with launch([*command, *rollout, *stable_args[:-1], "1"], root, env) as (process, line):
            assert line() == ready
            assert request(port) == (DONE, [2.0, 8.0])
            assert process.wait(timeout=10) == 0 and line().startswith("peak pending ")
        assert marker.is_file() and memory_events(group)["oom_kill"] == after_rollback
        print("Durable rollback suppressed the same candidate after supervisor restart")

        no_oom = rollout[:-1] + ["no-oom-state"]
        marker.unlink()
        with launch([*command, *no_oom, *stable_args], root, env) as (process, line):
            assert line() == ready
            pid, = map(int, (group / "cgroup.procs").read_text().split())
            os.kill(pid, signal.SIGKILL)
            assert process.wait(timeout=10) == 128 + signal.SIGKILL and line() is None
        assert not (root / "no-oom-state").exists() and memory_events(group)["oom_kill"] == after_rollback
        print("External SIGKILL did not trigger OOM rollback")

        with launch([*command, *stable_args], root, env) as (process, line):
            assert line() == ready
            process.kill()
            assert process.wait(timeout=10) == -signal.SIGKILL and line() is None
        # The worker is killed by the parent-death signal before it is
        # reaped by the container init, so allow time for cgroup accounting.
        deadline = time.monotonic() + 5
        while (group / "cgroup.procs").read_text().strip() and time.monotonic() < deadline:
            time.sleep(0.01)
        assert not (group / "cgroup.procs").read_text().strip()
        assert memory_events(group)["oom_kill"] == after_rollback
        print("Supervisor death left no running worker")

        config = json.loads((root / "providers.json").read_text())
        config["selections"]["inference"] = "b"
        (root / "pressure.json").write_text(json.dumps(config))
        permanent = [stable_args[0], "pressure.json", "b.lock", *stable_args[3:]]
        with launch([*command, *permanent], root, env) as (process, line):
            assert line() == ready
            expect_pressure(port)
            assert line() == ready
            after_retry = memory_events(group)["oom_kill"]
            assert after_retry > after_rollback
            expect_pressure(port)
            assert process.wait(timeout=10) == 128 + signal.SIGKILL and line() is None
        assert memory_events(group)["oom_kill"] > after_retry
        assert not (group / "cgroup.procs").read_text().strip()
        print("Stable model OOM stopped after one retry")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--worker-cgroup", type=Path, required=True)
    args = parser.parse_args()
    check(args.install, args.worker_cgroup)
