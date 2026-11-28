# Changelog

## v0.1.0-preview.1 (2026-11-28)

First public preview release of the **K programming language** (`klang`) compiler, runtime, standard library, tooling, and documentation.

### Highlights

- **Compiler & Runtime**:
  - Region- and stack-backed value/arena compilation targeting C (`clang`, `gcc`, MSVC) with TinyCC (`k run`) in-process execution where enabled.
  - Difference-logic bounds-check elimination (BCE), inlining (`-O0` through `-O3`), stack-top caching (`CacheTops`), and relative reference encoding.
  - Added maximum optimization and native codegen flags in `k` and `k build`:
    - `-O3`: aggressive inlining (`nc=24`, `ncu=144`) and C `-O3` (`/O2 /Oi /Ot` on MSVC).
    - `--lto`: ThinLTO on Clang (`-flto=thin`), `-flto` on GCC, and `/GL` + `/LTCG` on MSVC.
    - `--cpu=native`: host CPU tuning (`-mcpu=native` on Apple Silicon, `-march=native` elsewhere).
  - Runtime & codegen optimizations preserving full memory safety and bounds checks:
    - `K_LIKELY` / `K_UNLIKELY` branch hints on `K_IDX`, `k_rf`, `k_uleb_write`, `k_zig_read`/`k_zig_write`, and `K_E_RELOFF` range checks.
    - Fast 1-byte (`< 128`) and 2-byte (`< 16384`) inline LEB128/zigzag encode/decode paths with cold slow-path fallback.
    - Single unsigned comparison for 16-bit and 32-bit relative offset range validation.
    - Tail-call propagation elision (`k_rf` skip on compatible tail calls) and type-disjoint `SyncReach` checks avoiding spurious stack-top flushes across unrelated globals.
- **Standard Library & Tooling**:
  - Core modules under `stdlib/` (filesystem/OS I/O, JSON, formatting, collections, arenas, `slot_pool`, networking/HTTP, math).
  - `k format` formatter, `k lsp` Language Server Protocol backend, `k fetch` / `k cordis` package and provider tooling.
  - Tree-sitter grammar (`tree-sitter/`) and VS Code extension (`vscode/klang-language.vsix`, extension version `0.4.0`).

### Benchmarks (Extreme Safe Rust Baseline)

Paired interleaved runs comparing K (`-O3 --lto --cpu=native`, full bounds checks enabled) against safe expert Rust (`opt-level = 3`, `lto = "fat"`, `codegen-units = 1`, `panic = "abort"`, `RUSTFLAGS="-C target-cpu=native"`, `rustc 1.98.1`, commit `48a229ceaefd4985c50990b14116b6d856af0985`) across all 10 manifest workloads (20 size cases, `--no-jit`, 15 measured pairs + 3 warmup per size) on `macOS-27.2.1-arm64-arm-64bit · Apple M6`:

#### Series Aggregates (`Rust / K` geometric mean with 95% bootstrap CI; `> 1.0` means K is faster)

| Series | K-first order | Rust-first order |
| --- | ---: | ---: |
| **overall (all workloads)** | **1.163 [1.154, 1.173]** | **1.177 [1.168, 1.185]** |
| matched score | 1.006 [0.997, 1.014] | 1.011 [0.998, 1.023] |
| matched holdout | 1.001 [0.977, 1.024] | 1.023 [1.006, 1.041] |
| idiomatic holdout | 1.329 [1.304, 1.359] | 1.359 [1.337, 1.380] |
| idiomatic score | 1.822 [1.809, 1.834] | 1.828 [1.818, 1.839] |

#### Per-Workload Median Times & Ratios (`Rust / K`)

