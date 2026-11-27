"""A K application owns its provider selection and real TCP requests."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def check(exe, host, peer, provider_a, provider_b, policy_a, policy_b, bad):
    exe = Path(exe).resolve()
    with tempfile.TemporaryDirectory(prefix="cordis application ") as temp:
        root = Path(temp)
        lib = root / "lib"
        lib.mkdir()
        ext = ".dll" if os.name == "nt" else ".dylib" if sys.platform == "darwin" else ".so"
        prefix = "" if os.name == "nt" else "lib"
        shutil.copyfile(host, lib / f"{prefix}k_cordis_application{ext}")
        shutil.copyfile(peer, lib / f"{prefix}tcp_fixture{ext}")
        source = root / "application.k"
        shutil.copyfile(Path(__file__).with_name("cordis") / "fixtures" / source.name, source)
        binary = source.with_suffix(".exe" if os.name == "nt" else "")
        log = root / "events.log"
        env = dict(os.environ, K_HOME=str(root / "home"), K_TEST_CORDIS_APPLICATION_LOG=str(log),
                   K_TEST_CORDIS_MUTATION_MARKER=str(root / "mutate.flag"),
                   K_TEST_CORDIS_MUTATION_INPUT=str(root / "b.rules"))

        def invoke(*args, status=0):
            run = subprocess.run(args, cwd=root, env=env, capture_output=True, text=True, timeout=45)
            assert run.returncode == status, (args, run.stdout, run.stderr)
            return run.stdout

        manifest = {"version": 2, "selections": {"tcp.stream": "a"}, "providers": {}}
        for name, artifact, version, policy in (("a", provider_a, 1, 1), ("b", provider_b, 2, 2),
                                                ("bad", bad, 3, 2)):
            library = f"{name}{ext}"
            shutil.copyfile(artifact, root / library)
            manifest["providers"][name] = {
                "service": "tcp.stream", "version": version, "library": library,
                "requires": {"tcp.policy": {"minVersion": policy, "maxVersion": policy}},
            }
        for name, artifact, version in (("policy-a", policy_a, 1), ("policy-b", policy_b, 2)):
            library = f"{name}{ext}"
            shutil.copyfile(artifact, root / library)
            manifest["providers"][name] = {"service": "tcp.policy", "version": version, "library": library}
        manifest["providers"]["b"]["inputs"] = {"rules": "b.rules"}
        path = root / "application manifest.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        (root / "b.rules").write_bytes(b"original")
        expected = {}
        for name in ("a", "b", "bad"):
            selection = [] if name == "a" else ["--provider", name]
            lock = invoke(exe, "cordis", "resolve", "tcp.stream", "--manifest", path, *selection)
            (root / f"{name}.lock").write_text(lock, encoding="utf-8")
            expected[name] = json.loads(lock)
        assert not log.exists(), "resolving a lock started a Cordis provider"
        error = invoke(exe, "cordis", "resolve", "tcp.stream", "--manifest", path,
                       "--provider", "unknown", status=1)
        assert json.loads(error)["error"]["code"] == "missing_provider"

        invoke(exe, "build", "--offline", "-o", binary, source)
        explicit = None
        commands = (("explicit", (exe, "run", "--offline", source)),
                    ("explicit", (binary,)),
                    ("decorated", (exe, "run", "--offline", source, "--", "decorated")),
                    ("decorated", (binary, "decorated")))
        for expected_mode, command in commands:
            if log.exists():
                log.unlink()
            mode, output = invoke(*command).split("\n", 1)
            assert mode == expected_mode, (command, mode)
            if explicit is None:
                explicit = output
            else:
                assert output == explicit, (command, output)
            stale_start, first, invalid_capacity, denied, stale_input, bad_abi, changed, next_, *final = (
                output.splitlines()
            )
            assert json.loads(stale_start)["code"] == "stale_lock", stale_start
            assert json.loads(first) == expected["a"], first
            assert json.loads(invalid_capacity)["code"] == "stream_open_failed", invalid_capacity
            policy = json.loads(denied)
            assert policy["code"] == "stream_capacity_denied", policy
            assert policy["chain"] == ["tcp.stream=a", "tcp.policy=policy-a"], policy
            error = json.loads(stale_input)
            assert error["code"] == "stale_lock" and error["chain"] == ["tcp.stream=b"], error
            error = json.loads(bad_abi)
            assert error["code"] == "incompatible_abi" and error["chain"] == ["tcp.stream=bad"], error
            error = json.loads(changed)
            assert error["code"] == "changed_input" and error["chain"] == ["tcp.stream=b"], error
            assert json.loads(next_) == expected["b"], next_
            assert final == ["new request used b while a was pending",
                             "old request drained after bounded reads",
                             "retired a and its policy after its reader and pump released"], final
            assert log.read_text().splitlines() == [
                "start policy a", "start a", "start policy b", "stop policy b",
                "start policy b", "start b", "stop b", "stop policy b",
                "start policy b", "start b", "stop a", "stop policy a", "stop b", "stop policy b",
            ]


if __name__ == "__main__":
    check(*sys.argv[1:])
