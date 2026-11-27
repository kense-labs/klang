"""Exercise Cordis graph resolution, diagnostics activation, and LSP inspection."""
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from lsp_protocol import Client, uri_of


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check(exe, provider_a, provider_b):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        manifest_dir = root / "trusted settings"
        manifest_dir.mkdir()
        manifest = manifest_dir / "cordis.json"
        providers = {}
        for name, provider in (("a", provider_a), ("b", provider_b)):
            dest = manifest_dir / Path(provider).name
            shutil.copyfile(provider, dest)
            providers[name] = dest.name
        legacy = manifest
        if os.name != "nt":
            legacy = root / "legacy.json"
            manifest.symlink_to(legacy)
        legacy.write_text(json.dumps({"version": 1, "diagnostics": "a", "providers": providers}),
                          encoding="utf-8")
        program = root / "invalid.k"
        text = "fn main() { print(missing_value); }\n"
        program.write_text(text, encoding="utf-8")
        uri = uri_of(program)
        cli = subprocess.run([exe, "check", "--diagnostics", "json", program], cwd=root,
                             capture_output=True, text=True, timeout=30)
        cli_errors = json.loads(cli.stdout)
        assert cli.returncode and cli_errors, (cli.stdout, cli.stderr)
        messages = [d["message"] for d in cli_errors]
        log = root / "provider events.txt"
        client = Client(exe, root, args=("--cordis-manifest", manifest),
                        env=dict(os.environ, K_CORDIS_TEST_LOG=str(log)))
        try:
            result = client.request("initialize", {"capabilities": {}})
            assert result["capabilities"]["experimental"]["klangCordisDiagnostics"] == 1
            assert result["capabilities"]["experimental"]["klangCordisGraph"] == 1
            old_graph = client.request("klang/cordis/graph", None)
            assert old_graph["graph"]["nodes"][0]["provider"] == "a", old_graph
            client.send("textDocument/didOpen", {
                "textDocument": {"uri": uri, "version": 1, "languageId": "klang", "text": text}
            }, request=False)
            original = client.diagnostics_for(uri)
            assert [d["message"] for d in original if d["source"] == "klang"] == messages
            assert any(d["source"] == "cordis/a" for d in original), original

            method = "klang/cordis/selectDiagnostics"
            forbidden = client.request(method, {"provider": str(manifest_dir / providers["b"])}, raw=True)
            assert forbidden.get("error", {}).get("code") == -32000, forbidden
            assert forbidden["error"]["data"]["code"] == "missing_provider", forbidden
            assert client.request("klang/cordis/graph", None) == old_graph
            assert client.request(method, {"provider": "b"}) == {"provider": "b"}
            new_graph = client.request("klang/cordis/graph", None)
            assert new_graph["graph"]["nodes"][0]["provider"] == "b", new_graph
            assert new_graph["identity"] != old_graph["identity"]
            swapped = client.diagnostics_for(uri, lambda ds: any(d["source"] == "cordis/b" for d in ds))
            assert [d["message"] for d in swapped if d["source"] == "klang"] == messages
            assert not any(d["source"] == "cordis/a" for d in swapped), swapped

            client.request("shutdown", None)
            client.send("exit", request=False)
            assert client.p.wait(timeout=30) == 0, client.p.stderr.read()
            assert log.read_text(encoding="utf-8").splitlines() == [
                "start a", "start b", "stop a", "stop b"
            ]
        finally:
            if client.p.poll() is None:
                client.p.kill()
            client.p.communicate(timeout=30)