| Workload | Series | Size | K-first (K ms / Rust ms / Ratio) | Rust-first (K ms / Rust ms / Ratio) |
| --- | --- | ---: | ---: | ---: |
| sum | matched score | 1000000 | 2.863 / 3.055 / 1.061 [1.021, 1.097] | 3.089 / 3.080 / 1.064 [1.025, 1.100] |
| sum | matched score | 16000000 | 31.516 / 33.290 / 1.056 [1.049, 1.063] | 31.225 / 32.672 / 1.048 [1.039, 1.057] |
| calc | matched score | 100000 | 26.342 / 23.600 / 0.897 [0.893, 0.901] | 26.151 / 23.428 / 0.896 [0.889, 0.903] |
| calc | matched score | 400000 | 100.223 / 89.941 / 0.898 [0.895, 0.901] | 100.321 / 90.087 / 0.898 [0.896, 0.900] |
| graph-csr | matched score | 100000 | 10.592 / 11.385 / 1.096 [1.072, 1.119] | 10.145 / 11.034 / 1.095 [1.079, 1.114] |
| graph-csr | matched score | 500000 | 56.304 / 62.762 / 1.106 [1.087, 1.124] | 62.683 / 72.146 / 1.135 [1.095, 1.173] |
| logscan | matched score | 100000 | 1.934 / 1.843 / 0.981 [0.934, 1.025] | 1.977 / 1.941 / 0.966 [0.890, 1.044] |
| logscan | matched score | 2000000 | 12.488 / 12.428 / 0.978 [0.958, 1.002] | 13.250 / 13.738 / 1.015 [0.978, 1.047] |
| blur-rows | matched holdout | 1024 | 4.639 / 4.721 / 1.056 [0.985, 1.134] | 4.648 / 5.140 / 1.045 [0.982, 1.112] |
| blur-rows | matched holdout | 2048 | 15.013 / 14.796 / 1.015 [0.974, 1.067] | 15.535 / 16.579 / 1.039 [1.002, 1.074] |
| sexp-owned | idiomatic holdout | 20000 | 81.961 / 104.435 / 1.296 [1.255, 1.342] | 79.534 / 106.837 / 1.367 [1.342, 1.391] |
| sexp-owned | idiomatic holdout | 100000 | 405.322 / 542.608 / 1.363 [1.337, 1.398] | 419.895 / 581.225 / 1.351 [1.316, 1.384] |
| lru-open | matched holdout | 2000000 | 33.268 / 34.121 / 1.048 [1.005, 1.096] | 31.475 / 33.090 / 1.045 [1.028, 1.058] |
| lru-open | matched holdout | 8000000 | 216.979 / 178.108 / 0.960 [0.909, 1.000] | 124.726 / 126.041 / 1.018 [1.001, 1.035] |
| file-copy | matched holdout | 1048576 | 1.924 / 1.921 / 0.988 [0.910, 1.078] | 1.488 / 1.447 / 0.973 [0.923, 1.026] |
| file-copy | matched holdout | 8388608 | 4.804 / 4.578 / 0.945 [0.912, 0.976] | 4.258 / 4.304 / 1.020 [0.979, 1.062] |
| respond-dto | idiomatic score | 100000 | 35.204 / 70.502 / 1.992 [1.953, 2.022] | 29.507 / 58.514 / 2.001 [1.981, 2.028] |
| respond-dto | idiomatic score | 400000 | 109.615 / 219.295 / 2.000 [1.997, 2.003] | 108.969 / 220.791 / 2.023 [2.017, 2.030] |
| word-count | idiomatic score | 500000 | 15.570 / 24.325 / 1.562 [1.549, 1.578] | 16.084 / 25.072 / 1.561 [1.539, 1.585] |
| word-count | idiomatic score | 4000000 | 121.804 / 215.167 / 1.769 [1.737, 1.807] | 121.629 / 213.745 / 1.768 [1.749, 1.789] |

Reproduce with:
```sh
python3 bench/run_bench.py paired --exe dist/k-bench/k --cc /usr/bin/clang --rustc /path/to/rustc-1.98.1 --extreme-rust --k-flags="-O3,--lto,--cpu=native" --no-jit --reps 15 --warmup 3 --first k
python3 bench/run_bench.py paired --exe dist/k-bench/k --cc /usr/bin/clang --rustc /path/to/rustc-1.98.1 --extreme-rust --k-flags="-O3,--lto,--cpu=native" --no-jit --reps 15 --warmup 3 --first rust
```

### Release Archives

Tagged `v0.1.0-preview.1` triggers `.github/workflows/release.yml` to publish GitHub Pre-release archives (`linux-x86_64` core/bundled/cloud, `linux-arm64`, `macos-arm64` core/bundled, `windows-x86_64`) with `SHA256SUMS`.

### Known Preview Limitations

- Language surface, ABI details, and standard library APIs remain pre-`1.0` and may change before `v0.1.0`.
- Optional native media/GPU layers (audio, gfx, physics, ui) are excluded from the core release profile and skipped in hosts without those layers or a GPU device.
- WASI targets require `WASI_SDK_PATH` and `wasmtime` (not validated on hosts without those tools installed).
- TinyCC in-process JIT (`k run`) uses `-O0`-equivalent JIT codegen and requires building with bundled TinyCC.
