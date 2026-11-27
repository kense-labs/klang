"""Run a locked multi-module project with the optional Cordis runtime."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


def check(runner, k, frontend, project, compiler):
    with TemporaryDirectory(prefix="k-cordis-ts-project-") as tmp:
        env = {**os.environ, "PATH": str(Path(tmp) / "empty-path")}
        root = Path(tmp).resolve()
        app = root / "app"
        (app / "data").mkdir(parents=True)
        main = app / "main.ts"
        helper = app / "data/value.ts"
        frontend, project, compiler = [Path(shutil.copyfile(file, root / Path(file).name))
                                       for file in (frontend, project, compiler)]
        helper.write_text('export type Result = bigint;\nenum Part { Offset = 20 }\n'
                          'export const number: Result = BigInt(Part.Offset) + 1n;\n', encoding="utf-8")
        (app / "message.mjs").write_text('export const say = (value) => console.log("答案", value);\n', encoding="utf-8")
        manifest, lock = root / "cordis.json", root / "cordis.lock"
        graph = {"version": 2, "selections": {"typescript-project": "qjs", "typescript-frontend": "tsc"},
                 "providers": {"qjs": {"service": "typescript-project", "version": 1, "library": project.name,
                                       "requires": {"typescript-frontend": {"minVersion": 1, "maxVersion": 1}}},
                               "tsc": {"service": "typescript-frontend", "version": 1, "library": frontend.name,
                                       "inputs": {"compiler": compiler.name}}}}

        def relock():
            manifest.write_text(json.dumps(graph), encoding="utf-8")
            lock.write_bytes(subprocess.check_output([k, "cordis", "resolve", "typescript-project",
                                                     "--manifest", manifest], env=env, timeout=30))

        def run(source, entry="main.ts"):
            main.write_text(source, encoding="utf-8")
            return subprocess.run([runner, "--manifest", manifest, "--lock", lock, "--root", app, entry],
                                  capture_output=True, cwd=root, env=env, encoding="utf-8", timeout=25)

        relock()
        program = '''import type { Result } from "./data/value.js";
import { number } from "./data/value.js";
import { say } from "./message.mjs";
const dynamic = await import("./data/../data/value.ts");
const result: Result = await Promise.resolve(number + dynamic.number);
say(result);
console.log(dynamic === (await import("./data/value.js")));
const handled = Promise.reject(new Error("caught later"));
await Promise.resolve();
handled.catch(() => console.error("handled"));
'''
        result = run(program)
        assert result.returncode == 0 and result.stdout == "答案 42\ntrue\n" and result.stderr == "handled\n", result

        helper.write_text('enum Mode { Fail = 1 }\nexport async function reject() {\n'
                          '  await Promise.resolve(Mode.Fail);\n  throw new Error("async dependency");\n}\n',
                          encoding="utf-8")
        result = run('import { reject } from "./data/value.js";\nawait reject();\n')
        assert result.returncode and 'async dependency' in result.stderr and 'data/value.ts:4)' in result.stderr, result

        bad = 'const word = "😀你好"; export const bad: bigint = ;'
        helper.write_text(bad, encoding="utf-8")
        result = run('import "./data/value.ts";\n')
        assert result.returncode and f'data/value.ts:1:{len(bad[:-1].encode("utf-8")) + 1}: TypeScript:' in result.stderr, result

        result = run('await new Promise(() => {});\n')
        assert result.returncode and "top-level await remains pending" in result.stderr, result
        result = run('Promise.reject(new Error("forgotten"));\n')
        assert result.returncode and "Unhandled promise rejection" in result.stderr and "forgotten" in result.stderr, result
        result = run('await import("node:fs");\n')
        assert result.returncode and "unsupported module specifier 'node:fs'" in result.stderr, result
        (root / "outside.ts").write_text('console.log("outside");\n', encoding="utf-8")
        result = run('import "../outside.ts";\n')
        assert result.returncode and not result.stdout and "leaves the project root" in result.stderr, result
        if os.name != "nt":
            (app / "outside.ts").symlink_to(root / "outside.ts")
            result = run('import "./outside.ts";\n')
            assert result.returncode and not result.stdout and "leaves the project root" in result.stderr, result

        with compiler.open("ab") as file:
            file.write(b"changed")
        result = run('console.log("stale lock must stop startup");\n')
        assert result.returncode and not result.stdout and "Cordis tracked input compiler differs" in result.stderr, result


if __name__ == "__main__":
    check(*(str(Path(path).resolve()) for path in sys.argv[1:]))
