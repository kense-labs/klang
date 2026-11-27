"""Run the frozen K and Rust baseline from an installed K release."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import shutil
from statistics import mean, median
import time

import paired_inputs
from paired_inputs import digest
from run_bench import HERE, KLANGDIR, K_N, RUSTDIR, RUST_N, tc, write_sized

ROOT = HERE.parent
RUST_FLAGS = {"opt-level": 2, "codegen-units": 1, "strip": "none"}
CARGO_PROJECT = f"""[package]
name = "paired-bench"
version = "0.1.0"
edition = "2021"

[workspace]

[profile.release]
opt-level = {RUST_FLAGS['opt-level']}
codegen-units = {RUST_FLAGS['codegen-units']}
strip = "{RUST_FLAGS['strip']}"
"""


def tool(command, env):
    argv = list(map(str, command))
    start = time.perf_counter_ns()
    code, out, err = tc.run_capture(argv, env=env, timeout=180)
    elapsed = (time.perf_counter_ns() - start) / 1e6
    if code:
        raise RuntimeError(f"{' '.join(argv)} exited with {code}\n{out}{err}")
    return {"argv": argv, "ms": elapsed, "stdout": out, "stderr": err}


def bytes_on_disk(path):
    """Logical regular-file bytes, with hard-linked artifacts counted once."""
    seen = set()
    total = 0
    for file in path.rglob("*"):
        if file.is_symlink() or not file.is_file():
            continue
        info = file.stat()
        key = info.st_dev, info.st_ino
        if key not in seen:
            seen.add(key)
            total += info.st_size
    return total


def validate(manifest):
    if manifest["schema"] != 1:
        raise RuntimeError("unsupported paired workload manifest version")
    pins = manifest["sources"]
    required = {"bench/paired_inputs.py", "bench/klang/rng.k", "bench/rust/bench.rs"}
    names = set()
    for case in manifest["workloads"]:
        name = case["id"]
        sizes = {entry["size"] for entry in case["cases"]}
        if (not re.fullmatch(r"[a-z][a-z0-9-]*", name) or name in names or not sizes or min(sizes) <= 0 or
                len(sizes) != len(case["cases"]) or case["tier"] not in ("matched", "idiomatic") or
                case["role"] not in ("score", "holdout")):
            raise RuntimeError(f"invalid workload: {name!r}")
        names.add(name)
        required.update({"bench/klang/" + case["k"], "bench/rust/" + case["rust"]})
    if not names:
        raise RuntimeError("no frozen workloads")
    if not required <= pins.keys():
        raise RuntimeError(f"missing source pins: {sorted(required - pins.keys())}")
    for source, expected in pins.items():
        if digest(ROOT / source) != expected:
            raise RuntimeError(f"frozen source changed: {source}")


def prepare_sources(root, work, size):
    ksrc, rsrc = root / "main.k", root / "rust/src/main.rs"
    rsrc.parent.mkdir(parents=True)
    for original, dest, marker in ((KLANGDIR / work["k"], ksrc, K_N),
                                   (RUSTDIR / work["rust"], rsrc, RUST_N)):
        if "input" in work:
            shutil.copyfile(original, dest)
        else:
            write_sized(original, dest, size, marker)
    shutil.copyfile(KLANGDIR / "rng.k", root / "rng.k")
    shutil.copyfile(RUSTDIR / "bench.rs", rsrc.parent / "bench.rs")
    return ksrc, rsrc


def order(first, iteration):
    return ("k", "rust") if (first == "k") == (iteration % 2 == 0) else ("rust", "k")


def builds(root, ksrc, rsrc, settings, first):
    k_home, rust_out = root / "k-home", root / "rust-target"
    kbin = root / ("k-app" + tc.EXE_SUFFIX)
    rbin = rust_out / "release" / ("paired-bench" + tc.EXE_SUFFIX)
    manifest = root / "rust/Cargo.toml"
    manifest.write_text(CARGO_PROJECT, encoding="utf-8")
    envs = {"k": {**settings["env"], "K_HOME": str(k_home)},
            "rust": {**settings["env"], "CARGO_HOME": str(settings["cargo_home"]),
                     "CARGO_TARGET_DIR": str(rust_out), "RUSTC": str(settings["rustc"])}}
    tool([settings["cargo"], "generate-lockfile", "--offline", "--manifest-path", manifest], envs["rust"])
    commands = {"k": [settings["k"], "build", "--offline", "-O2", "--cc", settings["cc"], "-o", kbin, ksrc],
                "rust": [settings["cargo"], "build", "--release", "--offline", "--locked", "--manifest-path", manifest]}
    executables = {"k": kbin, "rust": rbin}
    results = {"k": {}, "rust": {}}
    for phase in ("cold", "warm"):
        for language in order(first, 0 if phase == "cold" else 1):
            results[language][phase] = tool(commands[language], envs[language])
            executable = executables[language]
            timestamp = executable.stat().st_mtime_ns
            if phase == "cold":
                results[language]["cold_output_mtime_ns"] = timestamp
            else:
                results[language]["output_mtime_unchanged"] = timestamp == results[language]["cold_output_mtime_ns"]
    for language, output in (("k", k_home), ("rust", rust_out)):
        binary = executables[language].stat().st_size
        store = bytes_on_disk(output)
        results[language]["executable_bytes"] = binary
        results[language]["intermediate_bytes"] = store if language == "k" else store - binary
        results[language]["total_build_bytes"] = results[language]["intermediate_bytes"] + binary
        results[language]["executable_sha256"] = digest(executables[language])
    results["rust"]["cargo_lock_sha256"] = digest(manifest.with_name("Cargo.lock"))
    stats = tool([settings["k"], "cache", "stats", "--json"], envs["k"])
    results["k"]["cache"] = json.loads(stats["stdout"])
    results["source_sha256"] = {"k": digest(ksrc), "rust": digest(rsrc)}
    return executables, envs, results


def arguments(root, work, case):
    kind = work.get("input")
    if kind is None:
        return (), None
    fixture = root / "input"
    generate = {"logscan": paired_inputs.logscan, "file_copy": paired_inputs.file_copy}[kind]
    if generate(fixture, case["size"]) != case["stdout"] or digest(fixture) != case["input_sha256"]:
        raise RuntimeError(f"frozen fixture changed: {work['id']}/{case['size']}")
    if kind == "file_copy":
        destination = root / "output"
        return (fixture, destination), (destination, case["input_sha256"])
    return (fixture,), None


def check_parser_errors(root, executables, envs):
    malformed = root / "malformed.csv"
    malformed.write_bytes(b"200,1\n600,99\n")
    for language, executable in executables.items():
        for path, expected in ((malformed, "invalid row 2"), (root / "missing", "cannot read input")):
            code, out, err = tc.run_capture([executable, path], env=envs[language], timeout=30)
            if (code, out.strip(), err.strip()) != (2, "", expected):
                raise RuntimeError(f"{language} {path.name}: invalid input behavior changed: {(code, out, err)!r}")


def sample(executable, args, env, expected, output, root):
    if output:
        output[0].unlink(missing_ok=True)
    run = tc.run_measured(executable, args, env=env, output_dir=root)
    if run.code or run.out != expected or run.ms <= 0:
        raise RuntimeError(f"{executable}: exit {run.code}, expected {expected!r}, got {run.out!r}\n{run.err}")
    if output and (not output[0].is_file() or digest(output[0]) != output[1]):
        raise RuntimeError(f"{executable}: file output does not match the frozen input")
    return {"ms": run.ms, "peak_rss_bytes": run.peak, "stdout": run.out}


def ratio_summary(groups):
    """Bootstrap matched repeats within each fixed size, then weight sizes equally."""
    rng = random.Random(3217)
    center = math.exp(mean(mean(values) for values in groups))
    draws = sorted(math.exp(mean(mean(rng.choices(values, k=len(values))) for values in groups))
                   for _ in range(2000))
    return {"ratio": center, "ci95": [draws[49], draws[1949]]}


def describe(samples):
    times = sorted(run["ms"] for run in samples)
    return {"median_ms": median(times), "p95_ms": times[math.ceil(len(times) * 0.95) - 1],
            "peak_rss_bytes": max(run["peak_rss_bytes"] for run in samples)}


def run_case(root, work, case, settings, args, first):
    ksrc, rsrc = prepare_sources(root, work, case["size"])
    parameters, output = arguments(root, work, case)
    executables, envs, cost = builds(root, ksrc, rsrc, settings, first)
    if work.get("input") == "logscan":
        check_parser_errors(root, executables, envs)

    pairs = []
    for iteration in range(args.warmup + args.reps):
        sequence = order(first, iteration if iteration < args.warmup else iteration - args.warmup)
        values = {"order": sequence}
        for language in sequence:
            values[language] = sample(executables[language], parameters, envs[language], case["stdout"], output, root)
        pairs.append(values)
    runs = pairs[args.warmup:]
    logs = [math.log(run["rust"]["ms"] / run["k"]["ms"]) for run in runs]
    result = {"build": cost, "warmup_pairs": pairs[:args.warmup], "pairs": runs, **ratio_summary([logs])}
    result["native"] = {language: describe([pair[language] for pair in runs]) for language in executables}
    if not args.no_jit:
        command = ("run", "--offline", "-O2", ksrc, "--", *parameters)
        jit = [sample(settings["k"], command, envs["k"], case["stdout"], output, root)
               for _ in range(args.warmup + args.reps)]
        result["tinycc"] = {"warmup": jit[:args.warmup], "samples": jit[args.warmup:],
                            **describe(jit[args.warmup:])}
    return result, logs


def markdown(report):
    lines = ["# Paired K and Rust benchmark", "", f"{report['host']['platform']} · {report['host']['cpu']}", "",
             f"{report['reps']} measured pairs per size. First process in the first pair is {report['first']}.",
             "", "| Workload | Series | Size | K median ms | Rust median ms | Rust / K with 95% interval |",
             "| --- | --- | ---: | ---: | ---: | ---: |"]
    for work in report["cases"]:
        k_ms, rust_ms = (work["native"][name]["median_ms"] for name in ("k", "rust"))
        low, high = work["ci95"]
        lines.append(f"| {work['id']} | {work['tier']} {work['role']} | {work['size']} | "
                     f"{k_ms:.3f} | {rust_ms:.3f} | {work['ratio']:.3f} [{low:.3f}, {high:.3f}] |")
    lines += ["", "## Aggregates", ""]
    for name, aggregate in report["summary"].items():
        low, high = aggregate["ci95"]
        lines.append(f"- {name} {aggregate['ratio']:.3f} [{low:.3f}, {high:.3f}]")
    lines += ["", "Raw samples, RSS, build costs, disk use, and TinyCC measurements are in report.json."]
    return "\n".join(lines) + "\n"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    for option in ("exe", "cc", "rustc", "out"):
        ap.add_argument(f"--{option}", type=Path, required=True)
    ap.add_argument("--manifest", type=Path, default=HERE / "paired_workloads.json")
    ap.add_argument("--reps", type=int, default=7)
    ap.add_argument("--warmup", type=int, default=2)
    ap.add_argument("--first", choices=("k", "rust"), default="k")
    ap.add_argument("--no-jit", action="store_true")
    args = ap.parse_args(argv)
    if args.reps < 3 or args.warmup < 1:
        ap.error("use at least three measured pairs and one warmup pair")
    args.out = args.out.resolve()
    k, cc, rustc = (p.resolve() for p in (args.exe, args.cc, args.rustc))
    cargo = rustc.with_name("cargo" + tc.EXE_SUFFIX)
    if not (k.parent / "stdlib").is_dir() or not (k.parent / "tcclib").is_dir():
        ap.error("--exe must point to an installed K release containing stdlib and tcclib")
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    validate(manifest)
    env = {key: value for key, value in os.environ.items()
           if key not in {"K_HOME", "K_STDLIB", "K_TCCLIB", "K_RUNTIME", "K_CC", "RUSTFLAGS",
                          "CARGO_ENCODED_RUSTFLAGS", "RUSTC_WRAPPER", "RUSTC_WORKSPACE_WRAPPER",
                          "CFLAGS", "CXXFLAGS", "LDFLAGS", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
                          "LIBRARY_PATH", "CCC_OVERRIDE_OPTIONS", "SDKROOT", "DEVELOPER_DIR", "TOOLCHAINS"}
           and not key.startswith(("CARGO_PROFILE_", "CARGO_BUILD_", "CARGO_TARGET_"))}
    env.update(LC_ALL="C", K_PROGRESS="plain")
    host = {"platform": platform.platform(), "cpu": platform.processor() or platform.machine(),
            "python": platform.python_version()}
    prerequisites = {}
    tools = {"k": k, "cc": cc, "rustc": rustc, "cargo": cargo}
    if platform.system() == "Darwin":
        host["cpu"] = tool(["/usr/sbin/sysctl", "-n", "machdep.cpu.brand_string"], env)["stdout"].strip()
        sdk = Path(tool(["/usr/bin/xcrun", "--show-sdk-path"], env)["stdout"].strip()).resolve()
        env["MACOSX_DEPLOYMENT_TARGET"] = "13.3"
        # K's macOS executable cache supports the system driver and selected SDK.
        # Record the actual Clang behind the shim as well as the shim itself.
        if cc in (Path("/usr/bin/cc"), Path("/usr/bin/clang")):
            tools["cc_backend"] = Path(tool(["/usr/bin/xcrun", "--find", "clang"], env)["stdout"].strip())
        prerequisites["sdk"] = {"path": str(sdk), "logical_bytes": bytes_on_disk(sdk),
                                "version": tool(["/usr/bin/xcrun", "--show-sdk-version"], env)["stdout"].strip()}
    rust_version = tool([rustc, "--version", "--verbose"], env)["stdout"]
    if f"commit-hash: {manifest['rust_commit']}" not in rust_version.splitlines():
        raise RuntimeError("rustc does not match the manifest's pinned Rust commit")
    sysroot = Path(tool([rustc, "--print", "sysroot"], env)["stdout"].strip())
    settings = {"k": k, "cc": cc, "rustc": rustc, "cargo": cargo, "env": env, "cargo_home": args.out / "cargo-home"}
    report = {"schema": 1, "host": host, "status": "failed", "first": args.first, "reps": args.reps,
              "warmup_pairs": args.warmup, "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
              "runner_sha256": {path.name: digest(path) for path in
                                (Path(__file__), HERE / "run_bench.py", ROOT / "scripts/toolchain.py")},
              "tools": {name: {"path": str(path), "sha256": digest(path),
                               "version": tool([path, "--version" if name != "k" else "version"], env)["stdout"].strip()}
                        for name, path in tools.items()},
              "rust_flags": RUST_FLAGS, "prerequisites": prerequisites,
              "environment": {key: env[key] for key in ("LC_ALL", "K_PROGRESS", "MACOSX_DEPLOYMENT_TARGET")
                              if key in env},
              "installation_bytes": {"k_release": bytes_on_disk(k.parent), "rust_sysroot": bytes_on_disk(sysroot),
                                     "cc_executable": tools.get("cc_backend", cc).stat().st_size}, "cases": []}
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "manifest.json").write_bytes(manifest_bytes)
    groups = {}
    try:
        for work in manifest["workloads"]:
            for case in work["cases"]:
                root = args.out / f"{work['id']}-{case['size']}"
                root.mkdir()
                print(f"{work['id']} {case['size']} ({work['tier']} {work['role']})", flush=True)
                results, logs = run_case(root, work, case, settings, args, args.first)
                report["cases"].append({**{key: work[key] for key in ("id", "family", "tier", "role")},
                                        **case, **results})
                groups.setdefault(f"{work['tier']} {work['role']}", []).append(logs)
        report["summary"] = {name: ratio_summary(logs) for name, logs in groups.items()}
        validate(manifest)
        report["status"] = "passed"
        (args.out / "report.md").write_text(markdown(report), encoding="utf-8")
    except (RuntimeError, OSError, ValueError) as error:
        report["error"] = str(error)
        raise
    finally:
        (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {args.out / 'report.md'}")
    return 0
