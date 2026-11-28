<a id="debugging-k-programs"></a>
# 调试 K 程序

<details>
<summary>目录</summary>

- [0. 概要](#0-summary)
- [1. 调试器今天看到的内容](#1-what-a-debugger-sees-today)
- [2. 预期目标](#2-what-is-wanted)
- [3. 方案 A：原生调试信息与现有调试器](#3-option-a-native-debug-information-existing-debuggers)
  - [3.1 源码行号：`#line`](#31-source-lines-line)
  - [3.2 让生成的 C 代码贴近源码](#32-keeping-the-c-close-to-the-source)
  - [3.3 展示 K 值](#33-showing-k-values)
  - [3.4 编辑器](#34-editors)
  - [3.5 测试](#35-testing)
  - [3.6 无法做到的事](#36-what-it-cannot-do)
- [4. JIT](#4-the-jit)
- [5. 方案 B：`k dap`，内置于编译器中的调试器](#5-option-b-k-dap-a-debugger-in-the-compiler)
  - [5.1 形态](#51-shape)
  - [5.2 停止：语句停靠点](#52-stopping-statement-sites)
  - [5.3 栈帧与值](#53-frames-and-values)
  - [5.4 K 表达式](#54-expressions-in-k)
  - [5.5 已构建程序与线程](#55-built-programs-and-threads)
  - [5.6 无法做到的事](#56-what-it-cannot-do)
  - [5.7 维护成本](#57-what-it-costs-to-keep)
- [6. 方案 C：位于 lldb-dap 前端之前的 `k dap`](#6-option-c-k-dap-in-front-of-lldb-dap)
  - [6.1 形态](#61-shape)
  - [6.2 调试映射表](#62-the-debug-map)
  - [6.3 适配器展示的内容](#63-what-the-adapter-shows)
  - [6.4 它的依赖项](#64-what-it-depends-on)
  - [6.5 无法做到的事](#65-what-it-cannot-do)
  - [6.6 需要收紧的地方](#66-what-to-tighten)
- [7. 三种方案对比](#7-the-three-compared)
- [8. 推荐方案与实施顺序](#8-recommendation-and-order-of-work)
- [9. 开放问题](#9-open-questions)
- [参考资料](#references)

</details>

研究笔记，2026 年 11 月。本文内容均尚未合入 master 分支。核心问题是：鉴于编译器输出的是 C 代码，如何在 VS Code 及其他编辑器中调试 K 程序——默认情况下，调试器看到的是这些 C 代码，使用的是 C 名称和 C 类型。本文对比了三种方案。方案 A 向 C 编译器的调试信息中写入 K 的源码行号与名称，并将调试工作交给本就支持调试适配器协议（Debug Adapter Protocol，DAP）的原生调试器，在其中通过可视化器展示 K 值。方案 B 在编译器中内置一个调试器 `k dap`，由它自行运行程序并以 K 的语义进行展示。方案 C（已在某个分支上的原型中实现）同样是一个 `k dap`，但它位于 LLDB 的适配器前端之前：由 LLDB 运行程序，编译器适配器则将 LLDB 上报的内容转换为 K 的视图。第 3 节为方案 A，第 5 节为方案 B，第 6 节为方案 C，第 7 节对三者进行对比，第 8 节给出推荐方案与构建顺序。

<a id="0-summary"></a>
## 0. 概要

**推荐方案 C。** `#line` 指令会在每款 C 编译器的调试信息（PDB 或 DWARF）中将生成的 C 代码映射回 K 源码行，这是所有方案都需要的基础。`k build --debug` 会生成一份映射表，记录每个函数的局部变量及每种类型的表示方式；`k dap` 一面向编辑器提供 DAP 接口，一面在底层驱动 `lldb-dap`：LLDB 负责断点、单步执行、线程与栈展开，适配器则按 K 类型从内存中读取值、按源码方式命名栈帧与局部变量、跳过编译器生成的代码并在检查失败时停下。关于 K 的知识集中在编译器这一处，随代码生成器同步演进并由测试套件覆盖，任何支持 DAP 客户端的编辑器只需运行同一条命令即可。它的局限在于不仅依赖协议本身，还依赖 `lldb-dap` 的具体行为，并且无法覆盖 Microsoft 的调试器。

**方案 A** 则是把关于 K 的知识放进调试器里：为 Microsoft 的调试器按程序生成一份 natvis 文件，为其余调试器提供一份 LLDB 模块，两者解释同一种描述信息，并在 `--debug` 模式下重塑生成的 C 代码，使 C 调试器更易读取。它能保留对 Visual Studio 与 `cppvsdbg` 的支持，但需要针对同一种描述维护两套解释器，呈现的是 C 名称与 C 表达式，且每个编辑器都需要单独配置。如果 Visual Studio 用户提出需求，其中的 natvis 部分仍然值得保留。

**方案 B** 是在编译器中内置一个 DAP 服务器，通过代码生成器插入的钩子驱动程序；这是唯一能在 JIT 下进行调试的方案，也是唯一支持带函数调用的 K 语法监视表达式的方案。其代价是需要编写一整套调试器代码（停止、单步、栈帧、值打印器、表达式编译器、协议服务器），无法深入查看 C 代码，且在 JIT 下无法调试多线程程序。如果将来证明在没有 C 工具链的情况下调试对大量用户很重要，可以回过头再看此方案；而方案 C 的映射表、值打印器和测试都可以迁移过来。

**JIT** 在方案 A 和方案 C 下均不具备交互式调试能力。但它可以低成本地利用 TinyCC 自带的回溯器，在程序崩溃时输出带 K 行号的调用栈（第 4 节）。

<a id="1-what-a-debugger-sees-today"></a>
## 1. 调试器今天看到的内容

`k build --debug` 会使用 `/Od /Zi` 或 `-O0 -g` 编译 C 代码（`src/build.h`），并开启溢出、范围和标签检查。目前 master 分支上的支持仅此而已。VS Code 扩展虽然贡献了 `klang` 调试类型，但它仅在任务中通过 JIT 运行程序（`noDebug`），并且拒绝 `attach`。

从 `k -o shapes.c samples/05_shapes.k` 生成的 C 代码可以看出，C 调试器当前面对的是什么：

* **没有行号映射。** 代码中没有 `#line`，因此断点与单步停留在 `shapes.c` 中，`.k` 文件无法设置断点。
* **K 内联器仍在运行。** `--debug` 会让 K 编译器保持默认的 `-O2`（`src/main.cpp`），因此调用栈可能会缺失在生成 C 代码之前就被内联到调用方中的函数。
* **所有局部变量都声明在函数顶部。** `main_g` 开头至少有 90 个变量声明，其中 70 个是编译器临时变量 `t0`、`t1`……。K 变量会保留原有名称并加上后缀（`total_g`），被遮蔽的变量则会变成 `s_g`、`s_g_2`、`s_g_3`，且同时全部可见。函数也是同样命名方式（`area_g_2` 就是 `area` 的某个分支函数）。
* **定长 ADT 和切片尚可阅读。** ADT 是一个标签加各变体载荷的联合体，切片则是一个带类型指针加长度的结构：

  ```c
  struct Shape_g {
      uint8_t tag;
      union {
          Shape_g_Circle_p v_Circle_g;
          Shape_g_Rect_p v_Rect_g;
          Shape_g_Tri_p v_Tri_g;
      } u;
  };
  typedef struct { uint8_t *data; int64_t len; } sl_u8;
  ```

  调试器会把标签显示为数字并展开所有联合体成员，把切片显示为指向其首元素的指针。
* **可变长数组丢失了元素类型。** 每个可变长数组的头部都是同一个 `k_rhdr { uint8_t *base; int64_t len; }`，因此调试信息里没有任何地方说明 `shapes_g` 存放的是 `Shape_g` 值。任何可视化器都无法展示它的元素。这是唯一会卡住方案 A 可视化器的表示形式；方案 A 在 C 代码层面修复它（第 3.2 节），方案 C 则通过在映射表中标注元素类型来解决（第 6.2 节）。
* **其余内容全是字节流。** 变长模式（variable-mode）ADT 值及其数组（`r_Shape_g_vm` 是 `uint8_t *`）、变长整数（varint）、自相对引用和池相对引用（整数）、胖引用（`k_rref`，一个头部指针和一个栈）以及根结构体（包含字段引用的描述符），都需要专门的解码代码才能展示。

<a id="2-what-is-wanted"></a>
## 2. 预期目标

从用户角度来看：

1. 能够在 `.k` 文件中设置断点和单步调试，包括标准库文件。
2. 显示由 K 函数构成的调用栈。
3. 以 K 名称显示局部变量，且仅显示当前作用域内的变量。
4. 以 K 值的形态展示数据：数组的所有元素、ADT 的当前激活变体、以文本形式展示的文本、沿引用追踪到的目标值。
5. 支持监视表达式与调试控制台表达式。
6. 优先支持 VS Code，随后覆盖 `docs/editors.md` 所列编辑器（Neovim、Helix、Emacs），支持 Windows、Linux 和 macOS。
7. 在运行时错误（检查失败、`abort`）或崩溃时停下，并保持完整栈帧。

从项目角度来看：活动部件尽量少，随语言和内部表示演进时需要同步维护的内容尽量少。每一部分都应纳入测试套件，以便破坏调试能力的代码生成改动能够触发测试失败。

<a id="3-option-a-native-debug-information-existing-debuggers"></a>
## 3. 方案 A：原生调试信息与现有调试器

调试适配器协议（Debug Adapter Protocol）是编辑器与调试器对话的方式；调试器本身仍须存在。C 语言的原生调试器已经支持该协议：Microsoft 的调试器（`cppvsdbg`，通过 C/C++ 扩展使用）、LLDB 的 `lldb-dap`（随 LLVM 一同发布）与 CodeLLDB，以及自第 14 版起内置 DAP 解释器的 GDB。针对 K 所支持的每款 C 编译器，这些调试器都已经实现了断点、单步、线程、附加（attach）、核心转储和数据断点。方案 A 让它们看到的 C 代码携带着 K 的行号、名称和类型，而无需编写任何实现 DAP 协议的代码。

<a id="31-source-lines-line"></a>
### 3.1 源码行号：`#line`

代码生成器在每条 K 语句对应的 C 代码前写入 `#line N "path.k"`，使用该语句的 `Line`（其中已经包含文件与行号信息，`src/ast.h`）。MSVC 会将其写入 PDB 的行号表，gcc 和 clang 会将其写入 DWARF 的行号表，从而使每款原生调试器都能在 `.k` 文件中设置断点、单步执行并展示对应的调用栈。TinyCC 的行号信息同样遵循 `#line`，第 4 节正依赖于此。

* 一条展开为多条 C 语句的 K 语句算作一步，因为调试器按行单步执行。
* 列信息会丢失：同一行上的两条 K 语句会被合并为一步。
* 第一个 `#line` 之后的每一行 C 代码都归属于某一行 K 代码。按类型生成的辅助函数（`k_size_Shape_g_vm`）采用该类型声明所在的行号，因此步入其中会落到枚举声明处；所有没有对应 K 行号的代码都放在第一个 `#line` 之前，保留其自身的 C 行号。
* 路径使用绝对路径，因此调试器无需额外配置源码映射。
* 标准库函数使用各自文件的行号。用户选择“步入（step in）”时就是想进入它们，此时展示的是标准库源码而非 C 代码。

运行时函数（`k_*`）没有对应的 K 源码。可以指示调试器自动跳过它们：LLDB 使用 `target.process.thread.step-avoid-regexp`，Visual Studio 使用面向原生代码的“仅我的代码（Just My Code）”规则（`.natjmc`），不过后者在 `cppvsdbg` 下的效果仍有待验证。

`--debug` 还应隐含开启 K 编译器的 `-O0`，从而使调试器展示的调用栈与源码结构一致。同时保留 `-O1` 和 `-O2` 搭配 `--debug` 的组合，用于调试内联器产生的结果。

<a id="32-keeping-the-c-close-to-the-source"></a>
### 3.2 让生成的 C 代码贴近源码

仅在 `--debug` 模式下，代码生成器改变输出代码的结构形态，但绝不改变其行为：

* **带类型的可变长数组头。** 为每种元素类型生成一个头部类型，例如 `struct k_rhdr_Shape_g { Shape_g *base; int64_t len; }`，其内存布局与 `k_rhdr` 相同，因此接收 `k_rhdr *` 的运行时函数只需通过指针类型转换即可接收它。这样，可变长数组就能像切片一样展示其中的元素。
* **每个结构体都带有标签名。** 使用 `typedef struct sl_u8 { ... } sl_u8;`，使 natvis、GDB 和 LLDB 拥有完全一致的类型名可供匹配。
* **将临时变量移出视野。** 编译器临时变量成为单个局部结构体 `k_tmp` 的字段，局部变量视图中只需显示一个折叠条目即可。在 `-O0` 下这没有任何额外开销：所有局部变量本来就保存在内存中。
* **在 K 作用域允许的位置用 C 语句块声明局部变量**，从而使被遮蔽的变量在 K 作用域结束时即从局部变量视图中消失。代码生成器能否在作用域开始处声明局部变量而非全部放在函数顶部，目前仍是一个开放问题（第 9 节）。
* 若某个局部变量的名称不可能与 C 关键字或运行时名称冲突，或许可以**去掉 `_g` 后缀**，使针对普通变量的 C 监视表达式读起来更像 K 代码。

这些都属于代码生成器的改动，测试套件会对其加以验证：每个测试夹具在 debug 构建下都必须继续产生预期输出。

<a id="33-showing-k-values"></a>
### 3.3 展示 K 值

由于每种 K 类型的表示方式都是代码生成器选定的，它清楚每一种类型的内部结构。在 `--debug` 模式下，它在可执行文件旁写出一份描述文件：记录每个 C 类型所对应的 K 类型、所属的表示种类，以及该种类的具体信息（切片或可变长数组的元素类型、ADT 按标签索引的变体名称及各自载荷所在的联合体成员、相对引用的位宽/目标类型/所属池、变长模式值的布局、根结构体的字段）。可视化器负责解释这份描述。每种表示*种类*只需一段代码，而无需针对每个具体类型编写代码。

**Microsoft 的调试器**读取 natvis，而 natvis 只能按精确的 C 类型名匹配（其通配符仅用于 C++ 模板实参），因此编译器需根据上述描述为每个程序生成对应的 natvis。以一个 ADT 为例：

```xml
<Type Name="Shape_g">
  <DisplayString Condition="tag == 0">Circle {u.v_Circle_g}</DisplayString>
  <DisplayString Condition="tag == 1">Rect {u.v_Rect_g}</DisplayString>
  <DisplayString Condition="tag == 2">Tri {u.v_Tri_g}</DisplayString>
  <DisplayString Condition="tag == 3">Dot</DisplayString>
  <Expand>
    <ExpandedItem Condition="tag == 0">u.v_Circle_g</ExpandedItem>
    <ExpandedItem Condition="tag == 1">u.v_Rect_g</ExpandedItem>
    <ExpandedItem Condition="tag == 2">u.v_Tri_g</ExpandedItem>
  </Expand>
</Type>
```

切片和可变长数组使用 `ArrayItems` 遍历数据指针与长度；文本是限长 UTF-8 字符串；相对引用则是指针算术加类型转换（池基址是全局变量，natvis 表达式可以直接引用）。遍历变长模式值的数组则需要使用 natvis 的 `CustomListItems` 循环——虽然勉强可行，但写起来很别扭。链接器的 `/NATVIS:` 选项会将该文件嵌入 PDB 中，因此 Visual Studio 和 VS Code 的 `cppvsdbg` 无需任何额外配置即可找到它。

**LLDB** 则获得一个存放在仓库中的 Python 模块 `k_lldb.py`，所有程序共用这一份文件。LLDB 支持通过正则表达式匹配类型名称来注册格式化器（`type summary add -x`、`type synthetic add -x`），因此该模块只需按表示种类注册格式化器，并在程序的描述文件中查找对应类型即可。Python 让原本棘手的情况变得轻松：变长整数是一个循环，变长模式值的数组是一次带上限的遍历。LLDB 在三大平台上都能读取 DWARF，在 Windows 上也能读取 PDB，因此这一个模块就能服务于所有平台上的所有编辑器；natvis 仅在用到 Microsoft 调试器的场景下作为补充。

**GDB** 有自己的一套 Python API。解码逻辑可以放在每个调试器各自的一层薄封装之后，与 LLDB 模块共享；GDB 前端可以等有人提出需求时再做，因为 `lldb-dap` 同样能够调试 gcc 的输出。GDB 会加载目标文件 `.debug_gdb_scripts` 节中指定的脚本（Rust 标准库就是这么做的），因此无需用户配置。LLDB 在 macOS 的 dSYM 包之外没有等价机制；需要由启动配置来导入该模块。

当语言发生变化时，由现有表示种类组合出的新类型无需任何额外改动。而新增一种表示种类，则需要新增一种描述种类、一个 natvis 模板和一个 Python 类：分别位于三个位置的三小段代码，均由代码生成器写出的同一份描述驱动或生成。

<a id="34-editors"></a>
### 3.4 编辑器

在 VS Code 中，扩展需要在两处做出改动：

* 在 `package.json` 的 `contributes` 中添加 `"breakpoints": [{ "language": "klang" }]`。没有这一项，VS Code 不允许用户在 `.k` 文件中设置断点（除非开启 `debug.allowBreakpointsEverywhere`）。
* `klang` 调试类型（`vscode/src/extension.js` 中解析启动配置的位置）在 `noDebug` 时继续运行原有的 JIT 任务。而在正常调试启动时，它会先作为任务运行 `k build --debug`——使构建错误直接显示在“问题（Problems）”视图中——然后通过 `vscode.debug.startDebugging` 启动原生调试会话并结束 `klang` 会话。对于 MSVC 或 clang-cl 构建且已安装 C/C++ 扩展的环境，原生配置选用 `cppvsdbg`；否则选用 `lldb-dap` 或 CodeLLDB，并在初始命令中加入 `command script import k_lldb.py`。若两者均未安装，则提示用户需要安装哪个扩展。

Microsoft 的调试器许可协议仅限用于 Microsoft 自家编辑器（Microsoft 官方构建的 VS Code 以及 Visual Studio），因此 VSCodium、Cursor 及其他所有编辑器在 Windows 上也同样使用 LLDB。其他编辑器无需任何 K 专属代码：nvim-dap、Emacs 的 dape、Helix 的 DAP 支持以及 Zed 都可以直接带上程序和初始命令启动 `lldb-dap`，只需在 `docs/editors.md` 中为每个编辑器各补充一段配置片段（就像目前对语言服务器所做的那样）。Visual Studio 本身则无需任何扩展即可直接打开附带 PDB（内含 natvis）的可执行文件。

<a id="35-testing"></a>
### 3.5 测试

为每种表示种类准备一个测试夹具，使用 `--debug` 构建，再用脚本以批处理模式运行调试器，按 K 行号设置断点，打印局部变量和调用栈，并与预期文本进行比对：

```bash
lldb -b -o "command script import k_lldb.py" -o "b shapes.k:120" \
     -o run -o "frame variable" -o bt ./shapes
```

在 Windows 上，可以使用 `cdb` 配合 `.nvload` 和 `dx` 对 Microsoft 调试器做同样的测试。这些测试在安装了对应调试器的环境中运行，在其余环境中跳过，与原生包测试夹具的做法一致。这样一来，破坏可视化器的表示方式变更会在测试阶段报错，而不会留到用户的调试会话中才暴露。

<a id="36-what-it-cannot-do"></a>
### 3.6 无法做到的事

* **监视表达式使用的是 C 语法。** 调试器求值的是 `s_g.u.v_Circle_g.r_g` 而不是 `s.r`，也无法用 K 函数名调用 K 函数。可视化器只能改变值的显示方式，不能改变输入框里能写什么。
* **无法在 JIT 下调试**（第 4 节），并且启动调试会话要求已安装 C 编译器和调试器。而在 Windows 上，`k run` 两者都不需要。
* **作用域是近似的。** 把临时变量分组并在语句块内声明局部变量能隐藏大部分 C 细节，但剩余部分仍会暴露出来。
* **在不同调试器中值的显示效果不一致**，因为 natvis 和 Python 是对同一份描述的两套独立实现。

<a id="4-the-jit"></a>
## 4. JIT

在方案 A 和方案 C 下，JIT 均不具备交互式调试能力。原生调试器即便附加到 `klang` 进程上，其中的程序代码也没有任何调试器可加载的调试信息：

* Windows 调试器读取 PDB，而 TinyCC 无法生成 PDB。
* GDB 和 LLDB 提供了 JIT 接口：由 JIT 在内存中构建带调试信息的目标文件，并通过 `__jit_debug_register_code` 注册。TinyCC 未实现该接口，且每次运行都需要生成带 DWARF 的 ELF 目标文件。即便生成了，Windows 上的两款调试器也都不会加载它。

因此，调试启动会构建可执行文件，而“运行（run）”仍使用 JIT。JIT 能获得的是在程序崩溃时输出一份 K 调用栈。TinyCC 自带回溯器（`tccrun.c` 以及 `libtcc.h` 中的 `tcc_set_backtrace_func`），它通过内存中的行号信息将出错地址及其调用者映射到文件与行号；有了 `#line` 之后，映射出的就是 `.k` 的行号。`cmake/libtcc.cmake` 目前将回溯与边界检查一并关闭了，因为其背后的支持目标文件需要完整的 Windows SDK 头文件（[jit_backend.md](jit_backend.md#backtraces-and-bounds-checking)）。阅读 TinyCC 源码可知，这一限制适用于 `bt-exe.c`（可执行文件的回溯器，其中包含了 `tccrun.c` 进而引入了 `windows.h`）和 `bcheck.c`。内存运行程序仅需引入 `bt-log.c`（只包含 `stdarg.h`、`stdio.h` 和 `string.h`），而回溯器本身位于 `tccrun.c` 中，宿主编译器本身就有可用的 SDK 来编译它。因此仅启用回溯功能在 Windows 上也可能可行。这一点尚未验证，且运行时自身的异常故障处理（在 Windows 上发生缺页异常时提交栈页）必须继续排在 TinyCC 处理之前执行。

<a id="5-option-b-k-dap-a-debugger-in-the-compiler"></a>
## 5. 方案 B：`k dap`，内置于编译器中的调试器

<a id="51-shape"></a>
### 5.1 形态

`k dap` 将作为与 `k lsp` 并列的子命令，通过 stdin 和 stdout 通信 DAP 协议。DAP 的基础协议采用与 LSP 相同的 `Content-Length` 帧格式，因此语言服务器的传输层（`src/lsp_rpc.h`：读取线程和 JSON 写入器）可以直接复用。需要处理的请求数量比语言服务器少：launch、setBreakpoints、configurationDone、threads、stackTrace、scopes、variables、continue、next、stepIn、stepOut、pause、evaluate、setVariable、disconnect，以及 stopped、output 和 exited 事件。

程序像 `k run` 一样通过 JIT 在编译器进程内运行。编译过程本身已有一个独立线程，程序运行在主线程上，协议服务器则作为第三个线程运行。所有支持 DAP 客户端的编辑器都能获得完全一致的调试器，且不依赖 `klang` 之外的任何工具。

<a id="52-stopping-statement-sites"></a>
### 5.2 停止：语句停靠点

代码生成器为每个可停止的位置（每条语句以及每个循环的回边）编号，并在该处插入检查：

```c
K_DBG_SITE(17);    // if (k_dbg_armed) k_dbg_stop(17, &k_f);
total_g = 0.0;
```

一张停靠点表（site table）记录每个停靠点的文件、行号、列号、所属函数以及该处作用域内的局部变量。每当存在断点、单步或暂停挂起时，就会置位 `k_dbg_armed`；`k_dbg_stop` 检查当前停靠点是否命中，若需要停下，则阻塞程序线程直至协议服务器通知继续。设置断点会标记对应行上的停靠点。单步执行通过比较栈帧深度（5.3）实现：step over 停在同深度或更浅深度的下一个停靠点，step in 停在下一个停靠点，step out 停在下一个更浅深度的停靠点。暂停请求则由服务器线程置位 `k_dbg_armed`。由于停靠点记录了列号，单步可以在同一行内按语句逐条推进，这是方案 A 和方案 C 都做不到的。

未设置任何断点或单步时，每个停靠点的开销是一次内存读取和一次未命中分支。在不做内联且把局部变量都放在内存中的 TinyCC 下，这带来的相对开销比在优化后的代码中更小。具体开销尚未测量。

<a id="53-frames-and-values"></a>
### 5.3 栈帧与值

每个函数在入口处链入一条栈帧记录，并在每条退出路径上摘除它：记录内容包括函数描述符、调用方记录、当前停靠点以及各局部变量的地址。`return … from` 通过普通函数返回逐层传播（规范附录 C.3），因此只有普通退出路径；abort 则会直接结束程序。在 TinyCC 下局部变量本就驻留于内存，取其地址没有任何额外成本。

由于程序与调试器共享同一个进程，调试器可以直接读取其内存，值打印器由编译器内的 C++ 代码实现，直接基于编译器自身的类型系统工作：可变长数组、变长模式数组、相对引用、根结构体或对象池均由紧邻其表示选择逻辑的代码负责解码。为了让 C 编译器继续担任内存布局的唯一权威（打包结构体、栈帧对象、根描述符），代码生成器会为调试器要展示的类型生成 `offsetof` 和 `sizeof` 表，而不是在打印器里重新推算一遍布局。局部变量信息来自停靠点表，因此调试器只显示当前作用域内真正存在的 K 变量，使用其 K 名称，绝不会混入任何临时变量。

当发生检查失败或 `abort` 时，代码会在运行时正式报告之前先回调调试器，使程序停在出错行并完整保留栈帧与局部变量。生成代码中的崩溃则会到达运行时的故障处理程序，同样可以按这种方式停下。

<a id="54-expressions-in-k"></a>
### 5.4 K 表达式

`evaluate` 请求中的表达式文本会在程序停止所在停靠点的作用域下进行解析与类型检查（语言服务器已经具备在给定位置解析名称的能力），接着编译为一个接收栈帧记录参数的 C 函数，由 TinyCC 编译进正在运行的进程中，并在已停止的线程上调用执行。这样一来，监视表达式就是纯正的 K 代码，支持函数调用，且语义与程序中完全一致。求值过程必须隔离表达式的副作用：监视表达式中的检查失败只能中止本次求值而不能中止整个程序，且求值结束后必须恢复数据栈的栈顶。

<a id="55-built-programs-and-threads"></a>
### 5.5 已构建程序与线程

已构建的可执行文件运行在独立进程中，编译器并不在其中。方案 B 要么仅支持 JIT，要么让链接进 `--debug` 可执行文件的调试运行时监听一条管道，提供一套精简的私有协议（读内存、标记停靠点、恢复运行、枚举栈帧），由 `k dap` 驱动——相当于一个专为 K 编写的 `gdbserver`。值打印器仍然留在编译器里。由于表达式编译器无法向另一个进程动态加载代码，针对已构建程序的表达式求值只能局限在可解释执行的子集：变量名、字段访问、索引操作。

JIT 会拒绝启动线程的程序（TinyCC 无法处理线程局部存储；[jit_backend.md](jit_backend.md#threads)），因此在解决该问题前，仅限 JIT 的方案 B 无法调试任何使用工作线程的程序。一旦支持线程，每个工作线程都需要各自的栈帧链，“暂停”操作也必须在下一个停靠点停下所有线程；阻塞在 C 代码（如队列或读取操作）中的工作线程无法到达任何停靠点，调试器只能将其报告为仍在运行。

<a id="56-what-it-cannot-do"></a>
### 5.6 无法做到的事

* **C 代码不可见。** extern 函数、包里的 C 代码和运行时都没有停靠点和栈帧记录：无法步入其中，且在其中发生崩溃时只能看到其上方的 K 栈帧。
* **不支持崩溃转储，无法附加（attach）** 到非 `k dap` 启动的进程，**不支持优化构建**（停靠点和栈帧记录维持着程序的原始结构），也**不支持数据断点**（除非在每个停靠点都检查一次）。
* **程序与调试器共享同一进程。** K 代码不会越界写入自身数据之外的内存，但 C 代码会，一次误写就可能破坏调试器以及打印器正要读取的编译器状态。
* 开发编译器本身仍需要用原生调试器调试生成的 C 代码，因此方案 A 和方案 C 所需的 `#line` 与 debug 构建无论如何都是需要的。

<a id="57-what-it-costs-to-keep"></a>
### 5.7 维护成本

* 需要为每一种引入新控制流的语法结构维护停靠点放置与作用域表：`match` 分支、`guard`、`defer`、`return … from`、循环、内联函数值，以及 [coroutines.md](coroutines.md) 中提出的协程——协程的栈帧是生命周期长于恢复其运行的调用的值，因此需要一种新形态的栈帧记录。
* 需要为每一种内部表示编写打印器。虽然只有这一套实现，直接对照编译器自己的模型（而方案 A 有一份描述和两套解释器），但它是针对可能已经损坏的内存运行的代码，必须编写得足够防御。
* 停止、单步与暂停机制；如果方案 B 要覆盖已构建程序，还需维护管道协议与多线程暂停机制。
* 表达式编译器对中止异常与数据栈栈顶的隔离保护。
* 跨客户端的协议兼容。虽然不同 DAP 客户端之间的差异小于 LSP 客户端，但各自仍有怪癖，正如 `docs/editors.md` 中针对语言服务器所记录的那样。

方案 B 的可测试性很好：与语言服务器类似，测试套件可以用 Python（`test/lsp_protocol.py` 的客户端改用 DAP 消息）驱动 `k dap`，无需安装任何外部调试器即可比对停止位置、调用栈和变量值。

<a id="6-option-c-k-dap-in-front-of-lldb-dap"></a>
## 6. 方案 C：位于 lldb-dap 前端之前的 `k dap`

某个代码分支上的原型实现了该方案（`pdoane/klang`，`dap-prototype` 分支，提交 `190a9e6a` 与 `4f2c161e`，2026 年 11 月）。该原型中的 `#line` 映射提供了源码级调试的基础部分。本节按代码实现描述该原型；编写本笔记时并未实际编译或运行它。

<a id="61-shape"></a>
### 6.1 形态

`k dap` 像 `k lsp` 一样通过 stdin 和 stdout 通信 DAP，同时将 `lldb-dap` 作为子进程启动，并反过来与其通过 DAP 通信（`src/dap.h`，约 1,400 行）。当启动目标是一个 `.k` 文件时，它会先对其执行 `k build --debug -O0`，再用 `lldb-dap` 启动生成的可执行文件。LLDB 负责设置断点（利用 `#line` 信息）、运行、单步、停止、栈展开以及读取内存。适配器自行应答 `stackTrace`、`scopes`、`variables`、`threads`、`evaluate` 和 `exceptionInfo`，并补充处理 `setFunctionBreakpoints`，在编辑器看到任何停止事件之前先由适配器检查一遍；其余所有请求和事件则原样透传。适配器依次通过 `--lldb-dap`、`K_LLDB_DAP`、macOS 上的 `xcrun` 或 `PATH` 查找 `lldb-dap`。

与方案 A 相比，底层的调试器是同一个，但关于 K 的知识放在了调试器前端之前而非调试器内部，编辑器直接与编译器对话。与方案 B 相比，协议服务器和值打印器是同样的思路，但生成的 C 代码里没有任何用来让程序停下来的逻辑：停止完全由 LLDB 负责。

<a id="62-the-debug-map"></a>
### 6.2 调试映射表

`k build --debug` 会在构建目录下写出 `<program>.debug.json`（`src/codegen_debugmap.h`）：

* 对每种类型：记录其种类，以及定长时的大小。包括以下种类：整数（含符号性或 varint）、浮点数、布尔值、结构体（字段及填充字节）、ADT（标签大小与各变体）、切片、数组（定长、有限长、变长或可变长）以及引用（可选、胖引用、自相对或池相对）。
* 对每个 C 函数：记录对应的 K 函数名及其局部变量——K 名称、C 左值表达式、声明行号、所在语句块结束行号、类型、是否以捕获指针形式传入，以及根结构体的组成部分。具名常量附带其常量值。
* 程序的全局变量、带类型的队列，以及是否使用了 `return … from`。

这份映射表定位为编译器与其自身适配器之间的私有契约，以便两者同步演进。它正是第 3.3 节中所说的那份描述，只是解释器从两套变成了一套，并且就放在编译器里。在 `--debug` 模式下，优化器还会停止将常量传播到使用点，确保 `let` 绑定在调试器中依然是可读的变量。

<a id="63-what-the-adapter-shows"></a>
### 6.3 适配器展示的内容

* **调用栈**由 K 函数按其 K 名称构成。映射表中未收录的函数帧（运行时、C 代码）会被过滤掉。
* **局部变量**按 K 名称显示：包括参数，以及在停止行之前声明且所属语句块尚未结束的局部变量。内层变量按声明行号遮蔽外层同名变量，临时变量绝不出现。全局变量和队列各自作为独立的作用域展示。
* **值**按类型读取，内存由适配器按 4 KB 分页向 `lldb-dap` 请求并在当次停止期间缓存：文本显示为文本，ADT 显示为 `Note.Count {n: 300}`，变长模式数组逐项遍历，varint 解码显示，自相对引用沿指针追踪，池相对引用显示其偏移量。值的单行摘要默认读取深度不超过 4 层、元素不超过 64 个，其余部分在编辑器展开时再行读取，这也自然防止了引用环导致的死循环。
* **线程**以它们正在运行的 K 函数、所在行号以及正在等待的对象（`qget`、`thread_wait`）来命名。
* **单步执行**若落到程序目录中 `.k` 文件之外的位置（例如生成 C 代码或标准库），会继续单步跳过。命中函数序言（prologue）的断点会自动步入到函数第一行。在 `return … from` 展开期间（运行时设置了进行中标志），单步会持续推进直至落地。
* **检查失败**会以异常形式停下——通过适配器始终在运行时 `k_fail` 上设置的函数断点实现，附带检查失败消息及出错的 K 行号。
* **调试控制台**在内存上求值一个小型的 K 表达式子集：作用域内名称、字段访问、索引、`.len`、算术运算（包含 K 的欧几里得取模 `%`）、比较运算以及 `&&` 和 `||`。不支持调用函数。

`test/dap_protocol.py` 从测试套件中驱动适配器，测试夹具位于 `test/dap/`，在任何安装了 `lldb-dap` 的环境中运行。它校验断点、名称与值、跳过生成代码、线程、`return … from` 以及检查失败。

<a id="64-what-it-depends-on"></a>
### 6.4 它的依赖项

适配器依赖的大部分是 `lldb-dap` 的实际行为，而不只是协议规范：

* 它需要解析 `lldb-dap` 返回的值文本：例如 `uint8_t` 被渲染成字符字面量、指针渲染中的 `0x` 前缀、失败消息带引号的字节串。
* 它在一次停止后会请求两次线程列表，因为 `lldb-dap` 的第一次应答可能只包含当前停止的线程。
* 它会替换 `lldb-dap` 的作用域引用，因为后者在所有栈帧上都返回相同的引用值。
* 它每次停止时都会让 LLDB 为每个局部变量求值一次 C 表达式 `&x_g` 来获取变量地址，并通过求值 `sizeof` 获取栈帧对象的大小。

上述每一点都依赖于特定版本的 `lldb-dap`，而方案 A 依赖的是调试器公开文档化的格式化器接口。此外，有几项内存布局事实也硬编码在适配器而非映射表中：可变长数组的长度位于基址之后 8 字节处、运行时定长有限数组的两个 `u32` 计数、栈帧对象尾部位于其 C 结构体末尾前 16 字节处、队列节点的 16 字节头部。代码生成器若修改其中任何一项都会破坏适配器，而只有覆盖到该结构的测试夹具才能发现问题。

底层唯一的调试器是 LLDB。Microsoft 的调试器仅授权给 Microsoft 自家编辑器使用（第 3.4 节），这导致无法在其他编辑器里把它放在 `k dap` 后面，在 VS Code 里同样存疑，因此 Visual Studio 和 `cppvsdbg` 都无法获得 K 视图。在 Windows 上，适配器靠 `lldb-dap` 读取 MSVC 构建生成的 PDB 或 clang 构建生成的调试信息；`lldb-dap.exe` 随 Visual Studio 的 LLVM 组件一同提供。原型中的路径处理假设分隔符为 `/`，且套件测试只在 macOS 或 `PATH` 中有 `lldb-dap` 的环境下运行，因此看起来尚未在 Windows 上实际跑通。

<a id="65-what-it-cannot-do"></a>
### 6.5 无法做到的事

* **无法在 JIT 下调试**，与方案 A 相同。
* **控制台表达式只是一个子集**，不支持函数调用，且解析器是适配器自己写的而非编译器前端，不会随语言演进而自动同步。
* **作用域基于行范围。** 同一行上的两个声明，以及 `const for` 循环体的各个展开副本（由于停止位置无法区分正在运行哪一份副本，所有副本的变量都会显示），只能是近似结果。
* **C 栈帧被隐藏**在调用栈之外，因此在 extern 代码中停下时只能看到其上方的 K 栈帧，尽管 LLDB 其实掌握了完整的栈信息。
* **每个值都要与 `lldb-dap` 往返通信一次**（单次停止内按页缓存），且每个局部变量都要触发一次表达式求值。在大型栈帧或大量线程场景下的性能表现尚未测量。
* **不支持 Microsoft 调试器**（6.4）。

<a id="66-what-to-tighten"></a>
### 6.6 需要收紧的地方

* **仅在行号变化时输出 `#line`，且仅用于 debug 构建。** 原型在每一种构建模式的每一行生成 C 代码前都写了 `#if K_DEBUG`、`#line` 和 `#endif`——每行变四行，就连 release 输出和 JIT 编译的 C 代码里也有。而对于编译器自行生成的代码，它写了 `#line 0`，超出了 C 标准允许的 `#line` 范围（1 到 2147483647）。
* **把内存布局放进映射表**——适配器所需的每一个偏移量都应直接由 C 编译器提供，而非推算第二遍：即把一张由 `offsetof` 和 `sizeof` 组成的表编译进 debug 构建中，由适配器在启动时读取一次。
* **从协议获取地址。** DAP 变量可以携带 `memoryReference`。凡是 `lldb-dap` 通过这种方式提供变量地址的地方，都可以替代对 `&x_g` 求值并解析文本的做法；需要逐一核查各类局部变量是否均支持。
* **将控制台接入编译器前端。** 表达式文本在停止行所在作用域中作为 K 表达式进行解析与检查（语言服务器已经支持在给定位置解析名称），再将检查后的语法树在内存上求值，使控制台真正理解编译器所理解的语义。
* **`--debug` 本身就隐含 `-O0` 与保留变量**，且对所有命令生效，而不是由适配器去传递 `-O0`、且优化器仅针对 `build` 保留变量。
* **Windows 支持**：处理路径分隔符，并用 Visual Studio 自带 LLVM 中的 `lldb-dap` 针对 MSVC 构建和 clang 构建运行适配器测试。

<a id="7-the-three-compared"></a>
## 7. 三种方案对比

| | A：原生调试器 + 可视化器 | C：位于 `lldb-dap` 前端之前的 `k dap` | B：独立的 `k dap` |
|---|---|---|---|
| 运行程序：断点、单步、线程 | 原生调试器 | `lldb-dap` | 编译器，通过生成 C 代码中的钩子 |
| 是否需要 C 编译器和调试器 | 是 | 是，且需要 `lldb-dap` | 在 JIT 下不需要 |
| 多线程程序 | 支持 | 支持 | JIT 在支持线程局部存储前不支持；已构建程序仅能依赖 B 自己的全局暂停 |
| 编辑器支持 | 所有 DAP 客户端（需逐个配置），外加 Visual Studio | 所有 DAP 客户端，运行同一条命令 | 所有 DAP 客户端，运行同一条命令 |
| Microsoft 调试器 | 支持，用于 VS Code 和 Visual Studio | 不支持 | 不涉及 |
| 断点与单步 | 按行 | 按行，可跳过编译器代码并穿过 `return … from` | 按行和列 |
| 调用栈 | C 函数名 | K 名称，过滤 C 栈帧 | K 名称 |
| 局部变量 | C 名称，包含函数的全部局部变量 | K 名称，按行范围判断 | K 名称，精确匹配作用域内变量 |
| 值展示 | natvis 与 LLDB 模块解释同一描述 | 编译器适配器解释自身映射表 | 编译器自身类型 |
| 调试控制台 | C | 内存上的 K 子集，无函数调用 | K，包含函数调用 |
| 运行时错误 | 在运行时失败路径上设函数断点 | 作为异常停在出错行 | 停在出错行 |
| C 与 extern 栈帧 | 可见 | LLDB 中存在，但在栈视图中隐藏 | 不可见 |
| 崩溃转储与附加（attach） | 支持 | LLDB 支持；适配器尚未接入 | 不支持 |
| 优化构建 | 达到普通 C 调试优化代码的上限 | 不支持（映射表仅用于 debug 构建） | 不支持 |
| 程序内运行开销 | 除 debug 构建本身外无额外开销 | 除 debug 构建本身外无额外开销 | 每条语句一次检查，每次调用一次栈帧链接 |
| 外部依赖 | 调试器的格式化器接口 | `lldb-dap` 的具体行为 | 不依赖 `klang` 之外的任何组件 |
| 当内部表示变化时 | 一项描述种类、一个 natvis 模板、一个 Python 类 | 映射表与适配器读取器 | 打印器代码 |
| 当控制流变化时 | 无需改动 | 无需改动，除非单步需要新规则 | 停靠点与作用域放置逻辑 |
| 需自行维护的组件 | `#line`、debug 代码形态、natvis 生成器、LLDB 模块、扩展胶水代码 | `#line`、映射表、适配器 | 整套调试器：协议服务器、停止与单步机制、打印器、表达式编译器、栈帧记录 |

代码规模：方案 C 的原型包含约 1,400 行适配器代码、240 行映射表代码，以及约 50 行代码生成器和优化器改动，外加对应测试。方案 A 大致处于同一量级，主要集中在 natvis 生成器和 LLDB 模块。仅针对 JIT 实现方案 B 就需要数千行代码。方案 A 和方案 B 的规模均为估算值。

<a id="8-recommendation-and-order-of-work"></a>
## 8. 推荐方案与实施顺序

采用方案 C，按以下各自独立可用的步骤推进：

1. **源码行号。** 在 debug 构建中于 K 行号变化处输出 `#line`，并让 `--debug` 隐含 `-O0` 与保留变量（6.6）。仅这一步就能让每款原生调试器用上 K 行号，且后续所有步骤都依赖它。
2. **位于 `lldb-dap` 前端之前的 `k dap`**，从现有原型起步，将内存布局纳入映射表，并在三大平台上跑通。
3. **VS Code**：添加 `breakpoints` 声明（3.4），并让 `klang` 调试类型的调试启动以 `k dap` 作为适配器可执行文件，同时 `noDebug` 继续运行 JIT 任务。在 `docs/editors.md` 中为其他编辑器各自补充配置片段，做法与语言服务器一致。
4. **基于编译器前端的调试控制台**（6.6）。
5. **按同一份映射表生成 natvis**，供 Visual Studio 和 `cppvsdbg` 用户在有需求时使用：这是方案 A 中仍然值得保留的一部分，而且在映射表就绪后实现成本很低。
6. **JIT 调用栈回溯**（第 4 节），与其他步骤相互独立。

方案 C 下不再需要方案 A 在代码生成器中的 debug 形态调整（3.2）和 LLDB 模块：适配器会隐藏临时变量，映射表也会提供可变长数组的元素类型。

如果大量用户需要在没有 C 工具链的环境下调试，或者带函数调用的 K 监视表达式被证明十分关键，届时值得回过头重新评估方案 B。而方案 C 的工作可以直接延续到方案 B 中：协议服务器、映射表、值读取器（通过统一的内存读取函数工作——在方案 B 下只需改为读取本进程内存即可）、调试控制台、测试以及编辑器配置全部可以复用。方案 C 是迈向方案 B 的一步，而不是背离它。

<a id="9-open-questions"></a>
## 9. 开放问题

* LLDB 在 Windows 上对 C 语言的 PDB 支持是否足够完善（局部变量、联合体、打包结构体），从而使架在 `lldb-dap` 之上的单一适配器能够覆盖 Windows 下的所有编辑器？
* 鉴于第 6.4 节所述情况，应当支持哪些版本的 `lldb-dap`，测试套件又该如何锁定版本并进行测试？
* release 构建中是否也应该写出 `#line`？那样分析器和崩溃转储就能直接报出 K 行号；但供人阅读的生成 C 代码以及语料库 diff（`test/emit_order.py` 差分测试框架）会产生更多变动噪声。
* 仅凭行范围来划分作用域是否足够，还是说映射表应当记录精确的语句范围？
* 当程序停在 extern 代码中时，适配器是否应该以折叠形式展示 C 栈帧？
* 映射表存放在何处：像原型那样放在构建目录的一个独立文件中，还是嵌入可执行文件的一个节（section）里？独立文件最简单；放入节中则不会与所描述的可执行文件分离。
* 对于方案 A 的 debug 形态：局部变量能否声明在其作用域起始处，还是代码生成器的结构（跨语句存活的临时变量、用于 `defer` 和 `return … from` 的跳转）决定了它们必须放在函数顶部？
* TinyCC 自身的调试信息（输出到文件时的 `-g`）在 Windows 上能为 LLDB 提供什么？如果 TinyCC 的 debug 构建能工作，没有安装其他 C 编译器的机器也能单靠 LLDB 完成调试。
* 在上述每种方案下，协程栈帧在调用栈中应如何呈现？

<a id="references"></a>
## 参考资料

* Debug Adapter Protocol：https://microsoft.github.io/debug-adapter-protocol/
* VS Code 调试器扩展指南：https://code.visualstudio.com/api/extension-guides/debugger-extension
* Natvis：https://learn.microsoft.com/en-us/visualstudio/debugger/create-custom-views-of-native-objects
* LLDB 变量格式化：https://lldb.llvm.org/use/variable.html
* lldb-dap：https://github.com/llvm/llvm-project/tree/main/lldb/tools/lldb-dap
* GDB 的 DAP 支持：https://sourceware.org/gdb/current/onlinedocs/gdb.html/Debugger-Adapter-Protocol.html
* GDB 的 JIT 接口：https://sourceware.org/gdb/current/onlinedocs/gdb.html/JIT-Interface.html
* GDB 的 `.debug_gdb_scripts` 节：https://sourceware.org/gdb/current/onlinedocs/gdb.html/dotdebug_005fgdb_005fscripts-section.html
* Rust 嵌入式调试器可视化器：https://doc.rust-lang.org/reference/attributes/debugger.html
* Microsoft 调试器的许可协议：https://github.com/OmniSharp/omnisharp-vscode/wiki/Microsoft-.NET-Core-Debugger-licensing-and-Microsoft-Visual-Studio-Code
* 方案 C 原型：https://github.com/pdoane/klang/tree/dap-prototype
