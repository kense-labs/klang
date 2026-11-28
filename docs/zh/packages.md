<a id="building-programs-and-using-packages"></a>
# 构建程序与使用包

<details>
<summary>目录</summary>

- [1. 运行与构建](#1-running-and-building)
- [2. 使用包](#2-using-a-package)
- [3. 自带 C 代码的程序](#3-a-program-with-c-of-its-own)
- [4. 创建包](#4-making-a-package)
- [5. 缓存](#5-the-cache)

</details>

本文说明如何将 K 程序编译为可执行文件、如何使用不属于编译器内置部分的库，以及如何编写这样的库。其背后的设计理念以及编译器的具体实现方式，请参阅 [design/packages.md](design/packages.md)。

<a id="1-running-and-building"></a>
## 1. 运行与构建

`k foo.k`（或 `k run foo.k`）会将程序直接编译到编译器自身的进程中并在其中运行，不需要 C 编译器，也没有独立的构建步骤。`k build foo.k` 则会调用机器上找到的 C 编译器，在源码同级目录下生成可执行文件 `foo` 或 `foo.exe`：

```bash
k build foo.k && ./foo
k build --dry-run foo.k         # 打印将执行的命令而不是实际运行
```

`k toolchain` 会显示当前构建使用的 C 编译器；通过 `--cc clang`、环境变量 `K_CC`，或在 `~/.klang/config` 中配置 `cc = "clang";` 可以选择其他编译器。运行时库以及每个包的 C 代码会按每种 C 编译器分别编译一次，生成的可执行文件会链接其所使用包的原生部分。直接运行时，除非包已被直接编译进编译器本身，否则也会将相同的原生部分构建为一个库并动态加载。编译器在此过程中产生的所有产物都存放在 `~/.klang` 目录下（可通过 `K_HOME` 更改路径，参见 [§5](#5-the-cache)）。

其余细节请参阅[命令行工具](command_line.md)：包括 run、build 与 check 的全部选项、编译器输出内容，以及如何在你自己的构建系统中编写相应的 C 构建规则。

<a id="2-using-a-package"></a>
## 2. 使用包

*包（package）* 是位于独立目录中的库：其中包含一个与包同名的模块 `name.k`、同目录中的其他模块，以及可选的 C 代码。编译器通过名称即可直接识别的包称为 *内置认证包（blessed packages）*（见 `src/blessed.txt`）。程序可以像导入普通模块一样直接导入它们：

```klang
import gfx;
import physics;
```

这些包随 K 一同发布。首次构建时，编译器会解析它们在固定修订版上的原生依赖、编译其 C 代码并缓存结果。如果编译器在配置时启用了 `-DK_BUILTIN_LIBS=all`，则这些内置认证包会被直接编译进编译器中，运行这类程序时无需额外安装任何依赖；运行 `k toolchain` 可以列出已内置编译的库。

其他任意包，或者内置认证包的其他版本，都需要在程序的 *根文件（root file）*（即命令行中直接指定的源文件）中连同其来源一起声明：

```klang
import mylib from "joe/mylib@v2";                  // GitHub，指定 tag v2
import mylib from "github.com/joe/mylib@3f2c1a9";  // 任意主机，指定 commit
import mylib from "https://git.example.com/x/mylib.git@main";   // 任意 git URL，指定分支
import mylib from "../mylib";                      // 本地目录，原地直接使用
```

`import name from "source";` 用于声明包 `name` 并导入其入口模块 `name.k`；该包中的其他模块可通过 `import name.other;` 导入。Git 来源需要在 `@` 后附带一个引用（ref）。tag 或 commit 会固定包的版本；分支同样会保持固定，直到执行 `k fetch --update program.k` 重新拉取为止。本地目录路径相对于包含该 `import` 语句的文件解析，且不会发生复制：你可以直接修改该库并立即运行程序。

**只有根文件能声明包。** 所有其他文件（包括库自身的文件）都只能按名称导入。如果某个库需要依赖另一个包，它同样在自己的文件中写出 `import other from "…";`，而编译器会将其视为一项 *依赖要求（requirement）*：若根文件中未声明 `other`，编译器报错会精确指出需要在根文件中添加哪一行。如果根文件从不同的 ref 声明了同一个包，编译器会发出警告；如果从不同的来源（source）声明了同一个包，则会报错。这样一来，程序的依赖关系就是扁平、版本固定且集中在一处的，整个程序中每个包只会存在单一版本。

`k fetch program.k` 会拉取根文件声明的全部依赖以供后续离线使用，而 `--offline` 选项会禁止任何命令发起网络拉取。当缺少所需外部工具时，错误信息会直接指明缺失的工具名称：拉取代码需要 `git`，构建基于 CMake 的包需要 `cmake`，编译任何 C 代码需要 C 编译器。既不含 C 代码也不依赖此类包的程序则不需要上述任何工具。

<a id="3-a-program-with-c-of-its-own"></a>
## 3. 自带 C 代码的程序

属于程序自身的 C 代码配置在根文件旁的附属构建文件（sidecar）中，例如 `foo.k` 旁的 `foo.build`：

```klang
// foo.build
c_sources = ["impl.c", "vendor/stb_impl.c"];   // 随程序构建一同编译
c_include = ["vendor"];
c_defines = ["IMPL_FAST=1"];
c_flags_cc = ["-mavx2"];                       // gcc 与 clang 的编译选项；MSVC 使用 c_flags_msvc
c_libs = ["m"];                                // 系统库，按名称指定
c_libs_windows = ["user32"];                   // 仅针对特定平台的部分
dylibs_linux = ["SDL2=libSDL2-2.0.so.0"];      // extern "SDL2" 块在此平台上打开的库
output = "foo";                                // 可执行文件名称（若与源文件不同）
```

该文件的语法与 K 相似（`//` 注释、`key = value;`、字符串、`true` 和 `false`、`[列表]`），并且每个 `c_*` 键都可以加上 `_windows`、`_linux` 或 `_macos` 后缀，表示仅对单一平台生效的配置部分。`k build` 会编译并链接这些 C 代码；`k run` 会将其构建为共享库并在进程内运行期间加载。无论哪种方式，程序中的 `extern fn` 都可以直接调用它们：

```klang
extern fn impl_sum(a: i32, b: i32) -> i32;
extern fn impl_count(s: const u8[:], c: u8) -> i64;   // 切片按 { data, len } 传入
```

对于单头文件库（single-header library），通常只需要一个定义其实现宏并包含该头文件的 `impl.c` 即可。此外，在命令行传入 `--include shim.h` 可以将头文件直接拼接到生成的 C 代码中；对于针对生成类型编写的小型垫片（shim），这种方式同样适用（见规范 §7.10 及 [command_line.md](command_line.md#8-writing-the-c-yourself)）。

<a id="4-making-a-package"></a>
## 4. 创建包

一个包就是一个目录（或 git 仓库），其布局如下：

```
mylib/
  mylib.k        入口文件：`namespace mylib;`，包含其对外声明
  mylib.build        附属构建文件，当有 C 代码需要构建时使用（可选）
  extra.k        其他模块：`import mylib.extra;`
  src/, include/     C 源码与头文件（如有）
  examples/, test/   包自带的程序，每个都是独立的根文件
  README.md
```

在包内部，`import helper;` 会在包根目录下查找 `helper.k`，`import .helper;` 会相对于当前导入文件查找，`import std;` 则访问标准库。按照惯例，包内的文件会声明包名作为自身的命名空间。

如果一个库需要依赖另一个包，只需在其入口文件中写上该依赖要求，例如 `import vec_extras from "github.com/x/vec_extras@v2";`，编译器会如前所述将其与程序的根文件进行核对。库自带的示例和测试都是根文件，因此该行在这些文件中直接起声明包的作用，示例程序也可以独立运行。

<a id="the-native-part"></a>
### 原生部分

包可以直接调用机器上已安装或随程序一同分发的动态库，而无需编译自己的 C 代码。`extern "name" { ... }` 代码块用于声明在程序启动时按名称从该共享库中加载的函数（规范 §7.10）。

如果该名称默认推导出的库文件名不正确，可以在附属构建文件的 `dylibs` 列表中通过平台后缀进行映射，例如 `dylibs_linux = ["SDL2=libSDL2-2.0.so.0"];`。包会为其自身声明的名称提供默认映射；程序的附属构建文件可以覆盖其中的任意名称，覆盖优先级按各个平台分别判定。映射一个没有任何 `extern` 块声明的名称会触发错误。

仅调用这类函数的程序不会编译或链接任何原生代码，因此无需 C 工具链即可使用 `k run` 运行。本节其余内容则介绍需要自行编译 C 代码的包。

包可以将头文件直接包含进程序生成的 C 代码中，从而使 C 编译器能够将小型适配器内联到 K 函数中：

```klang
// mylib.build
c_headers = ["include/adapters.h"];
```

这些路径相对于包根目录解析（对于程序自身的附属构建文件，则相对于程序所在目录）。与 `--include` 类似，头文件会排在生成的类型定义之后、extern 原型与函数体之前。它们可以为 `extern fn` 声明提供 `static inline` 实现，而无需单独的目标文件或命令行 `--include`。此时未导入模块中的类型不可用。头文件必须能通过当前所选后端的编译，包括用于 `k run` 的 TinyCC；因此纯头文件（header-only）包在该模式下无需外部 C 工具链即可运行。

`c_headers` 支持与其他 `c_*` 列表相同的平台后缀。头文件按列表顺序依次包含：首先是命令行 `--include` 条目，接着是程序自身的附属构建文件，然后按加载顺序包含各个包的头文件。头文件应自行包含其所需依赖；`c_include`、`c_defines` 与 `c_flags_*` 仍然只作用于单独编译的原生源文件，而不作用于程序生成的 C 代码。

附属构建文件可以通过以下两种方式之一指定 C 代码。第一种是普通源文件，编译器会结合指定的包含路径、宏定义、编译标志和链接库来编译它们：

```klang
// mylib.build
c_sources = ["src/mylib.c"];
c_include = ["include"];
c_libs_linux = ["m"];
```

第二种是在包根目录放置一个 CMake 工程，并通过其静态库目标名称来指定：

```klang
// mylib.build
cmake_target = "mylib";
cmake_options = ["MYLIB_SIMD=ON"];
```

随后编译器会围绕包自身的 `CMakeLists` 配置并构建一个小型包装工程，并使用该目标完整的链接命令行。因此，包可以作为现有 C 库的轻量级 K 封装层，无需专门为 K 编写额外的 CMake 代码。包所依赖的 git 子模块会随包一同克隆。无论采用哪种方式，原生部分都会按每种 C 编译器在 `~/.klang/build` 下构建一次，并由程序静态链接。

包中 `extern fn` 所命名的函数必须由这些 C 代码（或该 C 库）提供定义；运行时自身的 `k_os_*` 等函数属于标准库。包根目录下的所有 `.k` 文件都归属于该包，但以下两类除外：拥有自己独立附属构建文件的子目录（例如自带 C 代码的示例程序），以及附属构建文件中显式排除的目录：

```klang
// mylib.build
exclude = ["tools", "third_party/demo"];
```

由于包以其目录来标识，两个包不能共享同一个根目录：如果程序声明了来自同一目录的两个包，或者来自同一仓库同一 ref 的两个包，都会报错。每个包都必须拥有自己独立的目录或仓库。

`extern fn` 传递的大多数值在 C 中都有可以直接自行声明的内存布局，例如切片对应 `{ data, len }`（规范 §7.10）。构建器（builder，即 `T[>..]&`，例如 `u8[>..]&`）是个例外：C 代码需要通过运行时的 `k_rref` 向其追加数据，而 `klang.h` 为单独编译的 C 代码提供了访问运行时此能力的接口，以及用于追加字节的 `k_bld_append`。编译器会将该头文件自动加入所有原生部分（包括程序自身的 C 代码与 CMake 目标）的头文件搜索路径中，并在该头文件发生变化时重新构建它们：

```c
#include "klang.h"
void mylib_name(k_rref out, int64_t id) {   /* extern fn mylib_name(out: u8[>..]&, id: i64); */
    k_bld_append(out, "item", 4);
}
```

每个包的 C 代码都是自包含的：如果两个包必须共享同一份 C 状态或代码，它们应当共处同一个仓库，正如 `gfx`、`audio` 和 `ui` 的做法。如果一个包的函数在进程中维护全局唯一状态，或只能在主线程调用，则应在附属构建文件中注明 `main_thread_only = true;`；此后在 `thread_fn` 中调用它就会触发编译错误。

<a id="using-k-types-in-a-c-header"></a>
### 在 C 头文件中使用 K 类型

`c_headers` 适配器直接使用 K 已经声明的类型；它不会重新声明这些类型，也不会将 C 结构体反向导入 K。下面是一个完整的本地包及其调用程序的示例：

```klang
// mylib/mylib.k
namespace mylib;
struct Pair { lo: u32, hi: u32 }
extern fn swap(p: Pair) -> Pair = "mylib_swap";
```

```klang
// mylib/mylib.build
c_headers = ["include/adapters.h"];
```

```c
// mylib/include/adapters.h
#ifndef MYLIB_ADAPTERS_H
#define MYLIB_ADAPTERS_H

static inline mylib_Pair_g5 mylib_swap(mylib_Pair_g5 p) {
    mylib_Pair_g5 result;
    result.lo_g = p.hi_g;
    result.hi_g = p.lo_g;
    return result;
}

#endif
```

```klang
// app.k，位于 mylib 目录旁
import mylib from "./mylib";
fn main() {
    let p = mylib::swap(mylib::Pair { 20, 22 });
    assert(p.lo == 22 && p.hi == 20);
    print(p.lo, " ", p.hi);
}
```

`k run app.k` 会输出 `22 20`；`k build app.k` 则会用原生 C 编译器构建同一程序。两者都不需要 `--include` 或单独编译的 `.c` 文件。

头文件中的名称遵循规范 §7.10：

| K 声明 | C 拼写 | 规则 |
| --- | --- | --- |
| `mylib::Pair` | `mylib_Pair_g5` | 命名空间、`_`、类型名、`_g`、命名空间长度 |
| 不带命名空间的 `Pair` | `Pair_g` | 类型名后接 `_g` |
| 字段 `lo`、`hi` | `lo_g`、`hi_g` | 字段名后接 `_g`，不包含命名空间 |
| `swap(...) = "mylib_swap"` | `mylib_swap` | 显式指定的 extern 符号名，保持不变 |

其中的 `5` 是 `mylib` 的字符长度，而不是按导入顺序编号。例如，`goosimd::Bytes32` 会变为 `goosimd_Bytes32_g7`。决定这些名称的是包的 `namespace` 声明，而非其导入路径。

你可以通过 `k -o app.c app.k` 查看生成的实际声明：类型定义会出现在适配器 `#include` 之前，而 extern 函数原型紧随其后。这对于泛型类型或复合类型尤为有用，因为它们的名称包含类型参数，且在必要时可能带有防冲突后缀。用户定义的字段名会带有 `_g` 后缀；编译器定义的切片成员则仍保持为 `data` 和 `len`。

请直接使用编译器生成的类型，不要自行编写一个具有相似字段的第二个 C 定义。这些命名规则并不是稳定的二进制 ABI 契约：名称或字段匹配并不代表其与独立编译的 C 库具有兼容的内存布局、对齐方式或调用约定。K 的结构体是紧凑排列（packed）的，而非自然对齐（naturally aligned）的 C 结构体。为现有 C API 编写适配器时，必须显式处理该 API 自身的数据表示，而不能假设两种类型可以互换。此外，单独编译的 `c_sources` 看不到这些生成的声明；上述示例之所以成立，是因为头文件与 K 生成的代码在同一个 C 编译单元中一并编译。

<a id="files-a-package-needs-at-run-time"></a>
### 包在运行时需要的文件

`embed_file("path")` 会在程序编译时读取指定文件（路径相对于包含该调用的文件），并将其字节内容作为 `const u8[:]` 嵌入静态数据区，例如字体、查找表或着色器源码。通过这种方式，包无需额外分发独立资源文件即可随附数据。在运行时读取的文件则归属于程序自身（见 `resource_dir()`、`docs/stdlib.md`）。

<a id="publishing"></a>
### 发布

推送仓库、打上版本 tag，然后告诉用户需要引入的导入语句即可：

```klang
import mylib from "github.com/you/mylib@v1";
```

K 没有需要注册的中心仓库。由于 tag 可能移动而分支总会变动，需要可重现构建的用户应固定到具体的 commit。在构建所拉取包的 C 代码时，其代码会如同任意构建脚本一样在用户机器上执行；固定到特定版本即意味着对该版本代码的信任。

<a id="5-the-cache"></a>
## 5. 缓存

```
~/.klang/
  config                          cc = "clang";
  pkg/<host>/<owner>/<repo>/<ref>/   拉取的包，包含子模块
  build/pkg/.../<toolchain>/      各包按 C 编译器划分的原生构建产物
  build/native/v1-<digest>/       锁定的原生构建产物与保留的依赖源码
  build/runtime/<hash>/...        按 C 编译器与配置划分的运行时目标文件
  build/prog/<name>-<hash>/       各程序生成的 C 代码、目标文件与日志
  toolchain/                      查找 C 编译器的缓存信息（如 Windows 上的 vcvars）
```

删除缓存目录不会丢失任何无法通过重新构建恢复的内容。

使用 `k cache stats` 可以统计缓存占用空间，使用 `k cache prune --budget <bytes>` 可以回收闲置的运行时及锁定原生构建产物，同时保留已拉取的源码以供离线构建。详见[缓存使用与清理](command_line.md#cache-usage-and-pruning)。
