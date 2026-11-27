"""Verify K's WASI command target with WASI SDK and Wasmtime."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


def verify(k, clang, wasmtime, native):
    repository = Path(__file__).resolve().parent.parent
    assert native, "a native C compiler is required; pass --native-cc"
    with TemporaryDirectory(prefix="k-wasi-") as temp:
        root = Path(temp)
        tools = {**os.environ, "K_HOME": str(root / "home")}

        def build(source, *args, target="wasm32-wasip1", cc=clang):
            return subprocess.run([k, "build", "--offline", "--target", target, "--cc", cc,
                                   source, *args], cwd=root, env=tools, capture_output=True, timeout=90)

        def run(module, *args, preopen=False):
            options = ["--dir", "."] if preopen else []
            return subprocess.run([wasmtime, "run", *options, module, *args], cwd=root,
                                  capture_output=True, timeout=25)

        def success(source, expected, *, preopen=False):
            module = root / f"{source.stem}.wasm"
            result = build(source, "-o", module)
            assert result.returncode == 0, result
            assert module.read_bytes()[:8] == b"\0asm\x01\0\0\0", module
            result = run(module, preopen=preopen)
            assert result.returncode == 0 and result.stdout == expected, result

        entry = root / "hello.k"
        entry.write_text('import os;\nfn main() { print("WASM ", args()[1]); }\n', encoding="utf-8")
        result = build(entry, target="wasm32-wasi")
        assert result.returncode == 0, result
        module = entry.with_suffix(".wasm")
        original = module.read_bytes()
        assert original.startswith(b"\0asm\x01\0\0\0")
        result = run(module, "hello")
        assert result.returncode == 0 and result.stdout == b"WASM hello\n", result
        failed = build(entry, "--include", "missing-wasi-header.h")
        assert failed.returncode and module.read_bytes() == original, failed

        for directory, name in (
            ("codegen", "packed_call_abi"), ("codegen", "owned_callables"),
            ("codegen", "relative_composite_equality"), ("codegen", "async_scope_resize"),
            ("stdlib", "stdlib_slice_pool_model"), ("stdlib", "stdlib_os_dirs"),
            ("stdlib", "stdlib_os_replace"),
        ):
            source = repository / "test" / directory / f"{name}.k"
            expected = (repository / "test" / "expected" / f"{name}.out").read_bytes()
            success(source, expected, preopen=name.startswith("stdlib_os_"))

        region = root / "capacity.k"
        region.write_text('''fn main() {
    var values: i64[>..] = [5];
    let first = &values[0];
    for i in 1..4096 { values.push(i); }
    assert(first == 5);
    print(values.len);
    values.resize(1152921504606846976, 1);
}
''', encoding="utf-8")
        result = build(region, "--stack-reserve", "64K")
        assert result.returncode == 0, result
        result = run(region.with_suffix(".wasm"))
        assert result.returncode == 1 and result.stdout == b"4096\n", result
        assert b"data stack overflow" in result.stderr, result

        workers = repository / "test" / "threads" / "thread_cap.k"
        failed = build(workers, "-o", root / "unsupported.wasm")
        assert failed.returncode and b"does not support worker threads" in failed.stderr, failed
        assert not (root / "unsupported.wasm").exists()

        # Use a real sidecar-declared package. Its scalar call and packed
        # { 32-bit data pointer, i64 length } slices exercise the C ABI.
        package = root / "cmath2"
        shutil.copytree(repository / "test" / "packages" / "pkgs" / "cmath2", package)
        interop = root / "interop.k"
        interop.write_text('''import cmath2 from "./cmath2";
fn main() {
    print(cmath2::square(7));
    let a: i32[3] = [1, 2, 3];
    let b: i32[3] = [4, 5, 6];
    print(cmath2::dot(a, b));
}
''', encoding="utf-8")
        module = root / "interop.wasm"
        binary = root / "interop-native"

        def expect_interop(square):
            expected = f"{square}\n32\n".encode()
            result = build(interop, "-o", module)
            assert result.returncode == 0, result
            result = run(module)
            assert result.returncode == 0 and result.stdout == expected, result
            result = build(interop, "-o", binary, target="native", cc=native)
            assert result.returncode == 0, result
            result = subprocess.run([binary], cwd=root, env=tools, capture_output=True, timeout=25)
            assert result.returncode == 0 and result.stdout == expected, result

        # Alternate targets in one K_HOME, then confirm that a warm rebuild
        # reuses each target's own package objects rather than recompiling them.
        expect_interop(49)
        objects = sorted((root / "home").glob("build/pkg/local/*/*/cmath2-*.o"))
        wasm_objects = [p for p in objects if p.read_bytes()[:4] == b"\0asm"]
        native_objects = [p for p in objects if p.read_bytes()[:4] != b"\0asm"]
        assert len(wasm_objects) == 1 and len(native_objects) == 1, objects
        assert any("wasm32-wasip1" in p.name for p in wasm_objects[0].parents), wasm_objects[0]
        mtimes = {p: p.stat().st_mtime_ns for p in objects}
        expect_interop(49)
        assert all(p.stat().st_mtime_ns == mtimes[p] for p in objects), objects

        c_source = package / "src" / "cmath2.c"
        changed = c_source.read_text(encoding="utf-8").replace("x * x;", "x * x + 1;", 1)
        assert changed != c_source.read_text(encoding="utf-8")
        c_source.write_text(changed, encoding="utf-8")
        expect_interop(50)

        # A failed final link publishes nothing over the previous module.
        before = module.read_bytes()
        broken = interop.read_text(encoding="utf-8").replace(
            "fn main()", "extern fn wasi_missing_symbol() -> i32;\nfn main()", 1).replace(
            "print(cmath2::square(7));", "print(wasi_missing_symbol());", 1)
        interop.write_text(broken, encoding="utf-8")
        failed = build(interop, "-o", module)
        assert failed.returncode and module.read_bytes() == before, failed


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--k", required=True, type=Path)
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--wasmtime", required=True, type=Path)
    parser.add_argument("--native-cc", help="native C compiler (default: cc, clang or cl on PATH)")
    options = parser.parse_args()
    native = options.native_cc or shutil.which("cc") or shutil.which("clang") or shutil.which("cl")
    verify(options.k.resolve(), options.clang.resolve(), options.wasmtime.resolve(), native)
    print("WASI command target and static C package interoperability passed")
