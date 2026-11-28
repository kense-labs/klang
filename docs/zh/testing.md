<a id="compiler-and-runtime-tests"></a>
# 编译器与运行时测试

> **已弃用的语言特性：** `reusable` 和 `reusable[]`。请改用
> [`slot_pool<T>`](stdlib.md#slot_pool) 管理槽位，以及
> [`slice_pool<T>`](stdlib.md#slice_pool) 管理连续片段。
> 下文中的内建行为与历史示例对于现有代码仍然适用；移除工作已单独规划，将在剩余迁移缺口补齐后进行。

<details>
<summary>目录</summary>

- [运行测试套件](#running-the-suite)
- [K 与 Rust 成对基准测试](#paired-k-and-rust-benchmark)
- [测试夹具布局](#fixture-layout)
- [配置与后端](#configurations-and-backends)
- [预期输出与诊断信息](#expected-output-and-diagnostics)
- [附加编译器检查](#additional-compiler-checks)
- [测试夹具审计](#fixture-audit)

</details>

<a id="running-the-suite"></a>
## 运行测试套件

确定性语言服务器测试会在文档变更后直接通过 `Server::Handle` 发送保存的调用层次结构（call hierarchy）项。消息之间不会运行后台分析。使用以下命令构建并运行：

```sh
cmake -S . -B build-lsp -DK_BUILD_LSP_TESTS=ON
cmake --build build-lsp --config Debug
ctest --test-dir build-lsp -C Debug -R '^(lsp_|cordis_)' --output-on-failure
```

Cordis 测试会在替换提供方（provider）期间保持其存活，并测试激活失败、ABI 不匹配以及提供方状态释放。独立原生宿主通过与 LSP 相同的服务生命周期来交换已链接的提供方与共享库。第二个宿主在不依赖动态加载器的情况下验证已链接路径。当 Python 可用时，`lsp_cordis_protocol` 会针对已构建的 `k lsp` 可执行文件检查清单（manifest）选择与关闭流程。它会将选定的产物、受追踪输入以及图摘要（graph digest）与 Python 的 SHA-256 进行对比，在清单重排后检查图等价性，并验证无效依赖路径会在任何提供方启动之前被报告。它还会检查切换失败时是否保留活动图。
`cordis_source_transform` 针对普通 K 编译器运行可选构建驱动（build driver）。它检查多模块转换、保留的二进制产物、UTF-8 诊断映射、过期锁、ABI 拒绝、无效输出路径、输入漂移以及向同一目录的并发发布。
[可选构建驱动](design/embedded_languages.md#optional-build-driver)
当启用 `K_BUILD_CORDIS_PYTHON=ON` 时，`cordis_python_guest` 会测试真实的 CPython 提供方及其单独锁定的运行时桥接。它涵盖带类型的输入与结果、UTF-8 诊断信息、异常、溢出、离线构建以及重定位后的包（bundle）。[CPython 适配器](design/embedded_languages.md#optional-cpython-adapter)

当启用 `K_BUILD_CORDIS_TYPESCRIPT=ON` 时，`cordis_typescript_guest` 使用锁定的编译器与运行时输入对真实的 TypeScript 代码块进行检查和构建。它涵盖 `enum` 降级（lowering）、精确的 BigInt 与类型化数组转换、UTF-8 错误列号、重映射后的堆栈行号，以及在空搜索路径下运行重定位后的可执行文件。[TypeScript 适配器](design/embedded_languages.md#optional-typescript-adapter)
`cordis_typescript_project` 使用单独锁定的前端与执行服务测试静态与动态 ESM 导入、顶层 `await`、Promise 拒绝、映射后的依赖错误以及项目根目录模块加载。该测试在空可执行文件搜索路径下运行。
[TypeScript 项目运行器](design/functions_and_typescript_runtime.md#optional-project-runner)

`cordis_locked_build` 在启用 `--offline` 的 `k check` 与 `k build` 中使用同一份锁文件。它检查 ABI 拒绝、分析期间的变更，以及出错时保留旧输出。它会从生成的可执行文件中提取图结构，并在 macOS 可执行文件缓存可用时检查缓存复用与失效。
`cordis_tcp_receive` 测试通过 `k run` 与 `k build` 运行回环（loopback）TCP 接收。任务在等待原生接收之前会让出执行权。该测试涵盖接收字节、EOF、TCP 重置，以及在启动前、提供方替换期间和作用域退出时的取消操作。该夹具在替换期间延迟完成，以验证在另一个 K 任务运行时，套接字和旧提供方仍保持存活。
`cordis_application_stream` 使用 `k cordis resolve` 生成启动锁和替换锁。一个 K 应用程序打开可选宿主，在两种后端中处理真实的 TCP 数据并检查其活动图。它检查队列策略是否在流提供方之前启动，并拒绝超出其限制的套接字请求。过期锁、ABI 不匹配以及提供方启动期间变更的输入会触发逆序回滚。替换后的提供方在旧套接字仍处于挂起状态时为新请求提供服务。队列已满时会停止读取，且已退役的流提供方及其策略会在泵（pump）和读取器释放后按逆序停止。

在启用可选 ONNX Runtime SDK 后，`cordis_onnx_inference` 会在 TinyCC 与原生构建中测试真实的 CPU 提供方。具有不同权重的冻结 ONNX 图用于检查精确输出、输入拷贝、请求限制、取消操作以及失败和成功的模型替换。工作区检查同时追踪重叠的 arena 区域以及已关闭请求句柄的任务所持有的资源。第二个夹具将回环 TCP 载荷送入作为依赖项选定的推理服务。[构建配方](design/cordis_microkernel.md#locked-cpu-inference)
`cordis_inference_gateway` 使用真实的 TCP 客户端驱动两种 K 后端。它验证拆分输入、具有精确数值的多输出帧、超大请求、准入限制和截止时间。一个受控门控测试提供方在替换和取消期间暂停，使测试能够检查已完成和已取消的请求是否在最后一次回调之后释放已退役的代（generation）。该夹具还测试了省略分配器遥测信息的原始推理 ABI 前缀。
[线协议](design/cordis_microkernel.md#loopback-inference-gateway)

使用暂存的发布版和可选原生库，通过 Python 3.11 或更高版本测量网关性能。例如在 macOS 上运行：

```sh
python bench/inference_gateway.py --install dist/klang \
  --host build/libk_cordis_application.dylib \
  --provider build/libk_cordis_onnx.so \
  --runtime /path/to/onnxruntime/lib/libonnxruntime.dylib \
  --out build/inference_gateway.json
```

该基准使用全新的 `K_HOME`，验证每次预测结果，并交替执行 TinyCC 与原生运行。默认模型宽度为 2、512 和 32768，可通过 `--widths` 配置。另一项额外工作负载会预热最小模型并将其替换为最大模型。报告记录两个模型的摘要和大小、原始延迟、从启动到就绪的时间、顺序回环吞吐量、p95/p99 以及进程峰值 RSS。它还记录分配器快照以及触发替换的请求耗时。

`cordis_tcp_stream` 测试通过 `k run` 与 `k build` 驱动一个双槽流（two-slot stream）。它填满队列，验证在槽位释放前提交计数保持不变，并检查按 FIFO 顺序交付到更小切片中。它涵盖 EOF、缓冲字节后紧跟套接字错误、轮询前取消、接收期间释放读取器以及排空（drain）期间替换提供方。TCP 夹具检查当对端空闲时原生轮询计数保持固定，以及后续载荷会唤醒等待者。

使用 `scripts/package_release.py` 暂存发布版本后，运行 `python scripts/archive_release.py dist/klang dist/klang.tar.gz`。如需 ZIP 发布包，请使用 `.zip` 后缀。发布工作流会将 `SOURCE_DATE_EPOCH` 设为源码提交时间。归档器使用该时间戳并按路径排序条目，因此相同的打包字节在相同的压缩工具链下可生成相同的归档文件。请分别对比干净构建的载荷，以验证编译器和链接产物。

<a id="rebuilding-core-and-bundled-releases"></a>
### 重新构建核心版与捆绑版发布

在 macOS 上，使用 Python 3.12 或更高版本、CMake、Ninja 以及选定的 Xcode 工具链运行核心发布验证器：

```sh
python3 scripts/reproduce_core.py --out build/repro-core --version 0.0.0-repro
```

该验证器将工作源码捕获到两个全新的源码和 Ninja 构建目录中。`inputs.json` 记录提交（commit）、工作区文件与 vendored 输入、编译器和链接器指纹、SDK 版本和设置摘要、配置选项、归档纪元（epoch）以及打包工具。本地工作区变更也会包含并在记录中标明。传入 `--lock build/repro-core/inputs.json` 可在后续使用新输出目录、相同版本和纪元选项的调用中强制要求完全一致的输入。

使用 `--profile bundled` 可构建包含物理、图形（gfx）、音频和 UI 的版本。`--deps` 接受包含固定版本 `k_box3d.tar.gz`、`k_sdl.tar.gz` 和 `k_nuklear.tar.gz` 文件的目录，或包含相同归档的 CMake FetchContent 缓存。每条通道（lane）会对照包声明检查归档摘要，然后解压各自的源码树。

```sh
python3 scripts/reproduce_core.py --profile bundled --deps build/_deps \
  --out build/repro-bundled --version 0.0.0-repro --jobs 6 --serial
```

该验证器按路径、权限模式（mode）和 SHA-256 对比暂存载荷，然后对比两个归档的摘要。它会将配置好的源码树和构建树移开，解压其归档，并在空 `K_HOME` 环境下运行 `k check`、`k run` 和 `k build`。JIT 与原生运行结果均需匹配导览示例（tour）的预期输出。首次 macOS 原生构建必须复用随发布版选定系统 Clang 与 SDK 打包的运行时对象。带有损坏对象的隔离副本必须在本地重新构建并产生相同输出。另一项单独的离线检查会导入已打包的 Cordis 应用模块。`report.json` 按组件记录下载与安装字节数、冷构建与热构建耗时、修剪（pruning）前后的缓存字节数，以及失败时首个出现差异的输入或载荷条目。捆绑版冒烟测试在没有显示器或音频设备的情况下测试已链接的包与 JIT。每次构建都有各自的日志和载荷清单。使用 `--serial` 可在同一台宿主机上按顺序逐条计时运行各通道。

发布工作流在 macOS arm64 上运行此检查，并将经过验证的核心归档与各平台捆绑归档一并附带发布。

<a id="rebuilding-the-linux-cloud-release"></a>
### 重新构建 Linux 云端发布版

在带 glibc 的 Linux 上，使用 Python 3.12 或更高版本、CMake、Ninja、Clang 和 binutils。提供一个本地的 `Microsoft.ML.OnnxRuntime` 1.24.4 `.nupkg`。验证器会检查其固定的 SHA-256，并为每次构建独立解压匹配的 arm64 或 x64 SDK。云端归档中包含宿主、引擎、提供方、预构建网关以及 ONNX Runtime 许可证与声明。[ONNX Runtime 包](https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime/1.24.4)

```sh
python3 scripts/reproduce_core.py --profile cloud \
  --onnx-package /path/to/onnxruntime.1.24.4.nupkg \
  --out build/repro-cloud --version 0.0.0-repro --jobs 6 --serial
```

两个解压后的归档都必须使用随附的原生网关和 TinyCC 产生相同的预期预测结果。回环测试还涵盖无效请求、准入限制和截止时间。输入记录包括 Linux 发行版、glibc 和工具链版本。对比发布版摘要时，请在相同的固定构建镜像上重新构建。
发布工作流在 Linux arm64 和 x64 上运行云端检查，并保留经过验证的归档及其清单和日志。[网关协议](design/cordis_microkernel.md#loopback-inference-gateway)

<a id="testing-the-gateway-memory-limit"></a>
### 测试网关内存限制

使用一个配备 Python 3 及目标归档兼容系统库的一次性 Linux 容器。在启用 cgroup v2 的 Docker 宿主机上使用 `--network none --memory=96m --memory-swap=96m` 运行。以只读方式挂载代码仓库和解压后的云端发布版。这两次调用会检查启动门控、预算内的成功推理与替换，以及其中间张量触发内核 OOM killer 的替换场景。第二次调用请使用 Python 解释器作为容器入口点（entrypoint），以便观察者在网关退出后读取 cgroup 的 `oom_kill` 计数器。

```sh
python3 test/cordis_gateway.py --installed /install --memory-budget-mib 96
python3 test/cordis_gateway_memory.py --install /install --memory-budget-mib 96
```

[网关部署内存预算](design/cordis_microkernel.md#linux-deployment-memory-budget)

对于受监管的恢复（supervised recovery），请按部署指南中的说明准备一个独立的空 worker cgroup。测试驱动器与监管器（supervisor）运行在该 worker 组之外。将其上限设为 96 MiB，禁用交换分区（swap），启用 `memory.oom.group`，并委派进程迁移权限。在包含解压后的云端发布版和 Python 3 的一次性环境中运行以下命令：

```sh
python3 test/cordis_gateway_supervisor.py --install /install \
  --worker-cgroup /sys/fs/cgroup/cordis/worker
```

该夹具检查真实发生 OOM 后在同一端口上的稳定预测、跨监管器重启的持久抑制、普通的 `SIGKILL`，以及当稳定模型自身耗尽内存时的重试上限。[受监管 OOM 回滚](design/cordis_microkernel.md#supervised-oom-rollback)

在常规编译器构建后运行 `python test/run_tests.py --profile baseline --cc native --require-clang`。CI 会在 Windows、macOS 和 Linux 上运行此命令。

运行器会同时执行 `-j`/`--jobs` 个作业，默认每个逻辑处理器一个，上限为 32。其线程大部分时间都在等待 K、C 编译器以及它们启动的程序。每个作业会缓冲自身输出，并按作业顺序打印这些缓冲区。因此无论作业数如何，日志都保持可比性。`-j1` 会在单线程上按顺序运行全部内容。

同一夹具的多次运行是顺序执行的，因为一次运行可能会写出另一个运行会看到的文件（例如 `stdlib_os.k`）。K-in-K 链与示例运行器会最先启动。示例运行器（`samples/run_samples.py`）是一个使用相同作业数的独立进程。`-O0` 自举链是耗时最长的作业，整个测试套件的总耗时大致与该链相当。

`--gpu-jobs N` 会限制每个运行器内并发运行的 gfx 程序数，以适配无法处理过多无头（headless）设备的 GPU 驱动。默认无限制。

日志划分为多个区段（解析、往返与类型检查；生成的 C；JIT 运行；`errors_tc/`；示例；……）。每个区段以统计其检查数的行结尾，例如 `== typecheck errors (...): 1668 ok`。除了 ok 行之外，区段打印的任何内容都会在区段标题下方原位显示：失败信息连同触发它的前置输出，以及跳过项。`-v` 还会为每个通过的检查打印一行。在 GitHub Actions 下，这些 ok 行会折叠保存在每个区段的折叠组中，且在打印其他内容前会关闭该折叠组。日志结束后，所有 FAIL 行会连同所属区段标题再次汇总打印；在 GitHub Actions 下，它们会以注解（annotation）形式输出。

每个夹具的编译次数尽可能压缩到满足其检查需求的最少次数。单次运行 `k --roundtrip --check` 会解析夹具、检查其转储（dump）再次解析后是否得到相同转储，并对其进行类型检查。当夹具包含注解时，该次运行会加上 `--bce-test`；对于 `parse-only` 夹具则用 `--parse` 替代 `--check`；对于 `dump-runtime` 夹具则加上 `--dump-file` 以写出转储。每次运行都带有 `--stats`，借此每次通过解析消解（resolution）的运行都会打印 `parsed ok`，从而对 `errors_tc/` 夹具执行单次 `--check` 即可区分解析失败与类型检查失败。在某优化级别同时需要构建 C 并通过 TinyCC 运行程序时，单次 `k -O<n> --jit -o <file>` 即可兼顾两者：它先写出 C，然后在进程内运行程序。

上述大多数运行以及 `errors/` 和 `errors_tc/` 夹具的运行，会与其他夹具共享一个编译器进程。`k --multi-test <flags> a.k b.k ...` 会依次编译每个文件，效果与分别对该文件运行完全相同。它会在每个文件的 stdout 和 stderr 末尾输出一行 `==== k --multi-test: exit <code> <file>`。运行器据此为每个夹具赋予与单独运行完全一致的退出码和输出。启动编译器进程的耗时与检查一个小型夹具大致相当，因此一批 20 个文件只需十分之一的时间即可完成检查。如果某个批次崩溃或中止，其正在编译的文件之前的结果会被保留；该文件及后续文件随后会以单文件单进程的方式运行，从而将崩溃准确归因于对应的夹具。写出转储（`dump-runtime`）、使用 `runtime-define` 宏定义或运行程序（TinyCC）的运行仍然各自使用独立进程。

不链接原生层的夹具所生成的 C 代码，也会在每个优化级别以及转储和 `K_DEBUG` 程序下按批次构建。单次 C 编译器调用会编译每个程序重命名 `main` 后的编译单元，连同一个根据第一个参数运行指定程序的 `main`，并与运行时对象链接一次（`scripts/toolchain.py` 中的 `CC.compile_programs`）。每次程序运行依然是独立进程。当某个批次构建失败时，其中的每个程序会单独构建，从而显示失败原因。对这些程序而言，启动和链接占用了 C 编译器运行的大部分时间，因此一批 20 个构建只需五分之一的时间。`--batch N` 设置送入单个编译器进程的文件数以及打包进单个可执行文件的程序数（默认值为让每个作业都能分到若干批次的数量）；`--batch 1` 则让每个文件拥有自己的进程、每个程序拥有自己的可执行文件。

<a id="paired-k-and-rust-benchmark"></a>
## K 与 Rust 成对基准测试

使用 `scripts/package_release.py` 暂存核心发布版。需要 Python 3.12 或更高版本、Clang，以及固定在 [`bench/paired_workloads.json`](../../bench/paired_workloads.json) 中的 Rust 1.98.1 工具链。运行器使用与 `rustc` 同一工具链下的 Cargo。在 macOS 上，请使用同一份暂存发布版在同一宿主机上连续运行两组序列。请使用系统 Clang 驱动，以便 K 能够配合选定的 SDK 测试其可执行文件缓存。

```sh
rustc_path="$(rustup which --toolchain 1.98.1 rustc)"
python3.12 bench/run_bench.py paired --exe dist/klang/k \
  --cc /usr/bin/clang --rustc "$rustc_path" --first k --out build/paired-k-first
python3.12 bench/run_bench.py paired --exe dist/klang/k \
  --cc /usr/bin/clang --rustc "$rustc_path" --first rust --out build/paired-rust-first
```

清单在测量前即固定了源码摘要、工作负载规模和预期输出。匹配（matched）组得分涵盖整数求和、表达式解析、CSR 图遍历和 CSV 日志扫描。留出（holdout）组涵盖图像模糊、S-表达式解析、LRU 缓存和二进制文件拷贝。JSON 响应生成和词频统计构成惯用（idiomatic）组得分。S-表达式实现采用各自语言自身的分配策略，因此其结果归入惯用留出（idiomatic holdout）组。C++ 参考实现负责生成计算与数据结构输出，Python 独立计算文件夹具摘要和 CSV 统计信息。两个二进制文件都必须匹配每一项预期输出。文件拷贝还会检查结果文件的 SHA-256。

默认情况下，每个规模使用两对预热（warmup）和七对实测（measured）。运行器在每对中交替由哪个可执行文件先启动。它测量包含进程启动和任务执行的总时间，对于 I/O 用例采用预热后的文件系统和常规缓冲写入。`Rust time / K time` 为成对比率的几何平均值，每个规模权重相等，并附带在各规模内部对成对样本重抽样的 95% 自助法（bootstrap）置信区间。在默认样本数下，p95 即为最大值。使用 `--reps` 可运行更长序列。在推广结论前请在不同宿主机上重复验证。

两种原生构建在首次构建时均以空的工程缓存离线运行，在热重建时则使用相同的输入和缓存。K 在 Clang 下使用 `-O2` 并在首次构建时编译其运行时对象。无依赖的 Rust 构建使用随附的标准库和 Cargo，设置 `opt-level = 2` 与单个 codegen unit。两者均保留可执行文件符号。安装目录、独立可执行文件及构建输出按逻辑字节数统计，每棵目录树内的硬链接只计一次。Rust 占用体积反映已安装的 sysroot，包括任何额外组件。Clang 可执行文件和 macOS SDK 分开记录。

每个输出目录包含 `report.md`、`report.json` 和清单副本。JSON 包含单次挂钟时间（wall time）、峰值 RSS、构建命令、磁盘占用、工具指纹，以及单独的 `k run` 编译并运行序列。每次运行请使用新的输出目录。

<a id="fixture-layout"></a>
## 测试夹具布局

测试夹具按类别分组；`run_tests.py` 位于 `test/` 根目录：

| `test/` 下的路径 | 覆盖范围 |
|---|---|
| `syntax/` | 词法分析器、解析器、转储分组、导入与命名空间消解。被导入的辅助模块位于 `syntax/ns/` 和 `syntax/sub/`。`raw_strings_crlf.k` 是唯一按 CRLF 换行符检出的文件（`.gitattributes`）。 |
| `typing/` | 类型、泛型、常量、可选类型收窄（narrowing）、分派结果以及递归返回契约。 |
| `lifetimes/` | 引用根（reference root）、字节视图、借用内容以及在引用存活期间收缩。 |
| `codegen/` | C 名称、求值顺序、表示形式、栈帧/栈布局以及别名（aliasing）。 |
| `optimizer/` | 内联、尾递归与边界检查消除（BCE）。 |
| `runtime/` | 运行时诊断、可复用槽位池与切片池操作、数组大小检查，以及测试释放未使用数据栈内存的原生测试 `runtime_trim_memory.c`。 |
| `storage/` | 相对引用、对象池（pool）与序列化。 |
| `threads/` | 工作线程（worker）、队列、共享全局变量、受限工作线程的中止以及原生运行时生命周期测试 `runtime_threads_lifecycle.c`；`host_exit.c` 是 `host_exit.k` 的 C 宿主，后者是一个由宿主启动、以 `k_exit` 结束并再次启动的库构建（即 `host_exit` 任务）。被导入的辅助模块位于 `threads/spawn/`。 |
| `stdlib/` | 标准库模块。 |
| `k_in_k/` | 用 K 编写的编译器，作为单个多文件自举回归测试：构建并运行三代编译器，检查 C 不动点与第二阶段（stage-2）编译器的重复自检，并对比 JIT 与原生自编译结果。 |
| `audio/` | `audio` 模块：按精确帧数推进的离线 PCM 混音器；对错误采样率、PCM、参数与输出的拒绝；声部（voice）与音效容量及过期句柄；以及线程调用拒绝。 |
| `gfx/` | `gfx` 图形模块：无头渲染、纹理、计算着色器、帧与输入、运行时误用、来自文件与程序内的着色器；着色器与线程调用拒绝（带有 `// error:` 标记的夹具）。程序自身持有其着色器；身旁的着色器文件用于文件形式的 `embed_shader` 和 `--compile-shader`。 |
| `physics/` | `physics` 模块：物理世界、刚体、各类形状与几何体、全部关节类型、查询、事件、录制与回放、运行时误用以及线程调用拒绝。 |
| `ui/` | `ui` 模块：字体、窗口与布局、控件、弹窗与菜单、文本编辑、样式、绘制与输入查询、图表、通过无头 gfx 渲染与输入、按键和鼠标是否属于 ui、运行时误用、该层的误用提示消息以及线程调用拒绝。 |
| `packages/` | 声明了来自 `pkgs/` 下路径源的包的程序，由 `k build` 构建：纯 K 包（`pkg_basic`）、sidecar 下带纯 C 的包（`pkg_c_lib`）、CMake 包（`pkg_cmake_lib`，`native_toolchain.py` 也使用它），以及在 sidecar 中自带 C 代码的程序（`pkg_own_c`；`pkg_float_abi` 测试原生 C 与 TinyCC 之间栈上传参的浮点 ABI）。`pkgs/col` 是 `errors/pkg_collision.k` 导入的包。 |
| `errors/`, `errors_tc/` | 预期的解析器/消解器拒绝与语义拒绝。`errors/ns_alias/` 存放被 `ns_alias_*` 夹具导入的模块，它们本身不是独立夹具。 |
| `errors_tc/recovery/` | 包含多个错误的程序，解析器、消解与检查器会越过这些错误继续检查（`docs/design/language_server.md` §5 D）。按顺序取用时，每个 `// error:` 标记必须是编译器报告的同位置错误消息的子串，且在文本模式和 `--diagnostics json` 下均不得有其他错误。`imported/` 存放被 `imports.k` 导入的模块，本身不是独立夹具。 |
| `lsp/` | 由 `lsp_protocol.py` 通过协议驱动的语言服务器夹具，不作为程序编译。`basics.k` 在 `// lsp:` 行中声明查询与预期答案，并导入 `helper.k`。`complete.k` 以 `p.` 结尾以测试代码补全。`recovery.k` 在两个函数中有错误；`// lsp: error` 行指定它们的位置，查询使用截至目前已检查的代码。`broken_decls.k` 在结构体、枚举和函数内部有错误，外加一个未消解的导入。`exemplar.k` 使用样本检查（exemplar check）测试未调用泛型函数上的查询（`docs/design/language_server.md` §11）。`roots_lib.k` 包含一个错误，并与它的导入者 `roots_app.k` 和 `roots_app2.k` 一同打开和关闭。`source_index.k` 测试初始化器省略默认值或展开 `const for` 的全局变量、匹配 `const for` 分支列表的泛型函数，以及在字面量和注释中包含逗号的调用。`two_a.k` 和 `two_b.k` 都导入 `two_shared.k`；只有后者实例化了其泛型函数。`ufcs_ns.k` 从全局命名空间对带命名空间的类型发起 UFCS 调用，其中一个调用不匹配任何重载。`ns_alias.k` 通过别名命名某个命名空间的声明（`namespace g = geo;`）。`pools.k` 声明了一个根结构体并使用了 `slot_pool` 和 `slice_pool`，那些以池作为首个参数的泛型函数必须作为成员提供补全。`encodings.k` 将名称和错误放在多字节字符之后，用于位置编码检查。`builtins.k` 检查悬停提示、跳转定义、重命名拒绝、签名帮助（包括错误之后以及可选数组 UFCS 调用上）、补全、语义高亮标记（semantic tokens）以及内建函数和数组属性的内联提示（inlay hints）。 |
| `expected/` | 共享的输出与运行时诊断预期结果。 |
| `run_tests.py` | Python 测试运行器。 |
| `toolchain_selection.py` | 运行器选择使用哪个编译器（`scripts/toolchain.py` 中的 `find_klang`）。给定的 `--exe` 路径会按原样转换为绝对路径后使用，即使 `build/` 中存在其他编译器。未指定 `--exe` 时，运行器依次选用 `build/Release/`、`build/RelWithDebInfo/`、`build/` 本身（针对单配置生成器）和 `build/Debug/` 中的第一个可用项，以确保常规运行使用优化版编译器，而断言版构建必须通过 `--exe` 显式指定。如果这些位置都没有编译器，或者 `--exe` 路径不存在，运行会直接终止，而不是回退使用其他编译器。这是一个由主运行器执行的单元测试。 |
| `compiler_roots.cpp` | 根集并（join）定律、弱化反馈、与顺序无关的相等性以及刻意设计的比较投影；在两种原生配置下编译并运行。 |
| `compiler_containers.cpp` | 在随机操作下将编译器的 `small_vector` 和 `flat_map` 与 `std::vector` 和 `std::unordered_map` 对比，覆盖内联/堆存储、扩容与擦除，以及 `flat_map` 对已擦除键的值单元的复用；在两种原生配置下编译并运行。 |
| `resource_paths.py` | Unicode 源码与 C 输出路径、JIT 入口源码资源根目录、带独立或内置运行时的原生可执行文件根目录、重定位，以及保持工作目录不变的文件访问。由主运行器调用。 |
| `cli_paths.py` | 测试命令行与导入路径。对于生成的 C 或 `k build` 可执行文件，`-o` 可以指向尚不存在的目录。包源路径可以是 `"."` 或 `".."`，且 `--include` 路径可以以分隔符开头。未消解的导入必须报告所有搜索过的位置。其他用例包括将目录传给源文件参数或 `embed_file`，以及在旁边没有源码树的情况下运行编译器——此时编译器必须找到其配置好的标准库，并报告 `--gen-runtime-header` 找不到运行时源码。由主运行器调用。 |
| `extern_types.py` | 一个被导入的 extern 接口和 C 适配器，包含未使用和部分使用的函数：切片、引用所指类型和返回类型在 O0/O2 下通过原生 C 和 TinyCC 均可用于头文件，未使用的 extern 无需实现。由主运行器调用。 |
| `extern_dylib.py` | 绑定到测试所构建共享库的 extern 块。每个参数形状都要经过运行时的绑定流程，共享库分别按相对于资源目录的拼写和加载器路径上的裸名称打开，同时覆盖 JIT（无 C 工具链可用）和已构建程序两种情况。生成的 C 在多次编译间保持一致，且仅绑定被调用的成员。缺少库或符号会报出包含所尝试拼写的运行时错误，未使用的成员则两者都不需要。同时声明两种 extern 的包仅为调用其静态成员的程序构建其原生部分。sidecar 中的 `dylibs` 列表将名称映射到各平台打开的拼写，程序的映射优先于包的映射，畸形条目或任何 extern 块均未声明的名称会被拒绝。由主运行器调用。 |
| `runtime_cache.py`, `runtime_cache_cc.c` | 并发工程共享已完成的运行时对象。失败的编译会保留诊断信息且重试可成功。报告会统计不同的缓存组，修剪（pruning）会跳过链接器正在占用的对象，被驱逐的对象会离线重新构建。受控 C 编译器在调试与发布配置下测试两种驱动标志风格。由主运行器调用。 |
| `native_toolchain.py` | CMake 接口包、运行时和程序共用同一套工具链。包含空格和不含空格的缓存路径还会测试响应文件（response-file）参数，在 Windows 上可用时使用 MinGW GCC。由主运行器调用。 |
| `native_cache.py` | 当多个使用同一个带 C 代码包的程序同时首次构建时，该 C 代码只编译一次。其余构建会在该包构建目录的锁上等待，并链接首次构建生成的对象；`runtime_cache_cc.c` 的受控 C 编译器会检查这些对象是否完整。热构建不编译任何内容。两种驱动标志风格均经过测试。由主运行器调用。 |
| `native_rebuild.py` | 测试原生代码何时触发重建。编辑程序的 C 代码会触发 `k build` 和 `k run` 重建；换用另一路径指代同一程序则不会。在不修改 C 代码的情况下添加 extern fn 会重建 JIT 库。其他用例涵盖不同目录下同名的两个 C 文件、并排的 `--debug` 与发布构建，以及库符号表中省略的 extern fn。CMake 测试涵盖 `cmake_options` 变更后的重新配置、在配置阶段报告未定义的 `cmake_target`，以及 C++ 目标。当未发生变化时，生成的 CMake 文件必须保持其时间戳不变，且程序自身的 CMake 目标无论程序如何命名都必须正常生效。由主运行器调用。 |
| `native_builder.py` | 独立编译的 C 代码向 K 构建器追加内容：`u8[>..]&` 参数作为 `klang.h` 的 `k_rref` 传入，并通过来自程序 sidecar C 和 CMake 包目标的 `k_bld_append` 向其中追加字节，分别由 `k build` 构建和 `k run` 在进程内运行。由主运行器调用。 |
| `runtime_io.py` | 由 `k build` 构建以及在支持 JIT 时由 `k run` 运行的程序读取管道标准输入：`read_stdin` 读取每一个字节，其中包括 Windows 文本模式会转译并在此终止的 `\r\n` 和 Ctrl-Z；向已以 `\r` 结尾的构建器执行 `read_line` 会保留该 `\r`，并完整返回包含 NUL 字节的行和 70,000 字节的长行。`read_stdin(out, max)` 每次调用最多追加 `max` 字节（同样跨其 64 KB 缓冲区），当 `max` 为 0 时不读取任何内容。在 POSIX 上，`write_file_atomic` 覆盖文件时会保留其权限。由主运行器调用。 |
| `commit_limit.py` | 仅限 Windows：在将进程内存提交（commit）限制为 100 MB 的作业对象（job object）中运行的程序，将数据栈增长超过该上限。错误消息必须指出提交数据栈时内存不足，而不是说栈超出了保留区（reservation）。由主运行器调用。 |
| `huge_pages.py` | 仅限 Linux：每个数据栈区域的前 2 MB 被标记为禁用透明大页（`/proc/<pid>/smaps` 中的 `nh`），从而使仅持有少量字节的栈只提交普通页而非大页。由主运行器调用。 |
| `exit_race.py` | 主程序与八个工作线程在重定向输出的情况下同时向 stderr 写入并调用 `exit(7)`。由 `k build` 构建的程序连续运行 20 次，每次都必须以该退出码结束，即便在 Windows 上两个线程同时运行 C 库的销毁清理可能会导致崩溃。由主运行器调用。 |

正向夹具在 `test/` 下一级目录中发现；嵌套的导入辅助文件通过其入口程序运行。`k_in_k/` 作为单个自举程序单独处理，而不将其模块发现为独立夹具。请保持跨类别的夹具主文件名（stem）唯一，因为预期结果与生成的构建产物均使用这些名称。运行器会拒绝重复名称。

<a id="wasi-command-target"></a>
## WASI 命令目标

使用 WASI SDK 编译器和 Wasmtime 运行 WebAssembly 集成测试。

```sh
python3 test/wasi.py --k build/k --clang /path/to/wasi-sdk/bin/clang \
  --wasmtime /path/to/wasmtime --native-cc /path/to/native/clang
```

该测试构建命令模块，检查默认与显式输出路径，并运行产物。它验证 32 位布局、闭包环境、任务取消、预打开（preopened）文件操作，以及跨越数组扩容存活的引用。一个真实的 `cmath2` 包检查标量调用和带有 32 位数据指针的紧凑切片。该测试在同一个全新 `K_HOME` 中交替执行原生和 WASI 构建，检查它们各自不同的对象格式，确认热缓存复用与 C 源码失效机制，并检查最终链接失败时是否保留先前的可执行文件。当 `PATH` 上存在 `cc`、`clang` 或 `cl` 时，`--native-cc` 为可选参数。它还会检查数据栈溢出是否会在写入超出容量之前失败。
[WASM 设计](design/wasm.md#wasi-command-target)

<a id="configurations-and-backends"></a>
## 配置与后端

| 配置 | 目的 |
|---|---|
| Debug 版 K 编译器；K `-O0` / 原生 C `-O0` 对比 K `-O2` / 原生 C `-O2` | 对比真实的未优化与优化后可执行文件，包含原生优化器的效果；对照现有夹具检查输出。 |
| K `-O2`，原生 C `-O2`，`K_DEBUG=1`，选定夹具 | 在开启优化的情况下测试受检算术/转换与关键运行时路径。选定范围为 `codegen_exec.k` 以及首行带有 `runtime-debug` 的文件。 |
| 对 `codegen_exec.k` 额外执行 Clang C 构建（原生 `-O1`，发布/调试运行时）；对 `simd_versions.k`、`stdlib_base64.k` 和 `simd_mul_add.k` 执行 Clang 构建（原生 `-O2`，`K_SIMD=2`、`1` 和 `0`）；对 `vec_elemwise.k` 执行 Clang 构建（`K_VEC=1` 和 `0`） | 在不针对每个后端重复全套测试的情况下，为核心代码生成覆盖文件保留第二个 C 前端检查。`simd` 函数的多版本（规范 §7.12）仅在 clang 为 x86-64 构建时存在：其生成的 C 必须包含每个版本，且在各自可构建的每个级别上，程序必须打印与基线相同的输出。`mul_add` 是例外。在 `modes` 模式下运行时，`simd_mul_add` 会报告每种调用的舍入情况：普通函数（arm64 上一次，x86-64 上两次），以及在 `simd` 函数内部的直接调用、`inline fn` 辅助函数、普通辅助函数和函数值。在支持 AVX2 和 FMA 的 CPU 上，直接调用和函数值在 AVX2 与 AVX-512 版本中只舍入一次；辅助函数在 K 内联它们时也只舍入一次，而在不内联任何内容的 K `-O0` 下，它们的舍入方式与普通函数相同。C 代码在两个优化级别都会生成。运行器向操作系统询问 CPU 是否具备这些指令集，在无法询问时接受任一舍入结果。 |
| 在每个测试和每个示例上，通过进程内 TinyCC 后端运行 K `-O0` 与 `-O2`，并在相同的选定夹具上开启 `K_DEBUG=1` | 对照第三个截然不同的 C 实现检查生成的 C，并检查无论由哪个后端构建，程序的行为都保持一致。 |
| 单个 Linux Clang ASan/UBSan 作业 | 对 C++ 编译器、生成的 C、选定的调试运行时用例、所有示例以及直接运行时测试（`runtime_threads_lifecycle.c`、`runtime_trim_memory.c`）进行插桩检查。 |
| K-in-K 自举，两种配置 | 用真实编译器构建第 1 阶段，用第 1 阶段构建第 2 阶段，再用第 2 阶段构建第 3 阶段。构建全部三个可执行文件，对比第 2/第 3 阶段的 C 代码，并在发出和构建第 3 阶段的同时在第 2 阶段中运行两次自检（不动点使第 2 阶段与第 3 阶段成为完全相同的编译器）。基线配置要求宿主/原生 O0 与 O2 之间以及 JIT/原生执行之间产生完全相同的自编译 C 代码；sanitize 配置则对每个原生阶段进行插桩。 |

使用 `python test/run_tests.py --k-in-k-only --exe <klang> --cc native` 仅运行自举测试。常规的 `--profile`、`--no-jit` 和 `--nocgen` 选项同样适用。在没有原生 C 编译器时，它仍会检查源码并在可用时通过 JIT 自编译，但会将原生链报告为已跳过。源码及确切的阶段契约见 [k_in_k/README.md](k_in_k.md)；产物和日志保存在 `build/gen/<profile>/k_in_k/O<level>/`。

sanitizer 作业使用 `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all` 构建 K，然后运行 `python test/run_tests.py --exe <instrumented-klang> --profile sanitize --cc clang`。生成的 C 使用原生 `-O1`，同时为紧凑访问和 C++ 编译器开启 UBSan 对齐检查。ASan 可以观察到 C 的分配，但不知晓 K 自定义虚拟内存 arena 内部的逻辑对象边界。

生成的 C 按 `k -o` 所写的形式构建，并链接到独立的运行时（`docs/implementation.md` §7）。`KlangRuntime`（`scripts/toolchain.py`）会在每次运行时让被测编译器写出一次运行时 C 代码（`--emit-runtime`）。它会针对每种工具链配置（编译器、优化级别、宏定义和额外标志，因此 sanitize 配置的运行时也会被插桩）将该 C 代码编译一次到 `build/gen/runtime/` 中，对于示例则编译到其各自目录中。随后 `CC.compile(..., runtime=...)` 将该对象链接到以此方式构建的每个程序。自举的第 2 和第 3 阶段包含自身的运行时，不链接外部运行时。每次 TinyCC 运行都编译由 `--standalone` 写出的单单元形式；基准测试也构建该形式。

直接回归测试 `test/threads/runtime_threads_lifecycle.c` 检查在工作线程频繁创建与销毁过程中的内存分配、映射和 Windows 句柄，包括未 join 的工作线程、并发/重复等待、生命周期长于父线程的子线程以及中止的受限工作线程；还检查 `k_rt_stop` 结束主程序时是否释放所有分配与映射并恢复错误处理，以及此后程序能否再次启动并结束。`host_exit` 任务为其 C 宿主 `host_exit.c` 构建 `test/threads/host_exit.k`，宿主会启动、使用并结束该程序三次，每轮都使用全新的全局变量和队列，并且有一次在工作线程仍在运行时结束程序（这会触发运行时错误）。直接测试 `test/runtime/runtime_trim_memory.c` 检查释放未使用的栈内存时，Windows 上会解除页面提交（decommit），Linux 和 macOS 上会从常驻集（RSS）中剔除这些页面，同时保留存活的部分页面和其他栈。它还检查被丢弃的页面会被清零、Windows 上的增长会通过每次修剪（trim）移动的提交标记之后的错误处理程序重新提交，以及 macOS 上的增长在修剪前后都不需要运行时处理的缺页故障。两者均在两种配置下运行。常规测试套件还会检查用户可见的工作线程错误诊断信息。

直接回归测试 `test/runtime/runtime_tasks_lifecycle.c` 检查有界轮询（round-robin）驱动、回调重入、完成与错误、启动前取消，以及在外部操作仍持有资源时的关闭。`runtime_task_readiness.c` 回归测试在布防（arm）和等待边界注入完成信号，检查挂起的任务会为协作的同级任务让出空间，验证外部关闭与排空，并测试原始外部任务描述符布局。外部库夹具通过一个带所有权的 K 闭包和确定性完成回调来测试同一套驱动。原生 Cordis 测试检查已退役的提供方在任务排空前是否保持可调用。

`gfx/` 测试用于检验 `packages/gfx` 下的 gfx 包。它们总是会执行解析、类型检查和 C 代码生成。当编译器包含 gfx 层（通过包配置 `K_BUILTIN_LIBS`）时，`k build` 会将测试链接到该层并在 -O0、-O2 及 TinyCC 下运行。它们以无头模式渲染到纹理中，并且只回读像素对齐的结果，因此其输出在每种后端和 GPU 上都一致。没有 GPU 设备的机器（程序打印 `gfx: no GPU device`）以及未构建该层的编译器会将其报告为跳过。Linux CI 在 Mesa 的 lavapipe 上运行这些测试。运行器会设置 `K_GFX_HEADLESS=1`，因此任何测试或示例都不会打开窗口。隐藏的 `--compile-shader` 标志会在每个编译器上（无论是否包含该层）针对 `gfx/probe.frag` 和 `gfx/probe_badset.frag` 进行测试（[附加编译器检查](#additional-compiler-checks)）。

`physics/` 测试用于检验 `packages/physics` 下的包。它们总是生成 C 代码。当编译器配置了该包（`K_BUILTIN_LIBS`）时，`k build` 会链接它们并在 -O0、-O2 和 TinyCC 下运行。Box3D 在跨平台和不同工作线程数量下具有确定性，因此测试会打印舍入后的物理结果并精确比对。这些测试合起来调用了该层的每一个函数。

`ui/` 测试以同样方式检验 gfx 包中的 ui 模块（其 `docs/ui.md`）。`k build` 会链接 ui 层，对于通过 gfx 渲染的测试还会一并链接 gfx 层。一个测试需要其自身类别以及所导入每个模块的层，若其中任一层未构建进编译器则会被跳过。这些测试使用自带输入驱动上下文，并打印返回结果与绘制内容；`ui_render` 通过无头 gfx 绘制，并且仅在 ui 绘制了纯色的位置回读像素。这些测试合起来调用了该层的每一个函数。

每个包自身的 `test/api_check.py` 会将其模块中的函数、结构体和常量与相应的 C 头文件进行对比。这可以捕获两端各自都能成功编译通过的接口不匹配问题。它还会拒绝该层按值传递的如下结构体——即 TinyCC 在 System V x86-64 上会与 C 编译器以不同方式传递的结构体：不超过 16 字节、且包含未对齐字段，或者在一个 8 字节块（eightbyte）中只有浮点数而在另一个中只有整数的结构体。

在配有显示器和 GPU 的机器上运行 `packages/gfx/test/window/run_window_test.py`。它使用本仓库中的 `scripts/toolchain.py`，并可通过 `K_REPO` 选择另一个 K 检出目录。它会带窗口运行展示程序 `gfx_showcase.k`，同时覆盖进程内运行与由找到的每种 C 工具链构建的运行，并在该平台的每种 GPU 驱动上执行。它会检查程序对自身回读的报告，并解码程序保存的截图（位于包的 `build/gfx-window/` 下）以检查细节并在多次运行之间对比。

TinyCC 运行在 sanitizer 下会被跳过，因为此时未插桩的程序运行在已插桩的编译器内部，且在编译器退出时仍持有其运行时分配。后端尚不能运行的测试会报告为跳过而非失败：要么是因为编译器拒绝该程序（`JIT mode does not support`），要么是因为测试首行注明了 `no-jit`。应优先采用可移植的断言而非跳过某后端；数学测试允许超越函数结果存在微小的舍入差异。该后端的剩余局限与可能解决方案见 `docs/design/jit_backend.md`。

<a id="expected-output-and-diagnostics"></a>
## 预期输出与诊断信息

显式指定的编译器必须存在；CI 不会静默跳过它。每个可运行的夹具都需要一个 `expected/<name>.out`，当程序应无输出时也需提供空文件。预期中止的程序还额外需要 `.aborts` 文件，以及包含预期运行时诊断子串的非空 `.stderr` 文件。即使程序本就预期中止，sanitizer 报告依然会导致测试失败。受资源约束的夹具可以在其首行设置限制，例如 `// runtime-define: K_MAX_STACKS=1`。运行器会在每次编译时将其作为 `-D` 传入：编译器会对照程序的静态栈数量检查 `K_MAX_STACKS`，生成的 C 代码会处理其余部分。使用此机制的夹具有：

* `append_destination_storage.k` 检测仅对比输出会遗漏的临时结果栈。
* `cycle_scratch_locals.k` 展示递归的暂存局部变量在每一层都使用相同的少数几个栈。
* `discarded_resizable_results.k` 缩小每个栈的保留空间（`K_STACK_RESERVE`），使得除非释放每个被丢弃的结果，否则其循环就会溢出栈。`guard_flat_storage.k` 对守卫（guard）的条件、其 else 分支以及块的其余部分在数据栈上使用的空间执行相同检查（块的其余部分在发出时不加自身的大括号）。`continue_restores.k` 和 `large_fixed_exits.k` 对 `continue`、`break` 和 `return ... from` 恢复的水位线（watermark）执行相同检查。
* `thread_cap.k` 缩小地址空间预算（`K_STACK_BUDGET`），使 `hardware_threads()` 无论在何处运行都报告其最小值 1。
* `stack_stats.k` 让程序报告其栈使用情况（`K_STACK_STATS`），并通过 `expected/stack_stats.stderr` 进行检查。

`cycle_static_stacks.k` 运行递归环内调用所允许的各种形状（§7.8）；`errors_tc/cycle_temporary_*`、`cycle_construction_*` 和 `cycle_result_*` 夹具则是被拒绝的形状。如果任何程序跨越此类调用持有了数据栈，代码生成会报内部错误而失败。运行器还会检查 `thread_cap.k` 和 `cycle_scratch_locals.k` 的 `--stacks` 报告形状（但不检查具体计数）。对于工作线程不持有任何区域的 `thread_spawn_discarded.k`，它检查该报告不给出线程上限。预期仅在调试模式下中止的夹具会以同样方式设置 `K_DEBUG=1`，从而使其每次运行都是调试构建：`cast_abort_location.k` 和 `overflow_abort_location.k` 检查失败的检查所报告的位置，`thread_contained.k` 检查调试检查是否仅结束受限工作线程。

解析器/消解器错误位于 `test/errors/`；语义错误位于 `test/errors_tc/`，且在失败前必须通过解析和消解（`parsed ok`）。每个源文件声明一行或多行 `// error: <diagnostic substring>`。编译器必须以退出码 1 退出，且每个标记必须出现在以下位置之一：诊断头部、`in const for` 展开上下文行、实例化链中的 `in fn` 行（不含其中的 ` instantiated from <path>:<line>` 部分），或重载候选行上的原因说明。回显的源码行不计入匹配。编译器会跳过可恢复的错误继续检查，因此一个夹具打印的错误可能多于其声明的数目；`test/errors_tc/recovery/` 存放按顺序声明了全部错误的夹具（见上表）。在标记中，应使用具体的拒绝原因及相关的类型/根；省略源码路径、行号和规范章节号。`errors_tc/function_value_chain.k` 刻意写出代码块的文件、行号和列号，因为它检查的正是调用链如何命名代码块。这些内联断言取代了旧的编译器错误 `.stderr` 文件。

夹具以相同方式声明其 `--check` 打印的警告，每条警告对应一行 `// warning: <message substring>`；未声明任何警告的正向夹具必须不打印任何警告。标记与警告行一一配对，每个标记都必须是不同警告行消息的一部分。因此同一条警告打印两次会导致夹具失败，正如遗漏警告或出现未声明的警告一样（一条警告针对源码中所写的节点只打印一次，`docs/implementation.md` §3.12）。同一行上的两个 `&` 若产生相同警告，则会打印两行相同警告并需要两个标记，正如 `branch_reference_copies.k` 中的 `r6`。标记的顺序无关紧要。由于没有行号，标记无法指明警告所在位置，且警告按检查到达节点的顺序打印：被调函数体在其首次被调用时、循环或结构在其检查确定后，而冗余转换则在整个程序之后按行号顺序打印。检查器的改动可能会重新排列这一顺序，但不会改变程序收到的提示内容。拒绝类测试的警告仅在其声明了警告时才会被检查：检查会在遇到错误时停止，因此先打印出哪些警告取决于检查推进到了何处。例如，`cycle_local_store.k` 会在上一行赋值处由该调用揭示的错误之前，先对递归调用的 `&local` 发出警告。

<a id="additional-compiler-checks"></a>
## 附加编译器检查

运行器检查的内容不止退出状态和运行时输出：

| 检查项 | 契约 |
|---|---|
| `lexer_tokens.k` | 精确的词法单元（token）流，包括关键字分类、解码后的字面量和最长匹配标点符号。 |
| 每个正向 K 夹具 | 成功解析、成功初始转储、转储/重解析/转储结果一致，以及类型检查只产生其声明的警告，除非其首行包含 `parse-only`。解析同时还会消解类型名称；仅转储不会。 |
| 首行 `dump-runtime` | 编译并执行转储后的源码，对比原始输出。`control_expression_dump.k` 用此项检查分组语义，仅靠稳定的转储本身无法确立这一点。 |
| 带有 `// bce:elide` 或 `// bce:keep` 的每个夹具 | 运行 `-O1 --check --bce-test`，包括预期中止的回归测试。原生/JIT 的 O0 与 O2 运行也会独立检查程序行为。 |
| `gfx_err_shader_syntax.k`, `gfx_err_shader_part.k` | 除匹配其标记外，错误必须报告在程序中包含出错 GLSL 的那一行。 |
| `gfx/probe.frag`, `gfx/probe_badset.frag` | `k --compile-shader`，即每个编译器（无论是否包含 gfx 层）都内置的着色器编译器。`probe.frag` 应能编译（包含其对 `probe_common.glsl` 的 `#include`），并报告其单个采样器与 32 字节 uniform 块。`probe_badset.frag` 包含超出 SDL_GPU 分配给片段着色器纹理集合范围的采样器，应按其违反的规则予以拒绝。 |
| 生成的 `call_chain.k`、`call_chain_too_deep.k`（`build/gen/<profile>/`） | 经过 2000 个不同函数的编译期调用路径在 `-O0` 下通过检查并通过 TinyCC 运行。在 `-O2` 下，内联器可将其单次使用的函数折叠到单个函数体中，每次最多嵌套 64 个 C 代码块，它使用原生 C 编译器构建并运行（`docs/implementation.md` §4）。经过 6000 个函数、且每个函数都在 32 层代码块深处调用下一个函数的路径会被拒绝为对编译器栈过深，而不是溢出崩溃（§3.1）。 |
| 生成的 `guard_runs.k`（`build/gen/<profile>/`） | 函数体和循环体中连续 300 个守卫，使用原生 C 编译器构建并在 `-O0` 和 `-O2` 下运行：当守卫的 else 分支离开当前块时，该块的其余部分不会开启新的 C 代码块（`docs/implementation.md` §6.2）。这一点很重要，因为 MSVC 最多允许 128 层嵌套而 clang 为 256 层。一条由 200 个单次使用函数组成的链，每个函数都在四个守卫后调用下一个函数，在 `-O2` 下会折叠为深达 64 个块的函数体；若内联器的计数漏掉了代码生成开启的块，嵌套深度就会超过两者的限制（§4）。 |
| O1 下的 `loop_shapes.k` | 读取生成的 C：每个 `shape_` 函数都拥有与其命名相符的改写后循环，由代码生成的 `/* loop shape: ... */` 注释标出，且没有任何 `keep_` 函数包含此类注释（`docs/implementation.md` §6.12）。其运行结果检查每个改写后循环的计算结果与参考循环一致。 |
| O1 下的 `member_parameters.k` | 读取生成的 C：接收两个切片和一个 `T[>..]&` 的 `walk`，在 C 中将它们各自拆分为两个标量参数（数据指针与计数、头部指针与栈；规范 C.3，`ParamFields`）。其运行测试通过调用、递归、case 函数分派、嵌套函数的自由变量、`simd` 函数、`format` 重载和导出包装器传递切片、构建器和池引用。 |
| O1 下的 `member_stores.k` | 读取生成的 C：`store_links` 向经由不可重绑定引用且路径中无下标到达的字段赋值时，会直接生成 C 成员赋值，而不是先取指针再赋值（`StableDest`）。其运行测试检查赋值结果，并检查当右侧表达式重绑定了该引用时，赋值仍然写入在右侧运行前所指名的目标（`evaluation_order.k` 包含一个右侧直接改写目标本身的用例）。 |
| O0 下的 `merged_specs.k` | 读取 `--specs` 与生成的 C：除 `main` 和 `triple` 之外的每个函数都针对根位于全局和位于局部的切片各特化了一次，且只对应一个 C 函数（`docs/implementation.md` §6.15）：调用 `triple` 的普通函数只特化一次；相互调用的两个函数；长距离返回目标；位于另一个内部并将外层返回继续向上传递的目标；`simd` 函数及其元素运行孪生函数；传入两个在常量上 C 代码不同的块的 `mapped` 保留两个；而 C 代码相同的 `ignore` 与 `discard`（测试会检查这一点）各自只保留一个。捕获三个目标，抛出的每个编号都属于它们之一，且仅声明它们的通道。其运行测试检查重定向后的调用与重新编号后的返回是否落到原本应到达的位置。 |
| O0/O1/O2 下的 `optimize.k` | 检查 `--specs` 中具名的尾递归函数体：受支持的整数累加器递归和普通递归会变成循环，包括沿链表遍历并将从参数读出的节点继续传递的递归。取模、浮点重结合、非局部返回帧、嵌套循环内的返回以及向下传递局部变量的引用，均保留其自调用。混合运算符会保留不符合条件的调用。前置局部变量会阻止基例内联率先消耗这些用例。`gb_` 函数统计其首条语句的拷贝次数：当测试比较未赋值的参数时（包括用 UFCS 编写的自调用），受保护的基例内联会在每个自调用点拷贝一次函数体，但当测试读取全局变量或节点、执行算术运算或读取已赋值的参数时则不会拷贝。优化器创建的超过 `i64.max` 的 `u64`（此处为折叠操作与 `tre_and` 的起始累加器）会转储为该无符号值。 |
| O1 下的 `optimize.k` 及其 C 代码 | 返回两个值的 `inline fn` `il_pair` 在 K 中保持为调用形式，但在 C 中声明为 `K_INLINE`，交由 C 编译器内联（规范 §7.13）。不带该关键字的 `il_big` 副本 `il_plain` 则不会如此声明。 |
| `docs/reference.md` | 语言参考文档中的每一个 ```` ```klang ```` 代码块都能编译（按原文或包装进 `main`），标有 `// error` 的代码块编译失败，没有任何代码块产生警告，且在支持 JIT 运行的情况下，每个完整程序都会打印其后紧跟的代码块所展示的输出。`scripts/check_doc_examples.py` 像示例运行器那样在独立进程中完成该工作，并将代码块写到 `build/gen/<profile>/doc_examples/` 下；其文档字符串说明了文档用于标记仅作片段或只编译不运行的代码块的标记。手动对任意文档运行：`python scripts/check_doc_examples.py docs/reference.md --run -v`。 |
| 命令行（`run_tests.py` 中的 `driver_verbs`） | 验证命令动词与其对应标志：`k run`（在支持 JIT 运行时）打印程序输出，`k check` 仅检查程序，且 `k run -o` 被拒绝；`k version` 与 `k --version` 打印编译器版本和运行时的 12 位十六进制数字；既非命令、选项也非文件的词会报未知命令并提示相近命令；`--color always` 和 `CLICOLOR_FORCE` 会为错误和指示符（caret）着色，而非终端输出或 `NO_COLOR` 则无颜色；`k check` 成功时不打印任何内容，加上 `--stats` 则打印其 `typechecked ok` 行；每种 shell 的补全脚本都列出了所有命令及其选项，bash 的脚本（在有 bash 时）能补全命令、选项和 `.k` 文件，zsh 的脚本能正常解析（在有 zsh 时）；`k help --man` 包含每个命令的帮助，groff（在有 groff 时）可无警告读取它；小于 64 KB 的 `--stack-reserve` 会被拒绝。关于帮助信息：`k --help` 列出命令但不列出隐藏标志；合起来看，run、build、check、fetch、format、toolchain、version、lsp 和 completion 的 `k <command> --help` 以及 `k help c` 和 `k help internals` 列出了 `src/main.cpp` 读取的除隐藏着色器选项外的每一个选项；`k help build` 列出 `--dry-run`；未知主题会被拒绝；不传文件或传给 `k build` 时无文件会打印用法说明。`k toolchain --json` 能在运行器找到 C 编译器的任何地方找到它，会转义 `K_HOME` 中的制表符，且不会把 `PATH` 上名为 `git` 的目录误认为 git；在完全没有 C 编译器时，`k toolchain` 会列出其查找过的每一个编译器；`k build --cc` 指向非程序文件时会说明无法启动它的原因。存在 C 编译器时，`k build` 会构建出可运行的可执行文件，而 `--dry-run` 会打印将要执行的命令而不实际构建。 |
| `doc_comments.py` | `k --dump-docs` 打印程序声明、字段和变体的文档注释：即紧邻其正上方的一连串 `//` 行，去掉缩进。空行、块注释或任意 token 都会终止该连串注释，代码之后的注释不会开启文档注释，且后跟空行的横幅注释不属于任何声明。程序仍可通过检查和往返测试。 |
| `format.py` | `k format`：每个标准（golden）用例经标准输入格式化后，会打印预期排版并成为不动点。这些用例涵盖由解析器 token 角色决定的空格（一元与二元运算符、泛型列表与比较运算、类型后缀、`as!`、`[>..<]`、区间）、当列表右括号另起一行时添加尾随逗号（但初始化器列表的 `..` 后、数组填充形式、括号或以 `return` 或裸 `break` 结尾的最后一个 match 分支之后不添加）、成列对齐的一组声明、缩进（制表符、续行保持相对所续行的偏移且不比其更靠左，从而使在运算符后折行的链式调用保持对齐在第一个操作数之下、括号对齐行随括号移动、折行头部或条件的函数体、`})`）、空行、`\r\n` 与字节序标记（BOM）、随行移动的尾随注释及保持对齐的一组对齐注释、随行移动的块注释与多行字符串，以及分号后保持表格排版的语句。命令行行为包括：原地重写文件、已格式化的文件保留其修改时间、`--check` 列出未格式化的文件（退出码 1）且不写入任何内容、目录遍历（`.k` 文件，排序，除非显式指定否则跳过点目录和构建树）、无法解析的文件保持原样而其余文件照常格式化（退出码 2，优先级高于 `--check` 的 1）、拒绝只读文件、无文件或 `-` 时的标准输入、错误参数、`--verify` 与帮助信息。文件的文档注释会按 `--dump-docs` 打印的内容保留下来。第二个任务 `format_sweep` 会对 `samples/`、`stdlib/`、`bench/` 和 `test/` 运行 `k format --verify`：每个文件都必须能够格式化、通过格式化器自身的校验（相同的 token 与注释、仅在右括号前添加逗号、解析后得到相同的转储与文档注释）且再次格式化后输出不变；唯一因无法解析而跳过的文件必须位于 `errors/` 或 `errors_tc/` 下，或者是那四个刻意损坏的夹具。 |
| `lsp_protocol.py` | 通过协议驱动 `k lsp` 以检查编辑器查询、诊断信息、编辑、错误恢复、根节点变更以及跨多个程序的查询。见下文[语言服务器协议检查](#language-server-protocol-checks)。在首个客户端关闭前，对带有 `\r\n` 换行符的未保存文档请求 `textDocument/formatting` 会返回编辑列表，将其文本变为 `k format -` 的输出且仅触及发生变化的行；已格式化的文档返回空编辑，无法解析或未打开的文档返回 null。在包含孤立代理项（lone surrogate）后跟转义非 ASCII 字符的未保存文档中，其诊断信息应位于两者之后的 UTF-16 列号处。 |
| `packages_git.py` | 除拉取测试外，`k check --offline` 会在导入处拒绝不在缓存中的包并提示 `k fetch`（而不执行拉取），一旦拉取完成则予以接受。 |
| `copy_elision.py` | 由固定种子生成的程序，通过调用接收并返回超过 256 字节定长结构体（`PtrAbi`）的函数构建而成：同一变量既是实参又是目标、在调用期间被后续实参、函数值或引用改写、直接或通过参数或通过分派传递给 `var` 参数、全局变量或处于唯一使用处的变量、位于循环内外、传给保留为调用的被调函数（`recursive`）以及内联器接管的被调函数。无论使用代码生成器省略拷贝的版本，还是使用会生成全部拷贝的 `--no-copy-elision`，每个程序在 `-O0` 和 `-O2` 下都打印相同输出；有 C 编译器时构建运行，否则由 JIT 运行。`python test/copy_elision.py --seeds 0-499` 会运行更广泛的扫描并保留结果不一致的程序源码；`--print SEED` 打印单个程序。 |
| `diagnostics_json.py` | `k check --diagnostics json` 打印一个数组，每条错误或警告对应一个对象，其 `text` 字段与文本模式在 stderr 上打印的内容一致；进度行则输出到 stderr 而非 stdout。词法错误定位于其指示符（caret）所在的列，宽度为一字节，且在被词法分析器拒绝的 `"""` 字符串之后的错误定位于各自所在的行。类型检查错误的范围覆盖其所报告的节点，其实例化链条目的位置定位于它们所指名的调用处。警告同样具有范围。类型检查错误和警告的 `text` 会显示源码行并在该范围下方标出 `^~~`。导入错误覆盖整条导入语句。`checkfree` 函数中保留下来的检查会一次性作为多条错误报告。在程序中没有对应位置的错误仅包含其 `text`。 |

<a id="conditions-an-optimization-relies-on"></a>
### 优化所依赖的条件

当编译器因为“外部无法察觉”而省略某项操作（拷贝、检查、存储）时，它为此检查的每一个条件，在被去掉时都应当导致某个测试失败。在提交此类修改之前，应逐个去掉每个条件（每次一行），重新构建，并运行其涉及的夹具；如果某个条件去掉后没有任何测试察觉，就需要补充用例，而如果某个用例实际走上了并非为其设计的另一条路径（由另一个条件先行决定），则不算有效覆盖。条件旁的注释会指明覆盖它的用例，或者说明为何没有：对于超过 256 字节的定长结构体所发生的拷贝，参见 `codegen_calls.h` 中的 `CodeGen::StableArg`、`MovableArg`、`ParamUnwritten` 及其相邻代码，以及前文的 `codegen/copy_elision_guards.k` 和 `copy_elision.py`。若某个条件的缺失只会降低优化触发频率，则只会多出一次拷贝或检查，而不会产生错误的值；只有像 `last_use_moves` 这样对生成的 C 进行检查的测试才会察觉它。

<a id="language-server-protocol-checks"></a>
### 语言服务器协议检查

`lsp_protocol.py` 首先运行 `--probe`。随后运行器中的一个客户端以 `test/lsp/basics.k` 作为入口文件，并启用 VS Code 扩展的命令来初始化服务器。它打开该文件，等待诊断信息（应当没有），并发送由其 `// lsp:` 行指定的查询。这些查询检查：

* 悬停文本；某个位置处、文件内或不存在的跳转定义；文档符号；以及带活动参数的签名帮助。
* 某个位置（包括无 token 的位置）的语义高亮标记类型；引用计数；以及产生预期数量编辑或被拒绝的重命名操作。每次重命名编辑只能修改名称的叶节点。
* 内联提示文本、位置以及每行数量。由检查器插入的默认值不得产生提示。折叠范围必须覆盖预期行，选择范围链必须包含所请求范围且相互嵌套。
* 代码透镜（code-lens）位置与标题。实例化透镜的计数必须与其数据行及列出它们的命令相匹配。“Run”透镜必须携带文件的 URI；“not instantiated”必须仅作为纯标题。
* 必须出现或不得出现的补全标签，以及补全项详细信息中必须包含的文本。`complete.k` 以 `p.` 结尾，正如编辑器请求补全时源码常见的情况。
* 传入与传出调用，包括另一端的函数以及调用行。未被调用的函数不得有调用结果，非函数名称不得有调用项。
* 来自被导入文件 `helper.k` 的工作区符号。

该测试还检查分析过程是否请求客户端刷新代码透镜、语义高亮标记和内联提示。接着按顺序检查以下变更与新增根节点：

1. 突发的一组编辑引入一个错误并随后移除它。诊断信息必须反映最新文本，且变更必须取消被打断的分析。随后测试在顶部插入一行并立即在新偏移位置请求悬停。返回结果必须使用新文本。一次增量编辑（一个范围及其文本片段，如支持增量同步的客户端所发送的格式）在文件上方插入一行注释：其下方的提示必须保留并向下移动一行。
2. 将 `recovery.k` 和 `broken_decls.k` 作为独立根节点打开。每个错误都必须被发布，且查询仍须收到回答。打开 `recovery.k` 后立即请求的文档符号必须来自其分析结果，而不能返回空结果。
3. `source_index.k` 检查全局初始化器按原文索引，尽管检查器会就地改写它们。`const for` 分支中的绑定必须能够消解，签名帮助必须忽略字面量和注释中的逗号。`ufcs_ns.k` 检查在已消解的调用和不匹配任何重载的调用上，签名帮助、悬停和跳转定义是否在其接收者类型的命名空间而非全局命名空间中查找 UFCS 调用的函数。`ns_alias.k` 检查对通过命名空间别名拼写的名称（`g::norm` 对应 `geo::norm`）执行悬停、跳转定义、引用和重命名时，是否能找到其声明并仅编辑其叶节点，以及在 `g::` 之后补全是否提供该别名的命名空间中的名称。
4. `errors_tc/recovery/inside_braces.k` 检查结构体字面量和 match 大括号内部的错误。它们必须出现在各自的源码位置，且没有多余错误。
5. `exemplar.k` 和 `errors_tc/recovery/exemplar_unchecked.k` 检查通过样本（exemplar）对未调用泛型函数的查询。在第二个文件中，即使没有任何绑定能使这些函数体正确，它们也不得产生任何诊断信息。它唯一的错误位于 main 中，与 `k check` 报告的一致。
6. 根节点被打开、关闭和编辑。在一次请求驱动所有待执行的分析完成之后，测试会检查每个文件最后一次发布的诊断信息。它先单独打开包含一个错误的 `roots_lib.k`，然后将其与导入它的 `roots_app.k` 一同打开。关闭该导入者后，悬停和文档符号必须再次可用。接着它同时打开 `roots_app.k` 和 `roots_app2.k` 作为导入者，然后编辑前者使其停止导入该库。该库的错误必须在每一步都出现，且当两个根节点同时导入它时只出现一次。另外，当 `basics.k` 关闭并停止作为入口文件时，`helper.k` 保持打开。此时 `helper.k` 的悬停必须使用其自身的根分析。

第一个服务器干净关闭。第二个客户端以 `two_a.k` 和 `two_b.k` 作为入口文件启动。它重命名两者共同导入的 `two_shared.k` 中的一个函数，然后请求引用和传入调用。结果必须同时包含两个程序，且每个位置只列出一次。结果还必须包含仅通过第二个程序实例化泛型函数才触达的函数的调用方。随后第二个服务器关闭。

最后，针对 `general.positionEncodings` 的每一种提议（无、`utf-16`、`utf-32`、`utf-8` 在前、`utf-8` 在后）各启动一个客户端，打开其名称紧随 é、∑ 和 🦆 之后的 `encodings.k`。初始化结果必须恰好在客户端提供了 `utf-8` 时返回 `utf-8`，否则返回 `utf-16`。在该编码下，`missing` 处的错误、`total` 的定义、悬停与引用及其语义高亮标记都必须位于根据文本计算出的列号。在 UTF-8 下，在 🦆 内部某字节处的悬停不得找到任何名称。

手动运行 `test/lsp_sweep.py`；它独立于主运行器。它测量 `k lsp` 在每个示例、k_in_k 自举、每个 stdlib 模块以及所有正向、`errors/` 和 `errors_tc/` 夹具上的健壮性与延迟。

对每个程序，它会打开一个独立根节点并等待诊断信息。在每个标识符起点以及每个 `.` 之后，它会请求悬停、跳转定义、补全和签名帮助，对于此处命名的任何函数还会请求传入与传出调用。每个程序请求一次语义高亮标记、符号、折叠、内联提示和代码透镜。接着突发执行十次随机单字符编辑并等待诊断信息。恢复文本后，检查诊断信息是否与初始结果一致。

如果服务器退出、返回错误、单次请求耗时超过 2 秒或停止响应，扫描即告失败。它会打印最慢的文件与请求。`--quick` 可在一两秒内检查少量文件。基线指标见 `docs/design/language_server.md` §12。

<a id="fixture-audit"></a>
## 测试夹具审计

如果某个回归缺陷会导致某夹具失败而其他所有夹具都不受影响，那么该夹具就是“承重（load-bearing）”夹具。诊断信息相似的两个夹具并不一定是重复的：它们可能经过了不同的表达式访问器、特化/缓存状态、根传播路径或生成布局。全程序全局变量与调用图也是许多回归测试的一部分，因此合并两个夹具可能会改变受测属性。

2026 年 10 月的审计测量了这一点。审计使用带 clang `-fsanitize-coverage=trace-pc-guard` 构建的编译器与经插桩的运行时对象运行了测试套件，并为每个夹具记录了其运行命中的编译器边（edge）以及程序运行的运行时代码行。覆盖率仅计入能够察觉同一类回归的夹具范围内：

* 正向夹具的覆盖率仅面向其他正向夹具统计（拒绝类夹具无法察觉检查器误拒绝了正确程序）；
* 带 bce 注解的夹具在 `bce.h` 中的覆盖率仅面向其他带注解夹具统计；
* 拒绝类夹具在检查器文件中的覆盖率仅面向同一规则的其他拒绝类夹具统计（被错误接受的程序无法被正向夹具或其他规则的拒绝类夹具察觉）。

没有任何单个夹具的覆盖范围完全包含在任意另一个夹具之中。即使覆盖范围存在重叠，某个夹具仍可能捕获其他夹具遗漏的故障。因此，一个覆盖范围完全被同类别其余夹具覆盖的夹具，只有在以下所有条件同时满足时才会被移除：

* 指定的夹具通过相同的语言构造检查了相同的行为。

* 它并非后来作为针对那些夹具所遗漏缺陷的回归测试而添加。

* 它没有检查仅凭覆盖率无法体现的内容，例如预期的运行时诊断信息、整数位宽或极值、`K_DEBUG` 检查或输出文本。

第二轮复审对每一项拟议移除进行了质询。所有移除合在一起也必须保持每个类别的覆盖率不变。

这一过程移除了 153 个 `errors_tc/` 夹具和两个正向夹具。大多数被移除的 `errors_tc/` 夹具测试的是通过与兄弟夹具相同构造触达的收缩、存储或来源（provenance）规则。两个被移除的正向夹具分别是：`threads/thread_spawn_bound.k`，其泛型 `thread_fn` 绑定已被 `thread_spawn_modules.k` 通过相同泛型并带输出检查的方式启动覆盖；以及 `typing/fill_counts.k`，其计算出的填充计数已被 `named_const_readonly_refs.k`、`appended_literals.k` 和 `const_name_scopes.k` 构建并打印。几乎所有正向候选夹具都保留了下来：多数会以其他夹具的 `.stderr` 未曾固定的诊断信息中止，或者固定了走相同编译器边的值、类型或输出。同一主题下的小型夹具被合并。每个原夹具变为各自独立的函数，按原顺序调用，预期输出为其输出拼接而成：

| 夹具 | 合并自 |
|---|---|
| `codegen/adt_field_tags.k` | `variable_adt_field_tag.k`, `resizable_adt_tail_tag.k` |
| `codegen/relative_optional_null.k` | `relative_null_tests.k`, `relative_optional_targets.k` |
| `codegen/named_result_destinations.k` | `named_result_bytes.k`, `named_result_exclusions.k`, `inlined_named_result_tail.k` |
| `lifetimes/shrink_operand_values.k` | `shrink_owned_copies.k`, `shrink_fresh_receivers.k`, `shrink_argument_values.k` |
| `lifetimes/break_value_roots.k` | `break_value_provenance.k`, `break_value_holder_contents.k` |
| `storage/relative_default_values.k` | `relative_defaults.k`, `relative_default_fields.k` |
| `storage/serialize_image_checks.k` | `serialize_canonical.k`, `serialize_offset_overflow.k` |
| `syntax/extern_block_dump.k` | `extern_block.k`, `extern_block_escapes.k` |
| `optimizer/basecase_aliased_guards.k` | `basecase_reference_alias.k`, `basecase_slice_alias.k` |
| `optimizer/tail_reference_params.k` | `tail_reference_rebind.k`, `tail_reference_read.k` |
| `runtime/float_text_digits.k` | `float_text_f64.k`, `float_text_shortest.k` |
| `runtime/float_conversions_unchecked.k` | `f32_casts_unchecked.k`, `f64_conversions_unchecked.k` |
| `typing/variable_size_literals.k` | `variable_element_literals.k`, `variable_literal_equality.k` |
| `typing/nested_fn_assignments.k` | `nested_fn_exits_assigned.k`, `nested_fn_reused_assigns.k` |
| `typing/type_aliases.k` | `alias_nominal_recursion.k`, `variant_aliases.k` |
| `typing/dispatch_result_no_reference.k` | `dispatch_result_diverging_case.k`, `dispatch_result_null_case.k` |
| `typing/constant_widths.k` | `constant_extents.k`, `constant_shift_wrap.k` |

审计还将每个拒绝类测试的注释与其实际报告的错误进行了比对。当更靠后的规则先捕获某个夹具时，该夹具被修改为触达其所命名的规则。当其旧程序是其实际触达路径的唯一测试时，则保留该旧程序作为针对该路径命名的独立夹具。例如，`growshrink_reach_*` 夹具多持有一个根，使调用方的存储仅为其所存储的内容提供上界，这与持有者内容已知且精确的 `growshrink_exact_*` 夹具并列；`index_type_let_array.k` 和 `index_type_fixed_array.k` 分别固定了索引类型数组规则的两半；而 `fnval_*` 夹具则以其所触达的函数值规则命名。那些任何同类拒绝都会匹配的标记（`cannot clear`、`read-only`、类型名）被替换为具体错误消息。标记也可以匹配实例化链的帧以及重载候选的原因，部分夹具正是为了固定这些信息而存在。

简短的 `return_from_cycle.k` 仍保留在 `return_from_cached_call.k` 身旁：前者测试不经过额外缓存调用路径、直接通过递归回边（back edge）进行的重放，否则缓存调用路径可能会掩盖缺失的传播步骤。

CI 使用四个作业来覆盖这些配置，而不必测试平台、sanitizer、优化级别和运行时模式的每一种组合。基准测试独立于 CI 运行。
