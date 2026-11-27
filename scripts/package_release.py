#!/usr/bin/env python3
"""Lays out a release of the compiler from a CMake build of it, for
.github/workflows/release.yml, and as a way to try one locally.

The compiler finds what it needs beside its own binary when the build tree
it was built in is not there (main.cpp's StdlibDirs, jit.h's JitLibPath,
builtin.h's BuiltinSrcDir and BuiltinPackageLinkFile), so a release is one
directory:

  k[.exe]          the compiler, with the blessed packages compiled in
  stdlib/              the standard library
  include/cordis/      headers for native Cordis providers
  include/klang/       native task driver for host components
  tcclib/              TinyCC's headers and support archive, for in-process runs
  native-runtime/      verified Apple Clang runtime objects on macOS
  pkg/<package>/       each builtin package's K sources, sidecar, docs and
                       license, without its vendored dependencies (third_party/);
                       lib/ holding the static libraries a `k build` of a
                       program using it links, and link-msvc.rsp and
                       link-cc.rsp naming them relative to themselves
  packages/            bundled packages that were not compiled in
  docs/, samples/      as in this tree (less the samples' test harness)
  examples/            source for the optional inference gateway
  k-inference-gateway  prebuilt inference gateway in the Linux cloud profile
  k-inference-supervisor  Linux worker supervision and OOM rollback
  lib/                 the Cordis host, providers and engine in that profile
  vscode/              the prebuilt VS Code extension
  man/k.1          the help as a man page (k help --man)
  licenses/            the licenses of what the compiler and the packages'
                       libraries are built from
  README.md, LICENSE, LICENSE-APACHE, NOTICE

Configure with -DK_BUILTIN_LIBS=all (or a subset) and build the configuration
given here first. Directory inventories use the files Git tracks, so nothing
a run left in samples/ or docs/ is shipped. The Cordis application interface
is listed explicitly alongside the native ABI headers. If no packages are
compiled in, the release will omit the pkg/ directory.

  python scripts/package_release.py --build build --config Release --out dist/klang
"""

import argparse
from hashlib import sha256
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
EXE_SUFFIX = ".exe" if os.name == "nt" else ""

# What the samples directory holds for the test suite rather than for people.
SAMPLES_HARNESS = {"run_samples.py", "expected"}

LICENSE_NAME = re.compile(r"^(LICEN[CS]E|COPYING|RELICENSING)", re.IGNORECASE)


def fail(msg):
    sys.exit(f"package_release: {msg}")


def tracked(subdir):
    """The files git tracks under `subdir` of this tree, relative to it."""
    out = subprocess.run(["git", "-C", str(REPO_ROOT), "ls-files", "-z", "--", subdir],
                         check=True, capture_output=True).stdout
    return [Path(p) for p in out.decode("utf-8").split("\0") if p]