def check_graph(exe, provider_a, provider_b):
    exe = Path(exe).resolve()
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        manifest = root / "cordis.json"
        log = root / "provider events.txt"
        env = dict(os.environ, K_CORDIS_TEST_LOG=str(log))
        artifacts = {}
        for name, source in (("lint", provider_a), ("index", provider_b), ("other", provider_b)):
            dest = root / (name + Path(source).suffix)
            shutil.copyfile(source, dest)
            artifacts[name] = dest
        tracked = {}
        for size in (0, 55, 56, 63, 64, 65, 32768, 32769):
            data = bytes(i % 251 for i in range(size))
            name = f"input-{size}"
            (root / name).write_bytes(data)
            tracked[name] = digest(data)
        config = {
            "version": 2,
            "providers": {
                "lint": {"service": "diagnostics", "version": 7, "library": artifacts["lint"].name,
                         "capabilities": ["workspace.read"], "requires": {"symbols": {"minVersion": 2, "maxVersion": 3}},
                         "inputs": {name: name for name in tracked}},
                "index": {"service": "symbols", "version": 2, "library": artifacts["index"].name},
                "other": {"service": "symbols", "version": 3, "library": artifacts["other"].name},
            },
            "selections": {"diagnostics": "lint", "symbols": "index"},
            "grants": ["workspace.write", "workspace.read"],
        }

        def write(settings, **kwargs):
            manifest.write_text(json.dumps(settings, **kwargs), encoding="utf-8")

        def run(*flags):
            return subprocess.run([exe, "lsp", "--cordis-manifest", manifest, *flags], env=env,
                                  cwd=root, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)

        def resolve(settings, **kwargs):
            write(settings, **kwargs)
            return inspect_graph()

        def inspect_graph():
            process = run("--cordis-graph")
            result = json.loads(process.stdout)
            assert process.returncode == int("error" in result), (process, result)
            assert not log.exists(), "resolution activated a native provider"
            return result

        original = resolve(config)
        graph = original["graph"]
        encoded = json.dumps(graph, ensure_ascii=False, separators=(",", ":")).encode()
        assert original["identity"] == "sha256:" + digest(encoded), original
        assert graph["edges"] == [{"from": "diagnostics", "to": "symbols", "minVersion": 2, "maxVersion": 3}], graph
        assert graph["grants"] == ["workspace.read", "workspace.write"], graph
        assert [n["provider"] for n in graph["nodes"]] == ["lint", "index"], graph
        assert graph["nodes"][0]["inputs"] == tracked, graph
        assert graph["nodes"][0]["sha256"] == digest(artifacts["lint"].read_bytes()), graph
        assert graph["nodes"][1]["sha256"] == digest(artifacts["index"].read_bytes()), graph
        equivalent = deepcopy(config)
        equivalent["grants"].reverse()
        equivalent["providers"]["lint"]["library"] = "./" + artifacts["lint"].name
        del equivalent["providers"]["other"]
        assert resolve(equivalent, sort_keys=True, indent=4) == original
        (root / "input-0").write_bytes(b"changed tracked input")
        changed = resolve(config)
        assert original["identity"] != changed["identity"] and graph["nodes"][1] == changed["graph"]["nodes"][1]
        (root / "input-0").write_bytes(b"")
        # Resolution fingerprints the artifact without asking the native loader to open it.
        artifacts["index"].write_bytes(b"this is not a native library")
        changed = resolve(config)
        assert original["identity"] != changed["identity"] and graph["nodes"][0] == changed["graph"]["nodes"][0]
        shutil.copyfile(provider_b, artifacts["index"])
        assert resolve(config) == original

        def error(settings, code, chain):
            failure = resolve(settings)["error"]
            assert failure["code"] == code and failure["chain"] == chain, failure
            return failure

        diagnostics = "diagnostics=lint"
        symbols = "symbols=index"
        bad = deepcopy(config)
        del bad["providers"]["index"]
        error(bad, "missing_provider", [diagnostics, symbols])
        rejected = run()
        assert rejected.returncode == 1 and diagnostics in rejected.stderr and symbols in rejected.stderr, rejected
        bad = deepcopy(config)
        del bad["selections"]["symbols"]
        ambiguous = error(bad, "ambiguous_provider", [diagnostics, "symbols"])
        assert ambiguous["candidates"] == ["index", "other"], ambiguous
        bad["providers"]["lint"]["requires"]["symbols"]["maxVersion"] = 2
        assert resolve(bad)["graph"]["nodes"][1]["provider"] == "index"
        bad["selections"]["symbols"] = "other"
        error(bad, "incompatible_version", [diagnostics, "symbols=other"])
        bad = deepcopy(config)
        bad["grants"] = []
        error(bad, "missing_capability", [diagnostics])
        bad = deepcopy(config)
        bad["providers"]["index"]["requires"] = {"diagnostics": {"minVersion": 7}}
        error(bad, "dependency_cycle", [diagnostics, symbols, diagnostics])
        bad = deepcopy(config)
        bad["providers"]["index"]["inputs"] = {"absent": "./no-such-file"}
        error(bad, "unreadable_input", [diagnostics, symbols])
        if os.name != "nt":
            bad = deepcopy(config)
            alias = root / "index-alias"
            alias.symlink_to(artifacts["index"])
            bad["providers"]["other"]["library"] = alias.name
            error(bad, "invalid_manifest", [])
        manifest.write_text('{"version":2,"version":1,"providers":{}}', encoding="utf-8")
        assert inspect_graph()["error"]["code"] == "invalid_manifest"

        builtin = deepcopy(config)
        builtin["selections"]["diagnostics"] = "builtin"
        builtin["providers"]["other"]["service"] = "diagnostics"
        linked = resolve(builtin)["graph"]
        assert linked["edges"] == [] and len(linked["nodes"]) == 1, linked
        assert linked["nodes"][0]["kind"] == "linked" and linked["nodes"][0]["provider"] == "builtin", linked
        assert linked["nodes"][0]["sha256"] == digest(exe.read_bytes()), linked
        refused = subprocess.run([exe, "lsp", "--cordis-graph"], cwd=root, capture_output=True,
                                 text=True, timeout=30)
        assert refused.returncode == 1 and json.loads(refused.stdout)["error"]["code"] == "no_manifest", refused

        # A valid resolution can be inspected before a host has adapters for all its services.
        assert resolve(config) == original
        unsupported = run()
        assert unsupported.returncode == 1 and "can only activate the diagnostics service" in unsupported.stderr
        assert not log.exists()
        standby = resolve(builtin)
        client = Client(exe, root, args=("--cordis-manifest", manifest), env=env)
        try:
            client.request("initialize", {"capabilities": {}})
            assert client.request("klang/cordis/graph", None) == standby
            failed = client.request("klang/cordis/selectDiagnostics", {"provider": "lint"}, raw=True)
            assert failed["error"]["data"]["code"] == "unsupported_service", failed
            assert client.request("klang/cordis/graph", None) == standby
            assert not log.exists()
            assert client.request("klang/cordis/selectDiagnostics", {"provider": "other"}) == {"provider": "other"}
            selected = client.request("klang/cordis/graph", None)
            assert selected["identity"] != standby["identity"] and selected["graph"]["nodes"][0]["provider"] == "other"
            client.request("shutdown", None)
            client.send("exit", request=False)
            assert client.p.wait(timeout=30) == 0, client.p.stderr.read()
            assert log.read_text(encoding="utf-8").splitlines() == ["start b", "stop b"]
        finally:
            if client.p.poll() is None:
                client.p.kill()
            client.p.communicate(timeout=30)


if __name__ == "__main__":
    check(*sys.argv[1:])
    check_graph(*sys.argv[1:])
