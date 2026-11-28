"""Exercise the optional Cordis source host across its native ABI and the K CLI."""
from concurrent.futures import ThreadPoolExecutor
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


def check(driver, compiler, library, wrong_library):
    with TemporaryDirectory(prefix="k-cordis-source-") as temp:
        root = Path(temp).resolve()
        manifest, lock, log = (root / name for name in ("providers.json", "cordis.lock", "provider.log"))
        source, helper, rules = (root / name for name in ("program.ext", "helper.src", "rules"))
        source.write_text('show\ntwice(21)\n', encoding="utf-8", newline="\n")
        helper.write_text('fn twice(v: i64) -> i64 { v * 2 }\n', encoding="utf-8", newline="\n")
        rules.write_bytes(b"normal")
        transform, wrong = [Path(shutil.copyfile(file, root / Path(file).name))
                            for file in (library, wrong_library)]
        config = {"version": 2, "selections": {"source-transform": "fixture"}, "providers": {
            "fixture": {"service": "source-transform", "version": 1,
                        "library": transform.name, "inputs": {"rules": rules.name}},
            "wrong": {"service": "source-transform", "version": 1, "library": wrong.name},
        }}
        inherited_home = root / "user-cache"
        env = {**os.environ, "K_CORDIS_SOURCE_TEST_LOG": str(log)}
        env.pop("K_CORDIS_SOURCE_TEST_CHANGE", None)
        env.pop("K_HOME", None)

        def reselect(provider="fixture"):
            config["selections"]["source-transform"] = provider
            manifest.write_text(json.dumps(config), encoding="utf-8")
            data = subprocess.check_output([compiler, "cordis", "resolve", "source-transform",
                                            "--manifest", manifest], cwd=root, timeout=30)
            lock.write_bytes(data)
            return json.loads(data)

        def invoke(verb, *args, env_extra=None):
            run = subprocess.run([driver, verb, "--compiler", compiler, "--manifest", manifest, "--lock", lock,
                                  "--root", root, "--input", "helper.src", "program.ext", *args],
                                 cwd=root, env={**env, **(env_extra or {})}, capture_output=True, timeout=40)
            return run, json.loads(run.stdout)

        def events():
            return log.read_text().splitlines() if log.exists() else []

        def fail_graph(verb, code, *args, env_extra=None):
            run, result = invoke(verb, *args, env_extra=env_extra)
            assert run.returncode and result[0]["cordis"]["code"] == code, (run, result)
            return result[0]

        original_graph = reselect()
        assert not events()
        verify, diagnostics = invoke("check")
        assert verify.returncode == 0 and diagnostics == [], (verify, diagnostics)
        assert events() == ["start", "transform", "stop"], events()

        output = root / "release"
        build, diagnostics = invoke("build", "-o", output)
        assert build.returncode == 0 and diagnostics == [], (build, diagnostics)
        program = output / ("program.exe" if os.name == "nt" else "program")
        assert build.stderr.splitlines()[-1] == b"built " + os.fsencode(program), build
        assert subprocess.check_output([program], timeout=20).replace(b"\r\n", b"\n") == b"42\n"
        receipt = json.loads((output / "source-build.json").read_text())
        assert receipt["cordis"] == original_graph and receipt["entry"] == "main.k", receipt
        assert receipt["program_sha256"] == sha256(program.read_bytes()).hexdigest()
        assert receipt["inputs"][source.name] == sha256(source.read_bytes()).hexdigest()
        assert receipt["modules"]["main.k"] == sha256((output / "source/main.k").read_bytes()).hexdigest()
        asset = output / "assets/guest/data.bin"
        assert asset.read_bytes() == b"\x00\x02\xff"
        assert receipt["artifacts"]["guest/data.bin"] == sha256(asset.read_bytes()).hexdigest()
        assert receipt["spans"][1]["source"] == source.name
        assert not inherited_home.exists() and not (output / "cache").exists()
        shared, diagnostics = invoke("build", "-o", root / "shared-home-bundle",
                                     env_extra={"K_HOME": inherited_home.name})
        assert shared.returncode == 0 and diagnostics == [] and (inherited_home / "build").is_dir(), shared
        overlap = root / "overlap"
        failed, diagnostics = invoke("build", "-o", overlap, env_extra={"K_HOME": str(overlap / "cache")})
        assert failed.returncode and "outside the output bundle" in diagnostics[0]["message"], failed
        assert not overlap.exists()

        before = program.read_bytes()
        duplicate, diagnostics = invoke("build", "-o", output)
        assert duplicate.returncode and "output already exists" in diagnostics[0]["message"], duplicate
        assert program.read_bytes() == before
        contest = root / "concurrent"
        with ThreadPoolExecutor(max_workers=2) as threads:
            pair = [threads.submit(invoke, "build", "-o", contest) for _ in range(2)]
            assert sorted(call.result()[0].returncode for call in pair) == [0, 1]
        assert subprocess.check_output([contest / program.name], timeout=20).replace(b"\r\n", b"\n") == b"42\n"

        source.write_text('show\n"你好" + unknown_value\n', encoding="utf-8", newline="\n")
        invalid = root / "no-invalid-bundle"
        failed, diagnostics = invoke("build", "-o", invalid)
        assert failed.returncode and not invalid.exists(), failed
        issue = next(d for d in diagnostics if "unknown_value" in d["message"])
        expected_col = len('"你好" + '.encode("utf-8")) + 1
        assert (issue["file"], issue["line"], issue["col"]) == (str(source), 2, expected_col), issue

        source.write_text('show\ntwice(21)\n', encoding="utf-8", newline="\n")
        helper.write_bytes(b'fn twice(v: i64) -> i64 { bad_helper }\n')
        failed, diagnostics = invoke("check")
        assert failed.returncode and any(d.get("file") == str(helper) and "bad_helper" in d["message"]
                                         for d in diagnostics), (failed, diagnostics)
        helper.write_bytes(b'fn twice(v: i64) -> i64 { v * 2 }\n')

        for bad, detail in [(b"escape", "invalid relative"), (b"bad-map", "source span")]:
            rules.write_bytes(bad)
            last = len(events())
            fail_graph("check", "stale_lock")
            assert len(events()) == last
            reselect()
            issue = fail_graph("build", "source_transform_failed", "-o", invalid)
            assert detail in issue["message"] and not invalid.exists(), issue
            assert not (root / "escape.bin").exists()

        rules.write_bytes(b"normal")
        reselect()
        fail_graph("build", "changed_input", "-o", invalid,
                   env_extra={"K_CORDIS_SOURCE_TEST_CHANGE": str(rules)})
        assert not invalid.exists()
        reselect("wrong")
        fail_graph("check", "incompatible_abi")
        assert not any(root.glob(".k-source-*")), "private workspace leaked"


if __name__ == "__main__":
    check(*(str(Path(arg).resolve()) for arg in sys.argv[1:]))
