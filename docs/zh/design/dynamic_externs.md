<a id="dynamic-externs-calling-a-shared-library-by-name"></a>
# 动态 extern：按名称调用共享库

<details>
<summary>目录</summary>

- [1. 决策](#1-decisions)
- [2. 当前的边界机制](#2-the-boundary-today)
- [3. 声明语法](#3-the-declaration)
- [4. 运行时行为](#4-what-happens-at-run-time)
- [5. 生成的 C 代码](#5-the-generated-c)
- [6. 不经适配器直接调用真实库](#6-calling-a-real-library-without-an-adapter)
- [7. 编译器各组成部分](#7-the-compilers-pieces)
- [8. 曾考虑的替代方案](#8-alternatives-considered)
- [9. 测试](#9-testing)
- [10. 文档](#10-documentation)
- [11. 实施计划](#11-the-plan)
- [12. 延后与开放问题](#12-deferred-and-open)

</details>

目前，`extern fn` 是由链接器解析的 C 函数，它可以来自运行时、包的原生部分，或者通过 `--include` 传入的头文件。本文档提出第二种形式：一种指明其所在共享库（`.so`、`.dll`、`.dylib`）的 `extern fn`。程序启动时会打开该共享库，运行时按名称查找符号并将符号地址存入指针，后续调用则通过该指针完成。这样一来，程序就能直接使用机器上已有的库（例如 SDL2、sqlite3 或以 DLL 形式分发的厂商 SDK），而无需任何 C 源码、导入库或 `.build` 附属构建文件（sidecar），在 JIT 模式下也无需 C 工具链。本文基于代码树 `1e31b6d` 编写，尚未实现。

<a id="1-decisions"></a>
## 1. 决策

* **源码指定库名称。** `extern "SDL2" { fn SDL_Init(flags: u32) -> i32; ... }` 声明一组绑定自同一个库的 extern 函数。代码块内的每一行都是规范 §7.10 所定义的 extern 声明，同样支持在符号名与 K 名称不同时附带可选的符号名字符串。语言的其他部分均保持不变。对于检查器、重载解析、优化器和副作用分析而言，这些成员就是普通的 `extern fn`。
* **由运行时通过指针绑定。** 生成的 C 代码为程序调用的每个动态 extern 保留一个函数指针，并生成一张由 `(library, symbol, slot)` 条目组成的表。在程序启动时、全局变量初始化之前，运行时使用 `dlopen`/`dlsym`（Windows 上为 `LoadLibrary`/`GetProcAddress`）填充各个槽位。每次调用都是一次间接调用，与通过 PLT 调用动态链接符号完全相同。链接命令行中从不提及该库，因此不需要导入库或 `-dev` 开发包。按照后端设计（`jit_backend.md`）的要求，同一份生成的 C 代码同时服务于 JIT 运行和 `k build` 可执行文件。
* **即时解析，且只绑定实际调用的符号。** 代码生成所触及的动态 extern（`usedexterns`）对应的库和符号会在 `main` 之前完成解析。若缺少库或符号，将在程序执行任何逻辑之前抛出同时指明两者的运行时错误。未使用的声明不会被绑定，其对应的库也无需存在，这延续了规范 §7.10 中“未使用的 extern 无需实现”的规则。延迟绑定与可选库均延后处理（§12）。
* **单一名称，按平台推导拼写，且可在附属构建文件中覆盖。** 裸名称 `"SDL2"` 分别对应 `SDL2.dll`、`libSDL2.dylib` 和 `libSDL2.so`，由各平台的加载器按常规库查找方式定位。带版本号的 soname 或随程序一同分发的文件，可在程序的附属构建文件中按平台指定（`dylibs_linux = ["SDL2=libSDL2-2.0.so.0"]`）；程序自身的条目优先于包的条目。编译器在编译期绝不打开任何库。
* **符号是字符串，而非 C 标识符。** 它会按字面原样传给 `dlsym`，因此经过修饰（decorated）或重整（mangled）的符号名也能像普通符号名一样顺利绑定。
* **跨边界的数据形态没有新增。** 动态 extern 使用与规范 §7.10 相同的类型形态。变化在于 K 声明与库的真实函数原型之间不再有适配器，因此 §6 说明了如何用这些类型形态来声明真实的 C 函数签名：句柄使用 `u64`，字符串使用指向以 NUL 结尾的缓冲区的引用，结构体使用 `pad` 来对齐布局。

<a id="2-the-boundary-today"></a>
## 2. 当前的边界机制

一个 `extern fn` 目前有三种实现来源（规范 §7.10，实现 §6）：

* 运行时（`k_os_*`、`k_sqrt`），其原型声明在 `runtime_ext.h` 中。

* 通过 `--include` 提供的头文件，依据生成的 typedef 编写。

* 包的原生部分（`docs/packages.md`）：C 源码或 CMake 目标，按每种工具链构建一次并静态链接。

在 JIT 运行时，原生部分会被额外构建为一个导出单张表 `k_symbols` 的共享库。编译器加载该表（`shared_library.cpp` 中的 `LoadSharedSymbols`），并将其中的符号逐一传给 TinyCC（`jit.h` 中的 `tcc_add_symbol`）。代码生成为运行时未声明的每个已用 extern 生成函数原型，并直接调用该符号（`codegen_calls.h` 中的 `EmitExternCall`、`ExternProto`）。`NativePackagesUsed`（`native.h`）负责将用到的 extern 映射到需要链接原生部分的包。

若要用这种方式绑定一个已安装的 C 库，包必须配备一个仅包含链接选项的原生部分：即一份带有 `c_libs = ["SDL2"]` 的附属构建文件，以及一个甚至可以为空的 C 文件，以便编译器有东西可构建。即便程序自身没有任何需要编译的 C 代码，JIT 运行也仍然需要 C 工具链来生成 `k_symbols` 库。`k build` 则需要导入库（`SDL2.lib`）或无版本号的 `libSDL2.so` 符号链接（在 Linux 上来自 `-dev` 包）。若某个库仅作为 DLL 分发且没有 `.lib` 文件，则不手写 C 加载器就无法使用。

<a id="3-the-declaration"></a>
## 3. 声明语法

```klang
extern "SDL2" {
    fn SDL_Init(flags: u32) -> i32;
    fn SDL_Quit();
    fn SDL_GetTicks64() -> u64;
    fn delay(ms: u32) = "SDL_Delay";
}
```

语法新增一种顶层形式：

```
externblock  := "extern" strlit "{" externmember* "}"
externmember := "fn" declname "(" params? ")" ("->" rettypes)? ("=" strlit)? ";"
```

无论是在顶层（规范 §7.10）还是在代码块内部，符号名都作为 `=` 之后的字符串给出，位于原本放置函数体的位置。当 `extern` 后的字符串紧跟 `{` 时，该字符串指定库名；当紧跟 `fn` 时，该字符串则是已弃用的顶层符号语法，目前仍会伴随警告予以接受，但代码块成员不可使用该语法。未指定符号字符串的成员会绑定与其 K 末级名称相同的符号。这使 K 名称整齐地保持在同一列中：`fn delay(` 正是读者或 grep 查找的目标。成员接受 `declname`，因此带命名空间的 `fn gfx::init(...)` 的写法与在代码块外部相同。目前 extern 不能携带的修饰符（`checkfree`、`simd`、`inline`、`recursive` 以及函数体）在代码块内部同样会报错，且错误消息一致；`thread_fn` 和 `export` 也不是合法成员。代码块只能出现在顶层，且不可嵌套。尾部默认参数的处理方式与规范 §7.10 一致：每次调用都必须传入所有参数。

两个声明可以用不同的 K 签名绑定到同一个符号（例如 `math.k` 中的 `sqrt`/`sqrtf` 重载对，或同一函数的两种视图）；每个声明都会获得各自的函数指针，因此与原型形式不同，不会产生 C 级别的冲突需要规避。

**库的拼写。** 字符串可以是裸名称或文件名：

| 字符串 | Windows | macOS | Linux |
|---|---|---|---|
| `"SDL2"`（无分隔符、无点号） | `SDL2.dll` | `libSDL2.dylib` | `libSDL2.so` |
| `"libfoo.so.3"`、`"foo.dll"`（含点号） | 按原文 | 按原文 | 按原文 |
| `"bin/steam_api64"` 或 `"./x.so"`（含分隔符） | 相对于程序的资源目录，见下文 | | |

裸拼写会作为裸名称直接传递给平台的加载器，因此在 Windows 上按 DLL 搜索顺序查找，在 macOS 上通过 `DYLD_LIBRARY_PATH` 及常规路径查找，在 Linux 上通过 `LD_LIBRARY_PATH`、rpath 和 `ld.so.cache` 查找。除非它是绝对路径，否则含分隔符的拼写会拼接到程序的资源目录（`resource_dir()`，见 `docs/stdlib.md`：在 JIT 运行时为入口源文件所在目录，在已构建程序中为可执行文件所在目录）。因此，程序可以将 DLL 随自身一同分发，并在所有平台上用相同的名称引用它。裸名称转换规则应用于最后一个路径分量（`"bin/steam_api64"` 会变为 `bin/steam_api64.dll` 和 `bin/libsteam_api64.so`）。含点号的文件名会按字面原样使用，因此具有平台相关性；除非程序只针对单一平台，否则它更适合放在附属构建文件中而非源码里。

**附属构建文件（sidecar）。** `dylibs` 列表将源码中使用的名称映射到当前平台应当打开的库拼写。与附属构建文件中的其他所有列表一样，它可以带有 `_windows`、`_linux` 和 `_macos` 后缀（`packages.h`）：

```klang
// myprog.build
dylibs_linux = ["SDL2=libSDL2-2.0.so.0"];     // 各发行版普遍安装的 soname
dylibs_windows = ["SDL2=bin/SDL2.dll"];       // 随程序一同分发
```

等号左侧是源码中的字符串，右侧是如上表所述的拼写。程序的附属构建文件可以为其使用的每个包映射名称，而包的附属构建文件也可以为其自身的名称提供默认映射。如果映射的名称在程序中没有任何代码块声明，则会报错，这可以防止拼写笔误。附属构建文件中的配置并非强制要求：没有配置时，程序会直接打开按平台推导出的拼写。

这两种机制缺一不可。绑定 SDL2 的包希望在源码中只写一次 `"SDL2"` 就能让所有用户使用，而按平台推导的拼写恰好匹配安装了开发版库的用户环境。至于程序最终如何部署（是运行在只安装了运行时包的机器上，还是将 DLL 放在可执行文件旁），只有程序自身知道，而程序的附属构建文件本来就是它声明链接项和构建配置的地方。我们也曾考虑过在列表中按顺序尝试多个候选拼写的方案，现已延后处理（§12）。

<a id="4-what-happens-at-run-time"></a>
## 4. 运行时行为

运行时在 `runtime_os.h`（包含平台头文件并因此同时属于运行时目标文件和 `--standalone` 编译单元的那一半运行时）中新增以下接口：

```c
typedef struct k_dllib  { const char *name, *win, *mac, *linux; } k_dllib;
typedef struct k_dlbind { int32_t lib; const char *sym; void **slot; } k_dlbind;
K_API void k_dl_bind(const k_dllib *libs, int64_t nlibs,
                       const k_dlbind *binds, int64_t nbinds);
```

`k_dl_bind` 会打开每个库一次（使用 `dlopen(RTLD_NOW | RTLD_LOCAL)`，或在 Windows 上对按 `k_os_*` 相同方式转换为宽字符的 UTF-8 拼写调用 `LoadLibraryW`），绝不关闭它，并将每个符号的地址存入对应的槽位。每个库条目包含裸名称和来自附属构建文件的三个平台拼写，使用默认推导拼写的平台对应字段为 `NULL`。运行时根据其编译所在平台推导默认拼写，因此生成的 C 代码与其余部分一样保持跨平台可移植性。失败时会通过 `k_panic` 触发运行时错误，并附上加载器返回的文本：

```
cannot load SDL2 (libSDL2.so): libSDL2.so: cannot open shared object file: No such file or directory
SDL2 (libSDL2.so) has no symbol SDL_Foo
```

程序的 `k_program_init` 会在 `k_rt_init` 之后、`k_init_globals` 之前调用 `k_dl_bind`（`codegen_emit.h`）。因此，全局变量的初始化表达式可以调用动态 extern，`--header` 库的 `fn main()` 也可以调用它，因为 `k_init` 运行的是相同的初始化流程。绑定发生在主线程上、任何工作线程存在之前。各个槽位只写入一次，此后只读，因此 `thread_fn` 可以无需同步地读取它们，就像读取已初始化的全局状态一样。只能在主线程上调用的库沿用目前的标记方式：在声明它的包的附属构建文件中设置 `main_thread_only = true`。检查器已经会将该设置应用到包中的每一个 extern fn（`typecheck.h` 中的 `thread_fn` 遍历）。

程序绝不关闭已打开的库。当某个工作线程可能仍在执行其中的函数时对库调用 `dlclose`，正是本语言致力于避免的问题类型，况且进程很快就会结束。

**工具链影响。** `dlopen` 在 glibc 2.34 及以上版本中位于 libc 内，在此之前的版本中位于 `libdl` 内。在 Linux 上，`-ldl` 会被添加到 `SystemLibs()`（`build.h`）中；在较新系统上，`libdl` 是一个无害的空桩库。在 macOS 上，`dlopen` 位于 libSystem 中；在 Windows 上，`LoadLibraryW` 位于 kernel32 中；两者均已默认链接（运行时目前已调用 `GetProcAddress`）。JIT 运行使用 TinyCC 编译运行时，而 TinyCC 默认链接 libc 但不链接 libdl。因此，`RunJit` 在 Linux 上会调用 `tcc_add_library(s, "dl")` 并忽略其失败，因为在缺少该归档库的系统上，该函数已在 libc 中提供。TinyCC 对自身的边界检查器也采取了相同的做法（`tccelf.c`）。由于运行时的源码文本发生了变化，`K_RUNTIME_VERSION` 也会随之更新，运行时目标文件将像任何运行时改动一样重新构建。

<a id="5-the-generated-c"></a>
## 5. 生成的 C 代码

对于 §3 中的代码块，当所有成员都被调用时，生成如下代码：

```c
/* ---- extern prototypes ---- */
static int32_t (*k_dl0_SDL_Init)(uint32_t);
static void (*k_dl1_SDL_Quit)(void);
static uint64_t (*k_dl2_SDL_GetTicks64)(void);
static void (*k_dl3_SDL_Delay)(uint32_t);
static const k_dllib k_dllibs[] = {
    { "SDL2", 0, 0, "libSDL2-2.0.so.0" },
};
static const k_dlbind k_dlbinds[] = {
    { 0, "SDL_Init", (void **)&k_dl0_SDL_Init },
    { 0, "SDL_Quit", (void **)&k_dl1_SDL_Quit },
    { 0, "SDL_GetTicks64", (void **)&k_dl2_SDL_GetTicks64 },
    { 0, "SDL_Delay", (void **)&k_dl3_SDL_Delay },
};
```

并在 `k_program_init` 中生成 `k_dl_bind(k_dllibs, 1, k_dlbinds, 4);`。原型形式中写作 `SDL_Init(flags)` 的调用点，在此处变为 `k_dl0_SDL_Init(flags)`。调用前后的参数快照与构建器（builder）回写（`EmitExternCall`）均保持不变。指针名称由该声明在 `usedexterns` 中的位置和符号名共同构成，既具有确定性，又能保证每个声明的名称彼此不同。`k_` 前缀可防止它与 `--include` 头文件声明的任何内容发生冲突，因此头文件可以为了自身用途而 `#include "SDL.h"`，不会像直接原型形式那样产生原型冲突。通过 `void **` 将 `dlsym` 返回的 `void *` 存入函数指针，是所有 POSIX 程序的标准做法，也符合各平台 ABI 的定义。

各库按照程序中的代码块声明它们时的顺序（先按文件顺序，再按源码顺序）排列，各绑定条目按照 `usedexterns` 顺序排列，因此多次运行生成的输出保持稳定。当没有使用任何动态 extern 时，这两张表与槽位均会省略，而 `/* ---- extern prototypes ---- */` 部分仍保留静态 extern 的原型。

在编译器支持的目标平台（x86-64、AArch64）上，调用约定均为平台唯一的 C 调用约定。32 位 Windows 的 `__stdcall` 不在支持目标之列。可变参数函数无法绑定：声明语法没有办法表示一个函数是可变参数的，而通过固定参数个数的函数指针调用可变参数函数属于未定义行为（在 Apple AArch64 上确实会出错）。编译器无法检测此类情况。§6 将其列为程序员需遵守的规则，这类函数可以通过原生部分中的包装函数来调用。

<a id="6-calling-a-real-library-without-an-adapter"></a>
## 6. 不经适配器直接调用真实库

规范 §7.10 中的类型形态在设计时考虑了适配器的存在：依据生成的 typedef 编写的头文件会在 K 传入的数据与库期望的数据之间做转换。使用动态 extern 时没有适配器，因此声明必须用 K 的类型形态直接表达库的真实函数原型。库的大部分 API 都符合这些形态；不符合的部分列在最后。

* **标量**按其对应的 C 类型跨边界传递：`i32` 对应 `int`，`u32` 对应 `unsigned`，`i64` 对应 `int64_t`，`f32` 对应 `float`，`f64` 对应 `double`，`bool` 占一个字节。C 的 `long` 在 Linux 和 macOS 上是 `i64`，在 Windows 上是 `i32`，因此签名中包含 `long` 的函数需要按平台分别声明或使用适配器；`size_t` 在所有目标平台上都是 `u64`。
* **不透明句柄**，例如库返回并在后续重新传入的 `SDL_Window *`，使用 `u64`。在两个 ABI 上，指针与 64 位整数的返回和传参方式完全相同。K 无法解引用查看 `u64` 指向的内容，这正是本意：句柄仅作为一个令牌（token）。这是本文档确立的约定，而非语言变更，因为 `u64` 本来就可以跨边界传递。
* **指向 K 所拥有数据的指针**使用 `T&`（其中 `T` 为扁平定长类型），它会作为该指针直接跨边界传递：`fn SDL_GetWindowSize(w: u64, x: i32&, y: i32&)`。切片 `T[:]` 会按值以 `{ data, len }` 对的形式传递，没有 C 库会接受这种形态。当 C 需要 `(ptr, count)` 时，声明应将 `xs[0]` 与 `xs.len` 作为两个参数传入。当 C 需要以 NUL 结尾的字符串时，K 一侧会在末尾追加 `\0` 并按引用传入第一个字节：

  ```klang
  extern "SDL2" {
      fn SDL_CreateWindow(title: const u8&, x: i32, y: i32, w: i32, h: i32, flags: u32) -> u64;
  }
  let title = "K\0";
  let win = SDL_CreateWindow(title[0], 100, 100, 640, 480, 0);
  ```

这类调用精确地展示了跨边界传递的内容，但写起来颇为繁琐。`stdlib` 中的 `ffi` 模块可以提供下文所述的两个辅助函数。
* **按值传递和按引用传递的结构体**按照 C.2 的紧凑（packed）布局跨边界传递，只有当 K 的字段已经按 C 放置字段的方式对齐时，它才与 C 的布局一致。`SDL_Rect { int x, y, w, h }` 与 `struct Rect { x: i32, y: i32, w: i32, h: i32 }` 匹配；而若结构体中一个 `u8` 位于一个 `i32` 之前，则只有在 C 插入填充字节的位置显式写出 `pad` 时才能匹配（规范 §3.2 为此提供了 `pad` 和 `pad n`）。尾部填充同理：C 结构体的大小是其对齐值的整数倍，因此 `{ a: i32, b: u8 }` 在末尾需要加上 `pad 3`，才能放入由 C 进行索引的数组中。文档会配合这两个示例把这一点说明白。然而，字节布局匹配并不意味着调用约定匹配。`pad` 在生成的 C 代码中是一个 `uint8_t` 数组，因此按值传递时，`{ x: f32, pad 4, y: f64 }` 会被放入整数寄存器，而库中的 `{ float; double; }` 则会放入两个浮点寄存器（在 x86-64 System V 上如此；ARM64 浮点聚合类型的行为亦同）。因此，禁止在代码块中按值传递包含 `pad` 的结构体，这类结构体必须按引用传递。后续改动可以通过在调用点生成一个自然对齐的镜像结构体来解除这一限制。
* **仍需要适配器的情况。** 有三类情况仍需要适配器。第一类是读取库自身拥有的内存，例如库返回的 `const char *`（`SDL_GetError`）或库内部维护的数组（`SDL_GetKeyboardState`）：K 无法通过 `u64` 读取内存，也不应增加这种能力。第二类是回调函数：期望接收函数指针的库（如 `qsort`、GLFW 的事件回调）无法从 K 获得回调函数，因为 `export fn` 虽然有 C 符号，但没有表示其地址的 K 值。第三类是可变参数函数（`SDL_Log`、`printf`）。目前这三类情况都只需在原生部分写几行 C 代码即可处理，而下面这两个读取辅助函数将能够消除第一类情况对适配器的需求：

  ```klang
  // stdlib/ffi.k，拟议，待本特性落地后再行决策
  extern fn cstr(p: u64, out: u8[>..]&) = "k_ffi_cstr";             // 从 p 开始直到 NUL 为止的字节
  extern fn bytes(p: u64, n: i64, out: u8[>..]&) = "k_ffi_bytes";   // 从 p 开始的 n 个字节
  ```

  它们会将数据复制到 K 的存储空间中，因此外部内存绝不会在未经复制的情况下直接成为 K 值，这遵循了规范 §9 的边界规则（外部字节在进入时须经过验证）。它们信任传入的指针，就像每次 extern 调用都信任 C 一样，文档也会明确说明这一点。

<a id="7-the-compilers-pieces"></a>
## 7. 编译器各组成部分

* **AST。** `SFunction` 新增字段 `string dylib;`，对于当前的静态 extern 为空字符串。`isextern` 对这两种 extern 都保持为 true，因此所有询问“这是否是一个 C 函数”的逻辑都无需改动。
* **解析器**（`parser.h`，函数解析器中的 `T_EXTERN` 分支）。在读取 `extern strlit` 之后，遇到 `{` 即开始解析一个代码块。解析器循环读取成员，直到遇到 `}`。每个成员都通过现有的函数头路径解析，同时设置 `isextern` 并记录所属库名，并在其后期待一个 `;`，与单条 extern 声明一致。该代码块针对函数体、嵌套代码块、extern 不允许的成员修饰符、空库名字符串以及非顶层代码块都有各自的报错。允许空代码块：它的声明效果等同于一条注释，且包可能会自动生成空代码块。
* **检查器。** 沿用 `CheckExternSpec` 检查，保持不变。`main_thread_only` 遍历同样保持不变。
* **代码生成**（`codegen_calls.h`、`codegen.h`、`codegen_emit.h`）。代码生成负责输出 §5 中展示的函数指针、两张表和绑定调用。`ExternProto` 为动态声明输出指针声明、为静态声明输出函数原型，而 `EmitExternCall` 则通过指针名发起调用。附属构建文件中的库拼写通过 `Ast` 上的包记录传入代码生成（`dylibs` 保存在包记录上，在输出表格时将程序自身的条目合并并覆盖包的条目）。
* **原生部分**（`native.h`）。`NativePackagesUsed` 会跳过动态声明。因此，若一个包的所有 extern 都是动态的，它就没有原生部分；若一个包同时包含两种 extern，调用动态 extern 也不会把它的原生部分拉入链接。`ExternNamesIn`（词法层面的扫描，用于列出包的 `k_symbols` 表必须导出的符号）会跳过从 `extern strlit {` 到对应 `}` 的内容，因为这些符号不属于原生部分。
* **驱动程序**（`main.cpp`、`build.h`、`jit.h`）。在 Linux 上将 `-ldl` 添加到链接参数，并在 `RunJit` 中添加 `tcc_add_library(s, "dl")`，且允许找不到该库的情况（§4）。编译期不进行任何库探测，也不新增命令行选项。
* **附属构建文件**（`packages.h`）。将带有三个平台后缀的 `dylibs` 加入已知键列表。条目格式为 `name=spelling`；每条记录都会检查是否包含 `=` 并保存在包记录上。在输出表格时，程序的条目会覆盖包的条目。此时所有代码块都已已知，若某个条目映射的名称没有任何代码块声明，则报错。
* **运行时**（`runtime.h` 放置声明，`runtime_os.h` 放置定义）。见 §4。
* **编辑器。** tree-sitter 语法（`tree-sitter/grammar.js`，函数声明规则上的 `extern` 修饰符）和 VS Code 语法均支持该代码块；语言服务器只能看到普通函数，无需改动。自举测试中的 K-in-K 解析器（`test/k_in_k/parser.k`）也会解析该代码块，因为它会解析标准库和示例程序。
* **Wasm。** Wasm 不是本次工作的目标；`wasm.md` §3 将 extern 列为解码指针边界。§12 中提及了它，因为这种声明在概念上相当于一个 wasm 导入。

<a id="8-alternatives-considered"></a>
## 8. 曾考虑的替代方案

* **直接链接库而不是动态打开它。** 像现在一样生成原型，把 `-lSDL2`（`SDL2.lib`）放到链接命令行中，并在 JIT 运行时由编译器对库调用 `dlopen`，再对每个用到的函数调用 `tcc_add_symbol`，就像处理原生部分那样。已否决：这样构建时仍然需要导入库或无版本号的 `.so`，也就是需要 `-dev` 包或手工制作 `.lib`。JIT 与 build 两条路径解析符号的方式也会不同，导致同一个错误在一条路径上表现为链接错误，在另一条路径上表现为加载错误。没有随附 `.lib` 的 DLL 将无法使用。而通过指针调用并不比通过 PLT 调用慢。
* **编译器在编译期打开库**以验证符号是否存在，作为一种检查手段。已否决作为默认行为：构建程序的机器往往不是运行程序的机器（例如未安装 SDL 的 CI 构建机）。在真正运行程序的机器上，在 `main` 之前抛出指明库和符号的运行时错误已经足够早。后续可以为开发者本机增加 `--probe-libs` 检查选项（§12）。
* **逐条声明语法**，`extern "SDL2" "SDL_Init" fn init(...)`。已否决：对于习惯 C 系语言的人来说，两个相邻的字符串看起来像字符串拼接。此外，一份绑定通常涵盖一整套库函数，使用代码块只需写一次库名。我们也考虑过库句柄声明（`lib sdl = "SDL2"; extern sdl fn ...`）并予以放弃，因为它为了单一用途引入了一种新的声明形式。
* **仅在附属构建文件中命名库**（`dylibs = ["SDL2"]`，extern 沿用当前写法，通过搜索列出的库来解析）。已否决：包的绑定应当在其源码中自洽完整，符号所属的库应当在符号声明处一目了然，而且裸 `extern fn` 会悄无声息地具备双重含义。
* **首次调用时延迟绑定**，并在每次调用时检查。目前暂予否决：这会为了一个尚未有人提出需求的特性（可选库）给每一次调用增加一次分支判断，而且错误会在运行中途才报出，远不如在 `main` 之前报错清晰。见 §12。

<a id="9-testing"></a>
## 9. 测试

* **为测试构建的共享库**，`test/extern_dylib.py`，使用其他原生测试所用的工具链（`scripts/toolchain.py`）。该测试将一个 C 文件编译成共享库并放置在测试程序旁。该 C 文件包含 `extern_types.py` 所覆盖的参数形态（标量、按值与按引用传递的结构体、指向元素的引用、构建器），外加一个返回句柄的函数和一个接收该句柄的函数。测试程序同时使用裸名称和附属构建文件中的拼写（`dylibs_linux = ["dltest=./libdltest.so"]` 及其各平台对应项，用以测试覆盖机制和相对路径）来绑定该库。分别以 JIT 和 `k build` 两种方式运行，并比对输出结果。
* **失败路径测试**，位于同一测试文件中。若代码块指定了一个不存在的库且调用了其中某个成员，程序应以包含所尝试拼写的运行时错误退出。若库中缺少被调用成员的符号，行为亦同。若同样的声明未被使用，程序应能正常运行。在包含原生部分的包中，位于动态 extern 旁的静态 extern 仍会正常链接该原生部分，且不会引入多余内容。
* **语法与检查错误**，`test/errors_tc` 与 `test/syntax`：带有函数体的成员、嵌套代码块、泛型成员、`checkfree` 和 `simd` 成员、空库名字符串、函数体内的代码块、缺少 `=` 或未对应任何代码块的附属构建文件 `dylibs` 条目。
* **确定性**：比对测试程序两次编译生成的 C 代码，如同自举检查比对各阶段生成的 C 代码一样。
* **无工具链 JIT**：在找不到任何 C 编译器的环境下（按 `native_toolchain.py` 的方式设置）用 `klang` 运行一个使用动态 extern 的程序，并验证其正常运行。这是本特性的主要收益，也是原生部分路径无法处理的场景。它绑定第一个测试所构建的库，因此安排在该测试之后运行。
* 自举解析器测试以及 tree-sitter 语料库也将加入该代码块。

<a id="10-documentation"></a>
## 10. 文档

* 规范 §7.10：新增一节介绍该代码块、§3 中的拼写对照表、绑定时机与失败行为，以及相应的语法规则。§6 中关于真实签名的约定归入 `reference.md` §24 并附上示例，因为它们属于使用建议而非语言规范本身。
* `docs/packages.md`，“原生部分”：在前面增加一段说明基于已安装库的包可能无需原生部分，并指向规范 §7.10；在其配置键表格中加入该附属构建文件键名。
* `docs/stdlib.md`：`resource_dir()` 补充说明含分隔符的库拼写相对于该目录解析。
* `jit_backend.md`：补充关于 libdl 的说明，以及仅通过动态库使用 C 的程序无需工具链即可运行的事实。
* 在 `samples/` 中提供一个示例，若系统存在 SDL2 则绑定使用，否则打印提示说明原因，这原本很有吸引力；但由于示例套件会在所有环境运行，因此改为以测试用的库作为示例，并以规范中的 SDL2 代码块作为说明图例。

<a id="11-the-plan"></a>
## 11. 实施计划

每一步对应一个可工作的提交，且每一步完成后测试均全部通过：

1. **运行时**：实现 `k_dllib`、`k_dlbind`、`k_dl_bind`、错误提示文本、`SystemLibs()` 中的 `-ldl` 以及 `RunJit` 中的 `tcc_add_library("dl")`。此时尚无调用方；运行时版本号更新。
2. **解析器与 AST**：实现代码块、`SFunction` 上的 `dylib`、相关报错、tree-sitter 与 K-in-K 语法，以及 `ExternNamesIn` 跳过代码块。代码生成此时以“尚未支持（not supported yet）”拒绝动态声明，从而使代码树可构建，错误测试也可在此步骤添加。
3. **代码生成与驱动程序**：实现槽位、两张表和绑定调用、`NativePackagesUsed` 跳过动态声明，以及仅使用推导拼写的 `test/extern_dylib.py`。
4. **附属构建文件**：实现 `dylibs` 及其平台后缀、程序覆盖包的配置、拼写笔误检查，以及覆盖测试。
5. **文档**：按 §10 更新文档，并收入 §6 中的约定。
6. **`stdlib/ffi.k`**（`cstr`、`bytes`）：待本特性有实际用户需要它们时，再作为独立决策推进。

<a id="12-deferred-and-open"></a>
## 12. 延后与开放问题

* **可选库。** 若程序希望在库存在时才使用它（例如硬件加速或遥测 SDK），会需要 `extern? "foo" { ... }` 或 `available("foo")` 查询，以及在 `main` 启动后再进行绑定。这需要带每次调用检查的延迟绑定，或由程序显式发起 `bind` 调用。待有程序实际需要时再行设计。
* **源码中的候选拼写列表**，`extern "libSDL2-2.0.so.0", "SDL2" { ... }` 按顺序尝试，跳过不符合当前平台格式的拼写。这可以让包在没有 `-dev` 符号链接的发行版上开箱即用，而无需用户手动添加附属构建文件条目。由于附属构建文件已经覆盖了该场景，且排序规则会增加额外的心智负担，因此暂予延后。
* **按代码块粒度设置 `main_thread_only`** 而非按包粒度，以便没有附属构建文件的程序在绑定窗口库时也能使用。若有需求，很容易作为库名字符串后的一个关键字加上。
* **回调函数**：为 `export fn` 提供函数指针值，使其能够传给需要回调的库。这属于独立设计，会涉及函数值本身的定义（规范 §7.6）。
* **编译期探测**，`--probe-libs`，在编译器进程内打开每个库并解析每个用到的符号，供开发者在本机检查使用。
* **Wasm**：`extern "env" { fn log(x: i32); }` 代码块即为一个模块导入 `(import "env" "log")`，不需要运行时绑定。未来实现 `wasm.md` 时，该代码块可按其库名字符串直接映射到导入；本文的设计不构成任何阻碍。
* **交叉编译。** 编译器当前不支持交叉编译。由于库拼写由运行时选择，生成的 C 代码依然保持可移植性。若未来支持交叉编译，附属构建文件中带平台后缀的列表已经能够区分目标平台。
* **`long` 及其他随平台位宽变化的 C 类型。** 目前的声明无法表达“C 的 `long`”。若语言未来引入编译期常量（`const`）平台检测，就可以在这种检测下写两个代码块；在此之前，需要通过适配器处理。
