<a id="builds-and-packages"></a>
# 构建与包

<details>
<summary>目录</summary>

- [1. 决策](#1-decisions)
- [2. 现状](#2-what-there-is-today)
- [3. 命令行](#3-the-command-line)
- [4. 包](#4-packages)
- [5. 原生部分](#5-native-parts)
- [6. 构建程序](#6-building-programs)
- [7. 编译器自身的构建](#7-the-compilers-own-build)
- [8. 测试](#8-testing)
- [9. 文档](#9-documentation)
- [10. 实施计划](#10-the-plan)
- [11. 暂缓与开放事项](#11-deferred-and-open)

</details>

本文档描述了 `k build` 如何取代手写的 C 编译器调用，以及程序如何从 git 仓库拉取用 K 或 C 编写的库。本文既是设计文档也是迁移计划，基于 `de24a156` 版本的代码树编写，紧随敲定第 1 节各项决策的讨论之后。第 10 节给出了该方案的实施顺序，每次提交保持代码可正常工作。面向用户的文档见 [docs/packages.md](../packages.md)。

<a id="1-decisions"></a>
## 1. 决策

* **单一可执行文件，配合子命令动词（verb）。** `k build foo.k`、`k run foo.k`、`k check foo.k`、`k fetch foo.k`、`klang toolchain`、`k version`。原有的裸写形式 `k foo.k` 保留，含义等同于 `run`。现有所有标志保持不变，作为构建各子命令动词的底层基础设施；此外，`k build --dry-run` 会打印完整命令，方便拥有自有构建系统的用户使用。
* **依赖在根源文件中声明**，形式为带来源的导入语句：`import gfx from "./packages/gfx";`。其余所有文件（包括库自身的文件）均仅按名称导入。非代码类信息（待编译的 C 源文件、系统库、CMake 选项）放在入口文件旁一个可选的附属构建文件（sidecar）中，例如 `foo.k` 旁的 `foo.build`。
* **依赖是扁平的。** 根文件列出程序所需的每一个包，且每个包都固定版本。库使用相同的导入形式说明其依赖；编译器会对照根文件检查这些依赖，若根文件未声明，则报错并指明需要添加哪一行。同一个包在程序中只会存在唯一版本，这也是全程序编译与全局命名空间所要求的。不使用 lockfile、版本范围或中心注册表：来源就是一个固定到 tag 或 commit 的 git 仓库（默认为 GitHub）。
* **AOT 输出采用静态链接。** 共享库仅用于让 JIT 能够针对未编译进编译器的原生库运行程序。
* **编译器自身的构建可以将包编译进内部。** `cmake -DK_BUILTIN_LIBS=all` 会通过 CMake 自带的拉取器获取内置认证包（blessed packages）并将它们链接进 `klang`，此后运行使用这些包的程序时，机器上无需安装 git、CMake 或 C 编译器。默认值为空（none）：核心编译器本身可快速独立构建，而首次运行导入 `gfx` 的程序时会自动将其拉取并构建到用户的缓存中。这一过程只会发生一次，且构建结果由所有代码检出目录共享。README 中的构建说明使用 `all`。
* **TinyCC 转为随库打包（vendored）且成为必选组件。** JIT 是 K 的核心；其子模块将转为像 `cute_spirv` 那样直接复制进仓库的子树，所有 `have_jit` 回退分支均予移除。
* **内置认证的原生模块逐一移出本仓库**，首先是 physics（无编译器侧代码），接着是 ui，然后是共用同一个 SDL 构建的 gfx 和 audio。audio 以及可能包括 ui，将作为这同一个包中的更多模块存放在 gfx 仓库中。
* **调用系统工具，而非取而代之。** 在机器上查找 git、cmake 与 C 编译器；若缺少某工具，会给出清晰易读的错误信息，指明缺失的工具名、需要该工具的包以及解决步骤。v1 版本不提供预编译二进制文件。
* **缓存是全局的**：由 `K_HOME` 指定，默认为 `~/.klang`。程序的可执行文件生成在根源文件旁；所有中间产物均存放在缓存目录下。
* **`import` 相对于导入文件所在的包进行解析**，而非程序所在目录。对于单包程序，这不会带来任何变化。
* **`embed_file(path)`** 与 `embed_shader` 一起作为编译期读取机制，将文件内容嵌入静态数据，以便库能够附带字体或数据表而无需单独分发文件。
* **暂缓事项**：编译器版本号（必要时内置提交计数，绝不使用人工指定编号）、lockfile、自动传递依赖、预编译二进制包、不依赖 git 的拉取方式，以及让测试/示例运行器调用 `k build`（留到最后待其稳定后再改）。

<a id="2-what-there-is-today"></a>
## 2. 现状

JIT 运行命令为 `k foo.k`。同一程序的 AOT 构建过程为

    k --emit-runtime k_runtime.c && cc -O2 -c k_runtime.c
    k -o foo.c foo.k && cc -O2 foo.c k_runtime.o -o foo -lm -pthread

若还要加上 graphics、physics 和 ui，则是

    cc cube.c k_runtime.o -o cube @$(k --ui-link cc) @$(k --gfx-link cc) @$(k --physics-link cc)

必须按此顺序排列，因为 GNU ld 按从左到右的顺序解析静态归档库。完成这一套逻辑的代码位于 `scripts/toolchain.py`，测试、示例和基准测试运行器共用该脚本。它通过 vswhere 查找 MSVC 并导入 `vcvars64` 环境，在 PATH 上查找 gcc 和 clang，按各驱动风格期望的格式写出编译标志，输出一次运行时源码并按每种配置编译，最后追加 CMake 生成的响应文件（response file）。编译器自身从不启动外部进程。

一个原生标准库模块（gfx、audio、physics、ui）目前牵涉八处位置：

* `stdlib/<m>.k`；
* `src/<m>/` 下的 C 层代码，在 `<m>_api.h` 中包含一份函数列表的 X-macro；
* `cmake/<m>.cmake`；
* `third_party/` 下的一个子模块；
* `src/<m>.h`，包含 `have_<m>` 以及响应文件查找逻辑；
* `jit.h` 中的符号注册；
* `codegen_calls.h` 和 `typecheck.h` 中对 `k_<m>_` 前缀的嗅探（判断程序调用了哪个原生层，以及针对 `thread_fn` 的仅主线程规则）；
* 各运行器中的跳过（skip）判断逻辑。

JIT 通过对已链接进 `klang` 自身的函数调用 `tcc_add_symbol` 来获取原生层的函数地址。正因如此，未随编译器一同构建的原生层便无法运行。这些原生层从不反向回调程序（例如 `k_gfx_error` 会填充调用方传入的切片），因此它们可以做成共享库。

`import a.b;` 会先相对于根程序所在目录查找 `a/b.k`，再查找标准库；`import .a.b;` 则相对于导入文件查找。若库使用第一种写法，就会相对于使用它的程序去解析。所有名称都是公开的，且命名空间是全局的，因此两个各自声明 `namespace util;` 的库会发生冲突。`embed_shader` 是目前唯一的编译期文件读取机制。当前没有编译器版本号；链接校验依赖运行时源码的文本哈希。

<a id="3-the-command-line"></a>
## 3. 命令行

命令行格式为 `k <verb> [options] file.k [-- program args]`。
如果第一个参数不是子命令动词，命令行将完全按现状解析，因此 `k foo.k`、`k -o foo.c foo.k`、`k --emit-runtime r.c` 等依然可用；`k foo.k` 等价于 `k run foo.k`。

| 动词 | 作用 |
|---|---|
| `run` | 拉取根文件声明的包，构建程序触及的原生部分，在进程内编译程序（TinyCC）并运行；默认 `-O2` |
| `build` | 前序步骤相同，随后生成 C 代码、编译并链接出位于根源文件旁的可执行文件（`foo`、`foo.exe`）；默认 `-O2`，控制 K 的内联，而 C 编译器在所有级别下都会启用优化 |
| `check` | 拉取所需的 K 源码并执行类型检查；绝不构建 C 代码 |
| `fetch` | 拉取根文件声明的每一个包，不做任何构建；`--update` 会重新拉取非 commit 哈希形式的引用 |
| `toolchain` | 打印将使用的 C 编译器、cmake 和 git 及其原因，或说明未能找到的工具；`--json` 供脚本使用 |
| `version` | 打印编译器版本号（见第 11 节：待引入时的提交计数）以及运行时哈希 |

`build` 的选项：

* `-O0`/`-O1`/`-O2`：控制 K 的内联；C 编译器在所有级别下均进行优化。
* `--debug`：为生成的 C 代码传入 `-DK_DEBUG=1`，并为 C 编译器开启调试信息且关闭优化；包本身仍保持 release 构建。
* `--standalone`：不链接运行时目标文件。
* `-o <exe>`。
* `--cc <name|path>`（见第 6.1 节）。
* `--dry-run`：打印所有命令但不写入任何内容。
* `-v`：在每条命令执行时将其打印出来。
* 与底层命令共用的选项：`-D`、`--include`、`--stack-reserve`、`--stdlib` 和 `--offline`。`--offline` 不进行任何拉取：遇到缓存中不存在的包时会在其 import 处报错，需通过 `fetch` 解决。编辑器的检查功能会传入此标志。

`run` 接受除 `-o`、`--standalone` 和 `--debug` 之外的相同选项。

<a id="4-packages"></a>
## 4. 包

<a id="41-terms"></a>
### 4.1 术语

**包（package）**是一个根目录中包含入口文件 `<name>.k` 的目录，其旁可选包含附属构建文件 `<name>.build`（第 4.7 节），可选包含 C 源码或 CMake 工程（第 5 节），以及所需的任何其他文件。程序本身也是一个包，其入口即为根文件，包根目录即为该文件所在目录，附属构建文件即为 `foo.build`。**模块（module）**指单个 `.k` 文件；`import pkg.sub;` 对应模块 `<pkg root>/sub.k`。标准库同样是一个包，查找方式与现状相同（`--stdlib`、`K_STDLIB`，其次是编译器所在目录旁）。其中带有原生部分的模块会从标准库中移出（第 7 节），纯 K 模块则保留。

包的名称即根文件导入它时使用的名称。它同时作为缓存目录名和入口文件名，按照惯例也是其内部文件声明的命名空间名，但对此并不作强制约束。

<a id="42-declaring-a-dependency"></a>
### 4.2 声明依赖

```klang
import gfx from "./packages/gfx";                         // 随仓库附带的包
import mylib from "github.com/joe/mylib@3f2c1a9";         // 指定主机与 commit
import other from "https://git.example.com/x/other.git@main";  // 任意 git URL 与分支
import local from "../local";                             // 相对于当前文件的路径
```

`import name from "source";` 用于声明包 `name`。其中的 `from` 会像 `return … from f` 中那样作为上下文标识符解析。来源是一个字符串：

* `host/owner/repo@ref` 会从 `https://host/owner/repo` 克隆。仅包含两段的来源 `owner/repo@ref` 表示 `github.com/owner/repo`。带有 `://` 或 `user@host:` 前缀的来源会原样传给 git。
* 对于 git 来源，必须提供 `@ref`。tag 或 commit 哈希属于固定版本；分支名同样会被接受并视为固定版本，直到执行 `k fetch --update` 为止。文档中因此建议优先使用 tag。
* 路径形式（`.`、`..`、`./`、`../`、`/` 或盘符开头）表示原地直接使用的目录，相对于导入它的文件解析。不涉及 git 操作。该形式适用于在调用程序旁同步开发库。

语法规则更新为

    import := "import" "."? ident ("." ident)* ("from" strlit)? ";"

<a id="43-root-only-and-flat"></a>
### 4.3 仅限根文件，且扁平化

带来源的 import **仅在根文件中生效**。在其他任何位置，带来源的 import 都是一条**依赖要求（requirement）**：当该文件作为程序的一部分编译时，根文件必须声明该名称的包，否则编译器会报错并指出修复方法：

    joe/mylib needs the package vec_extras: add
        import vec_extras from "github.com/x/vec_extras@v2";
    to foo.k (packages are declared by the root file only)

如果根文件以不同的来源声明了所要求的包名，将触发错误并列出两个来源。如果根文件声明了相同来源但不同的 ref，则会发出警告并列出两个 ref，因为只有根文件知道哪些版本可以协同工作。某条依赖要求所指的包仅在根文件声明它时才会被加载；绝不会仅因库提出了要求就触发拉取。

因此，程序的依赖关系是扁平且显式可见的：根文件列出所有依赖，而库以编译器可读的形式记录自身需求。库的示例和测试文件各自都是独立的根文件，因此相同的语句在其中会直接生效。如果未来需要传递依赖，这些声明行本就已就位：只需一个 `klang deps add` 命令或 `--auto-deps` 标志，按上述错误信息提示的方式执行即可，无需更改语法格式。

两个包的 C 部分不能互相依赖（第 5.5 节）；库自身的 C 依赖（例如 gfx 包内的 SDL）封装在库内部，对根程序不可见。

<a id="44-resolving-import"></a>
### 4.4 解析 `import`

对于包 P 中某文件内的 `import a.b;`（不带来源）：

1. 查找 P 根目录下的文件 `a/b.k`（`import a;` 即 `a.k`）；对于程序而言，这就是根文件所在目录，与当前行为一致；
2. 查找根文件所声明的名为 `a` 的包，或内置认证包名（第 4.5 节）：即 `<a's root>/b.k`，而 `import a;` 即 `<a's root>/a.k`；
3. 查找标准库中的 `a/b.k`。

如果 P 根目录下存在与根文件显式声明的包同名的文件，则报错（根文件本意是引用该包）；本地文件仍然可以像今天覆盖标准库一样，覆盖内置认证包名或标准库模块。`import .a.b;` 保持不变，相对于导入文件解析。

规范第 11.1 节作相应修改：“相对于*导入文件所属的包根目录*（对于程序本身即根文件所在目录）解析，其次在根文件声明的包中查找，最后在标准库中查找”。

<a id="45-blessed-packages"></a>
### 4.5 内置认证包

内置认证包（blessed packages）列在编译器的 `src/blessed.txt` 表中，CMake 会将其生成进二进制文件，并在第 7 节中读取：

    # import-name  package  source             ref
    physics        physics  ./packages/physics  -
    gfx            gfx      ./packages/gfx      -
    audio          gfx      ./packages/gfx      -
    ui             gfx      ./packages/gfx      -

若一条裸 `import gfx;` 未能由第 4.4 节第 1 步满足，且根文件未显式声明它，编译器便视同根文件写出了该表中对应的那一行；`import audio;` 对应包 `gfx` 中的 `audio` 模块。根文件仍然可以用任意来源自行声明这些名称之一，以试用分支版本（fork）或另一个 ref：显式声明的优先级始终高于该表以及编译器内置副本。这些固定版本会随编译器一同更新：当某个内置认证包的 API 发生变更时，预期使用新 API 的编译器会同步提升其 ref。

<a id="46-fetching-and-the-cache"></a>
### 4.6 拉取与缓存

`K_HOME` 默认为 `~/.klang`（Windows 上为 `%USERPROFILE%\.k`）：

    pkg/<host>/<owner>/<repo>/<ref>/                         代码检出目录，包含子模块
    build/pkg/<host>/<owner>/<repo>/<ref>/<toolchain>/       已构建的原生部分：目标文件或归档库、
                                                             JIT 共享库、link-cc.rsp、link-msvc.rsp、日志
    build/pkg/local/<hash of the absolute path>/<toolchain>/ 路径来源包的对应构建产物目录
    build/native/v1-<input digest>/output/                   受控锁定原生构建的输出产物
    build/native/v1-<input digest>/dependencies/             按需保留的 CMake FetchContent 源码树
    build/runtime/v1-<input digest>/<config>/k_runtime.o
    build/prog/<stem>-<hash of the root's absolute path>/    程序的 C 代码、目标文件与日志
    config                                                   可选配置（第 6.1 节）

tag 或分支使用 `git clone --depth 1 --branch <ref> --recurse-submodules --shallow-submodules <url>`；commit 哈希则使用 `git init`、`git fetch --depth 1 origin <hash>`、`git checkout FETCH_HEAD`，再处理子模块。拉取操作先写入 `<ref>.tmp`，完成后再重命名，因此中断的拉取不会留下残缺的检出目录。拉取与更新操作会在检出目录旁持久化的 `<ref>.lock` 文件上获取跨进程锁；等待锁后，拉取动作会在触碰 `<ref>.tmp` 前重新检查缓存。`run`、`build` 和 `check` 会自动拉取缺失的包，每个包打印一行提示；`fetch` 则预先拉取全部包。处于哈希或 tag 状态的检出目录此后绝不会再被修改；`fetch --update` 会重新拉取分支引用。路径来源的包直接在原地使用。

当 PATH 上没有 git 时：

    cannot find `git` on PATH, needed to fetch the package mylib from
    github.com/joe/mylib@3f2c1a9. Install git, or clone that repository
    yourself and declare it in foo.k as
        import mylib from "<the directory>";

构建拉取的包会运行其指定的 C 编译器，而 CMake 包会运行 CMake：与所有语言的构建脚本一样，来自仓库的代码会在构建时于本机执行。固定到 commit 哈希可以防范 tag 被移动到不同 commit 的风险。

<a id="47-layout-and-the-sidecar"></a>
### 4.7 目录布局与附属构建文件

一个库仓库的最小结构如下：

    mylib.k          入口文件：`namespace mylib;`，对外声明与依赖要求
    mylib.build      附属构建文件，包有构建配置时提供
    sub.k            其他模块：`import mylib.sub;`
    src/*.c, include/    C 源码与头文件（如有）
    examples/, test/     各自作为独立根文件的程序

附属构建文件由 K 词法分析器解析，因此语法看似 K：`//` 注释、`key = value;`、字符串、`true`/`false`、整数以及 `[a, b, c]` 列表。出现未知键会报错并列出所有已知键名。

| 键 | 适用范围 | 含义 |
|---|---|---|
| `c_sources` | 两者皆可 | 相对于包根目录的 C 文件列表，由探测到的 C 编译器编译（第 5.1 节）；单头文件库只需提供一个定义实现宏并包含该头文件的 `impl.c` |
| `c_include` | 两者皆可 | 头文件搜索目录 |
| `c_headers` | 两者皆可 | 相对于包根目录的头文件，按与 `--include` 相同的方式插入到使用方生成的 C 代码中（位于类型定义之后、extern 原型与函数体之前）；仅使用此项不会产生原生部分 |
| `c_defines` | 两者皆可 | `NAME` 或 `NAME=VALUE` |
| `c_flags_cc`, `c_flags_msvc` | 两者皆可 | 按驱动风格区分的额外编译标志 |
| `c_libs` | 两者皆可 | 按名称指定的系统库（`m`、`user32`）；编译器会按各自驱动风格的格式输出 |
| `cmake_target` | 两者皆可 | 包根目录包含一个 CMake 工程，此项指定要构建并链接的静态库目标名（第 5.2 节） |
| `cmake_options` | 两者皆可 | 用于 configure 阶段的 `NAME=VALUE` 缓存条目 |
| `dylibs` | 两者皆可 | `name=spelling` 条目：用于打开 `extern "name" { ... }` 块所绑定共享库的文件名（规范 §7.10）。包的条目映射其自身名称并作为默认值；程序的条目可为任何包映射名称，并在各平台分别优先覆盖。映射未在任何块中声明的名称会报错 |
| `main_thread_only` | 两者皆可 | 其函数不得从 `thread_fn` 中调用（第 5.4 节） |
| `exclude` | 两者皆可 | 相对于包根目录、其中 `.k` 文件不属于该包的目录：JIT 库的符号表（第 5.3 节）不会在这些目录中查找 extern fn，也不会在根目录以下拥有自己附属构建文件的任何目录中查找 |
| `output` | 仅限程序 | 相对于根文件所在目录的可执行文件名称 |

每个 `c_*` 键都可加上 `_windows`、`_linux` 或 `_macos` 后缀，表示仅针对该平台的部分。程序的附属构建文件使用相同的键列出其自身的 C 配置；通过 `--include` 引入库 C 代码的程序依然可用，因为头文件会像现状一样直接拼接到生成的 C 代码中。

`c_headers` 会保留列表顺序，排在命令行头文件之后，且程序自身的附属构建文件优先，各包按加载顺序紧随其后。这些头文件可以使用已声明 extern 的类型（即便该 extern 未被使用），但不能使用未导入模块中的类型。它们必须支持使用方的 C 后端；纯头文件包可以通过 TinyCC 运行而无需外部工具链。原生源文件配置（`c_include`、`c_defines`、`c_flags_*`）不适用于这些头文件。

<a id="5-native-parts"></a>
## 5. 原生部分

拥有非空 `c_sources` 或指定了 `cmake_target` 的包具有原生部分。它在 release 配置下构建，在 Windows 上链接静态 C 运行时。它会构建为两种形态：供 AOT 程序链接的产物，以及供 JIT 加载的共享库。首次有 `run` 或 `build` 触及该包时会构建第一种形态，而首次 `run` 会基于相同的目标文件构建共享库；`check` 从不执行构建。

输入均带有时间戳标记（stamp）。对于路径包，只要其任一 C 源文件（按路径、修改时间与大小判断）、附属构建文件或 `extern fn` 集合发生变化，就会触发重新构建。所有包（包括 git 检出包）在 `klang.h` 发生变化时都会重新构建。`klang.h` 是编译器提供给原生 C 代码的头文件，用于处理 C 自身无法声明的值（例如构建器的 `k_rref`，规范 §7.10）。

触及同一个包的每个程序与进程都在同一个目录中构建它，并受该目录中持久化的 `build.lock` 跨进程锁保护。曾等待锁的进程在开始构建前会重新检查标记。发现标记仍有效的构建无需加锁即可直接读取响应文件与标记。两者均先写到最终路径旁再原子重命名就位，且标记最后写入。

固定到 Git commit 的导入使用一个受控缓存条目，其键由完整修订号、检出路径、构建配方、工具链以及声明的原生输入共同决定。声明的源文件与包含路径必须位于检出目录内部，才能使用该受控条目。该条目的共享租约涵盖读取阶段直至原生链接或 JIT 进程退出。写锁在该租约范围内序列化构建操作。

普通 C 的 AOT 与 JIT 构建共享目标文件；JIT 额外增加符号表与共享库。CMake 的 AOT 与 JIT 配置则使用各自独立的条目。清理（pruning）操作会获取排他租约，并在删除生成的输出之前撤销发布构建标记（build stamp）。它会保留 Git 检出目录以及 CMake FetchContent 填充的 `-src` 目录，以便在依赖已拉取的前提下仍能离线重复同一构建。包含共享库的 CMake AOT 构建会保留其输出目录树，因为链接后的可执行文件在运行时可能引用这些路径。

<a id="51-plain-c-sources"></a>
### 5.1 普通 C 源文件

`c_sources` 中的每个文件都会结合包的包含路径、宏定义与编译标志编译为目标文件。目标文件以文件名及其路径哈希命名，因此不同目录下的同名文件不会共用同一个目标文件。AOT 形态即这些目标文件本身，它们按顺序写在响应文件中且排在 `c_libs` 之前，因而不需要归档工具（archiver）。JIT 形态则通过同一驱动（`-shared` 或 `/LD`）将这些目标文件、`k_symbols.c`（第 5.3 节）和系统库链接为 `k_jit_<name>.{dll,so,dylib}`。只有 `.c` 文件按此方式编译；C++ 需通过 CMake 构建。

<a id="52-cmake-packages"></a>
### 5.2 CMake 包

对于 `cmake_target`，编译器会在包的构建目录中生成一个包装工程，并对其执行 configure 与构建，而绝不直接操作包自身的 CMakeLists：

```cmake
cmake_minimum_required(VERSION 3.20)
project(k_pkg_physics C)
# C++, and on macOS Objective-C and Objective-C++, where there is a compiler.
include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
    enable_language(CXX)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_subdirectory("<package root>" pkg EXCLUDE_FROM_ALL)
if(NOT TARGET <cmake_target>)
    message(FATAL_ERROR "... names the CMake target <cmake_target>, which ... does not define")
endif()
# The JIT library: the symbol table pulls what it names out of the target.
add_library(k_jit_physics SHARED k_symbols.c)
target_link_libraries(k_jit_physics PRIVATE <cmake_target>)
# A probe whose link line is the package's whole link closure.
add_executable(k_linkprobe k_linkprobe.c)
target_link_libraries(k_linkprobe PRIVATE <cmake_target>)
```

该包装工程在配置时会传入 `-DCMAKE_BUILD_TYPE=Release`、探测到的 C 与 C++ 编译器、`cmake_options` 以及生成器（若 PATH 上有 Ninja 则用 Ninja，否则使用平台默认生成器），并通过 `--config Release` 构建。上次成功执行的 configure 命令会保存在构建目录旁。一旦命令发生变化（源于修改了 `cmake_options`，或前一次 configure 失败），便会清空缓存重新配置。包装工程及其两个 C 文件仅在内容发生变化时才会被写入，因此无变更的构建不会重新编译或重链接任何内容。这一点很重要，因为在 Windows 上，已被另一进程加载的 JIT 库无法被覆写。

供 AOT 程序使用的响应文件记录了 `<cmake_target>` 的完整链接闭包：目标自身的归档库、它依赖的所有静态库，以及其后的系统库与链接选项（`-lm`、`-framework Cocoa`、`user32.lib`），且严格按顺序排列。CMake 没有用于获取传递链接闭包的生成器表达式；目前 `cmake/sdl.cmake` 是手工从 SDL 的收集器目标中提取闭包的。通用方案是采用 CMake 的 file API。配置前，编译器在构建目录中写入 `.cmake/api/v1/query/codemodel-v2`，随后读取回复中 `k_linkprobe` 对应的目标对象，其 `link.commandFragments` 即生成器将传入的完整链接行（包含 `libraries`、`libraryPath`、`frameworkPath` 和 `flags` 等角色）。相对于构建目录的路径会转换为绝对路径。这需要在编译器内实现一个小型 JSON 读取器。如此一来，任何带有 CMakeLists 的项目都能直接成为 K 包，包括直接绑定的上游库（`cmake_target = "box3d"`），无需专门为 K 编写任何 CMake 代码。

当 PATH 上没有 cmake 时：

    cannot find `cmake` on PATH, needed to build the C part of the package
    physics (packages/physics, target k_physics).
    Install CMake 3.20 or later.

<a id="53-the-symbol-table-and-the-jit"></a>
### 5.3 符号表与 JIT

JIT 需要程序所调用的每一个 C 函数的地址。目前 `jit.h` 从原生层的 X-macro 列表中获取它们，这也是只有编译进编译器的原生层才能运行的原因。新方案中，编译器根据包自身的声明生成一个 C 文件，并且每个包的 JIT 库都会链接该文件：

```c
/* k_symbols.c，为 physics 包生成。 */
typedef struct { const char *name; const void *addr; } k_symbol;
void k_phys_create_world(void); void k_phys_step(void); /* … */
K_EXPORT const k_symbol *k_symbols(void) {
    static const k_symbol table[] = {
        { "k_phys_create_world", (const void *)&k_phys_create_world },
        { "k_phys_step", (const void *)&k_phys_step },
        /* … */
        { 0, 0 } };
    return table;
}
```

表中的名称来自包根目录下所有 `.k` 文件中每一个 `extern fn` 的 C 符号名，但不包括附属构建文件 `exclude` 指定的目录，以及根目录之下自身含有附属构建文件的目录（第 4.7 节）。编译器为此会解析这些文件。函数原型只是占位声明，因为取函数地址并不需要完整签名。每个库只导出一个符号，因此 Windows 下无需导出列表。链接该符号表会将定义这些函数的目标文件及其依赖精确拉入共享库中。

JIT 运行时随后处理程序代码实际触及到其 extern 函数的每一个包（codegen 已经会收集 `usedexterns`，且每个 extern 的声明文件都记录了其所属包）。它通过 `LoadLibrary` 或 `dlopen` 加载 `k_jit_<name>`，调用 `k_symbols`，并在重定位前把每一项传给 `tcc_add_symbol`。当前 `AddGfxSymbols` 及其同类函数是通过静态表完成这一步的。编译进编译器内部的包（第 7 节）拥有相同结构的符号表，以 `k_symbols_<name>` 命名并链接在内，并列于生成的 `builtin_packages.h` 中。内置包无需共享库；而显式声明的包始终通过其共享库加载。`NativeLayers` 及 `k_<m>_` 前缀嗅探将被移除。

包只能为自身 C 部分或 C 标准库所定义的函数声明 `extern fn`。运行时的 `k_os_*` 和 `k_sqrt` 属于保留在标准库中的 `os` 和 `math`。若有包声明其中之一，其 JIT 库将链接失败，且报错会指出该符号名并说明其属于运行时。

<a id="54-main-thread-only-packages"></a>
### 5.4 仅限主线程的包

附属构建文件中的 `main_thread_only = true;` 取代了 `CheckThreadGlobals` 中硬编码的前缀列表。从 `thread_fn` 中调用此类包声明的 extern 函数会给出与现状相同的错误，并指明包名。gfx、audio、physics 和 ui 均启用此项。

<a id="55-rules"></a>
### 5.5 规则

* 包的 C 部分是自包含的：不能包含其他包的头文件或调用其函数。跨包协作在 K 层完成，正如今天 ui 通过 gfx 绘图一样。必须共享 C 代码的包应共处同一个仓库。
* 链接顺序遵循第 4.3 节的依赖要求：一个包排在它所要求的包之前链接，因此 ui 的响应文件位于 gfx 之前，这正是 GNU 链接器所需要的。其余情况下，包按根文件的声明顺序链接。
* 包以 release 配置构建，每个工具链只构建一次；`--debug` 仅影响程序本身及运行时目标文件。程序自身的 `c_sources` 跟随 `--debug`，其 debug 构建会与 release 构建分开存放（位于程序构建目录下的 `native/<toolchain>-debug`）。CMake 部分（包括程序自身的 CMake）始终以 release 配置构建。
* 如此构建的归档库可链接进由同一工具链构建的程序中。工具链 ID（第 6.1 节）包含在构建路径中，因此切换 `--cc` 会重新构建包，而不会混用工具链。

<a id="6-building-programs"></a>
## 6. 构建程序

<a id="61-toolchains"></a>
### 6.1 工具链

C 编译器按以下顺序选择：`--cc`，其次 `K_CC`，其次 `K_HOME/config` 中的 `cc = "…";`（采用附属构建文件格式），最后执行自动探测：

* **Windows**：若 PATH 上已有 `cl`（例如开发者命令提示符环境），则使用它；否则在 Visual Studio LLVM 目录或 PATH 上查找 `clang-cl` 或 `clang`（两者都能自行定位 MSVC 头文件与 Windows SDK，无需额外环境变量）；再否则通过 vswhere 与 `vcvars64.bat` 查找 `cl`。该环境会按 `toolchain.py` 的方式导入，并以安装路径与版本为键缓存在 `K_HOME` 中，因为导入该环境通常需要一两秒钟。
* **Linux**：在 PATH 上依次查找 `cc`、`gcc`、`clang`。
* **macOS**：`cc`（Apple clang）。

单次命令执行只探测一次工具链，并用它构建所有产物：原生部分、运行时目标文件以及最终链接。“在 PATH 上”指的是编译器启动时所处的 PATH。在 Windows 上构建 CMake 包会导入 vcvars64 环境，这会把 `cl` 放入 PATH，但无论此操作此前是否已在进程中发生，自动探测都会选中同一个工具链。

`--cc` 接受名称（`msvc`、`clang-cl`、`clang`、`gcc`）或可执行路径。每个工具链都有一个*风格（style）*，即 `msvc` 或 `cc`，它决定了所有编译标志的语法，以及包的构建应写出哪份响应文件。这与当前 `toolchain.py` 中的 `CC` 类及 `--gfx-link msvc|cc` 的划分完全一致。每个工具链还有一个 *ID*，格式为 `<name>-<version>-<8 hex of the driver's path and style>`，用于命名缓存目录。`cmake` 和 `git` 在 PATH 上查找，在 Windows 上还会额外查找 Visual Studio 安装 cmake 的路径。`k toolchain` 会打印以上全部信息。

外部进程由一个小型跨平台运行器启动（`src/process.h`：`CreateProcessW`、`posix_spawn`）。进程输出会被捕获到构建目录的日志文件中，一旦失败，便连同失败的命令一并显示出来。`-v` 会打印每条执行的命令，`--dry-run` 则仅打印不执行。进程运行期间，`src/progress.h` 会向用户汇报进度，同时避免刷屏污染日志。每个执行实际工作的步骤（拉取、构建包的原生部分、编译运行时目标文件）各打印一行信息。仅当 stderr 是终端时，还会额外显示一行原地刷新的实时状态行，展示当前运行的命令、已运行时间以及它打印的最后一行输出。`K_PROGRESS=plain|live` 可覆盖终端检测行为。

在 Windows 上找不到任何 C 编译器时：

    cannot find a C compiler (looked for cl, clang-cl and clang on PATH and in
    Visual Studio, for clang in Program Files\LLVM, and for gcc and cc on PATH);
    install Visual Studio with C++ or LLVM, or set K_CC

<a id="62-the-runtime-object"></a>
### 6.2 运行时目标文件

运行时目标文件在首次使用时由与 `--emit-runtime` 相同的源码编译而成。其缓存路径为 `build/runtime/v1-<input digest>/<config>/k_runtime.o` 或 `k_runtime.obj`。输入摘要覆盖生成的源码、C 驱动标识与横幅版本信息、编译器标志以及已知的目标平台和 SDK 环境变量设置。同一个 `K_HOME` 下的不同项目在这些输入一致时会复用同一个目标文件。编译器通过原子重命名发布完成构建的目标文件。构建过程会持有一个共享租约直至链接器结束，而清理操作在回收目录前会获取该租约的排他锁。零长度的租约文件会保留在原处，以便并发命令继续在同一文件上协调同步。`--standalone` 会跳过运行时目标文件。
[缓存使用与清理](../command_line.md#cache-usage-and-pruning)

<a id="63-what-k-build-does"></a>
### 6.3 `k build` 的执行流程

1. 解析根文件；拉取缺失的已声明包（第 4.6 节）；解析其余文件；执行类型检查；将 C 代码生成至 `build/prog/<stem>-<hash>/foo.c`。
2. 对于程序触及的每个带有原生部分的包，若尚未针对当前工具链构建，则构建之（第 5 节）。
3. 若运行时目标文件缺失则编译之（第 6.2 节），如有程序自身的 `c_sources` 也一并编译。
4. 编译 `foo.c` 并执行链接：包含程序的目标文件、运行时目标文件、按链接顺序排列的各包响应文件，以及对应风格所需的 `-lm -pthread`；通过 `/Fe:` 或 `-o` 输出到根源文件旁的可执行文件，或输出到附属构建文件 `output` / 命令行 `-o` 指定的路径。

在 macOS 上，若 release 可执行文件使用系统 Apple Clang、选定的 SDK 构建，且没有自定义 C 头文件、宏定义或原生部分，则可直接复用已有的可执行文件。若生成的 C 代码内容与之前一致，文件会保留在原处不作覆盖。在跳过第 4 步之前，编译器会核对可执行文件、工具链、SDK、运行时目标文件以及 Clang 与链接器报告的输入文件。针对同一根文件的构建会在代码生成与链接期间持有一把锁。链接成功后通过原子重命名发布可执行文件。在此模式下，如果需要新的运行时目标文件，编译器会将其与程序的 C 代码一并编译，待两者皆就绪后再执行链接。

`--dry-run` 会将第 2 到第 4 步以命令形式连同响应文件路径一起打印出来，便于 Makefile 或 CMake 工程直接复制使用。现有的标志（`-o foo.c`、`--emit-runtime`、`--gfx-link` 等）继续保留，作为服务于相同目的的底层设施；`--<m>-link` 将变为“包 m 的响应文件”的轻量别名，并一直保留至运行器完成迁移（第 10 节）。

<a id="64-k-run-with-packages"></a>
### 6.4 配合包使用 `k run`

`k run` 在第 2 步之前完全相同；随后在进程内编译程序并加载各包的 JIT 库（第 5.3 节）。包含 `thread_fn` 的程序仍需使用 `build`（TinyCC 无法在内存中布置线程局部存储；这一点保持不变）。

<a id="65-embed_file"></a>
### 6.5 `embed_file`

`embed_file("path") -> const u8[:]` 返回文件的字节内容。文件在程序编译时读取，路径与 `embed_shader` 的文件形式一样相对于调用文件解析，并以相同方式输出到静态数据区，每个不同文件仅嵌入一次。参数必须遵循 `embed_shader` 的规则，为字符串字面量或持有该字面量的 `let`/`const` 全局变量。这使包能够随附字体或数据表而无需依赖运行时文件，因为 `resource_dir()` 无法为库定位这类文件。

<a id="7-the-compilers-own-build"></a>
## 7. 编译器自身的构建

<a id="71-builtin-packages"></a>
### 7.1 内置包

`K_BUILTIN_LIBS` 是一个 CMake 缓存变量：默认为空，也可设为 `all` 或一组内置认证包名称列表。对于其中每个包，CMake 会读取 `src/blessed.txt`，添加该包的工程，将其 `cmake_target` 链接进 `k`，并生成其符号表。该符号表就是同样的 `k_symbols.c`，由一段 CMake 正则匹配 `extern fn name` 行及其尾部的 `= "…"` 生成，因为此时尚无编译器可以解析它们。最后，CMake 通过与第 5.2 节相同的包装工程和 file-API 探测（两者共用 `cmake/package.cmake`），在 `pkg/…/<config>/` 下生成 `link-cc.rsp` 与 `link-msvc.rsp`。编译器会记录该目录，并在查找内置包的响应文件时先检查该目录，然后再像查找包源码一样检查自身可执行文件旁的路径。带有纯 C 附属构建文件的内置包也按同样方式构建，只是省去 `add_subdirectory`：由 CMake 直接编译其 `c_sources`。

`FETCHCONTENT_SOURCE_DIR_K_PKG_GFX` 可将构建指向本地检出目录，这也是对照编译器同步开发包的方式。

发行版二进制包与 CI 均配置为 `all`。开发者首次执行 `cmake -B build` 时，仅构建编译器本身所需时间极短，而图形示例只需先经历一次拉取并构建至 `~/.klang` 后即可运行，且所有工作树共用该缓存。

打包后的发行版（`.github/workflows/release.yml`，由 `scripts/package_release.py` 组织目录）没有构建树可供查找。取而代之的是，每个内置包都位于编译器旁的 `pkg/<name>/` 中：包含去除了 vendored 依赖后的源码，以及位于 `lib/` 下其链接闭包内的全部静态库，该目录下的响应文件以相对自身的路径引用这些库。链接时会把这些响应文件复制到 `K_HOME/build/builtin/` 下并将路径转为绝对路径，每处解压编译器的位置各自对应一份副本。

<a id="72-what-stays-in-the-compiler"></a>
### 7.2 保留在编译器中的部分

`embed_shader`、`src/shaderc.c` 和 `third_party/cute_spirv` 保留在编译器中：着色器编译器是任何图形包都可使用的编译器特性。它生成的二进制数据块（blob）是编译器与 gfx 包之间的契约：`src/gfx/gfx_blob.h` 移至 `src/gfx_blob.h`，包内携带一份副本，同时该头文件增加一个版本常量，供原生层在读取 blob 时校验。在编译期检查的 SDL_GPU 绑定规则同样保留在原处。

<a id="73-tinycc"></a>
### 7.3 TinyCC

该子模块转为同路径 `third_party/tinycc` 下的直接复制子树：包含 `cmake/libtcc.cmake` 构建所需的源码（`libtcc.c`、`tcc.c`、x86-64/i386/arm64 目标平台的 `.c` 和 `.h` 文件、`tccpe.c`、`tccmacho.c`、`include/`、`lib/`、`win32/include` 和 `win32/lib`）、`VERSION`、`COPYING`、`RELICENSING`，以及一份记录上游 commit 与更新方式的 README（类似 `third_party/cute_spirv/README.md`）。移除 `K_LIBTCC` 与 `K_HAVE_LIBTCC`，`have_jit` 恒为 true。未指定 `-o` 时让 `klang` 退回输出 C 文件的逻辑，以及运行器中的 `have_jit` 探测，一并移除。编译器仅在经过测试的 x86-64 与 arm64 平台上构建。

<a id="8-testing"></a>
## 8. 测试

* `test/packages/` 包含若干根文件测试夹具（fixture），它们从同目录下的路径来源声明包。这些测试覆盖带有 `import pkg.sub;` 的纯 K 包，以及依赖要求分别处于已满足、缺失、来源不同或 ref 不同的情况。缺失依赖要求应报出对应行号；来源不同应报错，ref 不同应报警告。其余夹具覆盖包与本地文件冲突、JIT 运行和 AOT 构建的纯 C 包，以及一个 CMake 包（若 PATH 上没有 cmake 则附带提示跳过）。测试还覆盖从 `thread_fn` 中调用 `main_thread_only` 包，以及 `embed_file`。
* Git 来源：运行器使用 `git init` 创建一个带 tag 的裸仓库，测试夹具分别通过 `file://…@tag` 与 `@<hash>` 声明该包，从而离线验证拉取路径。若缺少 git 则跳过这些测试。
* 当运行器迁移完成后（第 10 节最后一步），对 `run_samples.py` 所构建的每一个示例使用 `k build` 构建，并与相同的预期输出比对。
* 在内置认证包仍处于内置认证阶段期间，它们自身的测试（`test/gfx/`、`test/physics/`、`test/ui/`、`test/audio/`）继续留在本仓库的测试套件中。CI 会配置 `K_BUILTIN_LIBS=all`，因为它的 gfx 测试本来就需要已安装的 Linux 系统包；而 sanitizer 任务则配置为 none，从而像今天一样覆盖无内置包的构建路径。`api_check.py` 随各个包一并迁至其各自仓库。
* 在运行器的跳过检测逻辑中，原本的 `"built without SDL3"` 及类似字符串替换为第 4.6、5.2 和 6.1 节的工具缺失错误信息，并按相同方式匹配。

<a id="9-documentation"></a>
## 9. 文档

以下文档将同步更新：

* 规范 §11.1（包根目录、带来源的导入、解析顺序、扁平化依赖要求）、附录 D（语法规则行）、§12（`embed_file`）。
* `implementation.md` §1（子命令动词、`process.h`、`toolchain.h`、`packages.h`）、§7（取代 X-macro 注册的符号表）。
* `stdlib.md`（gfx、audio、physics 与 ui 移至各自仓库编写文档，并在本文中提供链接）。
* `testing.md`。
* `README.md`（使用 `-DK_BUILTIN_LIBS=all` 构建，并用 `k build foo.k` 取代手工 cc 命令）。
* `samples/README.md`。
* `tutorial.md` 中涉及构建程序的部分。
* 被移出包的设计文档，随包一并迁移。
* `vscode/`：新增调用 `k build` 的 Build 命令。

<a id="10-the-plan"></a>
## 10. 实施计划

每一步对应一次或几次提交，且每一步提交后所有测试都必须通过。各步骤经过排序，避免任何重复构建，并确保在完全覆盖前旧路径始终保持可用。

1. **将 TinyCC 纳入仓库源码并使 JIT 成为必选**（第 7.3 节）。删除子模块条目和 `K_LIBTCC` 选项；移除所有 `have_jit` 分支、`.c` 回退路径以及运行器的探测逻辑。CI 暂不移除其他内容。
2. **子命令动词与进程运行器。** 引入 `src/process.h`；`Main` 根据动词分发，否则沿用现状解析命令行；实现 `k run`、`k check`、`k version`（仅含运行时哈希）。暂不改变原有行为。
3. **工具链与 `k toolchain`**（第 6.1 节）：引入 `src/toolchain.h`，实现探测顺序、带缓存的 vcvars 导入、`K_HOME` 与 `K_HOME/config`。
4. **面向现有功能的 `k build`**：运行时目标文件缓存（第 6.2 节）、生成程序 C 代码、链接、`-o`、`--standalone`、`--debug`、`-v`、`--dry-run`，原生模块仍通过当前沿 `--gfx-link` 方式查找到的响应文件进行链接。至此 `k build samples/27_gfx_cube.k` 已可正常工作。README 展示该用法；运行器暂不动。
5. **包根目录**（第 4.4 节第 1 步）：驱动器获得 `Package` 记录，每个源文件携带其所属包信息，import 相对于导入文件所在的包根目录解析。对现有程序无可见变化。
6. **从路径来源声明包**（第 4.2、4.3、4.4 节）：实现解析器的 `from` 语法、仅限根文件生效规则、依赖要求及其诊断信息、`import pkg.sub;` 以及冲突检查规则。开始建立 `test/packages/`。更新规范 §11.1 与附录 D。
7. **Git 来源与缓存**（第 4.6 节）：实现 `fetch`、自动拉取、`--update` 以及 git 缺失报错。添加离线 `file://` 测试。
8. **`embed_file`**（第 6.5 节），独立于其他所有步骤；可在此后的任意时机合入。
9. **附属构建文件与程序自身的 C 代码**（第 4.7、5.1、5.3 节）：实现附属构建文件解析器、程序上的 `c_*` 键、生成符号表、JIT 共享库及其加载。在引入任何库之前，程序是第一个拥有原生部分的包。
10. **纯 C 包**（第 5.1 节）：按包粒度复用相同机制，按工具链缓存，实现链接顺序。添加带有 `.c` 文件的夹具，同时覆盖 JIT 与 AOT。
11. **CMake 包**（第 5.2 节）：实现包装工程、file-API 探测与 JSON 读取器，以及响应文件生成。添加带有小型 CMakeLists 的夹具，无 cmake 时跳过。
12. **内置包与内置认证表**（第 4.5、5.4、7.1 节）：引入 `src/blessed.txt`、`K_BUILTIN_LIBS`、带 CMake 侧符号表生成的 `cmake/package.cmake`、`builtin_packages.h`、`main_thread_only`。首先将仓库内的四个模块**原地重构为包的形态**：`packages/physics/` 包含 `physics.k`、`physics.build`、`CMakeLists.txt` 和 `src/`，Box3D 子模块仍在 `third_party/` 下，`blessed.txt` 指向仓库内路径。原生层代码移动但行为保持不变，并移除 `cmake/physics.cmake`、`src/physics.h`、前缀嗅探以及 `jit.h` 中的注册；按模块分次提交：physics、ui、audio、gfx。
13. **独立 physics 包**，在 `packages/physics` 下包含其 API 检查与文档。其 CMake 构建改为解析固定版本的 Box3D 来源。
14. **独立 gfx、audio 与 ui 包**，共同置于 `packages/gfx` 下。其 CMake 构建改为解析固定版本的 SDL 与 Nuklear 来源。CI 使用 `K_BUILTIN_LIBS=all` 构建这些可选包。
15. **运行器改由编译器驱动**：`toolchain.py` 改为调用 `k toolchain --json` 而非自行探测，示例运行器改为使用 `k build` 构建；测试运行器在需要按优化级别传参、第二前端或多程序批处理的场景下仍自行驱动 C 编译器。退役 `--<m>-link`。VS Code 扩展新增 Build 命令。

<a id="11-deferred-and-open"></a>
## 11. 暂缓与开放事项

* **版本号。** 当某个库需要声明“需要编译器 N 及以上版本”时，可由 CMake 将 `git rev-list --count HEAD` 与提交日期烘焙进 `k version`；从分支构建的版本则会报告其为分支构建。无需人工指定编号，也无需打 tag。
* **按需支持传递依赖**（第 4.3 节），若分支引用在实际中被频繁使用则引入 lockfile，以及 `klang vendor`。
* **不依赖 git 的拉取**（通过 HTTPS 获取 GitHub tarball），以及在三大平台乘以多种配置的构建工作量值得投入时提供预编译包二进制文件。`klang.h` 复用了 codegen 输出的布局（`k_stack`、`k_rhdr`、`k_rref`），从源码构建的原生部分会在其变更时自动重建；而预编译部分则需要让这些布局携带版本号并据此校验。
* **运行时包数据**：库的随附文件在 JIT 下可通过 `package_dir("gfx")` 访问，在 `build` 时则复制到可执行文件旁。在此之前由 `embed_file` 满足字体与数据表需求。
* **构建步骤**：在程序编译期间运行某个工具的包（例如自带的着色器编译器）。自然形态是一个由编译器在 JIT 下运行的 K 程序，这样工具本身无需 C 编译器；附属构建文件将负责指定该工具。此项已有规划，尚未实现。
* 将**仓库子目录**作为包根目录、在 `c_sources` 中支持 C++、为无控制台窗口程序提供 `windows_subsystem` 键、`klang test`、`klang clean`。
* **命名空间隐私控制**仍不在考虑范围内（第 12 节）；库无法隐藏内部实现，且包内文件以包名作为命名空间的惯例也仅作为惯例存在。
