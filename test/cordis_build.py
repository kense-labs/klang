"""The same locked Cordis provider runs through k check and k build."""
from copy import deepcopy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def check(exe, provider_a, provider_b, incompatible, failed):
    exe = Path(exe).resolve()
    with tempfile.TemporaryDirectory(prefix="k-cordis-build-") as tmp:
        project = Path(tmp)
        manifest, lock = project / "providers.json", project / "cordis.lock"
        home, log = project / "home", project / "provider log"
        source = project / "app.k"
        source.write_text('fn main() { print("locked build"); }\n', encoding="utf-8")
        binary = project / ("app.exe" if os.name == "nt" else "app")
        tracked, unselected = project / "rules.txt", project / "unused.txt"
        tracked.write_bytes(b"rules 1")
        unselected.write_bytes(b"unused 1")
        env = dict(os.environ, K_HOME=str(home), K_PROGRESS="plain", K_CORDIS_TEST_LOG=str(log))
        env.pop("K_CORDIS_TEST_REJECT", None)
        env.pop("K_CORDIS_TEST_CHANGE_INPUT", None)
        cc = []
        if sys.platform == "darwin":
            env["PATH"] = "/usr/bin:/bin"
            cc = ["--cc", "/usr/bin/clang"]
        providers = {}
        for name, library in (("a", provider_a), ("b", provider_b),
                              ("incompatible", incompatible), ("failed", failed)):
            dest = project / Path(library).name
            shutil.copyfile(library, dest)
            providers[name] = {"service": "diagnostics", "version": 4, "library": dest.name}
        providers["a"]["inputs"] = {"rules": tracked.name}
        providers["b"]["inputs"] = {"unused": unselected.name}
        config = {"version": 2, "providers": providers, "selections": {"diagnostics": "a"}}

        def invoke(*args, extra_env=None):
            return subprocess.run([exe, *args], cwd=project, env=dict(env, **(extra_env or {})),
                                  capture_output=True, text=True, timeout=60)

        def snapshot(settings):
            manifest.write_text(json.dumps(settings), encoding="utf-8")
            resolved = invoke("lsp", "--cordis-manifest", manifest, "--cordis-graph")
            assert resolved.returncode == 0, resolved
            lock.write_bytes(resolved.stdout.encode("utf-8"))
            return json.loads(resolved.stdout)

        def command(verb, file=source, extra_env=None):
            target = ["-o", binary, *cc] if verb == "build" else []
            process = invoke(verb, "--offline", "--diagnostics", "json", "--cordis-manifest",
                             manifest, "--cordis-lock", lock, *target, file, extra_env=extra_env)
            return process, json.loads(process.stdout)

        def graph_error(verb, code, chain):
            before = log.read_bytes() if log.exists() else b""
            process, result = command(verb)
            assert process.returncode == 1 and len(result) == 1, (process, result)
            data = result[0]["cordis"]
            assert data["code"] == code and data["chain"] == chain, data
            after = log.read_bytes() if log.exists() else b""
            assert after == before, (before, after)
            return data

        def stored_graph(executable, document):
            encoded = json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode()
            assert encoded + b"\0" in executable.read_bytes(), document

        def warnings(result):
            assert [(d["severity"], d.get("source"), d["message"]) for d in result] == [
                ("warning", "cordis/a", "diagnostics from a")
            ], result

        original = snapshot(config)
        assert not log.exists(), "resolving the lock activated a provider"
        process, result = command("check")
        assert process.returncode == 0, process
        warnings(result)
        invalid = project / "invalid.k"
        invalid.write_text('fn main() { print(undeclared_value); }\n', encoding="utf-8")
        process, result = command("check", invalid)
        assert process.returncode == 1, process
        assert any(d["severity"] == "error" and "undeclared_value" in d["message"] for d in result), result
        assert any(d.get("source") == "cordis/a" and d["message"] == "observed compiler diagnostic"
                   for d in result), result
        assert log.read_text().splitlines() == ["start a", "stop a", "start a", "stop a"]

        process, result = command("build")
        assert process.returncode == 0, process
        warnings(result)
        stored_graph(binary, original)
        assert subprocess.check_output([binary], text=True, timeout=20) == "locked build\n"
        generated, = home.glob("build/prog/*/app.c")
        runtimes = list(home.glob("build/runtime/*/*/k_runtime.o"))
        runtimes += list(home.glob("build/runtime/*/*/k_runtime.obj"))
        runtime, = runtimes
        generated_stamp = generated.stat().st_mtime_ns
        runtime_stamp = runtime.stat().st_mtime_ns
        output_stamp = binary.stat().st_mtime_ns
        apple_stamp = generated.with_name("exe.inputs")
        if sys.platform == "darwin":
            sdk = subprocess.run(["/usr/bin/xcrun", "--show-sdk-path"], capture_output=True, text=True)
            overlays = [Path("/usr/local/include"), Path("/usr/local/lib")]
            if sdk.returncode == 0:
                overlays += [Path(sdk.stdout.strip()) / "usr/local" / name for name in ("include", "lib")]
            cache_disabling = ("CPATH", "C_INCLUDE_PATH", "LIBRARY_PATH", "SDKROOT", "DEVELOPER_DIR",
                               "TOOLCHAINS", "CCC_OVERRIDE_OPTIONS", "CLANG_CONFIG_FILE_SYSTEM_DIR",
                               "CLANG_CONFIG_FILE_USER_DIR", "COMPILER_PATH", "LD_LIBRARY_PATH",
                               "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "DYLD_INSERT_LIBRARIES",
                               "LD_FORCE_CLASSIC", "LD_NO_CLASSIC_LINKER")
            if sdk.returncode == 0 and not any(p.exists() for p in overlays) and not any(
                name in env for name in cache_disabling
            ):
                assert apple_stamp.is_file(), "locked build did not write the supported executable cache"
        has_link_cache = apple_stamp.is_file()

        unselected.write_bytes(b"unused 2")
        process, result = command("build")
        assert process.returncode == 0, process
        warnings(result)
        assert generated.stat().st_mtime_ns == generated_stamp
        assert runtime.stat().st_mtime_ns == runtime_stamp
        if has_link_cache:
            assert "up to date" in process.stderr and binary.stat().st_mtime_ns == output_stamp, process
            assert original["identity"].encode() in apple_stamp.read_bytes()

        # A provider runs on a cache hit too, and an error leaves published files intact.
        old_output = binary.read_bytes()
        process, result = command("build", extra_env={"K_CORDIS_TEST_REJECT": "1"})
        assert process.returncode == 1 and result[0]["severity"] == "error", (process, result)
        assert binary.read_bytes() == old_output and generated.stat().st_mtime_ns == generated_stamp

        process, result = command("build", extra_env={"K_CORDIS_TEST_CHANGE_INPUT": str(tracked)})
        assert process.returncode == 1 and result[0]["cordis"]["code"] == "changed_input", (process, result)
        assert result[0]["cordis"]["chain"] == ["diagnostics=a"], result
        assert binary.read_bytes() == old_output and generated.stat().st_mtime_ns == generated_stamp

        tracked.write_bytes(b"rules 2")
        for verb in ("check", "build"):
            error = graph_error(verb, "stale_lock", ["diagnostics=a"])
            assert "tracked input rules" in error["message"], error
        assert binary.read_bytes() == old_output and generated.stat().st_mtime_ns == generated_stamp
        assert runtime.stat().st_mtime_ns == runtime_stamp
        updated = snapshot(config)
        assert updated["identity"] != original["identity"], updated
        process, result = command("build")
        assert process.returncode == 0 and "up to date" not in process.stderr, process
        warnings(result)
        stored_graph(binary, updated)
        assert binary.read_bytes() != old_output and runtime.stat().st_mtime_ns == runtime_stamp
        if has_link_cache:
            assert updated["identity"].encode() in apple_stamp.read_bytes()
            process, result = command("build")
            assert process.returncode == 0 and "up to date" in process.stderr, process

        pruned = invoke("cache", "prune", "--budget", "0", "--json")
        assert pruned.returncode == 0 and json.loads(pruned.stdout)["budget_met"], pruned
        assert not runtime.exists()
        process, result = command("build")
        assert process.returncode == 0 and "up to date" not in process.stderr, process
        warnings(result)
        stored_graph(binary, updated)
        assert runtime.is_file()
        assert subprocess.check_output([binary], text=True, timeout=20) == "locked build\n"

        lock.write_text(json.dumps(updated, indent=2), encoding="utf-8")
        process, result = command("check")
        assert process.returncode == 0, process
        damaged = deepcopy(updated)
        damaged["graph"]["nodes"][0]["inputs"]["rules"] = "wrong digest"
        lock.write_text(json.dumps(damaged), encoding="utf-8")
        graph_error("check", "invalid_lock", [])
        lock.unlink()
        graph_error("check", "unreadable_lock", [])

        settings = deepcopy(config)
        settings["selections"]["diagnostics"] = "incompatible"
        snapshot(settings)
        graph_error("check", "incompatible_abi", ["diagnostics=incompatible"])
        graph_error("build", "incompatible_abi", ["diagnostics=incompatible"])
        settings["selections"]["diagnostics"] = "failed"
        snapshot(settings)
        before = log.read_text()
        process, result = command("check")
        assert process.returncode == 1 and result[0]["cordis"]["code"] == "activation_failed", (process, result)
        assert log.read_text()[len(before):].splitlines() == ["start failed", "stop failed"]
        assert subprocess.check_output([binary], text=True, timeout=20) == "locked build\n"


if __name__ == "__main__":
    check(*sys.argv[1:])
