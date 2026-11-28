<a id="the-k-command-line"></a>
# k 命令行工具

<details>
<summary>目录</summary>

- [1. 命令](#1-commands)
- [2. 运行程序](#2-running-a-program)
- [3. 构建可执行文件](#3-building-an-executable)
- [4. 检查程序](#4-checking-a-program)
- [5. run、build 与 check 的选项](#5-options-for-run-build-and-check)
- [6. k 的输出内容](#6-what-k-prints)
- [7. 包](#7-packages)
- [8. 自行编写与构建 C 代码](#8-writing-the-c-yourself)
- [9. 格式化](#9-formatting)
- [10. 语言服务器](#10-the-language-server)
- [11. toolchain、version、help 与 completion](#11-toolchain-version-help-and-completion)
- [12. 环境变量与文件](#12-environment-and-files)
- [13. 退出状态](#13-exit-status)
- [14. 用于编译器开发的选项](#14-options-for-working-on-the-compiler)

</details>

单个可执行文件 `k` 集成了编译器、进程内运行器、构建工具、包拉取器、格式化工具以及语言服务器。本页将对所有功能逐一说明。`k help` 会打印命令摘要，`k help <command>` 则会打印单个命令的选项。程序如何使用库以及如何编写库，请参见 [packages.md](packages.md)。

<a id="1-commands"></a>
## 1. 命令

```
k <command> file.k [options] [-- program args...]
k file.k [options] [-- program args...]
```

| 命令 | 说明 |
|---|---|
| `k run file.k` | 编译程序并在当前进程中运行（[§2](#2-running-a-program)）。`k file.k` 与其等价。 |
| `k build file.k` | 使用本机上的 C 编译器构建可执行文件，或配合 `--shared` 构建共享库（[§3](#3-building-an-executable)）。 |
| `k build --target wasm32-wasip1 file.k` | 使用 WASI SDK Clang 构建 [WebAssembly 命令模块](#webassembly-executables)。 |
| `k check file.k` | 对程序进行语法解析和类型检查，不写入任何文件也不运行（[§4](#4-checking-a-program)）。 |
| `k fetch file.k` | 拉取程序所声明的包（[§7](#7-packages)）。 |
| `k cache stats` | 报告共享缓存和项目构建中间产物的磁盘占用情况（[§7](#cache-usage-and-pruning)）。 |
| `k cache prune --budget bytes` | 回收空闲的运行时和锁定原生包输出（[§7](#cache-usage-and-pruning)）。 |
| `k cordis resolve service --manifest file` | 打印编译器或应用锁解析后的提供者图。[Cordis 图解析](design/cordis_microkernel.md#resolving-a-provider-graph) |
| `k format file.k\|dir...` | 按标准排版格式重写文件（[§9](#9-formatting)）。 |
| `k toolchain` | 报告检测到的 C 编译器、cmake、git 以及 `K_HOME`（[§11](#11-toolchain-version-help-and-completion)）。 |
| `k version` | 打印编译器及其运行时的版本。 |
| `k lsp` | 通过语言服务器协议（LSP）为编辑器提供服务（[§10](#10-the-language-server)）。 |
| `k completion <shell>` | 打印适用于 bash、zsh、fish 或 PowerShell 的自动补全脚本。 |
| `k help [command]` | 打印全部命令或指定命令的帮助信息。 |

命令需放在最前面。如果第一个参数不是已知命令，它将被视为选项或文件，因此 `k file.k` 会直接运行该程序。若第一个词中不含点号或斜杠，且既不是已知命令也不是已存在的文件，则会被视作拼写错误的命令：例如 `k biuld x.k` 会提示 `build` 且不执行任何操作。

选项可以出现在文件之前或之后，顺序任意。接受程序作为输入的命令会使用其*根文件*（root file），该文件包含 `main` 并声明程序所需的包。`--` 之后的所有内容都归被运行的程序所有。在 `--` 之前的任意位置使用 `--help` 或 `-h` 都会打印所给命令的帮助信息，而 `k --version` 等价于 `k version`。

<a id="2-running-a-program"></a>
## 2. 运行程序

```bash
k hello.k
k run hello.k -- input.txt --verbose
```

`k run` 会将程序编译为 C 代码，使用内置在 `k` 中的 TinyCC 编译器在内存中编译该 C 代码，并在编译器自身的进程中运行它。这无需构建步骤，也不会写入任何文件。在 Windows 上无需其他额外依赖；在 Linux 上需要 C 库头文件（`libc6-dev` 或 `glibc-devel`）；在 macOS 上则需要 Xcode 命令行工具的 SDK。

`--` 之后的参数归程序本身所有。程序可以定义 `fn main()` 或 `async fn main()`，也可以在无 `main` 的源文件中直接执行顶级语句（包括顶级 `await`）。`os` 模块会将这些参数传递给程序，其中第 0 个参数为传入时的根文件路径：

<!-- check-only -->
```klang
import os;

fn main() {
    let a = args();                      // a[0] 是程序本身
    guard a.len > 1 else { print("usage: hello file..."); exit(2); }
    for i in 1..a.len { print(i, ": ", a[i]); }
}
```

`k run` 会以程序的退出码退出：`main` 返回时为 0，调用 `exit(code)` 时为传入的 `code`，而当程序未通过检查或调用 `abort` 时为 1（同时在 stderr 上输出 `klang runtime error: ...`）。无法通过编译的程序也会在打印错误后以 1 退出。

运行模式做不到但构建模式可以做到的事情：

* 启动线程的程序必须进行构建。TinyCC 无法布置存放各工作线程数据栈的线程局部存储（TLS），因此 `k run` 会拒绝运行此类程序。
* TinyCC 不做优化，因此运行模式适合快速试错，而性能计时应当在构建后的产物上进行。
* 绑定 C 库函数的 `extern fn` 在运行模式下仅能解析 C99 `<math.h>` 中的双精度/单精度函数（除 `nexttoward` 之外的全部函数）。其他函数在运行链接时都无法找到；这类程序需要通过构建、在程序自己的 C 代码中提供，或使用指明库名的 `extern "lib" { ... }` 块来解决（[reference.md](reference.md#24-calling-c-and-being-called-from-c)）。
* 运行模式会将其数据路径相对于根文件解析：`resource_dir()` 返回根文件所在目录，而在构建出的程序中它返回的是可执行文件所在目录（[stdlib.md](stdlib.md#os)）。无论哪种模式，相对路径始终相对于当前工作目录。

即使程序所使用的包包含自有的 C 代码，也仍可通过这种方式运行。已编译进 `k` 本身的包（`k toolchain` 会列出它们）不需要额外操作。任何其他包的 C 代码，以及程序在附属配置文件（sidecar）中指定的自有 C 代码（[packages.md](packages.md#3-a-program-with-c-of-its-own)），会在首次运行时使用本机上的 C 编译器构建成共享库（仅构建一次），并加载到当前运行进程中。`--cc` 可以像构建模式那样选择所用的编译器，`-v` 则会打印用于构建该共享库的命令。

`k run` 接受 [§5](#5-options-for-run-build-and-check) 中的选项。它不接受 `-o`：标志形式 `k -o out.c` 会改为输出 C 代码（[§8](#8-writing-the-c-yourself)）。

<a id="3-building-an-executable"></a>
## 3. 构建可执行文件

```bash
k build hello.k && ./hello
k build -o out/hello hello.k     # 将可执行文件输出到其他位置
k build --debug hello.k          # 开启运行时检查并附带调试信息
k build --dry-run hello.k        # 仅打印命令而不执行
k build --shared hello.k         # 生成 libhello.so/.dylib，或 hello.dll
```

`k build` 会写出程序的 C 代码，使用本机上的 C 编译器进行编译，并将其与运行时以及程序所使用包的原生部分链接在一起。可执行文件默认生成在根文件同级目录下，以其文件名命名（`hello`，或在 Windows 上为 `hello.exe`），除非通过 `-o` 或附属配置文件中的 `output = "name";` 另外命名。构建完成后会打印 `built <executable>`。

C 编译器按以下顺序选用第一个指定的选项：

1. `--cc name|path`
2. 环境变量 `K_CC`
3. `K_HOME/config`（`~/.klang/config`）中的 `cc = "clang";`
4. 本机上找到的第一个编译器。

其中 `name` 可以是 `msvc`（或 `cl`）、`clang-cl`、`clang`、`gcc` 和 `cc` 之一，`path` 则是编译器可执行文件的路径。未作显式指定时，Windows 会先在 `PATH` 上查找 `cl`（例如在 Developer Command Prompt 中），接着在 Visual Studio 的 LLVM 目录或 `PATH` 上查找 `clang-cl` 和 `clang`，然后通过 Visual Studio 的 `vcvars64.bat` 查找 MSVC，最后在 `PATH` 上查找 `gcc` 和 `cc`。其他系统则依次在 `PATH` 上查找 `cc`、`gcc` 和 `clang`。`k toolchain` 会显示构建将使用的编译器及其原因。配置 Visual Studio 环境需要一两秒钟，因此 `k` 会将结果缓存在 `K_HOME/toolchain` 中，`k toolchain --refresh` 可重新初始化该环境。

无论 K 编译器使用哪个 `-O` 优化级别，生成的 C 代码始终以开启优化的方式编译（`/O2` 或 `-O2`，在指定 `-O3` 时为 `-O3`），唯独 `--debug` 模式下例外。除 [§5](#5-options-for-run-build-and-check) 中的选项外，构建还支持以下选项：

| 选项 | 说明 |
|---|---|
| `-o exe` | 指定可执行文件的输出路径。如有需要会自动创建其父目录。 |
| `--lto` | 跨程序与运行时启用链接时优化（LTO）。 |
| `--cpu=native` | 针对宿主 CPU 优化原生代码生成（`-mcpu=native` 或 `-march=native`）。 |
| `--debug` | 开启溢出、范围和标签检查（`K_DEBUG=1`），并在关闭优化且附带调试信息的情况下构建 C 代码（`/Od /Zi` 或 `-O0 -g`），以便使用调试器。 |
| `--cc name\|path` | 指定 C 编译器，规则同上。 |
| `--standalone` | 将运行时直接编译进程序中，而不是链接按每个 C 编译器预先构建一次的运行时目标文件。 |
| `--shared` | 构建自包含的共享库（`lib<stem>.so`、`lib<stem>.dylib` 或 `<stem>.dll`），而非可执行文件。该库不包含 C `main`；其宿主 API 由 `k_init`、`k_exit` 以及程序的 `export fn` 符号组成（[reference §24](reference.md#24-calling-c-and-being-called-from-c)）。该选项隐含 `--standalone`；配合 `--header out.h`（[§8](#8-writing-the-c-yourself)）还会同时生成宿主头文件。 |
| `-v` | 打印构建过程中执行的每条命令及其输出。 |
| `--dry-run` | 打印构建将会执行的命令及其路径，但实际不运行任何命令。Makefile 或 CMake 项目可据此获取构建步骤。 |

构建过程中生成的所有中间产物都会存放在 `K_HOME` 下。程序的 C 代码、目标文件和日志位于 `build/prog/<name>-<hash>/` 中，每个根文件对应一个目录。`build/runtime/` 存放每个 C 工具链及 release/debug 配置对应的运行时目标文件。在 macOS 上，使用系统 Apple Clang 构建的 release 版本包含针对其默认部署目标以及 `MACOSX_DEPLOYMENT_TARGET=13.3` 的已优化运行时目标文件。在将打包的目标文件加入受管缓存之前，K 会校验驱动计划、预处理后的 C 代码以及目标文件摘要。其他工具链配置或校验未命中时，会在本地重新编译运行时。编译与链接失败时会显示对应命令、其输出内容以及日志路径。

<a id="webassembly-executables"></a>
### WebAssembly 可执行文件

安装 WASI SDK 并将 `WASI_SDK_PATH` 指向其根目录，或通过 `--cc` 传入其 `bin/clang` 路径。`k build` 默认在源文件旁生成 `.wasm` 文件。使用 `-o` 可指定其他输出路径。

```sh
export WASI_SDK_PATH=/path/to/wasi-sdk
k build --target wasm32-wasip1 hello.k -o hello.wasm
wasmtime run hello.wasm
```

`wasm32-wasi` 是该目标的别名。生成的命令模块包含 K 目标运行时，并在 WASI Preview 1 下运行。标准流和命令行参数由 WASI 宿主提供。当程序需要文件 I/O 时，可传入 `wasmtime run --dir . hello.wasm` 以授予对当前目录的访问权限。WASI 构建支持协作式任务与静态 C 包互操作：来自 `.build` 附属文件的 `c_sources`、`c_include`、`c_defines`、`c_flags` 和 `c_libs` 会使用所选的 WASI 工具链进行编译，产生的目标文件会参与最终链接。C 声明必须匹配目标平台的 32 位指针以及 K 的紧凑聚合体布局。原生与 WASI 的包目标文件及时间戳标记按目标平台和工具链分别存放，已锁定包的缓存条目也是如此。工作线程、动态 extern、CMake 包以及内置原生包目前暂不支持该目标。浏览器和组件模型（component-model）宿主属于路线图规划中的后续工作。
[WASM 设计](design/wasm.md#wasi-command-target) ·
[Wasmtime CLI](https://docs.wasmtime.dev/cli.html)

<a id="4-checking-a-program"></a>
## 4. 检查程序

```bash
k check hello.k
```

`k check` 会对程序及其导入的所有内容进行语法解析和类型检查，包括编译器执行的生命周期检查和边界检查，但不写入任何文件也不运行。程序无误时不打印任何内容；若存在错误则打印错误信息并以退出码 1 退出。该命令接受 [§5](#5-options-for-run-build-and-check) 中的选项。`--diagnostics json` 可将错误以结构化数据形式提供给编辑器或其他工具（[§6](#6-what-k-prints)），配合 `--stacks` 时还会同时报告程序的数据栈情况。

<a id="5-options-for-run-build-and-check"></a>
## 5. run、build 与 check 的选项

以下选项在 `k run`、`k build`、`k check` 以及标志形式 `k -o out.c` 中具有相同含义。

| 选项 | 说明 |
|---|---|
| `-O0`、`-O1`、`-O2`、`-O3` | 控制 K 编译器的内联程度：不内联、少量内联、默认程度，或扩大后的 `-O3` 预算（此时也会以 `-O3` 编译 C 代码）。常量折叠和传播在每个级别都会运行，且构建模式的 C 编译器在每个级别下都会启用优化。 |
| `--debug` | 开启溢出检查、`as` 转换的范围检查以及标签检查（`K_DEBUG=1`）。每次失败都会指出其源码行，溢出或类型转换失败还会列出所涉及的值。构建模式下还会附带调试信息（[§3](#3-building-an-executable)）。 |
| `-D NAME[=VALUE]` | 在生成的程序中定义一个 C 宏，也可以写成 `-DNAME[=VALUE]`。该宏定义直接写入生成的 C 代码中，因此运行和构建看到的源码完全一致。未指定值时默认为 1。 |
| `--stack-reserve size` | 为每个数据栈预留的地址空间大小，范围从 64K 到 256T，带 `K`、`M`、`G` 或 `T` 后缀。默认值为 2G。地址空间仅在实际使用时才会提交物理内存，因此较大的预留量在未使用前不会产生成本。 |
| `--stacks` | 报告程序及其每个工作线程使用了多少个数据栈、总计占用多少，以及它对 `hardware_threads()` 施加的上限。 |
| `--bce-lines` | 按源码行逐行报告编译器消除了多少个边界检查以及保留了多少个。 |
| `--no-bce` | 保留所有边界检查。此时程序中的 `checkfree` 函数将不会得到验证，编译器也会对此发出警告。 |
| `--specs` | 在优化之后，以源码形式打印每个函数的所有特化版本。 |
| `--stats` | 打印关于本次编译的几行统计信息：解析的声明数、特化与优化次数、消除的边界检查数以及写入的文件。 |
| `--color auto\|always\|never` | 控制错误和警告是否以彩色输出（[§6](#6-what-k-prints)）。 |
| `--diagnostics text\|json` | 将错误和警告作为纯文本打印到 stderr（默认），或作为 JSON 打印到 stdout（[§6](#6-what-k-prints)）。 |
| `--offline` | 不拉取任何包：若遇到尚未拉取的包，则在其 `import` 处报错（[§7](#7-packages)）。 |
| `--update` | 重新拉取声明在 tag 或 branch 上的包（[§7](#7-packages)）。 |
| `--stdlib dir` | 指定标准库所在目录（[§12](#12-environment-and-files)）。 |
| `--cc name\|path` | 指定在需要编译 C 代码时使用的 C 编译器（[§3](#3-building-an-executable)）。 |
| `-v` | 打印用于构建 C 代码的每条命令及其输出。 |

以下是运行时读取的 `-D` 宏，用于配置程序行为：

| 宏 | 默认值 | 设置内容 |
|---|---|---|
| `K_DEBUG` | 0 | 设为 1 即 `--debug` 所开启的检查：溢出、范围与标签检查。 |
| `K_STACK_STATS` | 0 | 设为 1 会使每个线程程序在结束时向 stderr 报告其数据栈使用情况。 |
| `K_STACK_RESERVE` | 2 GB | 每个数据栈预留的地址空间，即 `--stack-reserve` 所设置的值。 |
| `K_THREAD_STACK` | 8 MB | 当系统默认值小于该值时（macOS 上为 512 KB，Windows 上为 1 MB），每个工作线程预留的原生栈大小。 |
| `K_STACK_GAP` | 1 MB | 每个数据栈之后的未映射保护间隙（gap），用于使失控的栈增长立即中止。 |
| `K_STACK_BUDGET` | 32 TB | 所有线程程序合计可占用的目标地址空间预算；`hardware_threads()` 会受限在此预算允许的范围内。 |
| `K_MAX_STACKS` | 1024 | 单个线程程序最多可以使用的数据栈数量。需要更多数据栈的程序将无法通过编译。 |
| `K_SIMD` | 2 | 在 x86-64 上使用 clang 编译时 `simd` 函数多版本生成的最高 CPU 级别：设为 0 或 1 可降低该级别。 |

如果程序耗尽了数据栈，它会以一条包含 `--stack-reserve` 的错误信息终止。`--stacks` 和 `-DK_STACK_STATS=1` 可用于查看程序使用了多少个栈，[实现笔记](implementation.md#7-the-runtime)则解释了数据栈是如何预留的。

<a id="6-what-k-prints"></a>
## 6. k 的输出内容

运行模式仅打印程序自身输出的内容。检查成功时不打印任何内容，而构建成功时仅打印 `built` 那一行。错误和警告信息输出到 stderr，每条都附带其所指向的源码行；当编译器知道列位置时，还会在对应代码下方标出 `^~~~`：

```
x.k:5: warning: redundant `as i64`: an i32 converts to i64 implicitly here
    xs.push(((r as i64) << 32) | (c as i64));
                                  ^~~~~~~~
```

`--stats` 会在 stdout 上追加几行编译统计信息；若用于运行模式，由于 stdout 归程序本身所有，这些统计会改在 stderr 上输出。

在终端中，错误和警告信息会带有颜色。`--color never` 或非空的 `NO_COLOR` 会关闭颜色；`--color always` 或非 0 的 `CLICOLOR_FORCE` 则会在任意输出目标上强制开启颜色。

诸如拉取包、构建包的 C 代码或编译运行时等耗时较长的任务，在开始时会在 stderr 上打印一行提示。在终端环境下，其下方还会显示一条实时刷新行，展示正在执行的任务、已耗时以及工具最后打印的一行内容，并在任务完成后擦除该行。当 stderr 重定向到其他位置（文件、管道、CI 日志、代理执行命令）或 `TERM` 为 `dumb` 时，不会显示实时刷新行，仅保留单行提示。`K_PROGRESS=plain` 或 `K_PROGRESS=live` 可覆盖这一行为，而 `-v` 和 `--dry-run` 始终使用纯文本（plain）输出。

`--diagnostics json` 会在运行模式的程序输出完毕之后，将错误和警告作为 JSON 数组打印到 stdout。原本会由 `k` 打印到 stdout 的内容（如 `--stats` 统计行和构建模式的 `built` 行）将改道输出到 stderr。退出码则保持与原本一致：

```json
[
  {"severity": "error", "message": "expected a value of type i32, got const u8[:]",
   "file": "bad.k", "line": 1, "col": 26, "endLine": 1, "endCol": 29,
   "related": [{"message": "in fn main() instantiated from bad.k:1",
                "file": "bad.k", "line": 1, "col": 1}],
   "text": "bad.k:1: error: expected a value of type i32, got const u8[:]\n..."}
]
```

每个对象都包含 `severity`（`error` 或 `warning`）和 `message`。当位置已知时，还会包含编译器所认定的 `file`、以 1 为起始的 `line` 与字节偏移 `col`，以及指向最后一个字节后一位的 `endLine` 和 `endCol`。`related` 保存紧随其后的关联提示行（格式相同），而 `text` 则保存原本会在终端上显示的完整文本。并非针对源码的错误（例如缺少 C 编译器）仅包含 `message` 和 `text`。

<a id="locked-cordis-diagnostics"></a>
### 锁定的 Cordis 诊断

`k check` 和 `k build` 支持同时传入 `--cordis-manifest file` 与 `--cordis-lock file`。锁文件可通过 `k lsp --cordis-manifest file --cordis-graph` 生成。编译器会在激活之前校验所选提供者与输入内容。锁定后的构建还会将该图记录在生成的原生产物中。[锁格式与诊断](design/cordis_microkernel.md#locked-checks-and-builds)

<a id="7-packages"></a>
## 7. 包

当程序导入了一个尚未拉取的包时，首次运行、构建或检查该程序会使用 git 将其拉取到 `K_HOME/pkg` 中，并在那里按每个 C 编译器构建一次该包的 C 代码。[packages.md](packages.md) 说明了程序如何声明它所需的包。

```bash
k fetch app.k               # 仅拉取 app.k 声明的包，不进行任何编译
k fetch --update app.k      # 重新拉取声明在 tag 或 branch 上的包
k run --offline app.k       # 不执行拉取；遇到未拉取的包则报错
```

`k fetch` 可以让后续会话在无网络环境下正常编译运行。固定在某个 commit 上的包永远不会再次拉取；固定在 tag 或 branch 上的包仅在使用 `--update` 时才会重新拉取，所有执行编译的命令也同样支持该选项。语言服务器永远不会自动拉取任何包。

当缺少所需的工具时，报错信息会明确指出缺失的工具名称：拉取包需要 git，构建 CMake 包需要 cmake，编译任何 C 代码需要 C 编译器。自身不含 C 代码且未使用含 C 代码的包的程序不需要上述任何工具。删除 `K_HOME` 不会丢失任何构建无法重新生成的内容（[packages.md](packages.md#5-the-cache) 展示了其中的目录结构）。

<a id="cache-usage-and-pruning"></a>
### 缓存使用与清理

```sh
k cache stats --json
k cache prune --budget 104857600 --json
```

`k cache stats` 统计 `K_HOME` 下的缓存 Git 检出目录、原生包输出、捆绑包副本、运行时目标文件、项目构建中间产物以及工具链元数据。它报告常规文件的逻辑字节数，包括扫描期间观测到的构建日志和临时文件，同时会跳过符号链接。报告中的 `managed_runtime_bytes` 和 `managed_native_bytes` 给出了运行时组与原生包组中符合清理条件的大小。安装在系统中的编译器以及位于 `K_HOME` 之外的应用可执行文件各有其自身的磁盘占用。

`k cache prune` 对当前的运行时目标文件和受管的原生包输出应用统一的字节预算，并优先回收最久未使用的空闲条目。构建过程在链接阶段结束前会持有租约（lease），JIT 进程在退出前也会保留其原生库。在固定 Git commit 上导入的原生包，只要其声明的 C 源文件和头文件路径位于该检出目录内，就会使用此缓存。AOT 和 JIT 共享普通的 C 目标文件。CMake 构建会维护独立的目标图，从而使 JIT 库的构建可以在 AOT 链接进行期间同时推进。

即使在缓存条目被驱逐之后，Git 检出目录和 CMake FetchContent 源码树仍会保留并保持可用。`k build --offline` 可以利用这些输入和本地工具链重新生成输出产物。包含共享库的 AOT 输出树会为可能加载它的可执行文件予以保留。它们的字节数、旧版缓存布局，以及基于可变路径、branch 或 tag 构建的原生产物，都会在 `stats` 中显示在清理预算之外。

配合 `--json` 时，清理报告会提供 `before_bytes`、`remaining_bytes`、`reclaimed_bytes`、`skipped_in_use_bytes` 和 `budget_met`。如果该命令与构建同时运行，请检查 `budget_met` 字段：当仍有活跃构建持有或正在生成产物时，一次成功的清理仍可能报告预算未达成。

<a id="8-writing-the-c-yourself"></a>
## 8. 自行编写与构建 C 代码

`k build` 能满足大多数需求，而 `k build --dry-run` 可以展示它执行的具体命令。如果希望在你自己的构建系统中构建 C 代码，或将 K 程序链接进 C/C++ 宿主程序中，可以向编译器指定一个输出文件而非子命令：

```bash
k -o tour.c tour.k                    # 生成程序的 C 代码
k --emit-runtime k_runtime.c          # 生成运行时的 C 代码
cc -O2 -c k_runtime.c                     # 使用与程序相同的编译器及标志编译一次
cc -O2 tour.c k_runtime.o -o tour -lm -pthread
```

在 Windows 上，后两条命令对应为 `cl /O2 /c k_runtime.c` 和 `cl /O2 tour.c k_runtime.obj`。在 Linux 上，若使用 glibc 2.34 之前的版本，请添加 `-ldl`。

`-o out.c` 会写出程序的 C 代码但不运行；加上 `--jit` 则会像 `k run` 那样同时在进程内运行该程序。生成的 C 文件包含了程序本身以及内联进其中的那部分运行时。其余部分的运行时（操作系统抽象层、数据栈预留、线程与打印）由 `--emit-runtime` 写出，这部分代码只需编译一次即可与所有程序链接。该文件本身不接受 `-D` 宏配置，因为程序会在启动时将配置传递给运行时。请使用与编译程序相同的 C 编译器和编译标志来构建它。来自不同编译器版本的程序无法与其链接（`k version` 会打印必须匹配的运行时哈希）。

| 选项 | 说明 |
|---|---|
| `-o out.c` | 写出程序的 C 代码，不运行它。 |
| `--standalone` | 写出包含完整运行时的 C 代码，可独立完成编译：`cc -O2 tour.c -o tour -lm -pthread`。 |
| `--header out.h` | 同时为 C 宿主生成一个头文件，声明程序中的所有 `export fn`（至少需要有一个导出函数）。此时生成的 C 代码不包含 `main`：宿主调用一次 `k_init(argc, argv)` 来运行程序的 `main`，随后即可调用各导出函数；`k_exit()` 用于结束程序，之后仍可再次调用 `k_init` 重新启动它。 |
| `--include header.h` | 让生成的 C 代码包含该头文件，用于针对生成的类型编写小型垫片代码（规范 §7.10）。可重复指定。 |
| `--jit` | 同时在当前进程中运行该程序。 |
| `--emit-runtime out.c` | 写出运行时的 C 代码。 |
| `--<package>-link msvc\|cc` | 打印用于链接已编译进 `k` 的某个包的原生部分的响应文件路径，支持 `msvc` 或 `cc` 两种命令行风格。 |

[教程](tutorial.md#19-calling-k-from-c)和[参考手册](reference.md#export-fn)展示了 C 宿主的具体用法。[§5](#5-options-for-run-build-and-check) 中的所有选项同样适用，包括 `-O` 和 `-D`。

使用了 `audio`、`gfx`、`physics` 或 `ui` 的程序还需要链接对应包的原生部分。当该包已编译进 `k` 中时，其响应文件会作为编译器安装目录的一部分随附提供：

```bash
cc -O2 cube.c k_runtime.o -o cube @$(k --gfx-link cc) -lm -pthread
```

使用 `ui` 的程序需要将 `ui` 响应文件放在它所依赖绘制的 `gfx` 响应文件之前。对于未编译进 `k` 的包以及程序自身的 C 代码，`k build --dry-run` 会打印用于构建每个原生部分及其生成响应文件的命令。

<a id="9-formatting"></a>
## 9. 格式化

```bash
k format src/                  # 重写 src/ 下的每一个 .k 文件
k format --check .             # 列出尚未格式化的文件
k format < in.k > out.k
```

`k format` 会按标准排版格式原地重写每个文件：包括缩进、词法单元之间的空格、空行以及尾随逗号，同时保留原有的换行位置。传入目录代表递归处理其下的所有 `.k` 文件，但会跳过以点号开头的目录以及构建目录树（包含 `CMakeCache.txt` 的目录）。未指定文件或传入 `-` 时，工具会读取标准输入并格式化输出到标准输出。对于存在语法错误无法解析的文件，工具会保持原样不作修改，并打印其错误。

`--check` 不会写入任何文件，仅列出不符合格式的文件。退出状态为：当所有文件均已符合格式（或已成功格式化）时返回 0；当 `--check` 发现至少一个未格式化的文件时返回 1；当文件无法解析、无法读写或命令行选项有误时返回 2。[editors.md](editors.md#formatting) 介绍了如何配置保存时自动格式化。

<a id="10-the-language-server"></a>
## 10. 语言服务器

```bash
k lsp
```

`k lsp` 通过标准输入和标准输出上的语言服务器协议（LSP）为编辑器提供服务：支持随文件变更实时更新诊断、悬停提示（hover）、跳转定义、查找引用与重命名、符号列表、语义高亮、签名帮助、代码补全、内嵌提示（inlay hints）、代码折叠、代码透镜（code lenses）以及格式化。它会分析编辑器在 `initializationOptions`（`entryFiles`）中指定的根文件所构成的程序，以及未被其中任何程序导入的所有已打开文件。语言服务器从不自动拉取包：遇到尚未拉取的包时会在其 `import` 处报错，可通过运行 `k fetch` 来解决。

| 选项 | 说明 |
|---|---|
| `--stdio` | 通过标准输入和标准输出提供服务（默认即如此，客户端通常会显式传入）。 |
| `--stdlib dir` | 指定标准库所在目录。 |
| `--probe` | 打印协议版本（`k lsp 3.17`）并退出。 |
| `--cordis-manifest file` | 启用显式选定的受信任原生诊断提供者。[Cordis 诊断](design/cordis_microkernel.md#native-diagnostics-in-the-language-server) |
| `--cordis-graph` | 配合 `--cordis-manifest`，解析诊断图并打印 JSON 而不实际激活。若解析失败，则以状态码 1 退出并输出结构化错误。[图格式](design/cordis_microkernel.md#resolving-a-provider-graph) |

[VS Code 扩展](vscode.md)会自动启动该服务器；[editors.md](editors.md) 提供了 Neovim、Helix 和 Emacs 的配置方法，[设计笔记](design/language_server.md)则记录了其实现细节。

<a id="11-toolchain-version-help-and-completion"></a>
## 11. toolchain、version、help 与 completion

`k toolchain` 会报告构建将要使用的 C 编译器（名称、版本、路径、命令行风格以及发现方式）、对应的 C++ 驱动、cmake、git、编译进 `k` 的包以及 `K_HOME`。当找不到 C 编译器时，该命令以 1 退出。它支持通过 `--cc` 查找特定编译器、通过 `--json` 以 JSON 格式输出报告，以及通过 `--refresh` 重新初始化 Visual Studio 环境而非沿用上次缓存的配置。

`k version`（或 `k --version`）会打印编译器及其运行时的版本：其中包含一个运行时文本的哈希值，通过 `--emit-runtime` 写出的运行时源码必须与该哈希值匹配。

`k help` 列出全部命令及常用选项；`k help <command>`（或 `k <command> --help`）列出单个命令的选项。`k help c` 涵盖自行编写与构建 C 代码的说明，`k help internals` 则列出 [§14](#14-options-for-working-on-the-compiler) 中的选项。`k help --man` 会将所有帮助内容以 man 手册页格式打印出来，发行版中将其作为 `man/k.1` 随附提供。

`k completion bash`（或 `zsh`、`fish`、`powershell`）会打印一份用于自动补全命令、选项和 `.k` 文件的脚本。若要在所有终端会话中永久启用：

```bash
k completion bash > ~/.local/share/bash-completion/completions/k
k completion zsh > "${fpath[1]}/_k"
k completion fish > ~/.config/fish/completions/k.fish
k completion powershell >> $PROFILE
```

或者仅在当前 shell 会话中启用：`source <(k completion bash)`。

<a id="12-environment-and-files"></a>
## 12. 环境变量与文件

| 变量 | 说明 |
|---|---|
| `K_HOME` | 已拉取的包、构建中间产物及配置的存放目录。默认值为 `~/.klang`（Windows 上为 `%USERPROFILE%\.klang`）。 |
| `K_CC` | 未指定 `--cc` 时使用的 C 编译器。 |
| `K_STDLIB` | 未指定 `--stdlib` 时使用的标准库目录。 |
| `K_PROGRESS` | `plain` 或 `live`：控制进度信息在 stderr 上的显示方式。 |
| `NO_COLOR` | 当设置且非空时关闭彩色输出，除非 `--color` 另有指定。 |
| `CLICOLOR_FORCE` | 当设为除 0 之外的任意值时强制开启彩色输出，即使 stderr 不是终端。 |
| `K_TCCLIB` | 当编译器被移动到既不在安装目录也不在构建目录的位置时，指定 TinyCC 头文件与支持库所在路径。 |

若未指定 `--stdlib` 或 `K_STDLIB`，编译器会先在可执行文件同级的 `stdlib` 目录中查找标准库，接着向上最多三级父目录查找（可命中发行版和源码树的布局），随后查找编译器构建时指定的目录（无论构建目录位于何处，均指其源码树中的 `stdlib`，或安装包所在位置，即 CMake 的 `K_STDLIB_PATH`），最后查找 `./stdlib`。

`K_HOME/config` 保存适用于所有构建的全局设置。它的写法与附属配置文件相同，目前唯一的配置项为 C 编译器：

<!-- fragment -->
```klang
// ~/.klang/config
cc = "clang";
```

程序的*附属配置文件*（sidecar）是位于 `foo.k` 同级的 `foo.build`，用于指定程序自有的 C 代码、链接的库以及可执行文件名（[packages.md](packages.md#3-a-program-with-c-of-its-own)）。`K_HOME` 下的缓存结构说明参见 [packages.md](packages.md#5-the-cache)。

<a id="13-exit-status"></a>
## 13. 退出状态

| 命令 | 退出状态 |
|---|---|
| `run` | 返回程序的退出码：`main` 返回时为 0，传入 `exit` 的退出码原样返回，程序异常中止时为 1。未通过编译时也为 1。 |
| `build`、`check`、`fetch`、`-o` | 成功时为 0；程序有误、C 编译失败或包拉取失败时为 1。 |
| `format` | 所有文件均已格式化时为 0；`--check` 发现存在未格式化文件时为 1；文件无法解析或选项有误时为 2。 |
| `toolchain` | 未找到 C 编译器时为 1。 |
| 任意命令 | 选项错误、命令拼写错误或文件缺失时为 1。 |

<a id="14-options-for-working-on-the-compiler"></a>
## 14. 用于编译器开发的选项

这些选项用于窥探编译器内部的工作过程，并服务于其测试套件（[testing.md](testing.md)）。`k help internals` 会列出这些选项。

| 选项 | 说明 |
|---|---|
| `--tokens` | 打印文件的词法单元（token）。 |
| `--parse` | 在语法解析之后停止。 |
| `--check` | 在类型检查之后停止，与 `k check` 相同。 |
| `--dump` | 将解析后的程序重新打印为源码。 |
| `--dump-docs` | 打印程序中声明、字段及变体的文档注释。 |
| `--dump-file out` | 将上述转储内容写入指定文件并继续执行。 |
| `--roundtrip` | 检查源码经解析并重新打印后能否与自身保持一致。 |
| `--multi-test file...` | 按独立运行的方式分别编译每个文件，并在每个文件输出结束时向 stdout 和 stderr 追加一行：`==== k --multi-test: exit <code> <file>`。`-o` 中的 `%` 代表每个文件各自的名称。 |
| `--bce-test` | 验证源码中的 `// bce:elide` 与 `// bce:keep` 注解。 |
| `--unsafe-no-rf-check` | 省去可能穿透 `return ... from` 的调用之后的检查。该选项不保证内存安全，仅用于测量这些检查的性能开销。 |
| `--no-copy-elision` | 对大于 256 字节的定长结构体的每个参数和返回值都执行拷贝，而不像通常那样在生成的 C 代码中共享调用方的存储空间。程序在两种方式下的输出应当一致；测试套件会比对二者的结果。 |
| `--gen-runtime-header` | 从 `src/runtime/` 重新生成 `src/runtime_inline.h`（若未找到该目录，可通过 `K_RUNTIME` 指定）。 |
| `--compile-shader file` | 在不构建外围程序的情况下打印着色器（shader）的编译结果；配合 `--shader-source msl` 或 `hlsl` 还会一并打印对应着色器源码。 |

`k format --verify file.k|dir...` 会在内存中格式化每个文件而不写入磁盘。它像正常格式化那样检查结果，并验证再次格式化不会产生任何改动，同时跳过无法解析的文件。在环境中设置 `K_ROUNDS` 会使类型检查器追踪是什么导致递归循环的轮次持续进行。
