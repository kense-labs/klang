#!/usr/bin/env python3
"""Build, archive, and smoke-test two independent releases of K."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tarfile
import time
import zipfile
import zlib

from archive_release import entries


ROOT = Path(__file__).resolve().parents[1]
SYSTEM_PATH = os.pathsep.join(("/usr/bin", "/bin", "/usr/sbin", "/sbin"))
GIT = "/usr/bin/git"
SAMPLE = Path("samples/01_tour.k")
EXPECTED = Path("samples/expected/tour.out")
VENDOR_ROOTS = ("third_party/tinycc", "third_party/subprocess", "third_party/cute_spirv", "packages")
CORE_OPTIONS = {
    "CMAKE_BUILD_TYPE": "Release",
    "FETCHCONTENT_FULLY_DISCONNECTED": "ON",
    "K_BUILTIN_LIBS": "",
    "K_BUILD_CORDIS_HOST": "OFF",
    "K_BUILD_LSP_TESTS": "OFF",
    "K_BUILD_TCP_READ_PROVIDER": "OFF",
    "K_ONNXRUNTIME_ROOT": "",
    "K_TCC_LIB_PATH:PATH": "",
    "K_STDLIB_PATH:PATH": "",
}
MACOS_DEPLOYMENT_TARGET = "13.3"
ONNX_VERSION = "1.24.4"
ONNX_SHA256 = "4b978d5065b85e7004b6c6f60ca494bd978fbe6836cbf0a0b52d82b61ab99638"
LINUX_ARCHES = {"aarch64": "arm64", "x86_64": "x64"}
NATIVE_DECLARATIONS = {
    "k_box3d": "packages/physics/CMakeLists.txt",
    "k_sdl": "packages/gfx/CMakeLists.txt",
    "k_nuklear": "packages/gfx/CMakeLists.txt",
}
NATIVE_SMOKE = """import physics;
import gfx;
import audio;
import ui;
fn main() {
    let world = physics::create_world();
    assert(physics::is_valid(world));
    defer physics::destroy(world);
    physics::step(world, 1.0 / 60.0, 4);
    assert(gfx::available() && ui::available() && audio::available());
    assert(audio::open_offline());
    defer audio::close();
    var buffer: f32[4] = [0.0; 4];
    assert(audio::render(buffer[..]));
    print("native packages ready");
}
"""


class ReproductionError(RuntimeError):
    pass


def file_digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def canonical(data):
    return json.dumps(data, ensure_ascii=True, sort_keys=True, separators=(",", ":")).encode()


def identity(data):
    return hashlib.sha256(canonical(data)).hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=True, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def output(*args, cwd=ROOT, env=None):
    run = subprocess.run(args, cwd=cwd, env=env, capture_output=True, check=True)
    return run.stdout.decode().strip()


def git_paths(env, *args):
    data = subprocess.check_output([GIT, "ls-files", "-z", *args], cwd=ROOT, env=env)
    return {Path(os.fsdecode(path)) for path in data.split(b"\0") if path}


def source_inputs(env, destination):
    tracked = git_paths(env, "--cached")
    extra = git_paths(env, "--others", "--exclude-standard")
    files = {}
    for rel in sorted(tracked | extra):
        path = ROOT / rel
        if path == destination or destination in path.parents:
            continue
        if rel.name == ".DS_Store" and rel not in tracked:
            continue
        try:
            info = path.lstat()
        except FileNotFoundError:
            continue
        if not stat.S_ISREG(info.st_mode):
            raise ReproductionError(f"core release expects a regular source file at {rel}")
        files[rel.as_posix()] = {
            "sha256": file_digest(path),
            "size": info.st_size,
            "mode": 0o755 if info.st_mode & 0o111 else 0o644,
            "tracked": rel in tracked,
        }
    status = subprocess.run([GIT, "diff", "--quiet", "HEAD", "--"], cwd=ROOT, env=env).returncode
    if status not in (0, 1):
        raise ReproductionError("git could not compare the worktree with HEAD")
    return {
        "commit": output(GIT, "rev-parse", "HEAD", env=env),
        "dirty": bool(status) or any(not f["tracked"] for f in files.values()),
        "epoch": int(output(GIT, "show", "-s", "--format=%ct", "HEAD", env=env)),
        "sha256": identity(files),
        "files": files,
    }


def tool_inputs(commands, env):
    tools = {}
    for name, (path, flags) in commands.items():
        executable = Path(path)
        record = {"path": str(executable), "realpath": str(executable.resolve()), "sha256": file_digest(executable)}
        if flags:
            record["version"] = output(path, *flags, env=env)
        tools[name] = record
    return tools


def host_inputs(devdir, cmake, ninja, env):
    common = {
        "cmake": (cmake, ("--version",)),
        "ninja": (ninja, ("--version",)),
        "git": (GIT, ("--version",)),
        "python": (sys.executable, ("--version",)),
    }
    if sys.platform == "linux":
        commands = {**common}
        for name, tool in (("cc", "clang"), ("cxx", "clang++"), ("ld", "ld"),
                           ("ar", "ar"), ("ranlib", "ranlib")):
            path = shutil.which(tool, path=env["PATH"])
            if not path:
                raise ReproductionError(f"{tool} is required on PATH")
            commands[name] = (path, ("--version",))
        return {
            "arch": platform.machine(),
            "linux": Path("/etc/os-release").read_text(encoding="utf-8").splitlines(),
            "libc": output("getconf", "GNU_LIBC_VERSION", env=env),
            "tools": tool_inputs(commands, env),
            "zlib": zlib.ZLIB_RUNTIME_VERSION,
        }

    def xcrun(*args):
        return output("/usr/bin/xcrun", "--sdk", "macosx", *args, env=env)

    sdk = Path(xcrun("--show-sdk-path")).resolve()
    settings = next((sdk / name for name in ("SDKSettings.json", "SDKSettings.plist")
                     if (sdk / name).is_file()), None)
    if settings is None:
        raise ReproductionError(f"could not find SDK settings under {sdk}")
    commands = {
        **common,
        "cc": (xcrun("--find", "clang"), ("--version",)),
        "cxx": (xcrun("--find", "clang++"), ("--version",)),
        "ld": (xcrun("--find", "ld"), ("-version_details",)),
        "ar": (xcrun("--find", "ar"), ()),
        "ranlib": (xcrun("--find", "ranlib"), ()),
    }
    return {
        "arch": platform.machine(),
        "macos": output("/usr/bin/sw_vers", env=env),
        "developer_dir": devdir,
        "sdk": {
            "path": str(sdk),
            "version": xcrun("--show-sdk-version"),
            "build": xcrun("--show-sdk-build-version"),
            "settings": settings.name,
            "settings_sha256": file_digest(settings),
        },
        "tools": tool_inputs(commands, env),
        "zlib": zlib.ZLIB_RUNTIME_VERSION,
    }


def native_archives(directory):
    archives = {}
    for name, declaration in NATIVE_DECLARATIONS.items():
        text = (ROOT / declaration).read_text(encoding="utf-8")
        pin = re.search(rf"FetchContent_Declare\({name}\s+URL\s+(\S+)\s+URL_HASH SHA256=([a-f0-9]{{64}})",
                        text)
        if not pin:
            raise ReproductionError(f"no pinned archive for {name} in {declaration}")
        url, expected = pin.groups()
        candidates = [directory / f"{name}.{suffix}" for suffix in ("tar.gz", "tar", "tar.xz")]
        candidates.append(directory / f"{name}-subbuild" / f"{name}-populate-prefix" / "src/archive.tar")
        path = next((p for p in candidates if p.is_file()), None)
        if path is None:
            raise ReproductionError(f"provide the pinned archive {name}.tar.gz under {directory} ({url})")
        actual = file_digest(path)
        if actual != expected:
            raise ReproductionError(f"archive digest differs for {path}: expected {expected}, got {actual}")
        archives[name] = {"path": str(path), "url": url, "sha256": expected, "bytes": path.stat().st_size}
    return archives


def onnx_archive(path, arch):
    digest = file_digest(path)
    if digest != ONNX_SHA256:
        raise ReproductionError(f"archive digest differs for {path}: expected {ONNX_SHA256}, got {digest}")
    return {
        "path": str(path), "bytes": path.stat().st_size, "sha256": digest, "arch": arch,
        "url": f"https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime/{ONNX_VERSION}",
    }


def discover(args, env, devdir, cmake, ninja):
    source = source_inputs(env, args.out)
    host = host_inputs(devdir, cmake, ninja, env)
    epoch = args.epoch if args.epoch is not None else source["epoch"]
    version = args.version or output(GIT, "describe", "--always", "--tags", "--dirty", env=env)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+_-]*", version):
        raise ReproductionError(f"invalid release version: {version!r}")
    if not 0 <= epoch <= 0xffffffff:
        raise ReproductionError("archive epoch is outside the gzip timestamp range")
    options = {**CORE_OPTIONS, "K_VERSION": version}
    system, arch = "macos", host["arch"]
    extra = {}
    if args.profile == "cloud":
        system, arch = "linux", LINUX_ARCHES[host["arch"]]
        extra["onnx_archive"] = onnx_archive(args.onnx_package, arch)
        options.update(K_BUILD_CORDIS_HOST="ON", K_BUILD_TCP_READ_PROVIDER="ON",
                       K_ONNXRUNTIME_ROOT="<dependencies>/onnxruntime")
    else:
        options.update(CMAKE_OSX_ARCHITECTURES=arch, CMAKE_OSX_DEPLOYMENT_TARGET=MACOS_DEPLOYMENT_TARGET)
    archives = native_archives(args.deps) if args.profile == "bundled" else {}
    if archives:
        options["K_BUILTIN_LIBS"] = "all"
        options["K_BUILTIN_INSTALLED_ONLY"] = "ON"
        options["SDL_REVISION"] = "SDL-" + archives["k_sdl"]["url"].rsplit("/", 1)[-1]
    return {
        "schema": 1,
        "profile": args.profile,
        "source": source,
        "dependencies": {
            prefix: identity({p: entry for p, entry in source["files"].items() if p.startswith(prefix + "/")})
            for prefix in VENDOR_ROOTS
        },
        "host": host,
        "native_archives": archives,
        **extra,
        "build": {
            "generator": "Ninja",
            "options": options,
            "compile_flags": ["-ffile-prefix-map=<source>=.", "-ffile-prefix-map=<build>=./build",
                              "-ffile-prefix-map=<dependencies>=./dependencies"],
            "link_flags": [f"-fuse-ld={host['tools']['ld']['path']}"],
        },
        "archive": {"format": "tar.gz", "epoch": epoch,
                    "name": f"klang-{version}-{system}-{arch}-{args.profile}"},
        "environment": env,
    }


def first_difference(expected, actual, path):
    if expected == actual:
        return None
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in sorted(expected.keys() | actual.keys()):
            name = f"{path}/{key}"
            if key not in expected:
                return {"path": name, "expected": None, "actual": "present"}
            if key not in actual:
                return {"path": name, "expected": "present", "actual": None}
            diff = first_difference(expected[key], actual[key], name)
            if diff:
                return diff
    return {"path": path, "expected": expected, "actual": actual}


def snapshot(source, manifest, epoch, env):
    source.mkdir()
    tracked = []
    for name, entry in manifest.items():
        path = source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, path)
        path.chmod(entry["mode"])
        os.utime(path, (epoch, epoch))
        if file_digest(path) != entry["sha256"]:
            raise ReproductionError(f"input changed during capture at source/files/{name}")
        if entry["tracked"]:
            tracked.append(os.fsencode(name))
    # The packager uses git ls-files as its inventory. Preserve that list
    # locally while keeping any new compiler sources present for development.
    subprocess.run([GIT, "init", "--quiet", "--template="], cwd=source, env=env, check=True)
    subprocess.run([GIT, "--literal-pathspecs", "-c", "core.autocrlf=false",
                    "add", "--force", "--pathspec-from-file=-", "--pathspec-file-nul"],
                   cwd=source, env=env, input=b"\0".join(tracked) + b"\0", check=True)


def run_step(log, *args, cwd, env, capture=False):
    start = time.monotonic()
    with log.open("wb") as stream:
        stream.write((shlex.join(map(str, args)) + "\n").encode())
        stream.flush()
        run = subprocess.run(args, cwd=cwd, env=env,
                             stdout=subprocess.PIPE if capture else stream, stderr=stream)
        if capture:
            stream.write(b"\n--- stdout ---\n")
            stream.write(run.stdout)
    if run.returncode:
        raise ReproductionError(f"{log.name} failed with status {run.returncode}; see {log}")
    return {"seconds": round(time.monotonic() - start, 3)}, run.stdout


def cmake_flags(flags):
    # CMake preserves double-quoted arguments in *_FLAGS when generating
    # the compiler probe and Ninja rules.
    return " ".join(json.dumps(flag, ensure_ascii=False) for flag in flags)


def payload_files(root):
    files = {}
    for path, _, directory, mode, size in entries(root):
        rel = path.relative_to(root).as_posix()
        files[rel] = {"mode": mode}
        if not directory:
            files[rel].update(size=size, sha256=file_digest(path))
    return files


def native_sources(lane, archives, epoch):
    sources = {}
    for name, entry in archives.items():
        dest = lane / "dependencies" / name
        dest.mkdir(parents=True)
        private_archive = dest / "pinned.tar"
        shutil.copyfile(entry["path"], private_archive)
        if file_digest(private_archive) != entry["sha256"]:
            raise ReproductionError(f"native dependency changed during capture at {name}")
        with tarfile.open(private_archive) as stream:
            stream.extractall(dest, filter="data")
        private_archive.unlink()
        children = list(dest.iterdir())
        if len(children) != 1 or not children[0].is_dir():
            raise ReproductionError(f"{name} must contain one root directory")
        for path in (children[0], *children[0].rglob("*")):
            if not path.is_symlink():
                os.utime(path, (epoch, epoch))
        sources[name] = children[0]
    return sources


def onnx_sdk(lane, archive, epoch):
    root = lane / "dependencies" / "onnxruntime"
    root.mkdir(parents=True)
    private_archive = root / "onnxruntime.nupkg"
    shutil.copyfile(archive["path"], private_archive)
    if file_digest(private_archive) != archive["sha256"]:
        raise ReproductionError("ONNX Runtime archive changed during capture")
    native = f"runtimes/linux-{archive['arch']}/native"
    headers = "build/native/include/"
    with zipfile.ZipFile(private_archive) as sdk:
        files = {
            "LICENSE": "LICENSE",
            "ThirdPartyNotices.txt": "ThirdPartyNotices.txt",
            f"{native}/libonnxruntime.so": "lib/libonnxruntime.so",
            f"{native}/libonnxruntime_providers_shared.so": "lib/libonnxruntime_providers_shared.so",
        }
        for name in sdk.namelist():
            if name.startswith(headers) and re.fullmatch(r"\w+\.h", name[len(headers):]):
                files[name] = "include/" + name[len(headers):]
        for suffix, relative in files.items():
            path = root / relative
            path.parent.mkdir(exist_ok=True)
            with sdk.open(suffix) as source, path.open("wb") as output:
                shutil.copyfileobj(source, output)
            os.utime(path, (epoch, epoch))
    private_archive.unlink()
    return root


def package_lane(lane, inputs, jobs):
    lane.mkdir()
    source, build = lane / "source", lane / "build"
    name = inputs["archive"]["name"]
    epoch = inputs["archive"]["epoch"]
    env = {**inputs["environment"], "SOURCE_DATE_EPOCH": str(epoch)}
    snapshot(source, inputs["source"]["files"], epoch, env)
    dependencies = native_sources(lane, inputs["native_archives"], epoch)
    tools = {name: value["path"] for name, value in inputs["host"]["tools"].items()}
    # Prepare both paths independently while giving paths embedded by Clang a
    # reproducible spelling across checkout and build directories.
    flags = cmake_flags([f"-ffile-prefix-map={source}=.", f"-ffile-prefix-map={build}=./build",
                         f"-ffile-prefix-map={lane / 'dependencies'}=./dependencies"])
    options = {
        **inputs["build"]["options"],
        "CMAKE_MAKE_PROGRAM:FILEPATH": tools["ninja"],
        "CMAKE_C_COMPILER:FILEPATH": tools["cc"],
        "CMAKE_CXX_COMPILER:FILEPATH": tools["cxx"],
        "CMAKE_AR:FILEPATH": tools["ar"],
        "CMAKE_RANLIB:FILEPATH": tools["ranlib"],
        "CMAKE_LINKER:FILEPATH": tools["ld"],
        "CMAKE_C_FLAGS": flags,
        "CMAKE_CXX_FLAGS": flags,
        "CMAKE_EXE_LINKER_FLAGS": cmake_flags(inputs["build"]["link_flags"]),
    }
    if "sdk" in inputs["host"]:
        options["CMAKE_OSX_SYSROOT"] = inputs["host"]["sdk"]["path"]
    if inputs["profile"] == "cloud":
        dependencies["onnxruntime"] = onnx_sdk(lane, inputs["onnx_archive"], epoch)
        options["K_ONNXRUNTIME_ROOT"] = dependencies["onnxruntime"]
        for kind in ("SHARED", "MODULE"):
            options[f"CMAKE_{kind}_LINKER_FLAGS"] = options["CMAKE_EXE_LINKER_FLAGS"]
    if inputs["profile"] == "bundled":
        options.update({
            "CMAKE_OBJC_COMPILER:FILEPATH": tools["cc"],
            "CMAKE_OBJCXX_COMPILER:FILEPATH": tools["cxx"],
            "CMAKE_OBJC_FLAGS": flags,
            "CMAKE_OBJCXX_FLAGS": flags,
            **{f"FETCHCONTENT_SOURCE_DIR_{dep.upper()}": path for dep, path in dependencies.items()},
        })
    steps = {}
    steps["configure"], _ = run_step(
        lane / "configure.log", tools["cmake"], "-S", source, "-B", build, "-G", "Ninja",
        *(f"-D{k}={v}" for k, v in options.items()), cwd=lane, env=env,
    )
    build_cmd = (tools["cmake"], "--build", build, "--config", "Release", "--parallel", str(jobs))
    steps["build"], _ = run_step(lane / "build.log", *build_cmd, cwd=lane, env=env)
    steps["warm-build"], _ = run_step(lane / "warm-build.log", *build_cmd, cwd=lane, env=env)
    staged = lane / "package" / name
    package_options = ("--cloud", "--cc", tools["cc"]) if inputs["profile"] == "cloud" else ()
    steps["package"], _ = run_step(lane / "package.log", tools["python"], source / "scripts/package_release.py",
                                   "--build", build, "--out", staged, *package_options, cwd=lane, env=env)
    packaged = staged / "pkg"
    if inputs["profile"] in ("core", "cloud") and packaged.exists():
        raise ReproductionError(f"the {inputs['profile']} payload has compiled-in packages at {packaged}")
    if inputs["profile"] == "bundled":
        for package in ("gfx", "physics"):
            if not (packaged / package / "link-cc.rsp").is_file():
                raise ReproductionError(f"the bundled payload is missing the link closure of {package}")
        for license in ("physics/box3d/LICENSE", "gfx/sdl/LICENSE.txt", "gfx/nuklear/LICENSE"):
            if not (staged / "licenses" / license).is_file():
                raise ReproductionError(f"the bundled payload is missing licenses/{license}")
    archive = lane / f"{name}.tar.gz"
    steps["archive"], _ = run_step(lane / "archive.log", tools["python"], source / "scripts/archive_release.py",
                                   staged, archive, cwd=lane, env=env)
    files = payload_files(staged)
    write_json(lane / "payload.json", files)
    groups = {}
    packages = {}
    for path, record in files.items():
        if "size" in record:
            group, _, leaf = path.partition("/")
            groups[group] = groups.get(group, 0) + record["size"]
            if group == "pkg":
                package, _, subpath = leaf.partition("/")
                sizes = packages.setdefault(package, {"archives_bytes": 0, "other_bytes": 0})
                sizes["archives_bytes" if subpath.startswith("lib/") else "other_bytes"] += record["size"]
    stats = {
        "archive": archive.relative_to(lane.parent).as_posix(),
        "archive_sha256": file_digest(archive),
        "download_bytes": archive.stat().st_size,
        "payload_sha256": identity(files),
        "installed_bytes": sum(file.get("size", 0) for file in files.values()),
        "installed_groups_bytes": groups,
        "builtin_packages_bytes": packages,
        "build_tree_bytes": sum(p.stat().st_size for p in build.rglob("*") if p.is_file() and not p.is_symlink()),
        "native_source_tree_bytes": sum(p.stat().st_size for directory in dependencies.values()
                                        for p in directory.rglob("*") if p.is_file() and not p.is_symlink()),
        "steps": steps,
    }
    return stats, files


def smoke_lane(lane, inputs, files, stats):
    # An installed k must work with both of its configured trees gone.
    detached_source = (lane / "source").rename(lane / "source-built")
    (lane / "build").rename(lane / "build-built")
    if (lane / "dependencies").exists():
        (lane / "dependencies").rename(lane / "dependencies-built")
    install = lane / "install"
    name = inputs["archive"]["name"]
    archive = lane.parent / stats["archive"]
    with tarfile.open(archive) as archive_file:
        archive_file.extractall(install, filter="data")
    diff = first_difference(files, payload_files(install / name), "install")
    if diff:
        raise ReproductionError(f"extracted payload differs at {lane.name}/{diff['path']}: {diff}")
    compiler = install / name / "k"
    smoke = lane / "smoke"
    smoke.mkdir()
    private_home = smoke / "k-home"
    private_home.mkdir()
    smoke_env = {
        **inputs["environment"],
        "SOURCE_DATE_EPOCH": str(inputs["archive"]["epoch"]),
        "K_HOME": str(private_home),
    }
    macos = "sdk" in inputs["host"]
    sample = install / name / SAMPLE
    cordis = smoke / "cordis-import.k"
    cordis.write_text(
        'import cordis;\n'
        'fn main() {\n'
        '    let host = cordis::open("application.json", "application.lock");\n'
        '    defer cordis::close(host);\n'
        '}\n',
        encoding="utf-8",
    )
    native = smoke / "tour"
    expected = (detached_source / EXPECTED).read_bytes()
    # The staged macOS runtime objects use the system driver and the SDK
    # selected by DEVELOPER_DIR, as in the packaging environment.
    cc = "/usr/bin/clang" if macos else inputs["host"]["tools"]["cc"]["path"]
    for step, args in (
        ("version", (compiler, "version")),
        ("check", (compiler, "check", "--offline", sample)),
        ("cordis-check", (compiler, "check", "--offline", cordis)),
        ("jit", (compiler, "run", "--offline", sample)),
        ("native-build", (compiler, "build", "--offline", "--cc", cc, "-o", native, sample)),
        ("native-warm", (compiler, "build", "--offline", "--cc", cc, "-o", native, sample)),
        ("native-run", (native,)),
    ):
        stats["steps"][step], stdout = run_step(lane / f"{step}.log", *args,
                                               cwd=smoke, env=smoke_env, capture=True)
        if step in ("jit", "native-run") and stdout != expected:
            raise ReproductionError(f"{step} output differs from {EXPECTED}; see {lane / (step + '.log')}")
        if step == "native-build":
            if macos:
                log = lane / "native-build.log"
                if b"using packaged runtime object" not in log.read_bytes():
                    raise ReproductionError(f"the installed build did not use its runtime object; see {log}")
                stats["packaged_runtime_used"] = True
            stats["tour_cold_cache"] = cache_stats(lane, "tour-cold", compiler, smoke, smoke_env)
    if macos:
        bad_release = smoke / "corrupt-runtime"
        bad_release.mkdir()
        bad_compiler = Path(shutil.copy2(compiler, bad_release / "k"))
        (bad_release / "stdlib").symlink_to(compiler.parent / "stdlib", target_is_directory=True)
        seeds = bad_release / "native-runtime"
        shutil.copytree(compiler.parent / "native-runtime", seeds)
        with (seeds / f"{MACOS_DEPLOYMENT_TARGET}.o").open("ab") as damaged:
            damaged.write(b"corrupt")
        fallback = smoke / "tour-fallback"
        fallback_env = {**smoke_env, "K_HOME": str(smoke / "fallback-home")}
        log = lane / "native-fallback.log"
        stats["steps"]["native-fallback"], _ = run_step(
            log, bad_compiler, "build", "--offline", "--cc", cc, "-o", fallback, sample, cwd=smoke, env=fallback_env)
        diagnostics = log.read_bytes()
        if b"compiling the runtime" not in diagnostics or b"using packaged runtime object" in diagnostics:
            raise ReproductionError(f"the corrupt runtime did not fall back to local compilation; see {log}")
        stats["steps"]["native-fallback-run"], actual = run_step(
            lane / "native-fallback-run.log", fallback, cwd=smoke, env=fallback_env, capture=True)
        if actual != expected:
            raise ReproductionError("the locally compiled fallback output differs from the sample")
    if (private_home / "pkg").exists():
        raise ReproductionError(f"the isolated smoke test created a package cache at {private_home / 'pkg'}")
    stats["tour_cache"] = cache_stats(lane, "tour", compiler, smoke, smoke_env)
    stats["tour_executable_bytes"] = native.stat().st_size
    stats["tour_prune"] = prune_cache(lane, "tour", compiler, smoke, smoke_env)
    if inputs["profile"] == "cloud":
        stats["steps"]["supervisor-help"], _ = run_step(
            lane / "supervisor-help.log", install / name / "k-inference-supervisor", "--help",
            cwd=smoke, env=smoke_env)
        stats["steps"]["gateway"], _ = run_step(
            lane / "gateway.log", inputs["host"]["tools"]["python"]["path"],
            detached_source / "test/cordis_gateway.py", "--installed", install / name,
            cwd=smoke, env=smoke_env)
    if inputs["profile"] == "bundled":
        packages = smoke / "native-packages.k"
        packages.write_text(NATIVE_SMOKE, encoding="utf-8")
        smoke_env = {**smoke_env, "K_HOME": str(smoke / "package-home")}
        private_home = Path(smoke_env["K_HOME"])
        binary = smoke / "native-packages"
        package_build = (compiler, "build", "--offline", "--cc", cc, "-o", binary, packages)
        for step, command in (
            ("packages-jit", (compiler, "run", "--offline", packages)),
            ("packages-build", package_build),
            ("packages-warm", package_build),
            ("packages-run", (binary,)),
        ):
            stats["steps"][step], stdout = run_step(lane / f"{step}.log", *command,
                                                   cwd=smoke, env=smoke_env, capture=True)
            if step in ("packages-run", "packages-jit") and stdout != b"native packages ready\n":
                raise ReproductionError(f"{step} failed to use the packaged native components")
            if step == "packages-build":
                stats["package_cold_cache"] = cache_stats(lane, "packages-cold", compiler, smoke, smoke_env)
        package_cache = cache_stats(lane, "packages", compiler, smoke, smoke_env)
        if (private_home / "pkg").exists() or package_cache["groups_bytes"]["native_packages"]:
            raise ReproductionError("bundled programs used a fetched or locally rebuilt native package")
        stats["package_cache"] = package_cache
        stats["package_executable_bytes"] = binary.stat().st_size
        stats["package_prune"] = prune_cache(lane, "packages", compiler, smoke, smoke_env)


def cache_stats(lane, label, compiler, cwd, env):
    _, stdout = run_step(lane / f"{label}-cache.log", compiler, "cache", "stats", "--json",
                         cwd=cwd, env=env, capture=True)
    stats = json.loads(stdout)
    stats.pop("home", None)
    return stats


def prune_cache(lane, label, compiler, cwd, env):
    _, stdout = run_step(lane / f"{label}-prune.log", compiler, "cache", "prune", "--budget",
                         "0", "--json", cwd=cwd, env=env, capture=True)
    report = json.loads(stdout)
    report.pop("home", None)
    if not report["budget_met"] or report["remaining_bytes"]:
        raise ReproductionError(f"{label} left managed cache bytes after its programs exited")
    report["after_prune"] = cache_stats(lane, f"{label}-pruned", compiler, cwd, env)
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, required=True, help="a new directory for builds, archives, and reports")
    ap.add_argument("--version", help="embedded version (default: git describe)")
    ap.add_argument("--profile", default="core", choices=("core", "bundled", "cloud"), help="release profile")
    ap.add_argument("--deps", type=Path, default=ROOT / "build/_deps",
                    help="directory holding the pinned native archives for the bundled profile")
    ap.add_argument("--onnx-package", type=Path, help="pinned ONNX Runtime .nupkg for the Linux cloud profile")
    ap.add_argument("--serial", action="store_true", help="run the independent lanes one at a time")
    ap.add_argument("--epoch", type=int, help="archive timestamp (default: commit timestamp)")
    ap.add_argument("--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 2) // 2)),
                    help="Ninja jobs per independent build")
    ap.add_argument("--lock", type=Path, help="require the same pinned inputs as a prior inputs.json")
    args = ap.parse_args()
    if sys.version_info < (3, 12):
        ap.error("the verifier requires Python 3.12 or later")
    if args.profile == "cloud":
        if sys.platform != "linux" or platform.machine() not in LINUX_ARCHES:
            ap.error("the cloud profile requires Linux arm64 or x64")
        if not args.onnx_package:
            ap.error("the cloud profile requires --onnx-package")
        args.onnx_package = args.onnx_package.resolve()
    elif sys.platform != "darwin":
        ap.error("the core and bundled profiles require macOS")
    elif args.onnx_package:
        ap.error("--onnx-package requires the cloud profile")
    if args.jobs < 1:
        ap.error("--jobs must be positive")
    args.out = args.out.resolve()
    args.deps = args.deps.resolve()
    cmake, ninja = (shutil.which(tool) for tool in ("cmake", "ninja"))
    if not cmake or not ninja:
        ap.error("CMake and Ninja are required on PATH")
    env = {
        "PATH": os.pathsep.join(dict.fromkeys((str(Path(cmake).parent), str(Path(ninja).parent), SYSTEM_PATH))),
        "LC_ALL": "C",
        "TZ": "UTC",
        "ZERO_AR_DATE": "1",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": os.devnull,
    }
    devdir = None
    if sys.platform == "darwin":
        devdir = str(Path(os.environ.get("DEVELOPER_DIR") or output("/usr/bin/xcode-select", "-p")).resolve())
        env.update(DEVELOPER_DIR=devdir, MACOSX_DEPLOYMENT_TARGET=MACOS_DEPLOYMENT_TARGET)
    if "HOME" in os.environ:
        env["HOME"] = os.environ["HOME"]
    if args.out == ROOT or args.out in ROOT.parents:
        ap.error("--out must name a new output directory")
    args.out.mkdir(parents=True, exist_ok=False)
    workers = 1 if args.serial else 2
    report = {"schema": 1, "profile": args.profile, "status": "failed", "jobs_per_lane": args.jobs,
              "concurrent_lanes": workers, "lanes": {}}
    try:
        inputs = discover(args, env, devdir, cmake, ninja)
        report["inputs_sha256"] = identity(inputs)
        write_json(args.out / "inputs.json", inputs)
        if args.lock:
            expected = json.loads(args.lock.read_text(encoding="utf-8"))
            diff = first_difference(expected, inputs, "inputs")
            if diff:
                report["difference"] = diff
                raise ReproductionError(f"pinned inputs differ at {diff['path']}")
        print(f"Building two {inputs['host']['arch']} {args.profile} releases in {args.out}", flush=True)
        with ThreadPoolExecutor(max_workers=workers) as pool:
            builds = {name: pool.submit(package_lane, args.out / name, inputs, args.jobs) for name in ("a", "b")}
            files = {}
            for name, build in builds.items():
                report["lanes"][name], files[name] = build.result()
        actual = discover(args, env, devdir, cmake, ninja)
        diff = first_difference(inputs, actual, "inputs") or first_difference(files["a"], files["b"], "payload")
        a, b = (report["lanes"][name] for name in ("a", "b"))
        if not diff and a["archive_sha256"] != b["archive_sha256"]:
            diff = {"path": "archive/sha256", "expected": a["archive_sha256"], "actual": b["archive_sha256"]}
        if diff:
            report["difference"] = diff
            raise ReproductionError(f"independent releases differ at {diff['path']}")
        with ThreadPoolExecutor(max_workers=workers) as pool:
            smoke = [pool.submit(smoke_lane, args.out / name, inputs, files[name], report["lanes"][name])
                     for name in ("a", "b")]
            for run in smoke:
                run.result()
        report["status"] = "passed"
        print(f"Both releases match: {a['archive_sha256']} ({a['download_bytes']} download bytes, "
              f"{a['installed_bytes']} installed bytes)")
    except (ReproductionError, OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        report["error"] = str(error)
        print(f"reproduce_core: {error}", file=sys.stderr)
    finally:
        write_json(args.out / "report.json", report)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
