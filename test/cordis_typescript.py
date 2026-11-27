"""Build typed TypeScript guest blocks and run them without Node.js."""
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

PROGRAM = '''import helper;
import os;

fn main() {
    /* using "typescript" /* nested comment */ still a comment */
    let using = 21;
    let answer, problem = using "typescript" (left: i64 = using) -> i64 {|
enum Offset { Delta = 21 }
export const answer: bigint = left + BigInt(Offset.Delta);
|};
    print(answer, " ", problem.len);

    let text, te = using "typescript" (input: text = "你好") -> text {|
export const answer: string = input + " {}";
|};
    print(text, " ", te.len);
    let binary, be = using "typescript" (input: bytes = "\\0\\xff") -> bytes {|
const expanded: Uint8Array = new Uint8Array([5, ...input, 9]);
export const answer: Uint8Array = expanded.subarray(1);
|};
    print(binary[0], " ", binary[1], " ", binary[2], " ", be.len);

    let empty, ee = using "typescript" () -> bytes {|
export const answer = new Uint8Array(0);
|};
    print(empty.len, " ", ee.len);
    let low, le = using "typescript" () -> i64 {|
export const answer: bigint = -(1n << 63n);
|};
    print(low, " ", le.len);
    let failed, trace = using "typescript" () -> i64 {|
enum Mode { Fail = 1 }
function example(): bigint { throw new Error("from guest"); }
export const answer = example();
|};
    if failed != 0 || trace.len == 0 { exit(11); }
    write_stderr(trace);
    let big, overflow = using "typescript" () -> i64 {|
export const answer = 1n << 63n;
|};
    print(big, " ", overflow);
    let numeric, wrong = using "typescript" () -> i64 {|
export const answer = 42;
|};
    print(numeric, " ", wrong);
    let absent, missing = using "typescript" () -> i64 {|
const answer = 42n;
|};
    print(absent, " ", missing);
    let invalid, utf = using "typescript" (input: text = "\\xff") -> text {|
export const answer = input;
|};
    print(invalid.len, " ", utf);
    let surrogate, encoding = using "typescript" () -> text {|
export const answer = "\\ud800";
|};
    print(surrogate.len, " ", encoding);
    let a, ae = helper_value();
    let b, be2 = helper_value();
    print(a, " ", b, " ", ae.len + be2.len);
}
'''

HELPER = '''fn helper_value() -> i64, u8[>..] {
    return using "typescript" () -> i64 {|
let count: bigint = 0n;
export const answer = ++count;
|};
}
'''

EXPECTED = '''42 0
你好 {} 0
0 255 9 0
0 0
-9223372036854775808 0
0 TypeScript answer is outside i64
0 TypeScript answer has the wrong type
0 TypeScript block did not export answer
0 TypeScript text binding is not UTF-8
0 TypeScript answer is not UTF-8
1 1 0
'''


