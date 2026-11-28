<a id="the-k-standard-library--design"></a>
# K 标准库 — 设计

<details>
<summary>目录</summary>

- [0. 概要](#0-summary)
- [1. 原则](#1-principles)
- [2. 约定](#2-conventions)
- [3. 模块总览](#3-module-map)
- [4. `std`](#4-std)
- [5. `dictionary`](#5-dictionary)
- [6. `vec`](#6-vec)
- [7. `math` 与 `os`（基于 extern）](#7-math-and-os-extern-backed)
- [8. 标准库所需的内建函数与语言规则](#8-builtins-and-language-rules-the-library-needed)
- [9. 有意未包含的内容](#9-deliberately-not-included)

</details>

本文档记录了最初的标准库设计：其范围、约定，以及它所需的内建函数和语言规则（§8）。文档保留了当时的设计考量。后续变更包括 `const` 类型、命名空间、泛型别名、序列化、图形、物理、用户界面、`os` 中的目录和原子替换，以及 `base64`、`csv`、`json` 和 `regex` 模块；因此下文中部分规则和限制已被取代。当前 API 请参阅 `../stdlib.md`，当前语言规则请参阅 `../k_spec.md`。文中形如 §N 的引用均指代该规范。

---

<a id="0-summary"></a>
## 0. 概要

最初的标准库包含五个 K 源文件（`std`、`math`、`dictionary`、`vec`、`os`），共计约 120 个函数。它引入了三类命名类型：`dictionary<K, V>`（语言本身没有提供构建块的唯一设施）、`rng`（一个字大小的伪随机数生成器状态），以及 §6.1 中已承诺的 `vec2/3/4<T>` 数学向量。其余部分全部是作用于用户自有数组、结构体和池的函数。

除以下内容外，其余全部用 K 编写：

* `print`/`str`/`format` 族内建函数、`default<T>()`、`abort(msg)`、`exit(code)`，以及来自 §8.7 传参规则的 `copy(x)`；以及
* `extern fn` 声明形式（规范 §7.10），`math` 和 `os` 模块通过它接入 libm 和一层薄 C 运行时，而无需为每个函数单独设立内建函数。

K 库代码不使用堆分配器：每个临时对象都是局部可变长数组或有限容量数组，每个结果都在其目标位置构造（§4.3），且每个高阶函数都会编译为循环（§7.6）。

---

<a id="1-principles"></a>
## 1. 原则

**函数优先于类型。** K 的强项在于数组家族：八种数组、池、相对引用、可变模式 ADT，全都由用户自行命名和布局。标准库应当直接操作这些用户定义的容器，而不是要求再套一层自己的包装。库函数接收切片（`T[:]`）或指向调用方所持任意数组的引用，绝不要求库定义的包装类型。它要么返回标量、要么返回指向输入的引用、要么直接在调用方的目标位置构建新数组。仅当语言本身没有可依托的表示形式（哈希表）或规范已明确要求（向量）时，才引入新的具名类型。

**精简。** 标准库为每项任务只提供一个函数，其调用形式覆盖常见用法。没有 `_if`/`_by_key`/`_reverse`/`_indices` 系列变体。只有当常见手写循环超过三行或容易写错时，才纳入标准库函数，例如排序、哈希、UTF-8、浮点数打印和二分查找。§9 列出了省略的函数以及替代它们的一行循环。

**K 优先，内建函数为例外。** 仅当某项操作无法用语言自身表达时，才值得设为一个内建函数：自定义类型检查（任意类型的 `print`、`default<T>()`）、类型检查器必须知晓的不返回调用（`abort`），或通向操作系统的出口。单靠速度本身尚不足以成为理由，因为标准库的 K 代码预期会编译为与 C 程序员手写相同的循环。若后续实测表明某个库函数能从自定义代码生成中获益（按字处理的字符串哈希、用于字节搜索的 `memchr`），该函数无需改变签名即可移入内建函数表。`extern fn` 机制（§8.4）在不膨胀类型检查器的前提下保留了这种可能性。

**让开销显式化。** 标准库不做隐式分配，也不做隐式拷贝。§2 中的约定确保了廉价的调用写起来最自然，而高开销的调用不可能在无意间写出。

**Lobster 的形态，K 的成本模型。** 从 Lobster 的 `std.lobster` 中，我们借鉴了围绕函数、`it` 代码块、`find`/`filter`/`fold`/`map` 组织的 API，以及诸如 `insert_ordered` 的实用辅助函数。结果均原地构建，而不是每次调用都分配新向量。诸如 `tokenize` 和 `concat_string` 等宽泛的内建函数被替换为更小、可组合的函数。C++ 和 Rust 指导了需要覆盖的操作清单：排序、稳定排序、二分查找、堆、去重、保留筛选、哈希表和字符串搜索。K 在没有迭代器协议、trait 约束或包装类型的情况下实现了这些功能。它使用 `T?` 引用、多返回值和 `return from` 来替代 `Option`、`Result` 和异常。

---

<a id="2-conventions"></a>
## 2. 约定

调用点的语法遵循规范 §4.1 的传参规则，其设计记录于 §8.7：在引用类型的目标位置不写 `&`。

<a id="21-how-containers-are-passed"></a>
### 2.1 容器如何传递

| 该函数…… | 参数 | 调用点 | 适用于 |
|---|---|---|---|
| 读取元素，或在不改变长度的情况下原地修改元素（`sort`、`reverse`、`fill`、`find`、`sum` 等） | `xs: T[:]` | `f(arr)`, `f(arr[1..])`, `f(slice)` | 所有数组种类及切片 |
| 改变长度（`push_n`、`insert_at`、`remove_at`、`retain`、`heap_push` 等） | `xs: A&`，其中 `A` 是绑定到整个数组类型的泛型参数 | `f(arr)` | 所有支持所用操作的数组种类 |
| 接收库类型（`dictionary`、`rng`） | `d: dictionary<K, V>&` | `d.insert(k, v)` | — |

设计依据及被排除的方案：

* 数组无需拷贝即可隐式转换为切片参数（§3.10），因此 `sort(arr)` 没有额外开销且读起来自然。当数组可写时，通过切片写入是合法的（§9.5），因此原地算法不需要第二种形式。将 `let` 数组传给 `sort` 会在编译期报错并报告调用链，这正是预期的行为。
* 泛型引用参数 `xs: A&` 将 `A` 绑定为完整的数组类型，并针对每种数组种类分别实例化：同一份 `push_n<A, T>(xs: A&, v: T, n: i64)` 函数体既可用于 `[>..<]`，也可用于 `[>..]` 和结构体中的 `[..k]` 字段。它不能接收切片，因此专门保留给改变长度的操作——对这些操作而言切片本来也毫无意义。
* 无类型标注的参数（`fn f(xs)`）会绑定实参的精确类型，对 `f(&arr)` 而言即包含引用类型。根据 §8.7 的规则，非固定大小的左值实参按引用传入无类型参数，而固定大小的左值实参按值传入，因此 `fn total(xs)` 适用于所有数组种类。不过，只读函数在标准库中仍优先使用 `T[:]`，因为它明确给出了元素类型（`find -> T?`、`contains(xs, v: T)`）并且可以接收子范围切片。
* 既然增删数组 `[>..<]` 已经可以切片（§8.7），它们就能像其他数组一样传给表格第一行的函数。当指向此类数组的切片仍在作用域内时缩减该数组，会由检查器直接报错，而不需要标准库去处理。
* 任何非固定大小的值都不会按值接收。语言强制执行这一点（规范 §4.1），因此需要自有副本的库函数会显式调用 `copy(x)`。

<a id="22-results"></a>
### 2.2 返回结果

* **新建数组**（`filter`、`map`、`split`、`concat`、`str`）以 `T[>..]` 形式返回：这是一个通过 NRVO 直接在调用方目标位置构造的局部可变长数组（§7.3）。因此，对于 `let ys = xs.filter() { it > 0 }`、`var ys = xs.filter() { … }`（并继续追加）、`out.append(xs.map() { … })` 以及 `words.push(str("w", i))`，唯一执行的工作就是构造元素本身。返回类型采用 `T[>..]` 而非 `T[]`，因为它没有长度字段上限，且调用方通常希望继续增长或对其排序。它所需的“新栈”只是用于追踪该值生命周期的栈索引，并非运行时开销。`T[]` 目标位置也会自动转换。
* **“可能存在的元素”**（`find`、`get`、`last`）类型为 `T?`：指向输入中元素的可选引用，其根生命周期与输入一致，若为 null 则表示“无”。调用方使用 `if`/`guard` 收窄类型（§3.8），并且可以通过它写入。语言没有 `Option<T>` 值类型，标准库也没有虚构一个。
* **位置**（`find_index`、`index_of`、`min_by`、`lower_bound`）类型为 `i64`，在可能不存在的情况下用 `-1` 表示“无”。
* **双结果标量**（`parse_int`、`binary_search`、`read_line`）采用规范中多返回值末尾附加 `bool` 的约定：`let v, ok = parse_int(s);`。没有错误类型；编程错误直接 `abort`。
* **文本输出**写入调用方传入的构建器（`format(out, …)`、`read_file(path, out)`），因此结果会追加到调用方正在组装的任何内容之后。唯一的例外是 `str(…)`，它的存在意义就是生成一个新的字符串。

<a id="23-function-value-parameters"></a>
### 2.3 函数值参数

* 函数值采用尾随代码块形式（§7.6）：`xs.find() { it.key == k }`、`xs.sort() { a, b => a.age < b.age }`，或传入具名函数。
* 比较器始终是严格的“a 排在 b 之前”谓词 `lt(a, b)`。排序、堆和极值函数均有两种参数数量的重载：带代码块，以及不带代码块（表示 `a < b`，因此 `sort(xs)` 适用于数字，而 `sort(xs) { a, b => compare(a, b) < 0 }` 适用于字符串）。重载解析通过是否提供了函数值来区分二者；这一点已得到验证。
* *键*（key）函数返回一个值（`min_by(xs) { it.score }`）。
* 当库函数向代码块传入 `&x` 时，代码块参数会绑定为一个**引用**，从而使代码块可以修改元素：`d.each() { k, v => v += 1; }` 之所以可行，正是因为 `each` 调用的是 `F(s.key, &s.val)`。各函数文档会说明哪些参数按引用传递。
* 在代码块内部，`return` 会从*外围具名函数*返回（§7.6），因此 `each_*` 函数的调用方无需任何额外机制即可提前退出。

<a id="24-trait-like-overload-sets"></a>
### 2.4 类似 Trait 的重载集

K 基于类型的重载机制就是它的 trait 机制。标准库定义了少数几个名称，预期用户代码通过为自己的类型添加重载来加以扩展：

| 重载集 | 内置提供 | 用户添加 | 使用方 |
|---|---|---|---|
| `hash(x) -> u64` | 所有标量类型、`bool`、`u8[:]` | `fn hash(k: mykey) -> u64` | `dictionary` |
| `compare(a, b) -> i64` | `u8[:]` | `fn compare(a: ver, b: ver) -> i64` | 字符串排序 |
| `format(out: u8[>..]&, x)` | 由内建函数为所有类型按结构提供（§8.1） | `fn format(out: u8[>..]&, p: point)` | `print`/`str`/`format`，无论该类型出现在何处都会使用用户的重载 |

两条解析规则（§7.1）决定了这些重载的写法：

* 具体重载优先于泛型重载（`hash(k: mykey)` 会优先于 `hash<T>(x: T)` 被选中），因此用户重载会自动生效。
* 每种整数位宽都必须有**自己**的重载：`u8` 实参同样可以拓宽为 `i64` 和 `u64`，若存在两个处于隐式转换层级的匹配就会产生二义性错误。因此 `hash` 是十个单行函数，而不是一个泛型函数。

<a id="25-ufcs"></a>
### 2.5 UFCS

`x.f(args)` 等价于 `f(x, args)`（§7.1）。根据传参规则（§8.7），当且仅当 `f` 的第一个参数是引用类型时，`x.f()` 才会按引用绑定 `x`，原因与自由调用 `f(x)` 完全相同。因此 `d.insert(k, v)`、`q.heap_push(v)` 和 `r.rand_int(6)` 都会严格按照各自签名所指定的方式绑定接收者。早期曾设想过为按引用接收的接收者引入逐函数标记，但出于两个原因被撤回：第一，这会让调用方习惯于到处省略 `&`，一旦有人忘了声明该标记，意外拷贝就会卷土重来；第二，它会导致调用形式依赖于了解每个函数的声明方式。

返回引用的调用不是可赋值的左值位置（`get_or_insert(d, k, 0) += 1` 会被拒绝）；绑定它需要使用带类型标注的形式 `let c: i64& = d.get_or_insert(k, 0);`（§3.8）。`dictionary` API 通过接收代码块的 `update`（§5.2）绕开了这个问题。

<a id="26-naming"></a>
### 2.6 命名

* 库类型采用小写字母命名，与内建类型一致：`dictionary<K, V>`、`rng`、`vec3<T>`、`float3`。重量级类型使用长名称。
* `each_*`：迭代高阶函数（`each_rev`、`each_split`、`each_utf8`、`each_chunk`）。
* `*_by`：值函数的键函数版本（`min`/`min_by`）。
* `*_at`：按索引寻址的数组变更操作（`insert_at`、`remove_at`），将不带后缀的动词（`insert`、`remove`）留给 `dictionary`。如果两个同名同参数数量的函数，第一个参数分别是 `A&` 和 `dictionary<K, V>&`，它们就会产生二义性（二者均在泛型层级匹配），因此数组与字典的词汇表保持互不重叠。
* `format_*`：带格式控制地向 `u8[>..]` 构建器追加文本形式；`parse_*`：其逆操作；`to_*`：原地大小写转换；`write_*`：仅用于 I/O。
* 不使用 `is_` 前缀（`any`、`all`、`contains`、`starts_with`）。
* v1 中没有命名空间（§11.1），因此所有库名称都是全局的。命名都选择了用户自己也会起的名字。其考量是：如果用户定义了带具体签名的自定义 `find`，就会选到自己的版本（精确匹配优先于泛型匹配）；而如果用户定义了泛型 `find`，则会得到明确的重复或二义性错误，而不是静默遮蔽。

<a id="27-strings"></a>
### 2.7 字符串

K 没有专门的字符串类型（§3.7）；标准库的字符串函数就是作用于 `u8` 数组的函数，分别承担以下角色：

| 角色 | 类型 | 说明 |
|---|---|---|
| 输入 | `u8[:]` | 字面量、任意 `u8` 数组及任意切片均可隐式转换 |
| 正在构建的输出 | `u8[>..]&` | 即“构建器”；`push`/`append`/`format` 是基本原语 |
| 持有的、存储的 | `u8[]`、`u8[varint]`、`u8[..k]` | 字段和元素；`u8[>..]` 结果可以存入其中 |

所有函数默认面向字节和 ASCII（`to_lower`、`trim`）；`each_utf8`/`push_utf8` 是仅有的编码感知函数。比较使用 `==`（结构相等，§4.5）和 `compare`。字符字面量是整型常量（§2）：`format(out, 'x')` 会追加 `120`，而追加单个字节应使用 `out.push('x')` 或 `format(out, "x")`。

---

<a id="3-module-map"></a>
## 3. 模块总览

| 文件 | 导入 | 内容 | 依赖项（§8） |
|---|---|---|---|
| `stdlib/std.k` | — | 标量、位运算、哈希、`rng`；数组高阶函数、搜索、排序、堆、调整大小；字符串、格式控制、解析、UTF-8 | 用于 `sum` 的 `default<T>()`；`abort` |
| `stdlib/math.k` | — | 以 `extern fn` 声明的 libm、`PI`/`TAU`、`radians`/`degrees`、`is_nan`/`is_inf` | `extern fn` |
| `stdlib/dictionary.k` | `std` | `dictionary<K, V>` 及其相关函数 | `default<T>()` |
| `stdlib/vec.k` | `std`, `math` | `vec2/3/4<T>`、`float*`、`double*`、`int*`、向量函数 | — |
| `stdlib/mat.k` | `vec`, `math` | `float2x2`…`double4x4`（列向量的固定数组）、变换、相机 | §6.1 固定数组的逐元素运算 |
| `stdlib/quat.k` | `mat`, `vec`, `math` | `quat<T>`、`floatq`、`doubleq`、旋转、球面线性插值（slerp）、与矩阵的互转 | — |
| `stdlib/os.k` | `std` | 文件、标准输入输出、命令行参数、环境变量、时间、休眠、熵源 | `extern fn`，`src/runtime/runtime_os.h` 中的运行时 C 代码 |

必须显式编写 `import std;`；没有任何模块会被自动导入。编译器按顺序在三个位置查找以解析 `import std;`。首先在导入文件的包根目录查找（规范 §11.1：对于程序自身文件即主文件所在目录，对于库模块即标准库所在目录）。其次，若指定了 `--stdlib <dir>` 或 `K_STDLIB`，则在该位置查找。最后从编译器自身可执行文件所在目录向上回溯查找 `stdlib/std.k`（最多三级）。这样一来，`build/Debug/k.exe`、`build/k`、`bin/k.exe` 以及与 `<prefix>/stdlib` 相邻安装的 `<prefix>/bin/k` 都能找到它，而无需强制单一安装路径。未使用的库函数仅消耗解析时间：函数从 `main` 起按调用图顺序进行类型检查，因此未被引用到的泛型函数绝不会被实例化。

---

<a id="4-std"></a>
## 4. `std`

函数签名以文档形式写出。当函数的返回类型是 `A&` 参数的元素类型时，源码中省略了该标注（由函数体推断，§7.1），而文档中写作 `-> T`，意为“T = 元素类型”。`xs: T[:]` 接受所有可切片数组（§2.1）。

<a id="41-scalars-and-bits"></a>
### 4.1 标量与位运算

```klang
fn min<T>(a: T, b: T) -> T
fn max<T>(a: T, b: T) -> T
fn clamp<T>(x: T, lo: T, hi: T) -> T
fn abs<T>(x: T) -> T            // integers; plus abs(f32) and abs(f64) overloads
fn rem<T>(a: T, b: T) -> T         // integers; pairs with `/`
fn div_euclid<T>(a: T, b: T) -> T  // integers; pairs with `%`
fn sign<T>(x: T) -> T           // -1, 0, 1 at x's type; f32/f64 overloads
fn lerp(a: f64, b: f64, t: f64) -> f64      // and an f32 overload
fn next_pow2(x: i64) -> i64     // smallest power of two >= max(x, 1); asserts x <= 2^62
fn popcount(x: u64) -> i64
fn clz(x: u64) -> i64           // 64 for x == 0
fn ctz(x: u64) -> i64           // 64 for x == 0
fn rotl<N>(x: u32, n: N) -> u32 // and u64, and rotr; n modulo the width
fn byte_swap(x: u32) -> u32     // and u16, u64
fn is_pow2<T>(x: T) -> bool
fn ilog2<T>(x: T) -> i64        // -1 for x <= 0
fn isqrt<T>(x: T) -> T          // aborts for x < 0
fn gcd<T>(a: T, b: T) -> T      // and lcm; assert the result fits
fn swap<T>(a: T&, b: T&)
```

`rem` 和 `div_euclid` 为截断除法 `/` 与欧几里得取模 `%` 补齐了对应的搭档，而不改变两个运算符本身。它们按操作数的整数类型进行计算，使用截断商及其余数——只要商不溢出，其乘积与减法就不会溢出。若改用构造 `a - a % b` 的方式则可能发生下溢。`rem` 会在除法前处理除数为 -1 的情况，因为即便商溢出，其零余数结果也依然可以表示。对全 1 负值的检测同样接受无符号类型，并要求操作数为整数。`div_euclid` 使用 `/` 计算初始商，从而保留其溢出检查。

`abs` 和 `sign` 拥有显式的浮点重载。`popcount`/`clz`/`ctz` 使用运行时的 `extern` 内在函数（§8.4）。

循环移位和字节序交换同样是运行时函数，这样性能就不必寄望于 C 编译器能否识别移位惯用法：它们使用 MSVC 的内在函数和 GCC 与 clang 的字节交换内建函数，以及 GCC 和 clang 都能编译为 `rol`/`ror` 的循环移位惯用法。移位次数是泛型的，这样像哈希函数所持有的 `u32` 次数就能精确选中 `u32` 重载；若参数类型写成 `i64`，则任何非 `i64` 次数都会发生一次转换从而导致两个重载并列冲突；次数在传入运行时之前会先按位宽做掩码截断。其余位运算辅助函数均用 K 编写。`ilog2` 遵循位置函数的 `-1` 表示无效约定，而不是直接中止。`gcd` 和 `lcm` 先将操作数归约为 `u64` 绝对值，因此 `MIN` 不需要单独的分支，并且会 `assert` 结果能够放回 `T`：发生回绕的公约数或公倍数毫无意义，因此与 `*` 不同，它们对无符号类型也会检查溢出。

<a id="42-hashing"></a>
### 4.2 哈希

```klang
fn hash(x: i8) -> u64        // … one overload per integer type, i8 … u64
fn hash(x: bool) -> u64
fn hash(x: f32) -> u64
fn hash(x: f64) -> u64
fn hash(s: u8[:]) -> u64     // 8 bytes per multiply; any u8 array coerces
fn hash_combine(seed: u64, h: u64) -> u64   // for composite keys
```

整数哈希采用乘法-异或移位混合（斐波那契哈希），使低位分布均匀，从而让 `dictionary` 可以直接按位掩码而无需取模除法。字节哈希以长度为种子，每次读取 8 个字节。除最后一个字外，每个字执行一次乘法和一次循环移位。最后一个字即末尾 8 字节（对于不足 8 字节的输入，则是将各字节聚集到一个字中），它会经过两轮乘法-异或移位，确保输入的每一位都能影响低 32 位——`dictionary` 会掩码保留这些位作为槽位标记（tag）。否则，仅在末尾字符不同的键（如 `item_17`、`item_18`）将会共享相同的低位。

每个字的那一轮计算必须等待前一个字的乘法完成。因此对于长度超过 24 字节的键，在最后至多 24 字节之前的那些字会拆分为两条通道（每 16 字节中的偶数字与奇数字），以便处理器重叠执行两条通道的运算。第二条通道在进入剩余字处理前会先经循环移位并折叠合并到第一条通道。这种双通道形式放在一个单独的函数（`hash_lanes`）中，通过提前返回路径调用，优化器会将其保持为非内联函数。这使得短键对应的代码足够精简，从而让 C 编译器能够将其内联到字典查找中。

用户键类型可定义 `fn hash(k: key) -> u64 { hash_combine(hash(k.a), hash(k.b)) }`。固定容量的内联字符串（`u8[..16]`）通过切片重载进行哈希，这使它们可以用作 `dictionary` 的键。

<a id="43-random-numbers"></a>
### 4.3 随机数

```klang
struct rng { s: u64 }                    // splitmix64 state; the seed is the state
fn rand_u64(r: rng&) -> u64
fn rand_int(r: rng&, n: i64) -> i64      // uniform in [0, n)
fn rand_flt(r: rng&) -> f64              // uniform in [0, 1)
fn shuffle<T>(xs: T[:], r: rng&)         // Fisher-Yates
```

使用 `var r = rng { 12345 };` 创建生成器，然后调用 `r.rand_int(6)`。splitmix64 仅有一个字的状态，只使用 64 位算术，且能通过常规的随机性测试套件；基准测试仍保留各自独立的 xorshift。若要从操作系统获取种子，请使用 `os` 模块的 `random_seed()`。

<a id="44-arrays-reading-and-searching"></a>
### 4.4 数组：读取与搜索

```klang
fn each_rev<T, F>(xs: T[:])                    // F(x), last to first
fn each_chunk<T, F>(xs: T[:], n: i64)          // F(chunk: T[:], i); asserts n > 0; last chunk may be short
fn find<T, F>(xs: T[:]) -> T?                  // first x with F(x), as a reference; null if none
fn find_index<T, F>(xs: T[:]) -> i64           // …or its index, -1 if none
fn position<T>(xs: T[:], v: T) -> i64          // first i with xs[i] == v, -1 if none
fn contains<T>(xs: T[:], v: T) -> bool
fn any<T, F>(xs: T[:]) -> bool
fn all<T, F>(xs: T[:]) -> bool
fn count<T, F>(xs: T[:]) -> i64                // number of x with F(x)
fn min<T>(xs: T[:]) -> T                       // asserts xs.len > 0; F(a, b) forms too
fn max<T>(xs: T[:]) -> T
fn min_by<T, F>(xs: T[:]) -> i64               // index of the smallest F(x); -1 if empty
fn max_by<T, F>(xs: T[:]) -> i64
fn last<T>(xs: T[:]) -> T&                     // asserts xs.len > 0
fn lower_bound<T, F>(xs: T[:]) -> i64          // first i with !F(xs[i]); F must be true on a prefix
fn binary_search<T>(xs: T[:], v: T) -> i64, bool   // sorted by <; position (or insertion point), found
fn find<T>(xs: T[:], sub: T[:]) -> i64         // first occurrence of a subsequence, -1 if none
fn rfind<T>(xs: T[:], sub: T[:]) -> i64
fn starts_with<T>(xs: T[:], p: T[:]) -> bool
fn ends_with<T>(xs: T[:], p: T[:]) -> bool
```

`each_chunk` 之所以存在，是因为按行切片是在二维计算核中消除边界检查的推荐做法（`bench/klang/blur_rows.k`）；而 `for y in h { each_chunk(img, w) … }` 正是把其中的下标算术集中做一次的惯用法。`find`/`rfind`/`starts_with`/`ends_with` 对元素类型是泛型的，因此它们也充当字符串函数（`find(line, "://")`）。它们与谓词版的 `find` 能够共存，因为函数值实参与切片实参绝不会匹配到同一个重载。

<a id="45-arrays-transforming"></a>
### 4.5 数组：转换

```klang
fn map<T, F>(xs: T[:]) -> U[>..]                 // U = F's result type (§8.5)
fn filter<T, F>(xs: T[:]) -> T[>..]
fn fold<T, A, F>(xs: T[:], acc: A) -> A          // acc = F(acc, x) for each x
fn sum<T>(xs: T[:]) -> T                         // fold from default<T>()
fn concat<T>(a: T[:], b: T[:]) -> T[>..]
```

`map` 的函数体为 `var out = []; for x in xs { out.push(F(x)); } return out;`，用到了 §8.5 的空数组声明。`sum` 需要 `default<T>()`；而 `fold(xs, 0.0) { a, x => a + x }` 则是一种不需要它的形式。

<a id="46-arrays-in-place"></a>
### 4.6 数组：原地操作

```klang
fn fill<T>(xs: T[:], v: T)
fn copy_from<T>(dst: T[:], src: T[:])            // asserts equal lengths; memmove semantics
fn copy_into<T>(dst: T[:], src: T[:])            // deprecated name of copy_from; a call warns
fn reverse<T>(xs: T[:])
fn sort<T>(xs: T[:])                             // by <
fn sort<T, F>(xs: T[:])                          // by F(a, b); unstable, in place, no allocation
fn stable_sort<T>(xs: T[:])
fn stable_sort<T, F>(xs: T[:])                   // natural merge sort; one temporary of xs.len elements
fn to_lower(s: u8[:])                            // ASCII, in place
fn to_upper(s: u8[:])
```

`sort` 是参考 pdqsort 与 Rust 的 ipnsort 实现的模式击败快速排序（pattern-defeating quicksort）。第一遍扫描会找出输入起始处的有序段（run）在哪里结束；如果该有序段就是整个输入，排序直接完成。否则每轮选取一个中位数枢轴（三数取中，或从 128 个元素起采用三个“三数取中”的中位数）。它采用无分支 Lomuto 循环进行划分（每个元素都会交换，边界根据比较结果推进）。随后将较长的一侧压入用于保存 (lo, hi, budget) 区间的 `i64[..144]` 栈，并继续处理较短的一侧。因此该排序绝不递归，不做任何堆/临时分配，且最多只保存 log₂(2⁴⁸) 个区间。

某区间左侧的所有元素都排在或等于该区间内的所有元素。因此，当枢轴并不排在紧邻该区间前方的元素之后时，一趟扫描会把等于枢轴的元素移到前端并将其从当前区间剔除，做法与 pdqsort 相同。由此，大量重复键只需线性趟扫描即可处理完毕。

每个区间携带 `2·log₂(n)` 轮的预算。当预算耗尽时，改用堆排序完成该区间，从而将最坏情况复杂度限定在 O(n log n)。`test/stdlib/stdlib_sort.k` 使用 McIlroy 对抗算法构造针对枢轴选择的输入来验证这一点。长度不超过 10 个元素的区间使用插入排序：在此规模下，实测带分支的插入排序比继续划分更快。

有一种输入形态比 `sort` 之前使用的 Hoare 划分更慢：除了少数错位元素外整体已然有序的输入。无分支划分会交换每个元素，相当于将有序区间的两侧各旋转一位。于是首次有序段检查之后的任何步骤都看不到可利用的有序性，此类输入耗时约为随机输入的三分之二。而旧版快速排序中分支预测良好的扫描在这类输入上耗时仅为新版的一半（20 万个元素：1.6 ms 对比 0.8 ms；随机输入则为 2.5 ms 对比 8.3 ms）。

`stable_sort` 是自然归并排序（natural merge sort）。一趟扫描将输入切分为有序段：反转严格降序的有序段，并通过插入排序将短于 16 的有序段补齐。如果整个输入本身就是一个有序段，排序到此结束。各有序段的结束位置保存在一个 `i64[>..]` 中。随后每趟在 `xs` 与位于新数据栈（只是一个栈索引，而非动态分配）上的 `T[>..]` 临时数组之间归并相邻的有序段对。各趟交替改变方向，从而省去回拷步骤。

两个本来就有序的段在归并时只需直接拷贝。否则，归并的每一步要么根据比较结果走分支，要么使用条件传送来选择元素和来源侧。当上一次归并在两段之间切换的次数少于四分之一时，归并采用分支版本。这是因为在随机输入上分支大约每两步就会预测失败一次，而在结构化输入上，预测命中的分支允许比较操作重叠执行。这一点对于需要解引用加载再比较的比较器尤为关键（例如 BWT 的 rank 对：在未排序轮次上两种形式相差 3.7 倍，而在随机整数上则反向相差 1.5 倍）。

无论比较器返回什么结果，两种排序最终都会得到输入的一个排列。不管比较结果如何，所有索引都始终保持在合法范围内，因此不满足严格弱序的比较器最多只会导致未指定的元素顺序。

<a id="47-arrays-changing-the-length"></a>
### 4.7 数组：改变长度

```klang
fn push_n<A, T>(xs: A&, v: T, n: i64)            // n pushes; grow-only arrays' answer to resize
fn insert_at<A, T>(xs: A&, i: i64, v: T)         // shifts [i..) up; needs push
fn remove_at<A>(xs: A&, i: i64) -> T             // shifts down; needs pop; returns the element
fn swap_remove<A>(xs: A&, i: i64) -> T           // O(1), reorders
fn retain<A, F>(xs: A&)                          // keeps x with F(x), stable; needs resize
fn dedup<A>(xs: A&)                              // drops adjacent duplicates (sort first for all)
fn heap_push<A, T>(xs: A&, v: T)                 // min-heap by <; F(a, b) forms of all three exist
fn heap_pop<A>(xs: A&) -> T
fn make_heap<A>(xs: A&)                          // O(n) heapify
```

每个函数仅针对具备其所需操作的数组种类进行实例化（只要调用方中没有指向只增数组的引用，只增数组也可以通过引用执行弹栈，规范 §5.1）。`[>..<]` 是预期的堆和队列容器；结构体内部固定上限的 `[..k]` 同样适用。

<a id="48-strings"></a>
### 4.8 字符串

```klang
fn format_int(out: u8[>..]&, v: i64, base: i64, width: i64, pad: u8)   // base 2..36, right-aligned
fn format_uint(out: u8[>..]&, v: u64, base: i64, width: i64, pad: u8)
fn format_flt(out: u8[>..]&, v: f64, decimals: i64)   // fixed decimals
fn parse_int(s: u8[:]) -> i64, bool              // optional sign, base 10, whole string
fn parse_int(s: u8[:], base: i64) -> i64, bool
fn parse_flt(s: u8[:]) -> f64, bool              // sign, fraction, exponent, or inf/-inf/nan; correctly rounded
fn compare(a: u8[:], b: u8[:]) -> i64            // bytewise lexicographic: -1, 0, 1
fn trim(s: u8[:]) -> u8[:]                       // ASCII whitespace; a sub-slice, no copy
fn trim_start(s: u8[:]) -> u8[:]
fn trim_end(s: u8[:]) -> u8[:]
fn split(s: u8[:], sep: u8) -> u8[:][>..]        // slices into s; empty parts kept
fn split(s: u8[:], sep: u8[:]) -> u8[:][>..]     // a separator of several bytes
fn each_split<F>(s: u8[:], sep: u8)              // F(part: u8[:]); no array built
fn each_split<F>(s: u8[:], sep: u8[:])
fn words(s: u8[:]) -> u8[:][>..]                 // the runs between whitespace; none empty
fn each_word<F>(s: u8[:])                        // F(word: u8[:])
fn join<T>(out: u8[>..]&, parts: T[:], sep: u8[:])   // T any u8 array/slice type
fn format_replaced(out: u8[>..]&, s: u8[:], old: u8[:], with: u8[:])   // `from` is a keyword
fn each_utf8<F>(s: u8[:])                        // F(cp: i64); malformed bytes yield 0xFFFD
fn push_utf8(out: u8[>..]&, cp: i64)             // the UTF-8 bytes of a code point
fn ascii::is_digit(c: u8) -> bool                // ASCII only, as <ctype.h> in the C locale
fn ascii::is_alpha(c: u8) -> bool
fn ascii::is_alnum(c: u8) -> bool
fn ascii::hex_value(c: u8) -> i64                // 0-15, -1 if not a hex digit
```

标量与字符串的常规输出由内建函数 `format` 承担（§8.1）；此处的函数在此基础上增加了格式控制与解析功能。`split` 返回指向输入切片的子切片（无拷贝，元素生命周期扎根于调用方的文本），它是 K 用来替代 Lobster 中 `tokenize` 的方案；`each_split` 形式则完全不构造数组。`words` 是命令行解析真正需要的按空白分词器，而以 `' '` 调用 `split` 并非如此：`words` 会丢弃连续空格和制表符留下的空片段。

---

<a id="5-dictionary"></a>
## 5. `dictionary`

<a id="51-representation"></a>
### 5.1 表示形式

```klang
struct dictionary_slot<K, V> { key: K, val: V, tag: u32 }
struct dictionary<K, V> { count: i64 = 0, slots: dictionary_slot<K, V>[>..<] = [] }
```

* 该哈希表使用开放寻址法、线性探测、2 的幂次容量以及 `hash(key) & mask`。`K` 和 `V` 必须是固定大小的类型（槽位数组需要随机索引，§3.3）；字符串键既可以是切片到调用方所持文本的 `u8[:]`，也可以是内联的 `u8[..k]`。删除操作采用后移填补（backward-shift，即 `bench/klang/lru.k` 所用的技术），因此不存在墓碑（tombstone），也不需要定期清理。
* `dictionary` 属于可变长类（其尾部是 `[>..<]`），因此它可以出现在所有允许可变长类型的位置：局部变量、全局变量、按引用传递的参数以及结构体尾部。它泛化了规范 A.3。
* 每个槽位保存一个 `tag`：未使用时为 0，否则为键哈希值的低 31 位并置第 31 位为 1。探测时仅在 `tag` 匹配时才比较键（对于切片键，比较长度和字节；对于内联键，比较字节）。对于不超过 2^31 个槽位的表，该 `tag` 直接保存了主槽位索引，因此扩容和删除绝不会重新对键做哈希；更大的表才会对其移动的键重新计算哈希。与一个 1 字节的 `used` 标志相比，该 `tag` 每个槽位多占 3 字节（对于 `u32 → i32` 的表需占 12 字节，原本只需 9 字节）。实测中，使用 `u32` 键（其键比较与 tag 比较同样廉价）进行 N-gram 计数时，速度在持平到慢 3% 之间；而字符串键哈希表（词频统计、LRU 缓存、解释器变量表、模板引擎）仅凭字典本身的这一改动就获得了 18–57% 的性能提升。
* 该表在负载率达到 ⅔ 时原地翻倍扩容。由于槽位数组位于其所在栈的栈顶，因此 `resize` 直接用未使用的槽位将其延长。随后只需从某个未使用槽位之后紧接着的位置开始，按循环顺序单趟遍历旧槽位即可重新安放所有表项。每个表项最终都会落入遍历已经经过的一段连续槽位中（它的新主槽位要么是旧主槽位，要么是旧主槽位加上旧容量的整数倍），因此绝不会出现“尚未移动的表项挡在已移动表项的探测路径上”的情况。无需构建第二张表，也无需任何回拷操作。槽位数组设为增删型（grow-shrink），以便通过引用调整其大小（§5.2）。最大负载率设为 ½ 时，在小型 LRU 表上快了 16%，在 n-gram 计数上快了 9%，但在频繁扩容的词频统计上慢了 8%，在 15 万表项的大表上更是慢达 1.8 倍（容量翻倍后不再能装入缓存）。最大负载率设为 ¾ 时与 ⅔ 处于误差范围内，唯独在词频统计上慢了 8%。
* `insert`、`update` 和 `get_or_insert` 会先检查是否需要扩容，然后再单趟遍历探测序列，直至找到该键或应当落脚的未使用槽位。
* 新槽位使用 `default<K>()`/`default<V>()` 填充（§8.2），因此引用类型的值表现为可选引用（`Cell?` 或 `in pool`）。更新值时写入整个槽位，因为对引用类型的 `V` 执行 `=` 会写入其所指对象（规范 §3.8）。

<a id="52-api"></a>
### 5.2 API

```klang
fn get<K, V>(d: dictionary<K, V>&, key: K) -> V?              // reference to the value, or null
fn contains<K, V>(d: dictionary<K, V>&, key: K) -> bool
fn insert<K, V>(d: dictionary<K, V>&, key: K, val: V) -> bool  // overwrites; true if the key was new
fn get_or_insert<K, V>(d: dictionary<K, V>&, key: K, v0: V) -> V&
fn update<K, V, F>(d: dictionary<K, V>&, key: K, v0: V)        // F(val&): inserts v0 if absent, then calls F
fn remove<K, V>(d: dictionary<K, V>&, key: K) -> bool
fn clear<K, V>(d: dictionary<K, V>&)
fn reserve<K, V>(d: dictionary<K, V>&, n: i64)                 // capacity for n entries without rehash
fn each<K, V, F>(d: dictionary<K, V>&)                         // F(key, val&); order unspecified
```

词频统计这一典型用法（已端到端原型验证）：

```klang
var counts = dictionary<u8[:], i32> {};
each_split(text, ' ') { counts.update(it, 0) { it += 1; } };
counts.each() { w, n => if n > 100 { print(w, " ", n); } };
```

`update` 之所以存在，是因为最自然的写法 `get_or_insert(d, k, 0) += 1` 并非可赋值的位置，而带类型标注的绑定又需要把值类型写出来（§2.5）。代码块可以直接接收该引用，从而同时避开这两个问题。

由 `get` 或 `get_or_insert` 返回的引用，或 `each` 中的绑定变量，在下一次调用 `insert`、`update`、`remove` 或 `clear` 时不得仍在作用域内。由于槽位数组是增删型的，检查器会直接对此报错（规范 §5.2），而不会让重哈希或后移填补使该引用悄悄指向另一个虽然类型合法却已是别人的槽位。请将这类引用绑定放在各自的代码块中，或者使用 `update`。

<a id="53-sets-and-other-idioms"></a>
### 5.3 集合与其他惯用法

* 集合就是 `dictionary<K, bool>`：`s.insert(k, true)`、`s.contains(k)`。专门的 `set<K>` 需要泛型类型别名（§8.7）和零大小的值类型，而这两者目前都还不存在；为了每槽位省下那 1 字节并不值得再维护第二套实现。
* 持有所有权的字符串键：在 `dictionary<u8[:], V>` 旁维护一个 `u8[>..]` 文本池（同一作用域的两个局部变量，或全局池）；键是指向该池的切片。`root struct` 现在可以在单个声明中同时持有这两个组件（规范 §3.4）；普通的紧凑结构体（packed struct）仍然不行。基于此惯用法构建的字符串驻留器（interner）是后续模块的候选之一。
* 插入顺序：`dictionary` 不保留插入顺序。若顺序重要，请将值保存在用户自己的 `[>..]` 中，并在字典里存放索引，这正是 `lru.k` 的做法。

---

<a id="6-vec"></a>
## 6. `vec`

```klang
struct vec2<T> { x: T, y: T }
struct vec3<T> { x: T, y: T, z: T }
struct vec4<T> { x: T, y: T, z: T, w: T }
type float2 = vec2<f32>;   type float3 = vec3<f32>;   type float4 = vec4<f32>;
type double2 = vec2<f64>;  type double3 = vec3<f64>;  type double4 = vec4<f64>;
type int2 = vec2<i32>;     type int3 = vec3<i32>;     type int4 = vec4<i32>;

fn dot<T>(a: vec2<T>, b: vec2<T>) -> T            // and vec3, vec4
fn cross<T>(a: vec3<T>, b: vec3<T>) -> vec3<T>
fn length_sq<T>(v: vec3<T>) -> T                  // all three sizes
fn length(v: float3) -> f32                       // f32 and f64 instances, all sizes
fn normalize(v: float3) -> float3
fn distance(a: float3, b: float3) -> f32
fn lerp<T>(a: vec3<T>, b: vec3<T>, t: T) -> vec3<T>
fn xy<T>(v: vec3<T>) -> vec2<T>                   // also xy(vec4), xyz(vec4)
let float3_0 = float3 { 0.0, 0.0, 0.0 };          // _0 and _1 for all nine aliases
let float3_1 = float3 { 1.0, 1.0, 1.0 };
```

逐元素 `+ - * /` 由语言本身提供（§6.1）。原型已确认 `type float3 = vec3<f32>` 别名支持按位置构造、按成员相加，并能针对 `f32` 和 `i64` 实例化泛型 `dot`。`int*` 别名使用 `i32`，因为向量属于数据（§3.1），且 `i32` 分量可以隐式拓宽为 `i64` 索引。逐元素的 `min`/`max`/`abs` 被有意省略：`min<T>(a: vec3<T>, b: vec3<T>)` 重载会与标量 `min<T>(a: T, b: T)` 产生二义性（二者都处于泛型匹配层级），而且无论如何用户本来就会按分量写 `min(a.x, b.x)`。扩维构造函数（`vec3(v, z)`）也已舍弃：`vec3 { v.x, v.y, z }` 一样简短，且避免了函数与类型重名。

---

<a id="7-math-and-os-extern-backed"></a>
## 7. `math` 与 `os`（基于 extern）

`math` 即 libm，每个函数按浮点位宽各声明一次：

```klang
extern fn sqrt(x: f64) -> f64;        extern fn sqrt(x: f32) -> f32 = "sqrtf";
// sin cos tan asin acos atan atan2 exp log log2 log10 pow floor ceil round trunc, likewise
let PI = 3.141592653589793;  let TAU = 6.283185307179586;
fn radians(deg: f64) -> f64
fn degrees(rad: f64) -> f64
fn is_nan(x: f64) -> bool             // x != x
fn is_inf(x: f64) -> bool
```

与 C 语言一致，`floor`/`ceil`/`round`/`trunc` 返回浮点数；可使用 `as i64` 转换。浮点 `%` 已经是 `fmod`（§6.1）。

`os` 封装了 `src/runtime/runtime_os.h` 中有限的一组 C 函数，足以编写工具程序和基准测试：

```klang
fn read_file(path: u8[:], out: u8[>..]&) -> bool     // appends the whole file
fn write_file(path: u8[:], data: u8[:]) -> bool
fn append_file(path: u8[:], data: u8[:]) -> bool
fn file_exists(path: u8[:]) -> bool
fn delete_file(path: u8[:]) -> bool
fn read_line(out: u8[>..]&) -> bool                  // stdin; strips the newline; false at EOF
fn read_stdin(out: u8[>..]&)                         // everything until EOF
fn write_stdout(s: u8[:])
fn write_stderr(s: u8[:])
fn flush_stdout()
fn args() -> u8[:][>..]                              // slices of a global buffer, over extern arg_count()/arg(i, out)
fn env(name: u8[:], out: u8[>..]&) -> bool
fn time() -> f64                                     // seconds since the epoch
fn clock() -> f64                                    // monotonic, high resolution
fn time_ns() -> i64      fn clock_ns() -> i64
fn sleep(seconds: f64)   fn sleep_ms(ms: i64)
fn random_seed() -> u64                              // OS entropy, for rng
fn random_bytes(out: u8[:]) -> bool                  // OS CSPRNG, for keys and nonces
```

目录枚举、子进程、套接字和日期格式化未纳入 v1。`exit(code)` 是内建函数而非 `extern`，因为检查器必须知道它不会返回（§8.3）。

---

<a id="8-builtins-and-language-rules-the-library-needed"></a>
## 8. 库所需的内建函数与语言规则

这些内容目前均已写入规范，按库对其依赖的程度排序。

<a id="81-the-print--str--format-family"></a>
### 8.1 `print` / `str` / `format` 系列

这三个内建函数共享同一个渲染引擎。每个函数都接受任意数量、任意类型的参数，并按顺序无分隔符地渲染它们：

```klang
print(a, b, …)                 // stdout, newline at the end
str(a, b, …) -> u8[>..]        // a fresh string, constructed at the destination
format(out, a, b, …)           // append to any growable u8 array; out.format(a, b, …) via UFCS
```

`str` 满足将若干内容快速拼接为新字符串的需求，K 语言会就地构造该字符串并鼓励这种用法：`words.push(str("item", i))` 会直接写入元素槽位。各类型的渲染方式如下：

* 整数、浮点数（最短往返表示形式）、`bool`。
* `u8` 数组或切片：顶层直接输出原始字节（拼接是最常见场景），嵌套在聚合类型中时加引号并转义。
* 其他数组与切片：`[1, 2, 3]`。
* 结构体与 ADT 变体：类型名称加上位置字面量形式，`float3 { 1, 2, 3 }`、`Circle { 1.5 }`，不带字段标签。保留名称可使嵌套值和变体保持可读，且输出依然是合法的 K 代码。
* 引用：输出其所指对象；可选类型：输出 `null` 或其所指对象；相对引用亦然。
* 带有用户重载 `fn format(out: u8[>..]&, v: T)` 的类型，无论出现在何处都改为通过该重载渲染。内建函数是 `format` 重载集的结构化回退方案，而用户重载则通过与普通调用相同的解析规则来查找。

代码生成会使用类似相等性比较的遍历方式，为每种类型特化渲染逻辑。这沿用了 Lobster 用于调试打印值的方案（规范 §3.7）。

<a id="82-defaultt"></a>
### 8.2 `default<T>()`

这是定长类型 `T` 在写入任何内容之前的初始值，并遵循声明的默认值：数值为 0、布尔值为 `false`、空数组（长度为 0，容量为声明值）、空的可选类型（`null`）、空切片、带各自默认载荷的变体 0，以及逐字段初始化的结构体。声明了默认值的字段（§3.2）取其声明的默认值，其余字段取各自类型的默认值。因此，若结构体将 `count: i64 = 1` 声明为不变量，当库用 `default` 填充槽位时，该默认值会得以保留。`sum` 需要它（作为泛型 `T` 的累加器初始值），`dictionary` 也需要它（用于填充新槽位）；在任何 `resize(n, v)` 需要中性值 `v` 的地方它也很有用。它之所以是内建函数，是因为泛型函数无法写出未知类型的字面量（§4.1），并且只有编译器才知道字段的默认值。

<a id="83-abortmsg-u8-and-exitcode-i64"></a>
### 8.3 `abort(msg: u8[:])` 与 `exit(code: i64)`

两者都是发散调用：它们之后的代码不可达，因此以它们结尾的分支不产生值（§6.4）。`abort` 会打印 `klang runtime error: <msg>` 并以非零状态码退出，与 §9.3 的检查中止行为相同；`exit` 会刷新缓冲区并以给定状态码退出。库使用 `abort` 处理并非普通 `assert` 的契约违规（例如 `format_int` 或 `format_uint` 中出现未知进制）。

<a id="84-extern-fn-spec-710"></a>
### 8.4 `extern fn`（规范 §7.10）

```klang
extern fn sqrt(x: f64) -> f64;
extern fn sqrt(x: f32) -> f32 = "sqrtf";
extern fn read_file(path: u8[:], out: u8[>..]&) -> bool = "k_os_read_file";
extern fn gl_vertex3(v: float3&);
```

* `extern fn` 是没有函数体的声明；可选的字符串表示 C 符号名（默认为 K 函数名）。它像普通函数一样参与重载解析，并从生成的 C 代码中直接调用；除非运行时已定义该符号，否则会为其生成函数原型。
* 返回类型可以是标量、扁平定长结构体或无返回值。参数类型可以是：
  * 整数标量、浮点标量以及 `bool`；
  * 任意*扁平、定长*的结构体或定长数组，按值传递为其生成的紧凑布局 C 类型（`k_float3`）；
  * 指向此类 `T` 的 `T&`，按指针传递（`const k_float3 *`，这正是 OpenGL 风格 API 所需的，因为紧凑布局的 `float3` 就是三个连续的浮点数）；
  * 此类 `T` 的切片 `T[:]`，按指针和元素计数传递；
  * `u8[>..]&`，以可调整大小数组的头部加上其栈指针的形式传递，使 C 代码可以通过运行时辅助函数（`k_bld_append(bld, ptr, n)`）进行追加。正是这种参数形式使得整个 `os` 层得以实现为 extern，而非内建函数。
* 以下类型不能跨边界传递：`varint`、结构体内部的引用、按值传递的可变大小与可调整大小的值，以及元素为可变大小的切片。检查器会拒绝这些类型并给出原因。
* 可写性在调用点检查，方式与 `push` 相同（§9.5）。当外部函数对应的 C 实现可重入时（运行时中的实现即是如此），也可在线程程序中使用它们。
* 初始实现为后续扩展预留了空间。编译器始终根据 K 声明生成自身的函数原型。库中外部函数的 C 实现位于 `src/runtime/runtime_os.h`，与其他运行时文件一样属于运行时的一部分。用户外部函数通过 `--include <header>` 引入其 C 代码；该头文件会在生成的类型声明之后输出，因此可以针对 `k_<name>` typedef 实现垫片（shim）。对于声明与生成原型存在差异（如对齐方式、`APIENTRY`、指针类型）的既有 C API，只需在该头文件中编写一行垫片函数即可。

<a id="85-map-and-the-empty-array-declaration-spec-42"></a>
### 8.5 `map` 与空数组声明（规范 §4.2）

泛型本身无需修改：泛型函数体仅在实例化时检查，此时每个类型都是具体的，包括 `F(x)` 的返回类型（规范 §7.7）。`map` 真正需要的是它的结果局部变量 `var out = [];`，其中的 `[]` *尚未*确定元素类型。没有类型标注的空数组局部变量会获得一个待定数组类型，并在第一次以具体元素执行 `push`/`append`/赋值时补全。在此之前以需要元素类型的方式使用该局部变量（`out[0]`）会报错，而 `out.len` 则合法。若函数体结束时仍未补全该类型，则在函数体末尾报错。代码生成阶段只会看到已补全的类型。因此 `map` 为

```klang
fn map<T, F>(xs: T[:]) { var out = []; for x in xs { out.push(F(x)); } return out; }
```

返回类型像往常一样自动推导（§7.1），而同一规则也让 `var names = [];` 成为地道的 K 用户代码写法。

<a id="86-where-import-std-looks-spec-111"></a>
### 8.6 `import std;` 的查找路径（规范 §11.1）

查找机制由 §3 中的搜索路径、`--stdlib` 标志以及 `K_STDLIB` 变量组成。编译器会根据自身所在位置找到源码树中的 `stdlib/`，因此测试运行器无需传递任何标志。

<a id="87-checker-and-language-rules"></a>
### 8.7 检查器与语言规则

**按大小类别传参（规范 §4.1）。** 这一规则取代了“两端显式写 `&`”。它取决于连接到目标位置的值的大小类别，其中目标位置包括调用参数、`let`/`var` 初始化器、赋值语句、字段初始化器、`push`、`for` 绑定、匹配绑定器或队列放入操作：

* **定长**（fixed）值按值连接。引用类型的目标（`x: T&`、`T?`、带标注的 `let r: T&`）会隐式绑定左值的地址；`&x` 仍然合法且语义相同，因此转发引用变量的行为保持不变。
* **非定长**（non-fixed）值绝不会隐式复制。引用类型的目标会绑定它。未标注类型或泛型目标也会按引用绑定它，因为这是唯一避免复制的选择。值类型的目标只接受右值（调用结果、字面量、切片或其他转换结果、正被返回的局部变量）或显式的 `copy(x)`。`copy` 是一个内建函数，用于在目标位置直接构造副本（§4.3 语义），同时也是一个易于搜索的显式标记。
* 调用点处的 `&` 变为可选。如果引用类型的目标原本就会绑定源值，再写 `&` 会产生一条警告。这有助于鼓励一致的调用风格。

该规则在各个语言构造上的应用如下：

* 对于非定长的 `x`，`let y = x;` 是错误：应改写为 `let y = &x;`（引用类型，即推导出的类型）或 `let y = copy(x);`。对于定长的 `x`，它会执行复制。
* `push(v)` 属于构造上下文（§4.2）：非定长左值 `v` 需要 `copy(v)`；切片 `v[..]`、字面量或调用结果则不需要。`append(src)` 以切片形式接收源数据，且复制元素正是其定义好的操作，因此 `g.append(h)` 保持原样。
* 可调整大小数组之间的 `g = h`（§4.4 中的先清空再复制）变为 `g = copy(h)`；`g = f()` 保持原样。这也正是首先会需要 `move(x)`（规范 TODO 3）的地方：从处于生命周期末尾的局部变量进行赋值。
* `return x` 返回非定长局部变量时会将其移动（NRVO，无复制）；返回非定长*字段*或参数则会复制，需要显式使用 `copy`。§7.3 中唯一的隐式复制——即在不同路径上返回两个不同局部变量时的情况——仍保持隐式，因为程序员无法确定两者中哪一个会被复制。
* `for x in xs` 和 `Circle c =>` 会复制定长元素与载荷；非定长元素与载荷原本就需要 `&x` 绑定形式（§6.5、§8.1），而本规则使其成为自然推论，而非特例。
* 当未标注类型或 `<T>` 参数接收一个非定长左值时，会将 `T` 绑定为引用类型；当接收一个定长左值时，则将 `T` 绑定为值类型。通常类型变量会原样采用调用方的类型，这是唯一一个出于实用考虑的例外，也是让 `fn total(xs)` 的同一套函数体能够同时服务于定长数组（按值复制）、`[>..]` 和 `[>..<]`（按引用传递）的原因。如果函数体通过 `xs` 写入数据，在定长类型实例化时会报错（写入不可变副本），而不会静默丢弃写入。
* 针对同一固定类型 `T` 的重载 `f(x: T)` 和 `f(x: T&)`，在传入左值参数时会产生二义性。而 `f<T>(xs: T[:])` / `f<A>(xs: A&)` 这对重载则不会（`[>..<]` 只能匹配第二个，数组在泛型层级匹配第二个，切片只能匹配第一个），因此库中的两套容器约定可以共存。
* 如果显式写出的 `&x` 参数在目标位置本就会在不带 `&` 时绑定 `x`，则会触发冗余警告。转发引用*变量*（在 `r: T&` 时调用 `f(r)`）不涉及 `&`，因此没有警告。在目标类型需要推导的上下文中写 `&x`（如 `let r = &x;`）也不会警告，因为正是这里的 `&` 决定了推导为引用类型。

对库而言，这意味着无需绑定器参数，任何示例中都不再出现 `&`；UFCS 适用于每一个修改方法；泛型高阶函数甚至可以不写类型标注；并且库自身的复制操作会在有意为之的地方清晰可见（`stable_sort` 的临时缓冲区就是一次构造）。

除最初促使提出该规则的 UFCS 修复外，其优点还包括：

* 该规则与成本模型相吻合（定长值使用寄存器，其余情况则采用就地构造或引用）。
* 非定长值的意外复制变得*不可能*发生。这比显式 `&` 规则更严格；在旧规则下，向按值传递的非定长参数传入 `f(arr)` 能够通过编译并静默复制。
* 单一规则统一覆盖了调用、`let`、`=`、字段、`push` 和 `for`。
* `copy` 为 `move` 提供了一个自然的对应物。
* §4.1 中的“一切皆值”简化为“定长值是值；非定长值只存在于单一位置，仅在明确要求时才进行复制”。

缺点及其限制因素：

* 对定长值的修改在调用点不再可见：`normalize(v)` 可能会修改 `v`。这一点已被接受：在 UFCS 的讨论中早已达成共识，可变参数在调用点不做标记，而且修改定长值比修改数组罕见得多。可写性溯源（§9.5）将其限制在 `var` 局部变量范围内：`let` 参数会产生不可写引用，若被调用方试图通过它写入，则会在编译期报错并报告调用链。因此，读者的心智模型是“传入函数的 `var` 可能会改变”，这相当于 C++ 的情况，但被严格限制在程序员已显式声明为可变的值上。
* “定长”并不等同于“小”。受限数组 `u8[..4096]` 或定长数组 `f32[1024]` 都属于定长类别，会静默复制。这一点也被接受，目前不做缓解处理：在该语言中，可调整大小数组将远比大型定长数组常见；而且当特化版本的根分析证明调用期间没有任何操作会写入源数据时，编译器也可以将未被写入的按值参数按指针传递。
* `copy` 会出现在一些看起来像普通数据流的位置，例如 `items.push(copy(item))` 和 `let name = copy(rec.name)`，以便读者能识别出执行数据复制的操作。如果最终发现它过于频繁，可以考虑引入 `*` 形式。现有的测试和基准测试只需在少数几处做出修改（`let copy: u8[][] = src;`、`g = h`、`d.slots = ns`，以及推送可变大小局部变量的位置）；它们原本写出的每一个 `&` 依然有效，只是会收到冗余警告。
* `&` 仍保留在语言中。它仅在目标类型已经明确表示“引用”的地方被省略：需要类型推导的 `let r = &x;` 保留 `&`，因为正是这里的 `&` 选择了引用类型。引用类型的*字段*初始化器和 `.=` 同样遵循这一规则隐式绑定左值：结构体声明本身就是权威来源，正如参数列表一样。

**[>..<] 的切片（规范 §5.2）。** 创建指向 `[>..<]` 的切片或引用是合法的。当作用域内仍有变量可能指向该数组时，对其进行缩容操作（`pop`、`resize`、`clear`、整体赋值）会在缩容处触发编译错误。报错信息会指出该变量及其绑定的位置，以便程序员修正任意一端：在缩容前结束切片所在的代码块，或移动缩容操作的位置。这里以作用域替代活性分析。这与仅可增长局部变量在缩容时使用的检查相同（§5.1），且无需最后使用分析。在循环内部，该检查同样精确，因为在循环体更早位置创建的绑定在缩容处仍处于作用域内。此类切片可以向下传递和返回，但不能存入容器中（字段、元素、全局变量）；正是这一限制确保了每一个持有者都是作用域扫描可见的变量。有些缩容操作无法通过作用域扫描看到：即通过引用参数执行的缩容，以及对全局变量的缩容。这些操作会按函数特化版本进行汇总（经由被调用方传递地记录哪些全局变量、哪些参数所指对象会被缩容），调用点会以同样的方式对照该汇总进行检查，因此由被调用方引发的缩容错误会在调用处报错。内存安全性论据依然成立：存活的 `[>..<]` 独占其栈区域（§1.3(2)），因此该规则捕获的是逻辑错误而非内存错误。同一遍检查还会将对仅可增长数组的整体赋值视为其本质上的缩容操作（§5.1）。

**指向可调整大小尾部的引用（规范 C.2）。** 在引入帧对象之前，`&bag.items` 是被拒绝的。当时带可调整大小尾部的结构体只是一个单一的帧头部 `{ base, len }`，其中 `base` 指向数据栈上该*结构体*的起始位置（先是静态前缀字段，然后是尾部元素），而 `len` 是尾部的元素计数。`T[>..]&` 是指向某个头部的指针，且该头部的 `base` 必须直接指向元素区域，但当时并没有专属于尾部自身的独立头部。早先的一个方案是对结构体头部做偏移，但这会以“每个结构体都必须相对于其尾部来寻址”为代价。评审时的直觉——为尾部提供自己独立的头部——引导出了如下布局：带可调整大小尾部的结构体被表示为一个*帧对象*，它像普通定长局部变量一样直接持有其固定前缀字段，紧随其后的是尾部自身的 `k_rhdr`。数据栈上只存放尾部的元素。采用这一布局后：

* `&bag.items` 是指向该头部字段的指针。它是一个普通的 `k_rref`，与指向独立可调整大小数组的引用毫无二致，因此下游没有任何代码需要知道它来自结构体。
* `&bag` 是指向帧对象的指针。
* 通过上述任一引用访问 `bag.n` 都是普通的字段访问。
* 通过 `d: dictionary&` 访问 `d.slots` 得到的是一个普通的头部。
* 隐式切片转换 `count(bag.items)` 也随之自然成立。

复制此类值会复制帧对象及其元素（栈上字节形状的 `RzShape` 前缀遍历变为一次结构体复制加上 `EmitCopyElems`）。按值传参和返回会像数组传递其头部一样传递帧对象（C.3 传递的是“与该类型相适应的元数据”）。嵌套同样适用：尾部本身又是带可调整大小尾部结构体的结构体，只需嵌套帧对象即可。仅有两个棘手情形仍保留栈上字节形状，挂在帧对象中的一个基址指针后面：其一是带有*可变大小*前缀（尾部之前存在 `u8[]` 字段，这是 `RzShape` 本身就拒绝复制的），其二是可调整大小类别的 ADT，其每个变体的载荷形状各不相同。规范 C.2 也从“最外层值拥有帧头部”修改为“可调整大小类别的值，其固定部分存放在所属帧中，而每个可调整大小尾部在那里都有自己的头部”。在编译器中，`StructInst::frameobj` 标记这一形状，而代码生成的 `IsFrameObj` 会将局部变量、参数、返回值、全局变量、复制、字面量和引用路由至帧对象。检查器的“嵌套可调整大小类型”错误仅保留给可变大小前缀和 ADT 形状。

**未标注类型的参数绑定精确类型（规范 §3.8）。** 未标注类型的参数在接收引用参数时会将其绑定为引用，与显式 `<T>` 参数完全一致，因此向 `fn f(xs)` 传入 `f(&gs)` 会修改 `gs`。

**`return null` 对根集合呈中性（规范 §9.2）。** 写作 `for &x in xs { if F(x) { return x; } } return null;` 的 `fn find<T, F>(xs: T[:]) -> T?` 能够通过类型检查：返回 `null` 不涉及任何根，因此不会向其他 `return` 语句提供的根集合添加任何内容。

**泛型类型别名**（`type set<K> = dictionary<K, bool>;`）尚不支持解析。泛型实例的非泛型别名（`type float3 = vec3<f32>;`）可以正常工作，且这正是 `vec` 所需的全部能力，因此这仅属于便利性特性。

**线程程序中的常量。** 扁平定长类型的 `let` 全局变量属于静态数据（§11.1）；`thread_fn` 应当被允许读取它们（`PI`、`float3_0`），因为它们并非可变的共享内存。这一点目前仍处于待定状态：检查器禁止线程程序访问任何全局变量。

**引用的同一性**（规范 TODO 0b）并非库所需。`lru.k` 使用 `index_of`（规范 §3.3）重新链接其条目，而检查器会通过根来校验这种减法操作。

---

<a id="9-deliberately-not-included"></a>
## 9. 刻意未包含的功能

以下操作均可使用现有函数和简短循环来表达：

* `zip`/`map2`/`each2`：`for i in min(a.len, b.len) { … a[i] … b[i] … }`。由于没有元组，`zip` 也无物可返回。
* `reduce`（不带初始种子的折叠）：`fold(xs[1..], xs[0]) { … }`。
* `reversed` 的复制版本、`enumerate`：分别用 `each_rev` 和 `for x, i in xs`。
* `product`、`average`：两行折叠即可完成。
* `first`/`top`：就是 `xs[0]`；而 `last` 之所以存在，只是因为 `xs[xs.len - 1]` 是所有人都会写错差一的表达式。
* `flatten`、`partition`、`filter_indices`、`find_or_push`、`insert_ordered`：各自都只需 `filter`/`lower_bound`/`insert_at` 再加一行代码。
* 字典的 `keys`/`values`：`each` 加上一次 `push`。
* 多片段字符串 `concat`、`repeat`、`pad`、`capitalize`、`escape`：使用 `str(a, b, c)`、`push_n`、`format_int` 的宽度参数以及一个循环。
* `lines`：`each_split(s, '\n')` 加上 `trim`。（`words` 收录在 §4.8 中，是因为 `each_split(s, ' ')` 会保留连续空格留下的空片段，而命令行解析需要丢弃它们。）
* 多可调整大小字段的容器（双端队列、拥有所有权的字符串字典）：语言现已通过 `root struct` 支持此类状态（规范 §3.4）；但对应的库 API 仍有待设计。参见 §5.3。
* 二进制序列化：K 数据本身就是其自身的序列化形式（`bench/design.md`）；将其通过 `read_file` 读入 `u8[>..]` 以及规范 TODO 16 的整区域复制才是正途，而 `read_u32_le` 这类带类型的访问器可以等出现需要它们的格式时再行添加。
* 日期/时间格式化、目录操作、子进程、网络通信：当有程序需要时，`os` 会以每次添加一个 extern 的方式逐步扩展。
* 线程辅助函数：`thread_fn` 不接受函数值也不支持泛型（§11.2），因此无法在库中编写 `parallel_for`。`wait_all(ids: i64[:])` 循环只需一行。待实现规范 TODO 9 时再行重新审视。

---
