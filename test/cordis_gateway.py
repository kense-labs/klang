"""Exercise the reference K inference gateway over TCP with two K backends."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
from queue import Empty, Queue
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
from threading import Thread
import time

from cordis_inference import classifier, identity_tensor


REQUEST = struct.pack("<4sI3f", b"KIG1", 3, 1, 2, 3)
WIDE_ELEMENTS = 2049
DATA, DONE, BAD_REQUEST, DEADLINE, BUSY = range(1, 6)


def exact(sock, size):
    payload = bytearray()
    while len(payload) < size:
        part = sock.recv(size - len(payload))
        assert part, (size, payload)
        payload.extend(part)
    return payload


def reply(sock):
    chunks = []
    while True:
        tag, size = struct.unpack("<BI", exact(sock, 5))
        if tag != DATA:
            assert not size
            return tag, chunks
        assert size == 4, size  # The test requests one float per frame.
        chunks.append(struct.unpack("<f", exact(sock, size))[0])


def connect(port, timeout=8):
    return socket.create_connection(("127.0.0.1", port), timeout=timeout)


def request(port, payload=REQUEST, *, timeout=8):
    with connect(port, timeout) as peer:
        peer.sendall(payload)
        return reply(peer)


def workspace_snapshot(read_line, phase):
    line = read_line()
    prefix = f"workspace {phase} "
    assert line.startswith(prefix), (phase, line)
    return json.loads(line[len(prefix):])


@contextmanager
def running(command, root, env, *args):
    lines = Queue()
    with tempfile.TemporaryFile("w+", encoding="utf-8") as diagnostics:
        server = subprocess.Popen([*command, *args], cwd=root, env=env, stdout=subprocess.PIPE,
                                  stderr=diagnostics, text=True, encoding="utf-8",
                                  start_new_session=os.name != "nt")

        def read_lines():
            for line in server.stdout:
                lines.put(line.rstrip())
            lines.put(None)

        def read_line():
            try:
                line = lines.get(timeout=30)
                assert line is not None, server.poll()
                return line
            except Empty:
                raise AssertionError("gateway did not report readiness or selection") from None

        reader = Thread(target=read_lines, daemon=True)
        reader.start()
        try:
            ready = read_line()
            assert ready.startswith("ready "), ready
            yield int(ready.split()[1]), read_line
            summary = read_line()
            exit_code = server.wait(timeout=15)
            assert summary.startswith("peak pending ") and exit_code == 0, (summary, exit_code)
        except BaseException:
            diagnostics.seek(0)
            print(diagnostics.read(), file=sys.stderr, end="")
            raise
        finally:
            if server.poll() is None:
                if os.name == "nt":
                    server.kill()
                else:
                    try:
                        os.killpg(server.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                server.wait()
            reader.join()
            server.stdout.close()


def wait_event(root, event):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        path = root / "task.events"
        if path.exists():
            events = path.read_text(encoding="utf-8").splitlines()
            if event in events:
                return events
        time.sleep(0.01)
    raise AssertionError(f"missing {event!r} from task events")


def setup(exe, host, provider, runtime, root, env, slow=None, source_path=None, native_binary=None, models=None):
    extension = ".dll" if os.name == "nt" else ".dylib" if sys.platform == "darwin" else ".so"
    prefix = "" if os.name == "nt" else "lib"
    lib = root / "lib"
    lib.mkdir()
    shutil.copyfile(host, lib / f"{prefix}k_cordis_application{extension}")
    source = root / "gateway.k"
    if source_path is None:
        source_path = Path(__file__).resolve().parents[1] / "examples" / "inference_gateway.k"
    shutil.copyfile(source_path, source)
    def invoke(*args):
        process = subprocess.run([exe, *args], cwd=root, env=env, capture_output=True, text=True, timeout=90)
        assert process.returncode == 0, (args, process.stdout, process.stderr)
        return process.stdout

    config = {"version": 2, "selections": {"inference": "a"}, "providers": {}}
    if models is None:
        models = {"a": classifier([0, 1]), "b": classifier([1, -1])}
    for name, content in models.items():
        module = root / f"provider_{name}{provider.suffix}"
        shutil.copyfile(provider, module)
        model = root / f"{name}.onnx"
        model.write_bytes(content)
        config["providers"][name] = {"service": "inference", "version": 1, "library": module.name,
                                      "inputs": {"model": model.name, "runtime": str(runtime)}}
    if slow is not None:
        wide = root / f"provider_wide{provider.suffix}"
        shutil.copyfile(provider, wide)
        wide_model = root / "wide.onnx"
        wide_model.write_bytes(identity_tensor(WIDE_ELEMENTS))
        config["providers"]["wide"] = {"service": "inference", "version": 1, "library": wide.name,
                                        "inputs": {"model": wide_model.name, "runtime": str(runtime)}}
        fixture = root / f"slow{slow.suffix}"
        shutil.copyfile(slow, fixture)
        fixture_input = root / "fixture_input"
        fixture_input.write_bytes(b"gateway control fixture")
        config["providers"]["slow"] = {"service": "inference", "version": 1, "library": fixture.name,
                                        "inputs": {"model": fixture_input.name, "runtime": fixture_input.name}}
    manifest = root / "providers.json"
    manifest.write_text(json.dumps(config), encoding="utf-8")
    if slow is not None:
        (root / "slow-providers.json").write_text(
            json.dumps({**config, "selections": {"inference": "slow"}}), encoding="utf-8")
        (root / "wide-providers.json").write_text(
            json.dumps({**config, "selections": {"inference": "wide"}}), encoding="utf-8")
    for name in config["providers"]:
        lock = invoke("cordis", "resolve", "inference", "--manifest", manifest, "--provider", name)
        (root / f"{name}.lock").write_text(lock, encoding="utf-8")
    if native_binary is None:
        native_binary = source.with_suffix(".exe" if os.name == "nt" else "")
        invoke("build", "--offline", "-o", native_binary, source)
    return [exe, "run", "--offline", source, "--"], [native_binary]


def cpu_requests(command, root, env, extra=()):
    with running(command, root, env, "providers.json", "a.lock", "0", "1", "1000", "16", "1", "6", *extra) as (port, _):
        with connect(port) as peer:
            for part in (REQUEST[:3], REQUEST[3:12], REQUEST[12:]):
                peer.sendall(part)
            assert reply(peer) == (DONE, [2.0, 8.0])
        assert request(port, struct.pack("<4sI", b"KIG1", 17)) == (BAD_REQUEST, [])
        assert request(port, struct.pack("<4sI", b"KIG1", 2)) == (BAD_REQUEST, [])
        with connect(port) as active:
            active.sendall(REQUEST[:8])
            time.sleep(0.25)
            with connect(port) as waiting:
                waiting.sendall(REQUEST)
                time.sleep(0.06)
                with connect(port) as excess:
                    assert reply(excess) == (BUSY, [])
                assert reply(active) == (DEADLINE, [])
                assert reply(waiting) == (DONE, [2.0, 8.0])
        assert request(port) == (DONE, [2.0, 8.0])


def wide_request(command, root, env):
    values = list(range(-1024, 1025))
    assert len(values) == WIDE_ELEMENTS
    with running(command, root, env, "wide-providers.json", "wide.lock", "0", "1", "2000",
                 str(WIDE_ELEMENTS), "1024", "1") as (port, _):
        with connect(port) as peer:
            peer.sendall(struct.pack(f"<4sI{WIDE_ELEMENTS}f", b"KIG1", WIDE_ELEMENTS, *values))
            output = []
            for count in (1024, 1024, 1):
                assert struct.unpack("<BI", exact(peer, 5)) == (DATA, count * 4)
                output.extend(struct.unpack(f"<{count}f", exact(peer, count * 4)))
            assert reply(peer) == (DONE, [])
            assert output == values


def clear_gates(root):
    for name in ("task.events", "reload", "release-task", "release-drain"):
        (root / name).unlink(missing_ok=True)


def replace(command, root, env, disconnect):
    clear_gates(root)
    with running(command, root, env, "slow-providers.json", "slow.lock", "0", "2", "20000", "16", "1", "2",
                 "reload", "b", "b.lock", "--workspace") as (port, line):
        initial = workspace_snapshot(line, "startup")
        assert not initial["active_measured"] and not initial["last_activation_measured"], initial
        with connect(port) as active:
            active.sendall(REQUEST)
            assert wait_event(root, "started") == ["started"]
            (root / "reload").touch()
            assert line() == "selected b"
            overlap = workspace_snapshot(line, "reload")
            assert overlap["retired_count"] == 1, overlap
            assert not overlap["retired_measured"] and not overlap["last_activation_measured"], overlap
            assert (root / "task.events").read_text().splitlines() == ["started"]
            if disconnect:
                active.close()
                assert wait_event(root, "cancelled") == ["started", "cancelled"]
                with connect(port) as next_peer:
                    next_peer.sendall(REQUEST)
                    next_peer.settimeout(0.08)
                    try:
                        unexpected = next_peer.recv(1)
                    except TimeoutError:
                        pass
                    else:
                        raise AssertionError(f"gateway progressed before drain: {unexpected!r}")
                    assert (root / "task.events").read_text().splitlines() == ["started", "cancelled"]
                    (root / "release-drain").touch()
                    next_peer.settimeout(8)
                    assert reply(next_peer) == (DONE, [3.0, 6.0])
                assert wait_event(root, "stopped") == ["started", "cancelled", "drained", "stopped"]
            else:
                (root / "release-task").touch()
                assert reply(active) == (DONE, [1.0, 3.0])
                assert request(port) == (DONE, [3.0, 6.0])
                assert wait_event(root, "stopped") == ["started", "completed", "stopped"]
        drained = workspace_snapshot(line, "shutdown")
        assert drained["active_measured"] == overlap["active_measured"], drained
        assert drained["retired_count"] == 0 and drained["retired_reserved_bytes"] == 0, drained


def queued_deadline(command, root, env):
    clear_gates(root)
    with running(command, root, env, "slow-providers.json", "slow.lock", "0", "1", "800", "16", "1", "2") as (port, _):
        with connect(port) as active:
            active.sendall(REQUEST)
            wait_event(root, "started")
            with connect(port) as waiting:
                assert reply(waiting) == (DEADLINE, [])
            assert wait_event(root, "cancelled") == ["started", "cancelled"]
            with connect(port) as recovered:
                recovered.sendall(REQUEST)
                (root / "release-task").touch()
                (root / "release-drain").touch()
                assert reply(active) == (DEADLINE, [])
                assert reply(recovered) == (DONE, [1.0, 3.0])
    assert wait_event(root, "stopped") == ["started", "cancelled", "drained", "started", "completed", "stopped"]


def check(*paths):
    exe, host, provider, runtime, slow = (Path(path).resolve() for path in paths)
    with tempfile.TemporaryDirectory(prefix="cordis gateway ") as work:
        root = Path(work)
        env = dict(os.environ, K_HOME=str(root / "home"))
        for command in setup(exe, host, provider, runtime, root, env, slow):
            cpu_requests(command, root, env)
            wide_request(command, root, env)
            replace(command, root, env, disconnect=False)
            replace(command, root, env, disconnect=True)
            queued_deadline(command, root, env)
    print("Cordis TCP framing, deadlines, admission and provider handoff passed in TinyCC and native K")


def check_installed(path, memory_mib=None):
    install = Path(path).resolve()
    lib = install / "lib"
    budget = () if memory_mib is None else ("--memory-budget-mib", str(memory_mib))
    with tempfile.TemporaryDirectory(prefix="cloud gateway ") as work:
        root = Path(work)
        env = {**os.environ, "K_HOME": str(root / "home")}
        commands = setup(install / "k", lib / "libk_cordis_application.so",
                         lib / "libk_cordis_onnx.so", lib / "libonnxruntime.so", root, env,
                         source_path=install / "examples/inference_gateway.k",
                         native_binary=install / "k-inference-gateway")
        for command in commands:
            rejected = subprocess.run([*command, "missing.json", "missing.lock", "0", "1", "1000",
                                       "16", "1", "1", "--memory-budget-mib", "1"],
                                      cwd=root, env=env, capture_output=True, text=True, timeout=30)
            assert rejected.returncode != 0 and '"code":"memory_budget_' in rejected.stdout, rejected
            assert "ready " not in rejected.stdout, rejected.stdout
            cpu_requests(command, root, env, budget)
            if budget:
                marker = root / "reload"
                marker.unlink(missing_ok=True)
                with running(command, root, env, "providers.json", "a.lock", "0", "1", "5000", "16", "1", "2",
                             marker.name, "b", "b.lock", *budget) as (port, line):
                    assert request(port) == (DONE, [2.0, 8.0])
                    marker.touch()
                    assert request(port) == (DONE, [3.0, 6.0])
                    assert line() == "selected b"
        assert not (root / "home" / "pkg").exists()
    print("Installed gateway passed in TinyCC and as a prebuilt native executable")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--installed":
        import argparse
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--installed", required=True)
        parser.add_argument("--memory-budget-mib", type=int)
        args = parser.parse_args()
        check_installed(args.installed, args.memory_budget_mib)
    else:
        check(*sys.argv[1:])