def copy_file(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_tracked(subdir, out, skip=()):
    for rel in tracked(subdir):
        parts = rel.relative_to(subdir).parts
        if parts[0] in skip:
            continue
        copy_file(REPO_ROOT / rel, out / rel)


def builtin_packages(build):
    """(name, source directory) of each package compiled into the compiler,
    from the build inventory kept by cmake/package.cmake."""
    inventory = build / "generated" / "builtin_package_sources.tsv"
    if inventory.exists():
        rows = [line.split("\t", 1) for line in inventory.read_text(encoding="utf-8").splitlines()]
        return [(name, Path(srcdir)) for name, srcdir in rows]
    # Also package builds made before the inventory was separate.
    header = build / "generated" / "builtin_packages.h"
    if not header.exists():
        fail(f"{header} is missing: configure {build} first")
    rows = re.findall(r'\{ "(\w+)", \w+, "([^"]*)",', header.read_text(encoding="utf-8"))
    return [(name, Path(srcdir)) for name, srcdir in rows]


def rsp_items(path):
    items = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if len(line) > 1 and line[0] == '"' and line[-1] == '"':
            line = line[1:-1]
        if line:
            items.append(line)
    return items


def rsp_text(items):
    return "".join((f'"{i}"' if " " in i else i) + "\n" for i in items)


def under(path, root):
    try:
        path.resolve().relative_to(root)
        return True
    except ValueError:
        return False


def package_libraries(name, rspdir, build, out):
    """Copies the libraries of the package's link closure that its build
    made into out/lib/ and writes its response files naming them there,
    relative to the response files; system libraries stay as they are."""
    build = build.resolve()
    copied = {}
    for style in ("msvc", "cc"):
        rsp = rspdir / f"link-{style}.rsp"
        if not rsp.exists():
            fail(f"{rsp} is missing: build the compiler in this configuration first")
        items = []
        for item in rsp_items(rsp):
            path = Path(item)
            if path.is_absolute() and path.is_file() and under(path, build):
                rel = f"lib/{path.name}"
                if copied.setdefault(rel, path.resolve()) != path.resolve():
                    fail(f"package {name}: two libraries named {path.name}")
                item = rel
            elif build.as_posix().lower() in item.replace("\\", "/").lower():
                fail(f"package {name}: {rsp} names the build tree in {item!r}, "
                     f"which a release cannot carry")
            items.append(item)
        (out / f"link-{style}.rsp").write_text(rsp_text(items), encoding="utf-8", newline="\n")
    for rel, src in copied.items():
        copy_file(src, out / rel)


def package_sources(name, srcdir, out):
    """The package as its repository has it, less version control and its
    vendored dependencies, whose license files go to licenses/."""
    if not (srcdir / f"{name}.k").is_file():
        fail(f"package {name}: {srcdir} has no {name}.k")
    for entry in sorted(srcdir.iterdir()):
        if entry.name.startswith(".git") or entry.name in {"third_party", "build", "__pycache__"}:
            continue
        if entry.is_dir():
            shutil.copytree(entry, out / entry.name, ignore=shutil.ignore_patterns("__pycache__"))
        else:
            copy_file(entry, out / entry.name)


def collect_licenses(third_party, out):
    if not third_party.is_dir():
        return
    for dep in sorted(third_party.iterdir()):
        if not dep.is_dir():
            continue
        for f in sorted(dep.iterdir()):
            if f.is_file() and LICENSE_NAME.match(f.name):
                copy_file(f, out / dep.name / f.name)


def package_cloud(build, config, out, cc):
    subprocess.run(["cmake", "--install", str(build), "--config", config, "--prefix", str(out),
                    "--component", "cloud"], check=True)
    for name in ("libk_cordis_application.so", "libk_cordis_tcp_read.so", "libk_cordis_onnx.so",
                 "libonnxruntime.so", "libonnxruntime_providers_shared.so"):
        if not (out / "lib" / name).is_file():
            fail(f"missing cloud library {name}; configure the Cordis host, TCP provider and ONNX SDK")
    with tempfile.TemporaryDirectory(prefix="k-cloud-build-") as home:
        env = {**os.environ, "K_HOME": home}
        for name in ("K_STDLIB", "K_TCCLIB"):
            env.pop(name, None)
        command = [str(out / "k"), "build", "--offline"]
        if cc:
            command += ["--cc", cc]
        subprocess.run([*command, "-o", "k-inference-gateway", "examples/inference_gateway.k"],
                       cwd=out, env=env, check=True)


def package_macos_runtime(out):
    """Seed the first native build for the system Clang's release profiles."""
    cc = "/usr/bin/clang"
    version = subprocess.run([cc, "--version"], capture_output=True)
    if version.returncode or not version.stdout.startswith(b"Apple clang version "):
        print("system Apple Clang unavailable, skipping native runtime objects")
        return
    seed = out / "native-runtime"
    seed.mkdir()
    with tempfile.TemporaryDirectory(prefix="k-native-runtime-") as temp:
        source = Path(temp) / "k_runtime.c"
        preprocessed = Path(temp) / "k_runtime.i"
        subprocess.run([out / "k", "--emit-runtime", source], check=True)
        for profile in ("default", "13.3"):
            env = dict(os.environ)
            if profile == "default":
                env.pop("MACOSX_DEPLOYMENT_TARGET", None)
            else:
                env["MACOSX_DEPLOYMENT_TARGET"] = profile
            object_file = seed / (profile + ".o")
            plan = subprocess.check_output([cc, "-O2", "-###", "-c", "-x", "c", "-", "-o", "-",
                                            "-fdebug-compilation-dir=/", "-fcoverage-compilation-dir=/"],
                                           env=env, stderr=subprocess.STDOUT)
            processed = subprocess.check_output([cc, "-O2", "-E", "-P", source], env=env)
            preprocessed.write_bytes(processed)
            subprocess.run([cc, "-O2", "-x", "cpp-output", "-c", preprocessed, "-o", object_file],
                           env=env, check=True)
            digests = (sha256(value).hexdigest() for value in (plan, object_file.read_bytes(), processed))
            (seed / (profile + ".stamp")).write_text("k-native-runtime-v1\n" + "\n".join(digests) + "\n",
                                                    encoding="ascii")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", default="build", help="the CMake build directory")
    ap.add_argument("--config", default="Release", help="the configuration that was built")
    ap.add_argument("--out", required=True, help="the release directory to write (replaced)")
    ap.add_argument("--cloud", action="store_true", help="include the Linux Cordis inference gateway")
    ap.add_argument("--cc", help="native C compiler for the cloud gateway")
    args = ap.parse_args()
    if args.cloud and sys.platform != "linux":
        ap.error("the cloud profile requires Linux")
    if args.cc and not args.cloud:
        ap.error("--cc requires --cloud")

    build = Path(args.build).resolve()
    out = Path(args.out).resolve()
    exe_name = "k" + EXE_SUFFIX
    exe = next((p for p in (build / args.config / exe_name, build / exe_name) if p.exists()), None)
    if not exe:
        fail(f"no {exe_name} under {build} for {args.config}")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    copy_file(exe, out / exe_name)
    if sys.platform == "darwin":
        package_macos_runtime(out)
    copy_file(REPO_ROOT / "src/cordis/application.h", out / "include/cordis/application.h")
    copy_file(REPO_ROOT / "src/cordis/gateway_net.h", out / "include/cordis/gateway_net.h")
    copy_file(REPO_ROOT / "src/cordis/inference.h", out / "include/cordis/inference.h")
    copy_file(REPO_ROOT / "src/cordis/diagnostics.h", out / "include/cordis/diagnostics.h")
    copy_file(REPO_ROOT / "src/cordis/tcp_read.h", out / "include/cordis/tcp_read.h")
    copy_file(REPO_ROOT / "src/cordis/tcp_policy.h", out / "include/cordis/tcp_policy.h")
    copy_file(REPO_ROOT / "src/cordis/tcp_stream.h", out / "include/cordis/tcp_stream.h")
    copy_file(REPO_ROOT / "stdlib/cordis.k", out / "stdlib/cordis.k")
    copy_file(REPO_ROOT / "examples/inference_gateway.k", out / "examples/inference_gateway.k")
    copy_file(REPO_ROOT / "src/runtime/runtime_tasks.h", out / "include/klang/runtime_tasks.h")
    tcclib = build / "tcclib"
    if not (tcclib / "include" / "stddef.h").exists():
        fail(f"{tcclib} is incomplete: build the compiler first")
    shutil.copytree(tcclib, out / "tcclib")

    packages = builtin_packages(build)
    for name, srcdir in packages:
        pkgout = out / "pkg" / name
        package_sources(name, srcdir, pkgout)
        rspdir = build / "pkg" / name / args.config
        if rspdir.is_dir():
            package_libraries(name, rspdir, build, pkgout)
        collect_licenses(srcdir / "third_party", out / "licenses" / name)
        print(f"package {name}: from {srcdir}")

    licenses = build / "generated" / "builtin_package_licenses.tsv"
    if licenses.is_file():
        for line in licenses.read_text(encoding="utf-8").splitlines():
            package, dependency, filename = line.split("\t", 2)
            path = Path(filename)
            copy_file(path, out / "licenses" / package / dependency / path.name)

    builtins = {name for name, _ in packages}
    for srcdir in sorted((REPO_ROOT / "packages").iterdir()):
        if srcdir.name not in builtins and (srcdir / f"{srcdir.name}.k").is_file():
            package_sources(srcdir.name, srcdir, out / "packages" / srcdir.name)

    copy_tracked("stdlib", out, skip=("cordis.k",))
    copy_tracked("docs", out)
    copy_tracked("samples", out, skip=SAMPLES_HARNESS)
    for rel in ("README.md", "LICENSE", "LICENSE-APACHE", "NOTICE", "vscode/klang-language.vsix",
                "vscode/README.md", "vscode/LICENSE"):
        copy_file(REPO_ROOT / rel, out / rel)
    collect_licenses(REPO_ROOT / "third_party", out / "licenses")
    man = subprocess.run([str(out / exe_name), "help", "--man"], capture_output=True)
    if man.returncode != 0 or not man.stdout.startswith(b".TH K 1"):
        fail(f"k help --man failed:\n{man.stderr.decode(errors='replace')}")
    (out / "man").mkdir()
    (out / "man" / "k.1").write_bytes(man.stdout)
    if args.cloud:
        package_cloud(build, args.config, out, args.cc)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