def check(driver, compiler, transform, runtime, typescript):
    with TemporaryDirectory(prefix="k-cordis-typescript-") as tmp:
        root = Path(tmp).resolve()
        transform, runtime, typescript = [Path(shutil.copyfile(file, root / Path(file).name))
                                           for file in (transform, runtime, typescript)]
        manifest, lock = root / "cordis.json", root / "cordis.lock"
        entry, helper = root / "program.ext", root / "helper.ext"
        entry.write_text(PROGRAM, encoding="utf-8")
        helper.write_text(HELPER, encoding="utf-8")
        provider = {"service": "source-transform", "version": 1, "library": transform.name,
                    "inputs": {"runtime": runtime.name, "compiler": typescript.name}}
        config = {"version": 2, "selections": {"source-transform": "ts"}, "providers": {"ts": provider}}
        env = dict(os.environ)
        env.pop("K_HOME", None)
        no_node = {**env, "PATH": str(root / "empty-path")}
        # The native linker on Unix only needs system programs. The runner
        # uses full paths on both platforms, including during check.
        build_env = {**env, "PATH": "/usr/bin:/bin"} if os.name != "nt" else env

        def relock():
            manifest.write_text(json.dumps(config), encoding="utf-8")
            lock.write_bytes(subprocess.check_output([compiler, "cordis", "resolve", "source-transform",
                                                     "--manifest", manifest], env=env, timeout=30))

        def invoke(action, *args):
            process = subprocess.run([driver, action, "--compiler", compiler, "--manifest", manifest,
                                      "--lock", lock, "--root", root, "--input", helper.name, entry.name, *args],
                                     cwd=root, env=no_node if action == "check" else build_env,
                                     capture_output=True, timeout=60)
            return process, json.loads(process.stdout)

        relock()
        checked, diagnostics = invoke("check")
        assert checked.returncode == 0 and diagnostics == [], (checked, diagnostics)
        bundle = root / "bundle"
        built, diagnostics = invoke("build", "-o", bundle)
        assert built.returncode == 0 and diagnostics == [], (built, diagnostics)
        receipt = json.loads((bundle / "source-build.json").read_text())
        assert set(receipt["modules"]) == {"program.k", "helper.k", "cordis_typescript_guest_v1.k"}
        native = next((bundle / "assets/typescript").glob("runtime.*"))
        assert native.read_bytes() == runtime.read_bytes()
        assert receipt["artifacts"][f"typescript/{native.name}"] == sha256(runtime.read_bytes()).hexdigest()
        license_file = bundle / "assets/typescript/quickjs-license.txt"
        assert b"Fabrice Bellard" in license_file.read_bytes()
        emitted = bundle / "assets/typescript/block-0.js"
        source_map = json.loads(emitted.with_suffix(".js.map").read_text())
        assert "Offset" in emitted.read_text() and source_map["version"] == 3 and source_map["mappings"]
        assert receipt["cordis"] == json.loads(lock.read_text())

        moved = bundle.rename(root / "moved bundle")
        elsewhere = root / "elsewhere"
        elsewhere.mkdir()
        executable = moved / ("program.exe" if os.name == "nt" else "program")
        run = subprocess.run([executable], cwd=elsewhere, env=no_node, capture_output=True, timeout=20)
        assert run.returncode == 0 and run.stdout.decode().replace("\r\n", "\n") == EXPECTED, run
        line = next(i for i, text in enumerate(PROGRAM.splitlines(), 1) if "throw new Error" in text)
        assert f'at example (program.ext:{line})'.encode() in run.stderr and b'from guest' in run.stderr, run.stderr

        bad_source = 'const text = "😀你好"; export const answer: bigint = ;'
        entry.write_text('fn main() {\n    let value, error = using "typescript" () -> i64 {|\n' +
                         bad_source + '\n|};\n    print(value, error);\n}\n', encoding="utf-8")
        bad = root / "syntax-error"
        failed, diagnostics = invoke("build", "-o", bad)
        assert failed.returncode and not bad.exists(), (failed, diagnostics)
        issue, = diagnostics
        assert issue["message"].startswith("TypeScript:"), issue
        assert (issue["file"], issue["line"], issue["col"]) == (
            str(entry), 3, len(bad_source[:-1].encode("utf-8")) + 1), issue

        entry.write_text('fn main() {\n    let value, error = using "typescript" (bad: i64 = "wrong") -> i64 {|\n'
                         'export const answer = bad;\n|};\n    print(value, error);\n}\n', encoding="utf-8")
        failed, diagnostics = invoke("check")
        assert failed.returncode and any(d.get("file") == str(entry) and d.get("line") == 2
                                         for d in diagnostics), (failed, diagnostics)

        with typescript.open("ab") as file:
            file.write(b"changed")
        failed, diagnostics = invoke("check")
        assert failed.returncode and diagnostics[0]["cordis"]["code"] == "stale_lock", (failed, diagnostics)
        del provider["inputs"]["compiler"]
        relock()
        failed, diagnostics = invoke("check")
        assert failed.returncode and "tracked compiler input" in diagnostics[0]["message"], (failed, diagnostics)


if __name__ == "__main__":
    check(*(str(Path(arg).resolve()) for arg in sys.argv[1:]))
