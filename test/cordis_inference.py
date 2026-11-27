"""Run locked ONNX inference through both implementations of K execution."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


def varint(value):
    out = bytearray()
    while value >= 128:
        out.append((value & 127) | 128)
        value >>= 7
    out.append(value)
    return bytes(out)


def integer(field, value):
    return varint(field << 3) + varint(value)


def blob(field, value):
    if isinstance(value, str):
        value = value.encode("utf-8")
    return varint((field << 3) | 2) + varint(len(value)) + value


def tensor_value(name, dimensions):
    shape = b"".join(blob(1, integer(1, d)) for d in dimensions)
    return blob(1, name) + blob(2, blob(1, integer(1, 1) + blob(2, shape)))


def node(operation, inputs, output):
    return b"".join(blob(1, arg) for arg in inputs) + blob(2, output) + blob(4, operation)


def model(graph):
    return integer(1, 8) + blob(2, "K test") + blob(7, graph) + blob(8, integer(2, 13))


def tensor(name, dimensions, data):
    return (b"".join(integer(1, d) for d in dimensions) +
            integer(2, 1) + blob(8, name) + blob(9, struct.pack(f"<{len(data)}f", *data)))


def classifier(bias):
    """A self-contained opset-13 graph with a frozen dense layer and ReLU."""
    graph = (
        blob(1, node("MatMul", ["input", "weights"], "product")) +
        blob(1, node("Add", ["product", "bias"], "sum")) +
        blob(1, node("Relu", ["sum"], "output")) +
        blob(2, "locked_classifier") +
        blob(5, tensor("weights", [3, 2], [1, -1, 2, 1, -1, 2])) +
        blob(5, tensor("bias", [2], bias)) +
        blob(11, tensor_value("input", [1, 3])) +
        blob(12, tensor_value("output", [1, 2]))
    )
    return model(graph)


def workspace_classifier(width, bias):
    """Scale the intermediate tensor while keeping the two output values."""
    if width == 2:
        return classifier(bias)
    copies = width // 2
    scale = 1.0 / copies
    return model(
        blob(1, node("MatMul", ["input", "weights"], "product")) +
        blob(1, node("Add", ["product", "bias"], "sum")) +
        blob(1, node("Relu", ["sum"], "hidden")) +
        blob(1, node("MatMul", ["hidden", "projection"], "output")) +
        blob(2, "workspace_classifier") +
        blob(5, tensor("weights", [3, width], [1, -1] * copies + [2, 1] * copies + [-1, 2] * copies)) +
        blob(5, tensor("bias", [width], bias * copies)) +
        blob(5, tensor("projection", [width, 2], [scale, 0, 0, scale] * copies)) +
        blob(11, tensor_value("input", [1, 3])) +
        blob(12, tensor_value("output", [1, 2]))
    )


def identity_tensor(elements):
    return model(
        blob(1, node("Identity", ["input"], "output")) +
        blob(2, "wide_tensor") +
        blob(11, tensor_value("input", [1, elements])) +
        blob(12, tensor_value("output", [1, elements]))
    )


def check(exe, host, provider, runtime, tcp, peer):
    exe, host, provider, runtime, tcp, peer = (
        Path(p).resolve() for p in (exe, host, provider, runtime, tcp, peer)
    )
    with tempfile.TemporaryDirectory(prefix="cordis inference ") as work:
        root = Path(work)
        lib = root / "lib"
        lib.mkdir()
        suffix = ".dll" if os.name == "nt" else ".dylib" if sys.platform == "darwin" else ".so"
        prefix = "" if os.name == "nt" else "lib"
        shutil.copyfile(host, lib / f"{prefix}k_cordis_application{suffix}")
        shutil.copyfile(peer, lib / f"{prefix}tcp_fixture{suffix}")
        source = root / "inference.k"
        shutil.copyfile(Path(__file__).with_name("cordis") / "fixtures" / source.name, source)
        binary = source.with_suffix(".exe" if os.name == "nt" else "")
        env = dict(os.environ, K_HOME=str(root / "home"))

        def invoke(*args):
            process = subprocess.run(args, cwd=root, env=env, capture_output=True, text=True, timeout=60)
            assert process.returncode == 0, (args, process.stdout, process.stderr)
            return process.stdout

        cfg = {"version": 2, "selections": {"inference": "a"}, "providers": {}}
        models = {"a": classifier([0, 1]), "b": classifier([1, -1]), "invalid": b"not an ONNX model"}
        assert hashlib.sha256(models["a"]).hexdigest() == "b6ef90de4908886b4e13794e9c74b7c8a53b06e9d577a2a714d4f0de9e35f322"
        assert hashlib.sha256(models["b"]).hexdigest() == "41d3e0e8f9d42f94bdcb2bade2ef1464d5db402a36f0083030ff0576f0ed4c96"
        for name, model in models.items():
            artifact = f"{name}{suffix}"
            shutil.copyfile(provider, root / artifact)
            (root / f"{name}.onnx").write_bytes(model)
            cfg["providers"][name] = {"service": "inference", "version": 1, "library": artifact,
                                      "inputs": {"model": f"{name}.onnx", "runtime": str(runtime)}}
        manifest = root / "providers.json"
        manifest.write_text(json.dumps(cfg), encoding="utf-8")
        engine = hashlib.sha256(runtime.read_bytes()).hexdigest()
        for name, model in models.items():
            lock = invoke(exe, "cordis", "resolve", "inference", "--manifest", manifest, "--provider", name)
            node, = json.loads(lock)["graph"]["nodes"]
            assert node["inputs"] == {"model": hashlib.sha256(model).hexdigest(), "runtime": engine}, node
            (root / f"{name}.lock").write_text(lock, encoding="utf-8")

        invoke(exe, "build", "--offline", "-o", binary, source)
        for command in ((exe, "run", "--offline", source), (binary,)):
            wrong_shape, busy, model, *steps = invoke(*command).splitlines()
            assert json.loads(wrong_shape)["code"] == "inference_input", wrong_shape
            assert json.loads(busy)["code"] == "inference_busy", busy
            failure = json.loads(model)
            assert failure["code"] == "inference_model_failed" and failure["chain"] == ["inference=invalid"], model
            assert steps == ["locked weights and hot replacement verified",
                             "cancelled work returned its request slot",
                             "reused slot after draining"], steps
        cfg["providers"]["tcp"] = {"service": "tcp.stream", "version": 1,
                                    "library": str(tcp), "requires": {"inference": {}}}
        manifest.write_text(json.dumps(cfg), encoding="utf-8")
        lock = invoke(exe, "cordis", "resolve", "tcp.stream", "--manifest", manifest)
        assert json.loads(lock)["graph"]["edges"] == [
            {"from": "tcp.stream", "to": "inference", "minVersion": 1, "maxVersion": 2**32 - 1}
        ]
        (root / "transport.lock").write_text(lock, encoding="utf-8")
        transport = root / "inference_transport.k"
        shutil.copyfile(Path(__file__).with_name("cordis") / "fixtures" / transport.name, transport)
        invoke(exe, "build", "--offline", "-o", binary, transport)
        for command in ((exe, "run", "--offline", transport), (binary,)):
            assert invoke(*command).splitlines() == ["loopback payload inferred through a locked dependency"]
    print("Locked ONNX Runtime inference passed in TinyCC and native K")


if __name__ == "__main__":
    check(*sys.argv[1:])
