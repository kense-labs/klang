#!/usr/bin/env python3
"""Measure loopback CPU inference, allocator use, and provider replacement."""
import argparse
from hashlib import file_digest, sha256
import json
import math
import os
from pathlib import Path
import platform
import shutil
from statistics import median
import subprocess
import sys
import tempfile
from time import perf_counter_ns

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "test"))
from cordis_gateway import DONE, REQUEST, request, running, setup, workspace_snapshot
from cordis_inference import workspace_classifier


def rss_command(path):
    if sys.platform == "darwin":
        return ["/usr/bin/time", "-l", "-o", path]
    if sys.platform.startswith("linux"):
        return ["/usr/bin/time", "-f", "%M", "-o", path]
    raise RuntimeError("peak RSS measurement requires macOS or Linux /usr/bin/time")


def rss_bytes(path):
    lines = path.read_text().splitlines()
    if sys.platform == "darwin":
        return int(next(line.split()[0] for line in lines if "maximum resident set size" in line))
    return int(lines[0]) * 1024


def quantile(sorted_samples, fraction):
    return sorted_samples[math.ceil(len(sorted_samples) * fraction) - 1]


def check_prediction(port, timeout, expected=(2.0, 8.0)):
    prediction = request(port, timeout=timeout)
    if prediction != (DONE, list(expected)):
        raise RuntimeError(f"unexpected gateway prediction: {prediction!r}")


def workspace(read_line, phase):
    result = workspace_snapshot(read_line, phase)
    if not result["active_measured"] or not result["last_activation_measured"]:
        raise RuntimeError(f"{phase} CPU arena statistics unavailable: {result!r}")
    return result


def run_sample(command, root, env, count, warmup, deadline_ms, replacement):
    rss = root / "rss.txt"
    marker = root / "reload"
    marker.unlink(missing_ok=True)
    switch_args = (marker.name, "b", "b.lock") if replacement else ()
    launch = perf_counter_ns()
    samples = []
    timeout = deadline_ms / 1000 + 2
    with running([*rss_command(rss), *command], root, env, "providers.json", "a.lock",
                 "0", "8", str(deadline_ms), "16", "1", str(count + warmup + replacement),
                 *switch_args, "--workspace") as (port, read_line):
        startup_ns = perf_counter_ns() - launch
        arenas = {"startup": workspace(read_line, "startup")}
        for _ in range(warmup):
            check_prediction(port, timeout)
        started = perf_counter_ns()
        for _ in range(count):
            sent = perf_counter_ns()
            check_prediction(port, timeout)
            samples.append(perf_counter_ns() - sent)
        elapsed = perf_counter_ns() - started
        switch_ms = None
        if replacement:
            marker.touch()
            sent = perf_counter_ns()
            check_prediction(port, timeout, (3.0, 6.0))
            switch_ms = (perf_counter_ns() - sent) / 1e6
            if read_line() != "selected b":
                raise RuntimeError("gateway did not publish the replacement")
            arenas["reload"] = workspace(read_line, "reload")
            if arenas["reload"]["last_activation_reserved_bytes"] <= arenas["reload"]["active_reserved_bytes"]:
                raise RuntimeError("replacement sample omitted the old generation")
        arenas["shutdown"] = workspace(read_line, "shutdown")
    ordered = sorted(samples)
    return {
        "startup_ms": startup_ns / 1e6,
        "requests_per_second": count * 1e9 / elapsed,
        "p95_ms": quantile(ordered, 0.95) / 1e6,
        "p99_ms": quantile(ordered, 0.99) / 1e6,
        "peak_rss_bytes": rss_bytes(rss),
        "latency_ns": samples,
        "workspace": arenas,
        **({"replacement_request_ms": switch_ms} if replacement else {}),
    }


def digest(path):
    with path.open("rb") as file:
        return file_digest(file, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("install", "host", "provider", "runtime", "out"):
        parser.add_argument(f"--{key}", type=Path, required=True)
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--warmup", type=int, default=100)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--deadline-ms", type=int, default=20000,
                        help="request deadline including model activation (default: 20000)")
    parser.add_argument("--widths", type=int, nargs="+", default=(2, 512, 32768),
                        help="intermediate widths, each a power of two between 2 and 65536")
    args = parser.parse_args()
    if args.requests < 1 or args.warmup < 0 or args.repetitions < 1 or args.requests + args.warmup >= 2**32 - 1:
        parser.error("request count and repetitions must be positive, and warmup must fit the request budget")
    if any(n < 2 or n > 65536 or n & (n - 1) for n in args.widths):
        parser.error("widths must be powers of two between 2 and 65536")
    if not 1 <= args.deadline_ms <= 3600000:
        parser.error("the request deadline must be between 1 and 3600000 ms")

    install = args.install.resolve()
    compiler = install / "k"
    source = install / "examples" / "inference_gateway.k"
    runtime, host, provider = (path.resolve() for path in (args.runtime, args.host, args.provider))
    report = {
        "schema": 2,
        "platform": platform.platform(),
        "k_version": subprocess.check_output([compiler, "version"], text=True).strip(),
        "sha256": {**{name: digest(path) for name, path in
                      (("compiler", compiler), ("gateway_source", source), ("host", host),
                       ("provider", provider), ("runtime", runtime))},
                   "request": sha256(REQUEST).hexdigest()},
        "requests_per_rep": args.requests,
        "warmup_connections": args.warmup,
        "deadline_ms": args.deadline_ms,
        "workloads": {},
    }
    with tempfile.TemporaryDirectory(prefix="gateway benchmark ") as work:
        root = Path(work)
        local_runtime = root / runtime.name
        shutil.copyfile(runtime, local_runtime)
        env = {**os.environ, "K_HOME": str(root / "home"), "K_STDLIB": str(install / "stdlib"),
               "K_TCCLIB": str(install / "tcclib")}
        widths = sorted(set(args.widths))
        workloads = [(f"width-{width}", width, None) for width in widths]
        workloads.append((f"replace-{widths[0]}-with-{widths[-1]}", widths[0], widths[-1]))
        for label, width, replacement in workloads:
            directory = root / label
            directory.mkdir()
            models = {"a": workspace_classifier(width, [0, 1]),
                      "b": workspace_classifier(replacement or width, [1, -1])}
            samples = {"tinycc": [], "native": []}
            commands = dict(zip(samples, setup(compiler, host, provider, local_runtime,
                                               directory, env, source_path=source, models=models)))
            names = list(commands)
            for i in range(args.repetitions):
                for name in names[::1 if i % 2 == 0 else -1]:
                    samples[name].append(run_sample(commands[name], directory, env, args.requests,
                                                    args.warmup, args.deadline_ms, replacement is not None))
            metrics = ("startup_ms", "requests_per_second", "p95_ms", "p99_ms", "peak_rss_bytes")
            if replacement is not None:
                metrics += ("replacement_request_ms",)
            report["workloads"][label] = {
                "initial_width": width, "replacement_width": replacement,
                "models": {key: {"bytes": len(blob), "sha256": sha256(blob).hexdigest()}
                           for key, blob in models.items()},
                "samples": samples,
                "medians": {name: {key: median(sample[key] for sample in runs) for key in metrics}
                            for name, runs in samples.items()},
            }
        assert not (root / "home" / "pkg").exists()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({name: workload["medians"] for name, workload in report["workloads"].items()}, indent=2))


if __name__ == "__main__":
    main()
