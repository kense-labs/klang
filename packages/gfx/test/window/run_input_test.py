#!/usr/bin/env python3
# Adapted for K.
"""Check gfx mouse/focus setters against an actual hidden SDL window.

Requires a display and GPU, so this complements the regular headless K
fixture rather than running in the main suite. It does not test physical
mouse confinement or application switching; those still need manual checks.

  python packages/gfx/test/window/run_input_test.py --exe build/k

Runs against a K checkout, whose scripts/toolchain.py it uses: the
source checkout, or what K_REPO names.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

PKG = Path(__file__).resolve().parents[2]
K_REPO = Path(os.environ.get("K_REPO", PKG.parents[1]))
sys.path.insert(0, str(K_REPO / "scripts"))
import toolchain as tc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", help="K compiler whose gfx library to test")
    parser.add_argument("--sdl-include", type=Path, help="SDL3 include directory for that build")
    args = parser.parse_args()
    tc.setup_console()
    k = tc.find_klang(args.exe)
    cc = tc.test_cc("native")
    links = tc.gfx_link(k, cc)
    if not links:
        print("the selected compiler has no gfx library", file=sys.stderr)
        return 1
    output = PKG / "build" / "gfx-window-input"
    output.mkdir(parents=True, exist_ok=True)
    exe = output / ("gfx_input" + tc.EXE_SUFFIX)
    include = args.sdl_include
    if include is None:
        binary_dir = Path(k).resolve().parent
        candidates = (binary_dir / "_deps/k_sdl-src/include",
                      binary_dir.parent / "_deps/k_sdl-src/include")
        include = next((path for path in candidates if path.is_dir()), None)
        if include is None:
            parser.error("SDL headers not found in the build directory. Pass --sdl-include.")
    flags = [f"/I{include}"] if cc.style == "msvc" else [f"-I{include}"]
    ok, log = cc.compile(Path(__file__).with_name("gfx_input.c"), exe,
                         opt=2, extra=flags, libs=links, log=output / "compile.log")
    if not ok:
        print(log, file=sys.stderr)
        return 1
    env = dict(os.environ)
    env.pop("K_GFX_HEADLESS", None)
    return subprocess.run([str(exe)], env=env, timeout=30).returncode


if __name__ == "__main__":
    raise SystemExit(main())
