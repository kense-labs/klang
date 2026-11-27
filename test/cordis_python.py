"""Build and execute real Python blocks through the optional Cordis transform."""
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
    // using "python" invalid text in a line comment
    /* using "python" invalid /* nested comment */ still a comment */
    let ignored = """using "python" in an ordinary K literal""";
    let using = 20;
    let value, error = using "python" (left: i64 = using + 1, right: i64 = (5 * 4 + 1)) -> i64 {|
import math
with open("guest-ran", "w") as marker:
    marker.write("runtime")
answer = left + math.isqrt(right * right)
|};
    print(value, " ", error.len);

    let label, te = using "python" (text_in: text = "你好") -> text {|
answer = text_in + " {}"
|};
    print(label, " ", te.len);
    let data, be = using "python" (bytes_in: bytes = "\\0\\xff") -> bytes {|
answer = bytes_in[::-1] + b"\\x05"
|};
    print(data[0], " ", data[1], " ", data[2], " ", be.len);

    let low, le = using "python" () -> i64 {|
answer = -(1 << 63)
|};
    print(low, " ", le.len);

    let failed, trace = using "python" () -> i64 {|
raise SystemExit(7)
|};
    if failed != 0 || trace.len == 0 { exit(11); }
    write_stderr(trace);
    let big, overflow = using "python" () -> i64 {|
answer = 1 << 63
|};
    print(big, " ", overflow);
    let boolean, wrong = using "python" () -> i64 {|
answer = True
|};
    print(boolean, " ", wrong);
    let absent, missing = using "python" () -> i64 {|
temporary = 3
|};
    print(absent, " ", missing);
    let a, ae = helper_value();
    let b, be2 = helper_value();
    print(a, " ", b, " ", ae.len + be2.len);
}
'''

HELPER = '''fn helper_value() -> i64, u8[>..] {
    return using "python" () -> i64 {|
counter = globals().get("counter", 0) + 1
answer = counter
|};
}
'''

EXPECTED = '''42 0
你好 {} 0
255 0 5 0
-9223372036854775808 0
0 Python answer is outside i64
0 Python answer has the wrong type
0 Python block did not assign answer
1 1 0
'''


def check(driver, compiler, transform, runtime):
    with TemporaryDirectory(prefix="k-cordis-python-") as tmp:
        root = Path(tmp).resolve()
        transform = Path(shutil.copyfile(transform, root / Path(transform).name))
        runtime = Path(shutil.copyfile(runtime, root / Path(runtime).name))
        manifest, lock = root / "cordis.json", root / "cordis.lock"
        entry, helper = root / "program.ext", root / "helper.ext"
        entry.write_text(PROGRAM, encoding="utf-8")
        helper.write_text(HELPER, encoding="utf-8")
        provider = {"service": "source-transform", "version": 1, "library": transform.name,
                    "inputs": {"runtime": runtime.name}}
        config = {"version": 2, "selections": {"source-transform": "python"}, "providers": {"python": provider}}
        env = dict(os.environ)
        env.pop("K_HOME", None)

        def relock():
            manifest.write_text(json.dumps(config), encoding="utf-8")
            lock.write_bytes(subprocess.check_output([compiler, "cordis", "resolve", "source-transform",
                                                     "--manifest", manifest], env=env, timeout=20))

        def invoke(action, *args):
            process = subprocess.run([driver, action, "--compiler", compiler, "--manifest", manifest,
                                      "--lock", lock, "--root", root, "--input", helper.name, entry.name, *args],
                                     cwd=root, env=env, capture_output=True, timeout=30)
            return process, json.loads(process.stdout)

        relock()
        verified, diagnostics = invoke("check")
        assert verified.returncode == 0 and diagnostics == [], (verified, diagnostics)
        assert not (root / "guest-ran").exists()
        bundle = root / "bundle"
        built, diagnostics = invoke("build", "-o", bundle)
        assert built.returncode == 0 and diagnostics == [], (built, diagnostics)
        assert not (root / "guest-ran").exists()
        receipt = json.loads((bundle / "source-build.json").read_text())
        assert set(receipt["modules"]) == {"program.k", "helper.k", "cordis_python_guest_v1.k"}
        native = next((bundle / "assets/python").glob("runtime.*"))
        assert native.read_bytes() == runtime.read_bytes()
        assert receipt["artifacts"][f"python/{native.name}"] == sha256(runtime.read_bytes()).hexdigest()
        assert len(list((bundle / "assets/python").glob("*.py"))) == 9

        # Code and runtime resolve after moving the bundle and switching cwd.
        moved = bundle.rename(root / "moved bundle")
        elsewhere = root / "elsewhere"
        elsewhere.mkdir()
        executable = moved / ("program.exe" if os.name == "nt" else "program")
        run = subprocess.run([executable], cwd=elsewhere, env=env, capture_output=True, timeout=20)
        assert run.returncode == 0 and run.stdout.decode().replace("\r\n", "\n") == EXPECTED, run
        line = PROGRAM.splitlines().index("raise SystemExit(7)") + 1
        assert f'File "program.ext", line {line}'.encode() in run.stderr
        assert b"SystemExit: 7" in run.stderr and (elsewhere / "guest-ran").read_text() == "runtime"

        entry.write_text('fn main() {\n    let value, error = using "python" () -> i64 {|\n'
                         'answer = "你好" + )\n|};\n    print(value, error);\n}\n', encoding="utf-8")
        bad = root / "syntax-error"
        failed, diagnostics = invoke("build", "-o", bad)
        assert failed.returncode and not bad.exists(), (failed, diagnostics)
        issue, = diagnostics
        assert "Python syntax error" in issue["message"], issue
        assert (issue["file"], issue["line"], issue["col"]) == (
            str(entry), 3, len('answer = "你好" + '.encode("utf-8")) + 1), issue

        entry.write_text('fn main() {\n    let value, error = using "python" (bad: i64 = "wrong") -> i64 {|\n'
                         'answer = bad\n|};\n    print(value, error);\n}\n', encoding="utf-8")
        failed, diagnostics = invoke("check")
        assert failed.returncode and any(d.get("file") == str(entry) and d.get("line") == 2
                                         for d in diagnostics), (failed, diagnostics)

        # The graph lock binds the runtime bytes before activation.
        with runtime.open("ab") as file:
            file.write(b"changed")
        failed, diagnostics = invoke("check")
        assert failed.returncode and diagnostics[0]["cordis"]["code"] == "stale_lock"
        provider.pop("inputs")
        relock()
        failed, diagnostics = invoke("check")
        assert failed.returncode and "tracked runtime input" in diagnostics[0]["message"]


if __name__ == "__main__":
    check(*(str(Path(arg).resolve()) for arg in sys.argv[1:]))
