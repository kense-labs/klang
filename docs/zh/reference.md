<a id="the-k-language-reference"></a>
# K 语言参考

<details>
<summary>目录</summary>

- [1. 词法结构](#1-lexical-structure)
- [2. 声明、变量与作用域](#2-declarations-variables-and-scope)
- [3. 标量与算术运算](#3-scalars-and-arithmetic)
- [4. 值的存储位置](#4-where-values-live)
- [5. 数组家族](#5-the-array-family)
- [6. 字符串与文本](#6-strings-and-text)
- [7. 切片](#7-slices)
- [8. 结构体](#8-structs)
- [9. 枚举（代数数据类型）](#9-enums-algebraic-data-types)
- [10. 引用与可选引用](#10-references-and-optionals)
- [11. 相对引用](#11-relative-references)
- [12. 生命周期：根、存储与可写性](#12-lifetimes-roots-stores-and-writability)
- [13. 数组的增长、缩减与整体赋值](#13-growing-shrinking-and-assigning-whole-arrays)
- [14. 可重用对象池](#14-reusable-pools)
- [15. 索引类型与无检查函数](#15-index-types-and-checkfree-functions)
- [16. 表达式与控制流](#16-expressions-and-control-flow)
- [17. 函数](#17-functions)
- [18. 泛型与函数值](#18-generics-and-function-values)
- [19. 递归](#19-recursion)
- [20. 跨层返回与错误](#20-long-distance-return-and-errors)
- [21. 程序结构](#21-program-structure)
- [22. 线程](#22-threads)
- [23. 序列化](#23-serialization)
- [24. 与 C 语言互相调用](#24-calling-c-and-being-called-from-c)
- [25. 内置函数](#25-builtins)
- [附录 A. 关键字与运算符](#appendix-a-keywords-and-operators)
- [附录 B. 术语表](#appendix-b-glossary)

</details>

这是 K 语言的参考手册：按主题逐一介绍每项特性并附带示例。它介于关于这门语言的另外两份说明之间。[教程](tutorial.md)通过示例进行教学，并将主要篇幅放在 K 与其他语言的不同之处；[规范](k_spec.md)则列出了实现者所需的语言精确规则。本文档面向已读过教程、希望全面了解整门语言的程序员。它与规范同样完整，但更易于阅读，并且每条规则都配有示例程序。

每个章节在末尾或文中都带有形如“规范 §3.3”的指引，指向对该主题具有最终决定权的规范章节。K 特有的功能占用了最多篇幅；与 C 或 Rust 行为相同的特性（例如 `if` 和 `while`）则用一段话简要说明。

本文档中的每一个示例都能在当前编译器下编译通过，`scripts/check_doc_examples.py` 会检查它们是否仍然如此。完整程序会在后面附带其输出。较短的示例要么是 `main` 的函数体，要么是一组声明，书写方式均便于直接粘贴到程序中。运行其中任意示例只需执行：

```bash
k example.k
```

该命令会将程序直接编译进编译器自身进程中并在其中运行，无需 C 编译器，也没有独立的构建步骤。`k check` 仅执行类型检查而不运行，`k build` 则生成可执行文件（命令行其余选项见 [command_line.md](command_line.md)）。

---

<a id="1-lexical-structure"></a>
## 1. 词法结构

K 源文件是 UTF-8 文本。文件开头的字节顺序标记（BOM）会被忽略。行尾使用 LF 或 CRLF，两者均按 `\n` 读取；若出现回车符后未紧跟换行符的情况，则视为错误。

**注释**包括延续至行尾的 `//` 以及支持嵌套的 `/* … */`：`/* a /* b */ c */` 是一整段注释。

**标识符**匹配 `[A-Za-z_][A-Za-z0-9_]*`。关键字列于[附录 A](#appendix-a-keywords-and-operators)。少数词仅在特定位置作为关键字，在其他任何地方均为普通名称：`return` 返回值之后的 `from`、类型中无符号类型之后的 `upto`、`fn` 之前的 `inline` 和 `checkfree`、其他函数限定符之前的 `deprecated`、`const for` 头部中的 `fields`、`variants` 和 `types`，以及 `const if` 条件中的 `has_default` 和 `same`。`in` 在全局范围内都是关键字。

**整数字面量**为十进制（`42`）或十六进制（`0xFF`）。支持整个无符号 64 位范围：大于 `i64.max` 的值是 `u64` 常量。字符字面量 `'a'` 是保存该字节值的整数常量，它接受与字符串字面量相同的转义序列。下划线（`_`）可用作数字分隔符以提升可读性。分隔符必须位于同一组数字序列的两个数字之间（`1_000_000`、`0xFF_AA`）；`1__0`、`1_` 和 `0x_1` 均为错误。分隔符不是值的一部分，`k format` 会按原样保留它们。

**浮点字面量**包含 `.`、指数部分，或两者兼有：`1.5`、`2.5e-3`、`1e9`。只有当 `.` 后紧跟数字时，它才开始小数部分，因此 `1..2` 是一个区间，而 `1.` 不是合法数字。同样支持带二进制指数的 C99 十六进制浮点数：`0x1.8p3` 为 12.0。分隔符可以出现在整数部分、小数部分和指数部分中，包括十六进制浮点数的二进制指数（`3.141_592`、`1_000e3`、`0x1.A_Bp3`、`1e1_0`、`0x1.8p1_0`）。诸如 `1.0_` 和 `0x1.8_p3` 的形式均为错误。

**字符串字面量**为 `"…"`，支持转义符 `\n \t \r \0 \\ \" \'` 与 `\xNN`（两位十六进制数字）。字符串字面量是静态只读数据，类型为 `const u8[:]`（[第 6 节](#6-strings-and-text)）。

**原始字符串**为 `"""…"""`。其中没有转义字符，下一个 `"""` 即结束文本。在单行内，两个定界符之间的所有内容（包括反斜杠和引号）都属于文本内容。多行原始字符串以行尾的起始 `"""` 开始，以独占一行的闭合 `"""` 结束。每一行都会去除与闭合定界符相同的缩进，因此文本可以缩进以与周围代码对齐。紧跟起始定界符之后和紧接闭合定界符之前的换行符不属于文本内容；若闭合定界符前有一个空行，则会在末尾添加一个换行符。

```klang
fn main() {
    let usage = """
        usage: wc [-l] file
          -l   count lines only
        """;
    print(usage);
    print("""C:\temp\new "quoted" \n""");
}
```

```
usage: wc [-l] file
  -l   count lines only
C:\temp\new "quoted" \n
```

**其他字面量**：`true` 和 `false`；`null`，表示任意可选类型的空值（[第 10 节](#10-references-and-optionals)）；以及 `self`，它仅在结构体或变体字面量内部合法，表示该字面量正在构造的值（[第 11 节](#11-relative-references)）。

<a id="statements-and-semicolons"></a>
### 语句与分号

表达式语句以 `;` 结尾。*本身*就是块结尾结构的语句——即 `if`、`match`、`block`、循环、`guard … else { }`、嵌套 `fn`，或者带尾随函数块的调用——不需要分号：它在闭合的 `}` 处结束。运算符绝不会延续此类语句，因此若要将这类结构用作操作数，需将其放入括号中：

```klang
let a = 3;
let v = (if a > 2 { 1 } else { 2 }) + 3;
if a > 2 { print("big"); }                 // 无需 `;`
let w = if a > 2 { 10 } else { 20 };       // 作为初始化器时直接书写即可
print(v, " ", w);
```

块后多余的 `;` 是无害的。所有带括号、以逗号分隔的列表均支持尾随逗号：结构体与数组字面量、调用参数、形参与泛型列表、字段列表以及 `match` 分支。

<a id="evaluation-order"></a>
### 求值顺序

所有内容均按从左到右的顺序求值：操作数、参数以及字面量的字段。按值传递的操作数会在其后的操作数执行前被读取；引用或切片操作数则保留其地址或视图，而不是内容的快照。赋值的目标位置最先解析，复合赋值会在其右侧表达式运行前读取旧值。规范 §2。

---

<a id="2-declarations-variables-and-scope"></a>
## 2. 声明、变量与作用域

<a id="let-var-and-const"></a>
### `let`、`var` 与 `const`

```klang
let answer = 42;              // 不可重新赋值的绑定
var count: i32 = 0;           // 变量：可重新赋值
const limits = [10, 20, 30];  // 既不能重新赋值，也不能写入其内容
count += 1;
count++;                      // 是语句，不是表达式
```

类型位于名称之后，通常可以省略：未标注类型的声明会采用其初始化器的类型。`let` 只禁止对该名称整体重新赋值：`let` 数组或结构体的内容是否可写取决于其类型，因此 `let xs = [1, 2, 3]; xs[0] = 9;` 是合法的。只读*内容*来自类型 `const T`（[第 12 节](#12-lifetimes-roots-stores-and-writability)），而 `const x = e;` 声明的是一个类型为 `const T` 的 `let`。类型为 `const` 的 `var` 则正好相反：它可以被重新赋值，但绝不能向其内容写入。

```klang
let xs = [1, 2, 3];
xs[0] = 9;                    // 合法：let 禁止的是 `xs = …`，而非修改元素
const ys = [1, 2, 3];
// ys[0] = 9;                 // 错误：ys 为 const
var s: const u8[:] = "text";  // 可重新指向的只读视图
s = s[1..];
print(xs, " ", ys, " ", s);
```

不带初始化器的 `let` 必须且只能获得一次值，编译器会对此进行检查：任何路径都不能在赋值前读取它，也不能赋值两次。在声明它的循环体内部赋值，或在被多次调用的函数中赋值，均视为赋值两次。

```klang
fn main() {
    let n: i64;
    let ready = true;
    if ready { n = 1; } else { n = 2; }
    print(n);
}
```

```
1
```

每个局部变量在每条路径上读取之前都必须完成初始化。定长数组必须完全初始化；填充字面量 `[v; n]` 可以很方便地初始化大型数组。内层作用域可以重新声明同名变量，从而遮蔽外层名称。规范 §4.4。

<a id="globals"></a>
### 全局变量

全局变量与局部变量的声明方式相同，位于文件顶层，可以是任何类型：标量、结构体，也包括可增长数组。它们在 `main` 运行之前按顺序初始化——先初始化导入的文件，再按文件内的声明顺序初始化——其初始化器可以调用函数。仅指定类型而不带初始化器的 `var` 全局变量会以该类型的默认值起步：`var log: u8[>..];` 是一个空数组。`let` 或 `const` 全局变量必须给出其值。[第 21 节](#21-program-structure) 介绍了其余细节。

<a id="named-constants"></a>
### 命名常量

不带显式类型且具有常量初始化器的 `let` 或 `const` 全局变量称为**命名常量**。整数命名常量的使用方式与字面量完全一致：它可以适配任何能容纳其值的整数类型，可用作数组大小或填充计数，也可在 `match` 分支中命名一个值。

```klang
let SIZE = 16;
let HALF = SIZE / 2;
var grid: u8[SIZE] = [0; SIZE];
fn main() {
    let b: u8 = SIZE;            // 与字面量 16 一样自动适配
    let wide = SIZE * 1000000000000;
    grid[HALF] = 1;
    print(b, " ", wide, " ", grid.len, " ", match 8 { HALF => "half", _ => "other" });
}
```

```
16 16000000000000 16 half
```

*带有*显式类型的全局变量（`let SIZE: i64 = 16;`）、局部 `let` 以及任何 `var` 都是其对应类型的普通值，不会适配到其他类型。规范 §3.1、§11.1。

<a id="type-aliases"></a>
### 类型别名

`type Name = T;` 为类型命名。别名与原类型是同一个类型，而不是新类型：

```klang
type Id = u32;
type Names = u8[..16][>..];
fn main() {
    let a: Id = 7;
    let b: u32 = a;
    var ns: Names = [];
    ns.push("klang");
    print(a + b, " ", ns);
}
```

```
14 ["klang"]
```

具名类型来自 `struct` 或 `enum` 声明（[第 8 节](#8-structs)、[第 9 节](#9-enums-algebraic-data-types)）。

<a id="type-syntax-at-a-glance"></a>
### 类型语法一览

类型语法采用后缀形式。下表中的每一种形式都在各自章节中详细介绍，这里仅作概览。

| 写法 | 含义 | 章节 |
|---|---|---|
| `i8 … i64`, `u8 … u64`, `f32`, `f64`, `bool`, `varint` | 标量 | [3](#3-scalars-and-arithmetic) |
| `T[4]`, `T[]`, `T[varint]`, `T[..8]`, `T[..]`, `T[>..]`, `T[>..<]` | 数组家族 | [5](#5-the-array-family) |
| `T[:]` | 切片：指向一段连续 `T` 的视图 | [7](#7-slices) |
| `Point`, `Pair<i64>` | 结构体、泛型结构体实例 | [8](#8-structs) |
| `Shape`, `Shape..`, `Shape.Circle` | 固定模式枚举、可变模式枚举、单个变体 | [9](#9-enums-algebraic-data-types) |
| `T&`, `T?` | 引用、可选（可空）引用 | [10](#10-references-and-optionals) |
| `T&<u32>`, `T&<u32 in pool>`, `T&<u32>?` | 相对引用 | [11](#11-relative-references) |
| `const T`, `const T&`, `const T[:]` | 只读内容、只读引用或只读视图 | [12](#12-lifetimes-roots-stores-and-writability) |
| `u32 in a`, `u32 upto a` | 全局数组 `a` 的索引、它的上界类型 | [15](#15-index-types-and-checkfree-functions) |
| `fn(i64) -> bool` | 函数值的形状（作文档说明之用） | [18](#18-generics-and-function-values) |

`const` 修饰构建在基类型之上的第一层引用或切片：`const u8[:]` 是字节的只读视图。若要将 `const` 应用于外层引用或切片，请使用括号：`const (u8[:])&` 是指向切片变量的只读引用。索引类型自身不带后缀，因此索引类型的数组写作 `(u32 in a)[>..]`。

---

<a id="3-scalars-and-arithmetic"></a>
## 3. 标量与算术运算

<a id="integers"></a>
### 整数

整数类型包括 `i8 i16 i32 i64` 与 `u8 u16 u32 u64`，均采用二进制补码表示，可用于任何地方：字段、元素、局部变量、参数、返回值。`i64` 是计数、大小和索引的常规类型；`.len`、整数字面量和内置函数产生的都是它。较窄的类型适用于需要紧凑存储的数据以及要求特定位宽的计算核心。

**算术运算按操作数自身的位宽进行。** `u8 + u8` 是 8 位加法，`i32 * i32` 是 32 位乘法。不存在向 `int` 的隐式提升：由声明的类型决定运算位宽。

**字面量本身没有类型。** 它会适配任何范围能够容纳它的整数类型；若没有任何上下文约束，则为 `i64`。`let x: u8 = 255;` 是合法的，而 `let x: u8 = 256;` 是编译错误。

```klang
let byte: u8 = 255;
let answer = 42;              // i64
let wrapped: u8 = byte + 1;   // 8 位加法：0，因为无符号算术会回绕
let widened = byte + answer;  // u8 扩宽为 i64：297
print(wrapped, " ", widened);
```

**混合位宽。** 二元运算符的两个操作数必须统一到同一个公共类型，该类型也是结果的类型。规则按顺序如下：

1. 若两个类型相同，则该类型就是公共类型。
2. 字面量会适配到另一个操作数的类型（若放不下则报错）。
3. 否则，若恰有一个操作数能隐式扩宽为另一个的类型，则使用更宽的类型：`u8 + i64` 是 `i64` 加法。
4. 整数与浮点数相遇时，整数转换为该浮点类型：`i32 * f32` 是 `f32` 乘法。
5. 其他情况均会触发编译错误，要求显式强转：例如相同位宽的有符号与无符号类型（`u32 + i32`），或 `u64` 与任何有符号类型。没有任何类型会被静默提升为 64 位。

```klang
let a: u32 = 7;
let b: i32 = 3;
// let c = a + b;             // 错误：u32 和 i32 无法统一；请强转其中之一
let c = a as i64 + b;         // i64 加法
let d: u8 = 200;
let e = d + 100;              // 100 适配为 u8：8 位加法，结果为 44
print(c, " ", e);
```

移位运算是例外：无论移位计数的类型是什么，`x << k` 都保持 `x` 的类型，并且计数会按位宽进行掩码截断。比较运算按相同规则统一操作数类型，但结果为 `bool`。

**`%` 是欧几里得取余。** 在每种整数类型下，无论被除数符号如何，结果都落在 `[0, |b|)` 内，永不为负。因此对于长度为 `n` 的数组，`x % n` 永远是合法索引，编译器会省去其边界检查。而 C 风格的余数必须先加保护判断才能省略检查。

```klang
print(-7 % 3, " ", 7 % -3, " ", -7 / 3);
```

```
2 1 -2
```

整数 `/` 向零截断，因此对于负被除数，`/` 与 `%` 并不构成匹配的商/余数对。`std` 在每种整数位宽下提供了缺失的两个函数：`rem(a, b)` 与 `/` 配对，`div_euclid(a, b)` 与 `%` 配对。

```klang
import std;
fn main() {
    print(-7 / 3, " ", rem(-7, 3));
    print(div_euclid(-7, 3), " ", -7 % 3);
    print(div_euclid(-1, 10), " ", -1 % 10);  // 图块编号与块内偏移
}
```

```
-2 -1
-3 2
-1 9
```

`div_euclid` 在除数为正时向下取整、为负时向上取整；其商发生溢出的条件与 `/` 完全相同。`rem` 的结果为零或与被除数同号，且在所有有符号位宽下 `rem(MIN, -1)` 均为零。两个函数在除数为零时都会中止程序（abort）。浮点数 `%` 使用 C 的 `fmod`，因此 `-7.0 % 3.0` 为 `-1.0`；这些 `std` 函数仅接受整数。

**溢出。** 按照定义，无符号算术在所有构建模式下均按模 2^width 回绕，因为哈希、随机数生成器和位运算技巧正是依赖无符号数学的这一行为。有符号溢出在调试构建（`--debug`）下会中止程序，在发布构建下则会回绕。除以零或对零取余始终中止，`i64.min / -1` 同样如此。调试构建会在优化之后对实际执行的每一步操作进行检查，因此它是一种查找 bug 的工具，而不是对源码中写出的每一个中间值都做出保证。

```klang
var h: u64 = 14695981039346656037;
for c in "klang" { h = (h ^ c) * 1099511628211; }   // FNV-1a，有意依赖回绕
print(h);
```

如上所示，大于 `i64.max` 的 `u64` 字面量是被接受的；对其取负则是错误，唯一的例外是 `-9223372036854775808`，它恰好就是 `i64.min`。规范 §6.2。

<a id="floats"></a>
### 浮点数

`f32` 和 `f64` 是 IEEE 浮点数。`f32` 算术保持 32 位；`f32` 可隐式扩宽为 `f64`，反之则不行。所有整数都能隐式转换为任一浮点类型，因此在需要 `2.0` 的地方都可以直接写 `2`。

浮点字面量会适配其目标位置或另一操作数的浮点类型；若没有任何上下文提供类型，则为 `f64`。只由字面量和整数决定其浮点属性的表达式也是如此：`n * 0.5` 传给 `f32` 参数时会以 32 位计算，而在 `let h = n * 0.5;` 中则是 `f64`。未标注类型的 `let`（无论局部还是全局）若其初始化器是此类表达式，也会在每次使用时以同样方式自动适配。标准库中的 `PI` 正是通过这种机制同时支持两种位宽。

```klang
let PI = 3.141592653589793;
fn area32(r: f32) -> f32 { PI * r * r }     // PI 在此处适配为 f32
fn main() {
    let n = 3;
    let h = n * 0.5;                        // f64
    let r: f32 = 2;                         // 整数隐式转换为浮点数
    print(h, " ", area32(r), " ", PI);
}
```

```
1.5 12.566371 3.141592653589793
```

浮点数会以能还原为*其自身类型*同一值的最短形式打印，整数后带 `.0`：`1.0`、`0.1`、`-0.0`；以及 `inf`、`-inf` 和 `nan`。浮点数上的 `%` 是 C 的 `fmod`。规范 §3.1、§6.3。

<a id="bool"></a>
### `bool`

`bool` 在单个字节中保存 `true` 或 `false`。比较运算产生布尔值，`if`、`while` 和 `guard` 要求布尔值。不存在整数到 `bool` 的强制转换，因此对整数 `n` 写 `if n { … }` 会报错；请改写为 `if n != 0`。`&&` 和 `||` 具有短路求值特性。

<a id="varint"></a>
### `varint`

`varint` 是一种用于结构体字段和数组元素的变长整数*存储*类型，根据值的大小占 1 到 10 个字节。读取它会得到 `i64`；只能在构造其所属值时写入它。[第 8 节](#8-structs) 会与其他字段类型一并介绍它。

<a id="conversions"></a>
### 类型转换

在同一类数值内部，编译器能够证明保值的转换都是隐式的，任何可能丢失位或翻转符号的转换都需要显式强转。在不同类别之间，整数可隐式转为浮点数，而浮点数只能通过强转转为整数。

| 转换 | 方式 |
|---|---|
| 转为相同符号的更宽整数（`i8 → i32`、`u16 → u64`） | 隐式 |
| 无符号转为严格更宽的有符号类型（`u8 → i16`、`u32 → i64`） | 隐式 |
| `f32 → f64` | 隐式 |
| 任意整数（含 `u64`）转为 `f32` 或 `f64` | 隐式，舍入到最接近的浮点数 |
| 字面量或命名常量转为其值能容纳的任何类型 | 隐式 |
| 除 `u64` 外的任意整数存入 `varint` 存储 | 隐式 |
| 变窄、同位宽符号变化、有符号转无符号、`u64` 转有符号、`f64 → f32`、浮点转整数 | `x as T` 或 `x as! T` |

`x as T` 可在任意两种数值类型之间转换。在调试构建下它会进行范围检查，若值发生改变则中止；在发布构建下则会静默截断、回绕或向零舍入。`x as! T` 从不检查，即使在调试构建下也不检查：它用于有意丢弃高位或改变符号的场景。两种形式对转往浮点数的转换均不做检查。浮点数到整数的转换行为是完全确定的：先向零截断，再回绕进目标位宽；NaN 产生 0。

```klang
let big = 300;
let n: u8 = big as! u8;       // 44，有意截断
let f = 2.75;
let i = f as i64;             // 2
let back = i as f64 / 4;      // 0.5
let neg = -1;
let u = neg as! u32;          // 4294967295
print(n, " ", i, " ", back, " ", u);
```

不产生任何变化的强转会触发**警告**，编译器以此指出按照上述规则本就不需要的强转：把 `u8` 存进 `i64` 时写 `b as i64` 会提示 ``redundant `as i64`: a u8 converts to i64 implicitly here``。只要强转起到了决定作用就不会报警，无论是确定一个别处未给出的类型（`let k = 5 as u32;`）、确定运算位宽（操作数为 `i32` 时的 `(a as i64) * b`），还是决定浮点数的舍入方式。

**`u64` 与有符号值的比较。** `u64` 是唯一没有更宽有符号类型可容纳的无符号类型。只有当有符号操作数在*语法上*已知非负时，才允许 `u64` 与有符号操作数直接比较：非负字面量、`.len` 或 `.cap`，或者绑定到其中之一的 `let`（且未同时被可写引用绑定）。此时的比较是一次单独的无符号比较。其他任何情况都需要显式强转。

```klang
let h: u64 = 18446744073709551615;
var xs: i64[>..] = [1, 2, 3];
let n = xs.len;
print(h > 5, " ", h > xs.len, " ", h > n);   // 均允许：5、.len 及其 let 绑定
```

规范 §6.1、§6.3。

<a id="arithmetic-on-structs-and-fixed-arrays"></a>
### 结构体与定长数组上的算术运算

对于两个相同类型的结构体或定长数组，只要其所有标量叶字段全为整数或全为浮点数，算术运算符便会按成员逐一应用。当没有叶字段为无符号类型时，一元 `-` 也可按成员应用。当所有叶字段恰好具有相同类型时，`*` 和 `/` 还接受一个标量操作数（任意顺序）；该标量会转换为叶字段的类型。标准库中的向量类型正是这样工作的；K 语言本身没有运算符重载。

```klang
struct V3 { x: f32, y: f32, z: f32 }
fn main() {
    let a = V3 { 1, 2, 3 };
    let b = V3 { 0.5, 0.5, 0.5 };
    var c = a + b;
    c *= 2;
    print(c, " ", -a, " ", a / 2.0, " ", 2.0 / a);
    let sum = [1, 2, 3] + [10, 20, 30];
    print(sum);
}
```

```
V3 { 3.0, 5.0, 7.0 } V3 { -1.0, -2.0, -3.0 } V3 { 0.5, 1.0, 1.5 } V3 { 2.0, 1.0, 0.6666667 }
[11, 22, 33]
```

`+`、`-` 和 `%` 需要两个匹配的聚合体。两个操作数均在写入任何成员之前各自求值一次。规范 §6.1。

<a id="bitwise-operators"></a>
### 位运算符

`~ & | ^ << >>` 作用于整数并保持操作数的类型。`>>` 对无符号类型补零，对有符号类型复制符号位。位运算符优先级高于比较运算符，因此 `x & mask != 0` 等价于 `(x & mask) != 0`，而移位优先级高于 `&`。非负常量的按位取反 `~c` 仍像常量一样自动适配，因此 `let a: u8 = ~4;` 为 251，`flags & ~F_ROAD` 可清除 `u8` 的某一位。用变量计数移位的常量也同样自动适配：`1 << k` 采用其目标位置的类型。

```klang
var flags: u8 = 0;
let F_ROAD: u8 = 4;
flags |= F_ROAD;
flags |= 1 << 1;              // 常量 2 适配为 u8
flags &= ~F_ROAD;
let k = 40;
let m: u64 = 1 << k;          // u64 移位，因为 m 是 u64
print(flags, " ", m, " ", flags & 2 != 0);
```

没有二进制字面量；请使用 `0x` 十六进制或移位。完整优先级表见[附录 A](#appendix-a-keywords-and-operators)。

---

<a id="4-where-values-live"></a>
## 4. 值的存储位置

本节篇幅不长，但后面所有内容都建立在它的基础之上。

<a id="size-classes"></a>
### 大小类别

每个类型都属于以下四种**大小类别**之一：

* **固定大小（fixed）**：字节大小是编译期常量。包括标量、全由固定大小字段构成的结构体、定长数组 `T[k]`、静态容量数组 `T[..k]`、引用和切片。
* **变长（variable）**：大小在构造值时确定，之后永不改变。包括 `T[]`、`T[varint]`、`T[..]`、带有 `varint` 或变长字段的结构体，以及处于可变模式（variable mode）的枚举。
* **可调整大小（resizable）**：大小在构造后仍可能改变。包括 `T[>..]`（只增）和 `T[>..<]`（可增可减），以及最后一个字段为其中之一的结构体。
* **根（root）**：`root struct` 在同一生命周期下拥有各自独立存储的字段。它没有连续的值内存布局，只能由局部变量或全局变量声明所拥有。参见[根结构体](#root-structs)。

普通复合类型的类别取其各组成部分中最大的那个类别。即使只包含固定大小字段，根结构体也始终保留其 `root` 类别。当一个类型在任意深度上都不包含引用、切片或相对引用，且不是根结构体时，该类型是**扁平的**（flat）；只有扁平值才能在线程之间传递（[第 22 节](#22-threads)）。规范 §1.1。

<a id="data-stacks"></a>
### 数据栈

一个 K 程序拥有原生调用栈、一些静态数据（字符串字面量和编译期常量），以及 **N 个数据栈**，其中 N 是编译器为该程序计算出的数量。数据栈是一块预先保留的大地址空间，配有一个指针碰撞（bump pointer）。这就是全部的内存系统——没有 `malloc`、空闲链表、垃圾回收器、引用计数或析构函数。

固定大小的值存储在原生栈或寄存器中。每一个变长或可调整大小的值都存储在某个数据栈上，并由声明它的变量所拥有（局部变量、全局变量、按值传递的参数，或上述成员的字段或元素）。两条规则保证了这套机制的运作：

1. 值按生命周期顺序入栈，并在拥有它的作用域结束时一次性全部释放。释放只是一次写操作：将栈顶指针退回原处即可。
2. 每个数据栈上同时最多只有一个可调整大小的值存活，并且它在其整个生命周期内始终位于该数据栈的最顶部。因此扩容只是一次无容量检查的指针递增，且任何内容都永不移动。

同时存活的三个可增长数组会各自得到三个栈；一旦某作用域释放了它，该栈便立即得到复用。编译器会静态证明这一切，语言中没有任何关于栈的语法。规范 §1.2、§1.3、§10.3。

```klang
fn main() {
    for round in 3 {
        var scratch: u8[>..] = [];                   // 每轮都启用一个新缓冲区
        for i in 100000 { scratch.push((i % 256) as! u8); }
        print("round ", round, ": ", scratch.len, " bytes");
    }                                                // 在此释放：只需一次存储操作
}
```

```
round 0: 100000 bytes
round 1: 100000 bytes
round 2: 100000 bytes
```

<a id="nothing-moves"></a>
### 一切永不移动

因为可增长数组的元素永不移动，所以不管后续向其中 `push` 多少元素，指向它的引用依然有效：

```klang
struct Item { id: i32, weight: f32 }
fn main() {
    var items: Item[>..] = [];
    let first .= items.push(Item { 1, 0.5 });         // 指向元素 0 的引用
    for i: i32 in 2..1000001 { items.push(Item { i, 0.0 }); }
    first.weight = 99.5;                              // 依然有效
    print(items.len, " items, first is ", items[0]);
}
```

```
1000000 items, first is Item { 1, 99.5 }
```

`std::vector` 或 `Vec` 会重新分配内存，无法做出这一承诺。正因如此，在其他语言中使用整数索引访问竞技场（arena）的地方，K 代码可以直接持有带类型的引用。

<a id="copies-are-visible"></a>
### 拷贝是显式可见的

与 C 语言一样，固定大小的值在赋值时会静默拷贝，无论其尺寸多大。**非固定大小的值绝不会隐式拷贝**：这样的拷贝耗时 O(size)，应当在源码中清晰可见。当一个变长或可调整大小类别的变量、字段或元素到达引用、切片、无类型参数、未标注类型的 `let`/`var` 或 `for` 绑定等目标位置时，它会*按引用绑定*。若到达一个需要独立自身值的目标位置，则会报错。值类型的目标只接受新构造的值（字面量、调用返回值）或显式的 `copy(x)`。

```klang
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b = copy(a);          // 显式、可见的 O(n) 拷贝
    b.push(4);
    let c = a;                // 不拷贝：c 与 a 指代同一个数组
    c.push(5);
    var p = Item { 1, 0.5 };
    var q = p;                // 固定大小：静默拷贝
    q.id = 2;
    print(a, " ", b, " ", p.id, " ", q.id);
}
struct Item { id: i32, weight: f32 }
```

```
[1, 2, 3, 5] [1, 2, 3, 4] 1 2
```

同样的规则也适用于函数调用：`f(xs)` 会将可增长数组按引用传给切片参数、`T[>..]&` 参数以及无类型参数。规范 §4.1。

<a id="built-in-place"></a>
### 原地构建

任何非固定大小的构造值都会**直接在它的最终位置构建**，无论中间穿过多少层函数调用，也无论 `if` 或 `match` 走哪条分支。按值返回可增长数组的函数会直接把元素写入调用方的变量、正在被 `push` 的元素，或正在另一个数组内部构造的记录的字段中。永远不会先构造临时对象再拷贝到位。因此，通过返回值构建大型结果与填充传出参数一样高效，而 `words.push(str("word", i))` 会直接把文本格式化进新元素之中。规范 §4.3、§7.3。

---

<a id="5-the-array-family"></a>
## 5. 数组家族

K 中没有单一的数组类型，而是存在一个数组家族，其成员之间的唯一区别在于大小可以发生什么变化。所有数组都采用 `[metadata][elements…]` 的连续内联布局，绝不会是指向另一块独立内存的指针。

| 类型 | 类别 | 说明 | 存储的元数据 |
|---|---|---|---|
| `T[k]` | 固定大小 | `k` 个元素，`k` 为编译期常量 | 无 |
| `T[]` | 变长 | 构造时确定大小，之后冻结 | 一个 `u32` 长度 |
| `T[u8]`, `T[u16]`, `T[u32]`, `T[u64]` | 变长 | 同上，长度采用指定位宽 | 该类型的长度 |
| `T[varint]` | 变长 | 同上，采用变长编码的长度 | 短数组仅占 1 字节 |
| `T[..k]` | 固定大小 | 内联容量 `k`；在容量内增长与缩减 | 一个长度，采用能容纳 `k` 的最小类型 |
| `T[..]` | 变长 | 构造时选定容量；在容量内增长与缩减 | 一个 `u32` 容量和长度 |
| `T[>..]` | 可调整大小 | **只增**：主力容器 | 一个长度 |
| `T[>..<]` | 可调整大小 | **可增可减**：栈、队列、堆 | 一个长度 |

```klang
fn main() {
    var fixed: i32[4] = [1, 2, 3, 4];
    let zeros: f64[8] = [0.0; 8];           // 填充形式
    let frozen: i64[] = [7, 8, 9];          // 确定大小一次，永不改变
    let packed: u8[varint] = "hi";          // 字节前的 1 字节长度
    var small: u8[..8] = [];                // 容量 8，无论放在哪里都内联
    small.push('h'); small.push('i');
    var sized: i64[..] = [..16];            // 此刻选定容量 16，长度为 0
    sized.push(1);
    var grow: i64[>..] = [];                // 默认的可增长数组
    for i in 5 { grow.push(i * i); }
    var stack: i64[>..<] = [];              // 可弹出元素的数组
    stack.push(1); stack.push(2); stack.push(3);
    let top = stack.pop();
    print(fixed, " ", zeros.len, " ", frozen, " ", packed, " ", small, " ",
          small.len, "/", small.cap, " ", sized.cap, " ", grow, " ", stack, " ", top);
}
```

```
[1, 2, 3, 4] 8 [7, 8, 9] hi hi 2/8 16 [0, 1, 4, 9, 16] [1, 2] 3
```

<a id="which-one-to-use"></a>
### 该选用哪一种

* **`T[>..]`** 是默认选择：竞技场、对象池、字符串构建器、树存储、以及各阶段之间重填的临时缓冲区。“只增”指的是指向其元素的引用始终保持有效这一保证，而非它支持的操作集合；在编译器能够确认没有任何引用指向其中时，它仍然可以调用 `pop` 和 `clear`（[第 13 节](#13-growing-shrinking-and-assigning-whole-arrays)）。
* **`T[>..<]`** 用于栈、队列或堆。指向它的引用只能存在于变量中，绝不能存入存储结构，从而使编译器能够在每次缩减操作时检查它们。
* **`T[..k]`** 与 **`T[..]`** 用于数组必须内联嵌套在其他值内部的情况：结构体里的 16 字节名称、数组元素里的小列表。`T[..k]` 是固定大小的，因此包含它的结构体依然是固定大小的。
* **`T[]`** 与 **`T[varint]`** 用于已完成构建的数据：记录中的字符串、对象池中的节点，即任何构建一次后只读的数据。
* **`T[k]`** 用于固定形状：4x4 矩阵、256 项查找表。

只有 `T[>..]` 和 `T[>..<]` 是可调整大小的，也只有它们受限于“每栈至多一个”的规则。其余类型都可以放在其大小类别允许的任何位置。

<a id="compile-time-sizes"></a>
### 编译期大小

`T[k]` 与 `T[..k]` 中的 `k` 以及填充字面量 `[v; n]` 中的计数都必须是常量表达式：字面量、命名常量、对它们进行的算术运算，以及在声明时即固定长度的全局变量的 `.len`（`const names = ["a", "bc"]; var counts: i64[names.len];`）。类型参数始终是类型，绝不能作为大小：`fn f<N>(a: i64[N])` 是错误的，因为 K 中没有大小参数。遮蔽了全局名称的局部变量也不是常量。规范 §3.3。

<a id="element-restrictions"></a>
### 元素限制

* `T[>..<]`、`T[..k]` 与 `T[..]` 的元素必须是**固定大小的**：缩减操作或存储空间复用绝不能改变某个位置所持有的类型。
* `T[]` 与 `T[>..]` 的元素可以是固定大小或变长的，绝不能是可调整大小的。字符串数组 `u8[][>..]` 是一整块连续内存：先是一个长度，随后每个字符串各自的长度与字节紧挨着排列。
* 元素为变长类型的数组是**顺序数组**（sequential）：每个元素从哪里开始取决于它前面所有元素，因此这种数组只能迭代，不能按索引访问，也没有 `pop`。若要按任意顺序访问其元素，可在旁边维护一个引用索引（[第 11 节](#an-index-for-a-sequential-array)）。

```klang
fn main() {
    var words: u8[][>..] = [];
    for i in 3 { words.push(str("w", i)); }
    for w in words { print(w); }         // 合法
    // print(words[0]);                  // 错误：包含变长元素的数组不能按索引访问，只能迭代
}
```

```
w0
w1
w2
```

存储的长度必须能够容纳实际的元素个数：`T[u8]` 最多容纳 255 个元素；若在编译期就能得知超出其长度类型范围，则直接拒绝；若在运行时才发现，则中止程序。规范 §3.3。

<a id="placement-what-may-contain-what"></a>
### 放置规则：什么可以包含什么

* **固定大小（Fixed）** 值可以放在任何地方：局部变量、全局变量、任意字段、元素、枚举载荷、参数、返回值。
* **变长（Variable）** 值可以放在局部变量与全局变量、参数与返回值、枚举载荷、数组元素（会使数组成为顺序数组）以及结构体任意位置的字段中。位于变长字段之后的字段需要计算偏移量才能访问，这在迭代时开销很小，但随机访问成本较高。
* **可调整大小（Resizable）** 值可以放在局部变量与全局变量、按值传递的参数、返回值，以及普通结构体的**最后一个字段**（这也会使该结构体变成可调整大小的）。根结构体可以拥有多个此类字段，且可位于任意字段位置。它们绝不能作为数组元素。
* **根（Root）** 所有者只能出现在局部或全局声明中。根结构体不能由另一个结构体、数组或切片（即使是定长数组）或枚举载荷所拥有，也不能按值传递或按值返回。指向根结构体的引用遵循普通的引用规则。
* **零大小（Zero-size）** 值（`T[0]`、`T[..0]`、无字段变体）只能作为独立值存在，绝不能作为字段或元素。

任何类型都不能按值包含自身，无论是直接包含还是通过其他类型间接包含。它可以通过引用或切片指代自身，因为无论指向什么，引用和切片都是固定大小的：`enum List { Nil, Cons { v: i64, next: List? } }` 是合法的。规范 §3.4。

<a id="array-literals"></a>
### 数组字面量

* `[1, 2, 3]`，以及在目标已知元素类型时的 `[]`：带类型注解的变量、参数、字段、元素或返回值。
* `[v; n]`：`n` 个 `v` 的拷贝，其中 `n` 为编译期常量；`v` 只求值一次。
* `[..cap]`：带有给定运行时容量的空 `T[..]`。
* 没有目标类型的字面量是 `T[k]`（当元素不是固定大小时则为 `T[]`），由其元素决定 `T`：单独写的 `[1, 2, 3]` 是 `i64[3]`。由常量组成的字面量会自动适配其所符合的参数形状：`take([0, 0, 0, 7])` 会将一个 `u8[4]` 传给 `fn take(c: u8[4])`。
* 仅写 `var out = [];` 会使 `out` 成为只增数组 `T[>..]`，其 `T` 由第一次对其执行的 `push`、`append`、`format` 或整体赋值来确定，且该操作必须发生在任何其他使用之前。

```klang
fn take(c: u8[4]) -> i64 { c[3] }
fn main() {
    var out = [];                    // T[>..]；T 由第一次 push 决定
    out.push(2.5);                   // f64[>..]
    let tbl: i64[4] = [7; 4];
    let names: u8[..8][3] = ["a", "bb", "ccc"];
    print(out, " ", tbl, " ", names, " ", take([0, 0, 0, 7]), " ", [1, 2, 3].len);
}
```

```
[2.5] [7, 7, 7, 7] ["a", "bb", "ccc"] 7 3
```

结构体字面量与变体字面量分别在[第 8 节](#8-structs)和[第 9 节](#9-enums-algebraic-data-types)中介绍。规范 §4.2。

<a id="members"></a>
### 成员操作

以下是内置操作。根据 UFCS（[第 17 节](#17-functions)），`a.push(v)` 就是 `push(a, v)`，因此标准库中针对数组的函数也以相同方式调用。

| 成员 | 适用对象 | 含义 |
|---|---|---|
| `.len -> i64` | 所有数组种类及切片 | 元素个数 |
| `.cap -> i64` | `T[..k]`、`T[..]` | 容量 |
| `.push(v) -> T&` | 有限容量、只增、可增可减 | 追加一个元素（原地构造）；返回指向它的引用 |
| `.append(src)` | 有限容量、只增、可增可减 | 拷贝并追加数组、切片或数组字面量中的元素 |
| `.move_from(src)` | 固定大小元素的切片 | 按重叠安全的顺序拷贝等长切片 |
| `.pop() -> T` | 有限容量、可增可减；允许缩减位置的只增数组 | 移除并返回最后一个元素 |
| `.pop_n(n)` | 同上 | 丢弃最后 `n` 个元素 |
| `.resize(n, v)`, `.resize(n)` | 同上 | 用 `v` 的拷贝增长，或缩减 |
| `.clear()` | 同上 | 清空数组 |
| `.index_of(r) -> i64` | 固定大小元素的数组 | `r` 所指向元素的索引 |
| `.range_index(s, msg?) -> i64` | 同上 | 完全被包含的切片的起始索引；若来源无法静态证明则在运行时检查 |
| `.check_length(n, msg?) -> i64` | 只增、可增可减、固定大小元素 | 根据数据栈预留空间校验计数；不修改数组直接返回该计数 |

增长操作始终提供元素值：不存在暴露未初始化槽位的操作。超出有限容量数组的容量会中止程序；在空数组上缩减同样如此。`index_of` 要求传入的引用能被编译器证明确实指向该数组内部（[第 12 节](#12-lifetimes-roots-stores-and-writability)）。`range_index` 接受仅在运行时才知晓来源的切片：非空切片必须按元素对齐并完全位于数组当前长度之内；空切片（包括默认或外来空切片）返回零。`check_length` 会拒绝负数计数以及仅元素本身就会超出数据栈预留空间的计数（包括过大的无符号计数）；它既不分配存储空间，也不承诺后续追加一定能放入数组的剩余空间。这两项检查在所有构建模式下都会执行。将字符串字面量作为最后一个参数传入（如 `a.range_index(s, "msg")`）可指定检查失败中止时输出的自定义消息，以替代运行时默认消息；标准库中的对象池正是这样编写提示信息的。`dst.move_from(src)` 要求长度相等，若长度不等则在写入前直接中止。即使两个切片存在重叠，它也会拷贝源切片的原始值；源切片不会被清空。两个操作数按从左到右的顺序各求值一次。目标切片必须可写，且适用常规生命周期与相对引用拷贝限制。空切片（包括默认切片）也可以拷贝。标准库对象池类型与已废弃的 `reusable` 操作见[第 14 节](#14-reusable-pools)。规范 §3.3、§12。

```klang
fn main() {
    var xs: i64[>..<] = [];
    xs.push(1);
    xs.append([2, 3, 4]);
    let last .= xs.push(5);
    print(xs.index_of(last), " ", xs.pop(), " ", xs);
    xs.resize(6, 0);
    xs.pop_n(2);
    print(xs, " ", xs.len);
    xs.clear();
    print(xs.len);
}
```

```
4 5 [1, 2, 3, 4]
[1, 2, 3, 4] 4
0
```

<a id="equality"></a>
### 相等性

`==` 和 `!=` 按结构比较数组：先比较长度，再比较各元素。只要两种*不同*数组种类对应的切片类型相同，它们也可以像切片一样相互比较：例如将 `u8[>..]` 与字符串字面量比较，或将 `u8[..32]` 字段与 `u8[:]` 比较。规范 §4.5。

---

<a id="6-strings-and-text"></a>
## 6. 字符串与文本

语言中没有专门的字符串类型。凡是元素类型为 `u8` 的数组家族类型都可以作为字符串，具体选用哪一种取决于你的意图：

* `u8[>..]`：**构建器**。追加操作只需移动指针（pointer bump）。
* `u8[]` 与 `u8[varint]`：**已完成字符串**，内联存储在其所属容器中。
* `u8[..16]`：结构体内部的**小字符串**，无间接寻址开销。
* `u8[:]`：指向位于别处的字节的**视图**。字符串字面量正是其中一种——具体来说是 `const u8[:]`，因为字面量属于只读静态数据。

语言不强制特定编码；UTF-8 是标准库的约定，库中的 `each_utf8` 和 `push_utf8` 支持它。

```klang
fn main() {
    var s: u8[>..] = [];
    s.append("hello");
    s.push(',');
    format(s, " world! ", 6 * 7);            // format appends the text of anything
    let t: u8[] = "fixed";                   // a copy of the literal, 5 bytes and a length
    let v: const u8[:] = s[..5];             // a view of s's first five bytes
    print(s, " [", s.len, " bytes] ", t, " ", v);
}
```

```
hello, world! 42 [16 bytes] fixed hello
```

由于字符串字面量是只读的，它只能存放在声明为 `const` 的槽位中；将其复制到任意 `u8` 数组（`let t: u8[] = "fixed";`）会得到一个独立的可写值。`u8[..16]` 及其他受限数组在其容量范围内同样可以作为构建器：`format(p.name, "linus")`。

<a id="rendering-text"></a>
### 文本渲染

有三个内建函数可将值转换为文本，它们都接受任意数量、任意类型的参数，且不在参数之间插入任何内容：

* `print(a, b, …)` 将文本和一个换行符写入标准输出，且作为单次写入完成，因此不同线程输出的行永远不会交错。
* `format(out, a, b, …)` 将文本追加到 `out`（任意可增长的 `u8` 数组）。
* `str(a, b, …)` 根据文本构建一个新的 `u8[>..]`，并直接在目标位置构造：`words.push(str("item", i))` 会直接写入新元素中。

每个标量都有其文本形式：整数为十进制，浮点数为能够读回相同值的最短形式，布尔值为 `true` 和 `false`，`u8` 数组则输出其字节。所有其他值按结构化形式渲染：数组渲染为 `[1, 2, 3]`，结构体或变体渲染为带类型名的位置字面量（`Point { 1, 2 }`、`Circle { 1.5 }`），引用渲染为其所指对象，空可选值渲染为 `null`。字符字面量本质上是整数，因此 `str('x')` 的结果是 `"120"`；追加单个字节应使用 `push`。嵌套在聚合类型中的 `u8` 数组会加上引号并进行转义。

如果定义了 **`format` 重载** `fn format(out: u8[>..]&, v: T)`（按值或按引用接收 `T`），那么在这三个内建函数的参数中每当出现 `T` 时都会调用它来渲染 `T`，从而让类型在一处统一定义其文本形式。泛型类型通过泛型重载（如 `fn format<T>(out: u8[>..]&, b: Box<T>)`）进行渲染，除非存在针对该具体实例的非泛型重载；作用于裸类型变量的重载不会用于渲染。查找重载时，先在声明 `T` 的命名空间中查找，再在全局查找，因此如何打印由类型本身决定，而非由调用打印的代码决定。非定长大小的 `T` 永远不会被隐式复制，因此其重载按引用接收它。

```klang
struct Person { name: u8[..16], age: i32 }
fn format(out: u8[>..]&, p: Person) { format(out, p.name, " (", p.age, ")"); }

fn main() {
    var people: Person[>..] = [];
    people.push(Person { name: "ada", age: 36 });
    people.push(Person { name: "grace", age: 45 });
    print(people, " ", str(people[0]).len);
    print(['a', 'b'], " ", "text"[0], " ", str('x'));
}
```

```
[ada (36), grace (45)] 8
[97, 98] 116 120
```

规范 §3.7。标准库的 `std` 模块提供了常见操作，全部作用于 `u8[:]`：`find`、`trim`、`split`、`words`、`join`、`parse_int`、`parse_flt`、`format_int`、`compare`、`to_lower` 等（[stdlib.md](stdlib.md#strings)）。`split` 返回一个指向输入文本的切片数组：每个分段只占几个字节，不复制任何文本。

```klang
fn main() {
    let line = "  key = some value  ";
    let body = trim(line);
    let eq = find(body, "=");
    print("'", trim(body[..eq]), "' -> '", trim(body[eq + 1..]), "'");
    let parts = split("alice,30,paris", ',');
    print(parts.len, " parts: ", parts, " ", words("  go   north "));
}
```

```
'key' -> 'some value'
3 parts: ["alice", "30", "paris"] ["go", "north"]
```

---

<a id="7-slices"></a>
## 7. 切片

`T[:]` 是一个引用加上元素计数：它是某数组内部一段连续 `T` 元素的视图。它是通用的“处理一段范围”参数类型，因为每一种数组类型都能在不复制元素的情况下转换为切片。

* **切片操作**：`a[x..y]` 表示从 `x` 到 `y`（不含 `y`）的元素；任意一端边界都可以省略，`^k` 表示“ `len - k` ”：`a[^2..]` 表示最后两个元素。对切片再次切片总是允许的，且绝不会扩大它。
* **隐式转换**：当一个数组（或指向数组的引用）到达类型为“其元素类型的切片”的目标位置时，它会变成该整个数组本身的切片，效果等同于 `a[..]`。这适用于参数、带类型标注的变量、赋值、字段、push 进去的元素、`return` 语句，以及另一分支为切片的 `if` 分支。
* 定长大小元素的切片支持索引和迭代，并按切片自身长度做边界检查。变长大小元素的切片仅支持迭代。
* 在 `push` 之前从只增数组获取的切片依然有效，但不会包含新加入的元素。

```klang
fn total(xs: i64[:]) -> i64 { var t = 0; for x in xs { t += x; } return t; }

fn main() {
    let fixed: i64[4] = [1, 2, 3, 4];
    var grow: i64[>..] = [10, 20, 30, 40, 50];
    let view: const i64[:] = fixed;       // fixed itself, not a copy
    print(total(fixed), " ", total(grow), " ", total(grow[1..4]), " ",
          total(grow[^2..]), " ", view[0], " ", grow[..2].len);
    let s = grow[1..3];
    grow.push(60);                        // s still views elements 1 and 2
    print(s, " ", s == [20, 30]);
}
```

```
10 150 90 90 1 2
[20, 30] true
```

切片是**受检查的**。它携带一个静态根，即限定其所观察对象生命周期的变量。切片既不能比该变量活得更久，也不能跨越会释放这些元素的收缩操作。检查器跟踪的是活跃性而非作用域：如果一个切片的最后一次使用发生在 `clear` 之前，则它已经失效，此时允许执行 `clear`。详见[第 12 节](#12-lifetimes-roots-stores-and-writability)，收缩部分的规则见[第 13 节](#13-growing-shrinking-and-assigning-whole-arrays)。

```klang
fn main() {
    var line: u8[>..] = [];
    line.append("hello world");
    let w = line[..5];
    print(w);
    line.clear();                         // fine: w's last use is above
    line.append("again");
    let v = line[..];
    // line.clear();                      // error: cannot clear while v is still used
    print(v);
}
```

```
hello
again
```

切片属于引用，因此遵循相同的可写性规则。来自 `var` 数组的切片可通过切片写入，而字符串字面量以及来自 `const` 值的切片则不可写。声明为 `T[:]` 的参数两者皆可接受，并根据传入的实参进行检查。仅通过切片参数读取数据的函数应将参数声明为 `const T[:]` 以表明这一点。规范 §3.10、§9.5。

由于切片从不复制数据，它是查找表天然的键类型。标准库中以 `const u8[:]` 为键的字典存储的就是指向程序已有文本的视图：

```klang
import dictionary;

fn main() {
    let text = "the quick brown fox jumps over the lazy dog the fox";
    var counts = dictionary<const u8[:], i32> {};
    each_split(text, ' ') { counts.update(it, 0) { it += 1; }; };
    let n = counts.get("the");
    if n { print(counts.count, " distinct words; 'the' x", n); }
}
```

```
8 distinct words; 'the' x3
```

---

<a id="8-structs"></a>
## 8. 结构体

```klang
struct Unit { kind: i32, hp: i32 = 100, x: f32, y: f32, name: u8[] }
```

普通结构体按声明顺序依次排列其字段，并采用**紧凑布局（packed）**：不插入对齐填充，假设非对齐访问代价很低，因此包含一个 `i8[3]` 和一个 `i32` 的结构体占 7 字节。结构体声明引入名义类型；具有相同字段的两个结构体属于不同类型。

<a id="literals"></a>
### 字面量

结构体字面量既可以按字段名指定，也可以按声明顺序按位置列出；两种形式不可混用。带有声明默认值的字段可以省略：在位置形式中可省略末尾字段，在具名形式中可省略任意字段。没有默认值的可选字段在省略时为 `null`。以 `..` 结尾的字面量可以省略*任意*字段，被省略的字段将取其类型的默认值：零、`false`、`null`、空数组或空字符串。对于没有默认值的类型（非可选引用），其字段仍必须显式提供。

```klang
struct Unit { kind: i32, hp: i32 = 100, x: f32, y: f32, name: u8[] }
var selected: Unit;                                             // a global with a type only:
                                                                // every field its default
fn main() {
    let a = Unit { kind: 1, x: 1.5, y: 2.5, name: "scout" };   // hp 100
    let b = Unit { 2, 50, 0, 0, "" };                           // positional, all given
    let c = Unit { kind: 3, .. };                               // hp 100, x 0, y 0, name ""
    print(a, " ", b.hp, " ", c, " ", selected.hp);
}
```

```
Unit { 1, 100, 1.5, 2.5, "scout" } 50 Unit { 3, 100, 0.0, 0.0, "" } 100
```

仅指定类型的全局 `var` 变量将以该类型的默认值作为初始值。当局部变量的类型不是定长大小时，必须在声明时初始化，因为值的存储布局是在构造时确定的。定长大小的局部变量可以先声明，只要在读取前于每条执行路径上完成赋值即可（[第 2 节](#2-declarations-variables-and-scope)）。

只有当编译器能够确认按声明顺序重新排列不会改变任何行为时，具名初始化器才可以不按声明顺序书写：读取操作以及不会触发异常（trap）的算术运算是允许的；带副作用的调用、索引和循环则不允许。构造总是按声明顺序直接在每个字段内进行。如果副作用必须以其他顺序发生，请在字面量之前先执行它们。

结构体字面量还支持在末尾使用 `..base` 从同类型的已有值复制其余省略字段。通过 `..base` 复制的每个省略字段必须是定长类型（非定长字段必须显式写出），且 `base` 不能持有自相对引用。此外，可以直接在 `let`/`var`/`const` 或 `.=` 声明中解构结构体字段（`let { field, other: alias } = value;`）：

```klang
struct Config { host: const u8[:], port: i64, tls: bool = false }
fn main() {
    let base = Config { host: "localhost", port: 8080 };
    let prod = Config { port: 443, tls: true, ..base };
    let { host, port, tls } = prod;
    let { host: h } .= base;
    print(host, ":", port, " tls=", tls, " ", h);
}
```

```
localhost:443 tls=true localhost
```

规范 §4.2。

<a id="field-defaults"></a>
### 字段默认值

默认值是一个表达式，会在每次省略该字段的构造过程中求值。它夹在显式初始化器之间按字段顺序求值。它可以引用全局变量和调用函数，但绝不能引用构造处的局部变量或同级兄弟字段。`default<T>()`（[内建函数](#25-builtins)）会根据声明的默认值构造一个定长大小的 `T`，从而确保声明中以默认值编码的不变式即使在填充值的泛型代码中也能得到保持。

```klang
var serial = 0;
fn next() -> i64 { serial += 1; serial }
struct Rec { id: i64 = next(), tag: u8[..4] = "none", score: f64 = 1.0 }

fn main() {
    let a = Rec { .. };
    let b = Rec { score: 2.0 };
    let c = default<Rec>();
    print(a, " ", b, " ", c.id);
}
```

```
Rec { 1, "none", 1.0 } Rec { 2, "none", 2.0 } 3
```

默认值不能环回导致再次使用同一默认值的构造，因为那样会无限循环。规范 §3.2。

<a id="field-qualifiers-and-padding"></a>
### 字段限定符与填充

* `let f: T` 标记一个在构造后不再被赋值的字段。
* `const f: T` 等价于 `let f: const T`：其内容同样是只读的。
* `pad n` 插入 `n` 个未使用的字节；单独的 `pad` 则将*下一个*字段按其标量宽度对齐，用于适配外部内存布局。填充字节永不被读取，`==` 也会忽略它们。

```klang
struct Hdr { magic: u8[4], pad, size: u32, let kind: u8, const name: u8[..8] }

fn main() {
    var h = Hdr { magic: "GOOS", size: 4, kind: 1, name: "x" };
    h.size = 8;
    // h.kind = 2;                    // error: kind is a let field
    // h.name.push('y');              // error: name is const
    print(h, " ", h.name.cap);
}
```

```
Hdr { "GOOS", 8, 1, "x" } 8
```

规范 §3.2、§4.4。

<a id="variable-size-fields-and-varint"></a>
### 变长字段与 `varint`

结构体可以包含变长大小的部分，它们按声明顺序内联存放。`varint` 是一种 LEB128 整数字段，根据其值需要占用相应字节数（不超过 63 占 1 字节，不超过 8191 占 2 字节，以此类推；绝对值较小的负数与较小的正数一样短）。包含 `varint` 或 `u8[]` 字段的结构体属于变长类（variable-class），位于这类字段之后的字段需要通过顺序遍历定位。`varint` 字段仅在构造时写入；读取时返回 `i64`。

```klang
struct Item  { sku: u8[varint], qty: varint, cents: varint }
struct Order { id: varint, customer: u8[varint], items: Item[varint] }

fn main() {
    var book: Order[>..] = [];
    book.push(Order { id: 1001, customer: "alice",
                      items: [Item { sku: "SKU-441", qty: 2, cents: 1999 },
                              Item { sku: "SKU-7", qty: 1, cents: 500 }] });
    for o in book {
        var t = 0;
        for it in o.items { t += it.qty * it.cents; }
        print(o.customer, " owes ", t, " cents over ", o.items.len, " items, ",
              bytes_of(book).len, " bytes in all");
    }
}
```

```
alice owes 4498 cents over 2 items, 29 bytes in all
```

这份订单仅占 29 字节且不含任何指针。代价在于 `book` 只能顺序访问：要么迭代遍历它，要么在旁边另行维护索引。变长类字段或元素一旦构建即被冻结：若要修改其中之一，需重建其容器。规范 §3.4、§3.6。

<a id="a-resizable-tail"></a>
### 可调整大小的尾部

普通结构体的**最后一个**字段可以是可调整大小的数组，这会使该结构体本身也成为可调整大小的：它可以出现在任何可增长数组能出现的地方（局部变量、全局变量、按值参数、返回值），但绝不能作为数组元素或中间字段。尾部会像普通可增长数组一样通过该结构体进行增长。

```klang
struct Stack { name: u8[..8], items: i64[>..<] }

fn push_twice(s: Stack&, v: i64) { s.items.push(v); s.items.push(v); }

fn main() {
    var s = Stack { name: "main", items: [] };
    push_twice(s, 7);
    s.items.pop();
    print(s.name, " ", s.items, " ", s.items.len);
}
```

```
main [7] 1
```

规范 §3.4、附录 C.2。

<a id="root-structs"></a>
### 根结构体

与普通结构体一样，`root struct` 将相关状态归拢在具名字段之后。它取消了这些字段必须构成单个连续值的限制：每个字段拥有独立的存储空间，因此多个字段可以各自独立增长。字段可以是定长、变长或可调整大小的，顺序任意，且全部共享所属所有者变量的生命周期。

其代价在于放置位置：拥有所有权的根结构体必须是局部变量或全局变量，而不能作为字段或数组元素。它适合用来组织应用状态、工作区或库实例：

```klang
root struct Workspace<T> {
    items: T[>..] = [],
    scratch: i64[>..<] = [],
    calls: i64 = 0
}

fn add<T>(s: Workspace<T>&, value: T) -> T& {
    s.calls += 1;
    let result .= s.items.push(value);
    s.scratch.push(s.calls);
    s.scratch.clear();                // does not invalidate result
    return result;
}

fn main() {
    var state = Workspace<i64> { .. };
    let first .= state.add(7);
    state.add(8);
    print(first, " ", state.items, " ", state.calls);
}
```

```
7 [7, 8] 2
```

在局部变量或全局变量声明中直接使用字面量即可构造一个所有者。字段默认值与 `..` 均可正常工作；带类型的全局变量可以省略初始化器。编译器会静态确定所有组件栈，并在退出作用域时恢复它们。现有的递归环路限制同样适用于带有非定长字段的所有者：将此类状态声明在环路之外，再把引用传入其中。

函数通过引用接收根结构体。从已有所有者建立的无类型标注绑定也会借用它；`.=` 可用于显式表达这一意图。所有者引用可以是可选的，可以存放在普通结构体或数组中，可以作为返回值，也可以被嵌套函数捕获，同时受正常的生命周期和可写性规则约束。字段访问、UFCS、反射与格式化均可通过这些引用工作。

全局 `var` 根结构体的可变只增数组字段可以出现在紧凑引用与索引类型中：`Node&<u32 in pool.items>`、`u32 in pool.items` 和 `u32 upto pool.items`。该名称解析到该特定字段的存储；另一个字段或另一个所有者则是不同的数组。仅支持直接字段：路径不能经过别名、引用、索引或嵌套字段。

所有者本身不能被复制、重新赋值、按值传递或返回、用作临时值，也不能放入另一个结构体或数组中。即使根结构体不包含任何可调整大小的字段也是如此。`root` 仅作为 `struct` 前的修饰符生效；变量、字段和函数仍可命名为 `root`。

根结构体没有紧凑字节映像、整值相等性或算术运算，也没有公开的 C 布局。`pad` 属于普通组件结构体，而不应放在根结构体上。需要序列化时请单独序列化符合条件的字段，例如 `to_bytes(state.items)`；所得映像仅包含所选组件。Worker 可以构造局部根结构体，但不能接收所有者或所有者引用，也不能直接或通过其他函数使用根全局变量。Worker 未使用的根全局变量则不受影响。嵌套的所有权根结构体以及所有者的定长数组暂未支持。规范 §3.4、附录 C.2。

<a id="generic-structs"></a>
### 泛型结构体

```klang
struct Pair<T> { a: T, b: T }
struct Node<T> { v: T, next: Node<T>? }

fn main() {
    let p = Pair<i64> { 1, 2 };
    let q = Pair { a: 0.5, b: 1.5 };           // T inferred from the fields
    var n = Node<u8> { v: 1, next: null };
    print(p, " ", q, " ", n.next == null);
}
```

```
Pair<i64> { 1, 2 } Pair<f64> { 0.5, 1.5 } true
```

泛型结构体与函数一样进行单态化，其实例化必须是有限的：在字段中以每一轮*更大*的类型实参再次引用自身结构体（如 `struct P<T> { v: T, next: P<T[]>? }`）会被拒绝。规范 §3.2。

<a id="equality-and-arithmetic"></a>
### 相等性与算术运算

`==` 和 `!=` 按成员逐一比较普通结构体，填充字节除外；引用或切片*字段*按身份（地址与长度）比较，因为递归深入比较可能导致死循环。算术运算符按成员逐一应用（[第 3 节](#arithmetic-on-structs-and-fixed-arrays)）。大小比较运算符仅适用于数值类型。规范 §4.5。

---

<a id="9-enums-algebraic-data-types"></a>
## 9. 枚举（代数数据类型）

```klang
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }
```

枚举由一个标签后接其某个变体的载荷构成。它是语言中唯一的动态多态形式；没有继承、没有 trait，也没有虚函数表（vtable）。变体载荷的声明方式与结构体字段相同，并适用同样的默认值与 `..` 规则；不带载荷的变体就是一个普通常量。所有变体都不带载荷的枚举就是类似 C 语言的枚举，占 1 字节。变体字面量写作 `Shape.Circle { r: 1.0 }`，不带载荷的变体则写作 `Shape.Dot`。泛型枚举（例如 `enum Opt<T> { None, Some { v: T } }`）需要给出类型实参：`Opt<i64>.Some { v: 1 }`、`Opt<i64>.None`。

<a id="two-modes"></a>
### 两种模式

每个枚举类型都可以用两种方式存储，具体由使用处所写的类型决定：

**定长模式**（`Shape`）采用类似 Rust `enum` 或 C++ `variant` 的布局：一个标签加上足以容纳最大载荷的空间，因此每个 `Shape` 都占 17 字节，包括 `Dot`。它是定长大小的，所以由它构成的数组支持索引，且值可以在原地被覆写为另一个变体。作为交换，任何指针都不能指向其载荷*内部*：`match` 只能按值绑定载荷。仅当所有载荷都是定长大小时才能使用此模式。

**变长模式**（`Shape..`，带有末尾的两个点）为每个值精确分配其变体自身的大小：`Dot` 占 1 字节，`Circle` 占 9 字节，`Rect` 占 17 字节。由它构成的数组只能顺序访问，且值永远不能改变变体，因为紧挨着它的后面就排布着相邻元素。作为交换，你可以取得指向载荷内部的引用并在原地编辑它。带有变长大小载荷（比如字符串）的变体永远只能在这种模式下使用。

```klang
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

fn main() {
    var shapes: Shape[>..] = [];                 // fixed mode: indexable
    shapes.push(Shape.Circle { r: 1.0 });
    shapes.push(Shape.Dot);
    shapes[1] = Shape.Rect { w: 1.0, h: 1.0 };   // a different variant, in place
    print(shapes);

    var packed: Shape..[>..] = [];               // variable mode: packed
    packed.push(Shape.Circle { r: 1.0 });
    packed.push(Shape.Rect { w: 2.0, h: 3.0 });
    packed.push(Shape.Dot);
    for s in packed {
        match s {
            Rect &r => { r.w += 1.0; },          // edits the payload inside the array
            _ => {},
        }
    }
    print(packed, " ", bytes_of(packed).len, " bytes");
}
```

```
[Circle { 1.0 }, Rect { 1.0, 1.0 }]
[Circle { 1.0 }, Rect { 3.0, 3.0 }, Shape.Dot] 27 bytes
```

允许“替换变体”或“内部引用”二者之一，但绝不同时允许，这正是保持类型系统健全的关键。规范 §3.5。

<a id="match"></a>
### `match`

```klang
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

fn name(s: Shape) -> u8[:] {
    match s {
        Circle c => if c.r > 1.0 { "big circle" } else { "circle" },
        Rect r => if r.w == r.h { "square" } else { "rectangle" },
        Dot => "dot",
    }
}

fn round(s: Shape) -> bool {
    match s { Circle, Dot => true, Rect => false }
}

fn main() {
    print(name(Shape.Circle { r: 2.0 }), " ", name(Shape.Rect { w: 1.0, h: 1.0 }), " ",
          round(Shape.Dot));
}
```

```
big circle square true
```

* 对枚举进行匹配必须是穷尽的：每个变体都要有对应分支，或者用 `_` 覆盖其余变体。同一变体不能命名两次。
* `Circle c =>` **按值**绑定载荷，得到一份不可写入的副本。`Circle &c =>` **按引用**绑定它。只有变长模式的被匹配值才合法，且绝不能通过该引用替换变体。
* 一个分支可以列出多个变体（如 `Circle, Dot =>`），此时不绑定任何变量，因为它们的载荷类型不同。
* `match` 是表达式；其各分支的类型必须一致，并适用常规的字面量适配规则（[第 16 节](#16-expressions-and-control-flow)）。包含多条语句的分支使用代码块：`Rect &r => { … },`。

**对整数匹配**时，分支可以是字面量、具名常量、半开区间，或由它们组成的逗号分隔列表，且必须提供 `_`。命中第一个匹配的分支；若某个分支已被更靠前的分支完全覆盖，则会报错。当被匹配值是枚举时，名称解析为变体；当被匹配值是整数时，名称解析为常量。

```klang
let GROUND = 0;
let SHALLOW = 1;
let DEEP = 4;
let LAVA = 7;

fn terrain(t: i64) -> u8[:] {
    match t {
        GROUND => "ground",
        SHALLOW..DEEP => "water",
        LAVA, 10..12, -3 => "hot",
        _ => "rock",
    }
}

fn main() {
    for t in [0, 2, 7, 11, -3, 20] { print(t, " ", terrain(t)); }
}
```

```
0 ground
2 water
7 hot
11 hot
-3 hot
20 rock
```

当表达式的期望枚举类型可由上下文确定时——例如带类型标注的 `let`/`var`/`const` 声明、赋值、`return`、结构体字段、数组字面量元素、函数调用参数、`==`/`!=` 比较或 `match` 分支——可以直接使用前导点缩写 `.Variant` 或 `.Variant { ... }`，无需重复书写 `EnumName`：

```klang
enum Status { Ok, Err { code: i64 } }
fn code_of(s: Status) -> i64 {
    match s {
        .Ok => 0,
        .Err e => e.code,
    }
}
fn main() {
    var st: Status = .Err { code: 404 };
    if st != .Ok { print(code_of(st), " ", code_of(.Ok)); }
}
```

```
404 0
```

规范 §8.1。

<a id="case-functions"></a>
### 分支函数

`match` 的各条分支也可以写成一组独立的函数——每个变体类型对应一个——然后直接传入枚举值来调用。调用会通过跳转表按标签进行分发，且穷尽性检查与 `match` 完全一致：每个变体都必须恰好有一个适用的重载，并且返回类型必须一致。这无需虚函数表即可实现虚调用的效果，而且一旦新增变体，在为它补齐对应分支函数之前都会产生编译错误。

```klang
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

fn area(s: Shape.Circle) -> f64 { 3.14159 * s.r * s.r }
fn area(s: Shape.Rect) -> f64   { s.w * s.h }
fn area(s: Shape.Dot) -> f64    { 0.0 }

fn main() {
    var shapes: Shape[>..] = [];
    shapes.push(Shape.Circle { r: 1.0 });
    shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
    shapes.push(Shape.Dot);
    var total = 0.0;
    for s in shapes { total += area(s); }
    let c = Shape.Circle { r: 2.0 };          // a variant type is an ordinary type
    print(total, " ", area(c));
}
```

```
9.14159 12.56636
```

变体类型（`Shape.Circle`）本身就是普通类型：它可以作为值的类型、参数类型、字段类型，或引用类型 `Shape.Circle&`。接收 `Shape.Rect&` 的重载按引用绑定载荷，与 `&` 绑定符一样需要变长模式的被匹配值。分发发生在单个参数位置上；其余实参会直接透传，且在根据标签选择分支前每个实参仅求值一次。只有在没有普通重载匹配该调用时，才会尝试按标签分发。规范 §8.2。

<a id="enums-with-references"></a>
### 带引用的枚举

枚举可以通过引用指向自身类型，这使得在内存池中构建链表与树形数据成为可能。结合变长模式和[第 11 节](#11-relative-references)中的窄链接，主要由小节点构成的树不再需要为每个节点都预留最大变体的空间：

```klang
enum Expr {
    Num { v: f64 },
    Add { l: Expr..&<u16>, r: Expr..&<u16> },
}

fn eval(e: Expr.Num&) -> f64 { e.v }
recursive fn eval(e: Expr.Add&) -> f64 { eval(e.l) + eval(e.r) }

fn main() {
    var pool: Expr..[>..] = [];
    let a = pool.push(Expr.Num { v: 1.5 });
    let b = pool.push(Expr.Num { v: 2.0 });
    let sum = pool.push(Expr.Add { l: a, r: b });
    print(eval(sum), " in ", bytes_of(pool).len, " bytes");
}
```

```
3.5 in 23 bytes
```

每个 `Num` 占 9 字节，`Add` 占 5 字节：1 个标签加上两个 2 字节偏移量。`recursive` 关键字将在[第 19 节](#19-recursion)中说明。

---

<a id="10-references-and-optionals"></a>
## 10. 引用与可选类型

`T&` 是指向 `T` 的引用：单个机器地址，不支持指针算术、不支持强制类型转换，也绝不可能悬空。规范 §3.8。

<a id="references-are-transparent"></a>
### 引用是透明的

在任何值上下文中，表示引用的表达式都表现得如同它的目标对象本身：读取、算术运算、比较（`r1 == r2` 比较的是目标对象）、传给按值参数、`print(r)` 皆是如此。语言中没有解引用运算符，也没有 `->`。通过引用写入（`r = v` 或 `r += v`）会直接写入目标对象。

由于引用本身在普通表达式中是隐形的，语言为“操作引用本身而非目标对象”提供了单独的语法：

* `let r .= e;` 和 `var r .= e;` 将 `r` 声明为指向 `e` 的引用，无论 `e` 是什么：变量、字段、元素，或是返回引用的调用。
* `r .= e;` 将已有的引用变量、字段或元素重新绑定为指向 `e`。
* `r1 .== r2` 与 `r1 .!= r2` 按地址比较两个引用。

```klang
fn main() {
    var xs: i64[>..] = [10, 20, 30];
    var r .= xs[0];              // r refers to element 0
    var c = xs[0];               // c is a copy of element 0
    r = 11;                      // writes xs[0]
    c = 99;                      // writes the copy
    r .= xs[2];                  // r now refers to element 2
    r += 1;
    print(xs, " ", c, " ", r, " ", r .== xs[2], " ", r == xs[2]);
}
```

```
[11, 20, 31] 99 31 true true
```

<a id="making-and-binding-references"></a>
### 创建与绑定引用

`&x` 生成指向左值的引用：变量、字段、元素，或另一个引用所指的对象。对临时值使用 `&` 会报错。对存放引用的位置使用 `&` 会得到其中存储的引用：不存在“指向引用的引用”。

当目标的声明类型为引用时，所指类型的左值**无需**写 `&` 即可绑定，如果写了 `&` 反而会产生警告：

```klang
fn inc(n: i64&) { n += 1; }

struct Cursor { line: i64& }

fn main() {
    var count = 0;
    inc(count);                  // binds count by reference
    inc(count);
    let r: i64& = count;         // an annotated reference variable
    r += 10;
    let cur = Cursor { line: count };
    cur.line += 100;
    print(count);
}
```

```
112
```

当变量类型通过推导得出时，指向**定长大小**值的引用会退化为目标对象的副本：`let x = r;` 会执行复制。例外情况是显式使用 `&lvalue` 初始化（这会推导出引用类型），以及使用 `.=` 形式。而**非定长**左值无论采用哪种形式都会按引用绑定，因为非定长值绝不会隐式复制（[第 4 节](#copies-are-visible)）：

```klang
fn main() {
    var n = 1;
    var xs: i64[>..] = [1];
    let a = &n;                  // a reference: explicit &
    let b .= n;                  // a reference: the .= form
    let c = xs;                  // a reference: xs is not fixed-size
    var r .= n;
    let d = r;                   // a copy of n: r decays
    a += 1; b += 1; c.push(2);
    print(n, " ", d, " ", xs);
}
```

```
3 1 [1, 2]
```

返回类型写作 `T&` 的函数返回一个引用。返回类型靠推导得出的函数，当返回非定长左值或显式 `&x` 时返回引用，当返回指向定长大小值的引用时则返回副本。若要绑定返回定长元素引用的调用结果，请使用 `.=` 或加上类型标注：`let e .= pool.push(v);`。

`let` 引用变量不能重新绑定，但这并不限制目标对象的可写性，目标仍然保持原有的可写权限：`let r .= xs[i]; r = 5;` 会写入该元素。

<a id="optionals"></a>
### 可选类型

引用绝不会为空。`T?` 是**可选的 `T`**：一种可空引用，以地址 0 表示空引用，没有任何额外空间开销。它可以与任意类型组合（`i64?` 表示按引用传递的可选整数），而应用于引用类型时只会使该引用变得可空：`T&?` 等价于 `T?`。字面量 `null` 是它的空值，没有声明默认值的可选结构体字段在省略时为 `null`。

可选类型**不是透明的**。必须先通过 `if`、`while`、`guard`、`assert` 或与 `null` 的比较将其*收窄*。在收窄后的作用域内，它*就是*一个 `T&`：透明、可通过它写入，并可用 `.=` 绑定。重新绑定该变量会结束收窄状态，除非是在以该测试作为条件的 `while` 循环中——该条件会在每次迭代前重新执行判断。

```klang
fn first_at_least(xs: i64[:], min: i64) -> i64? {
    for &x in xs { if x >= min { return x; } }
    return null;
}

struct Node { v: i64, next: Node? }

fn main() {
    let xs: i64[4] = [1, 5, 8, 2];
    let big = first_at_least(xs, 5);
    if big { print("first >= 5 is ", big); } else { print("none"); }
    let none = first_at_least(xs, 100);
    print(none == null);

    var c = Node { v: 3, next: null };
    var b = Node { v: 2, next: c };
    var a = Node { v: 1, next: b };
    var cur: Node? = a;
    var sum = 0;
    while cur {                      // narrowed for the body, re-tested each time
        sum += cur.v;
        cur .= cur.next;
    }
    print(sum);
}
```

```
first >= 5 is 5
true
6
```

可选类型可用 `.=` 重新绑定到一个引用（从而使其收窄），或重新绑定到 `null`（不收窄）。收窄后的可选值可以像普通引用一样通过它写入；不存在*不经*收窄就通过它写入的方式。`assert(o)` 会为该代码块剩余部分收窄 `o`。

在读取端，可选链 `x?.field` 与 `x?.method(args)` 仅在 `x` 非空时解包并继续访问成员：返回值为普通值或引用的访问会提升为可选引用 `R?`（原本已为 `T?` 时保持），`void` 方法则在 `x` 为 `null` 时跳过执行。空合并运算符 `a ?? b` 对 `a` 只求值一次，非空时返回解包后的值，否则短路求值并返回 `b`：

```klang
struct Item { val: i64, next: Item? = null }
fn twice(it: Item&) -> i64 { it.val * 2 }
fn main() {
    let tail = Item { val: 21 };
    let head = Item { val: 10, next: tail };
    let missing: Item? = null;
    let p: Item? = head;
    print(p?.next?.val ?? -1, " ", missing?.twice() ?? 0);
}
```

```
21 0
```

规范 §3.8。

<a id="what-references-may-point-to"></a>
### 引用可以指向什么

引用可以指向除定长模式枚举载荷之外的任何对象（[第 9 节](#two-modes)）。指向增缩数组 `T[>..<]` 的引用还有一项额外限制：它只能保存在变量中，并且在该数组下一次收缩时必须不再被使用（[第 13 节](#13-growing-shrinking-and-assigning-whole-arrays)）。指向只增数组的引用只要仍能被命名就始终有效。指向受限数组的引用在 `pop` 和 `push` 操作期间依然有效，并读取其槽位当前保存的值。

---

<a id="11-relative-references"></a>
## 11. 相对引用

`T&` 占八个字节，且地址在其生成进程之外毫无意义。**相对引用**则是以窄偏移量形式存储的同一链接：`T&<u8>`、`T&<u16>`、`T&<u32>`、`T&<u64>` 或 `T&<varint>`。读取它会得到普通的 `T&`，写入时会重新编码；除此之外，其用法与其他引用无异。它有两种形式，区别在于偏移量的测量基准：

* **自相对（Self-relative）**，默认形式，`T&<u32>`：从字段自身到目标的偏移量，目标必须位于同一个外层数组中。该结构与位置无关，因此可以整体复制、保存和加载（[第 23 节](#23-serialization)）。
* **池相对（Pool-relative）**，`T&<u32 in pool>`：从具名全局数组 `pool` 基址起算的偏移量。存储操作只需用已在寄存器中的基址做一次减法，并且*其他*数组也可以持有指向该池的链接。

`T&<u32>?` 是可选形式，以偏移量 0 表示空（null），因此包含空值标记在内的单个链接仅占四个字节。规范 §3.9。

<a id="a-tree-in-one-array"></a>
### 单个数组中的树

```klang
struct Node { key: i32, count: i32 = 1, left: Node&<u32>?, right: Node&<u32>? }

fn insert(pool: Node[>..]&, key: i32) {
    if pool.len == 0 { pool.push(Node { key: key }); return; }
    var cur .= pool[0];
    loop {
        if key == cur.key { cur.count += 1; return; }
        let next = if key < cur.key { cur.left } else { cur.right };
        if next { cur .= next; continue; }
        if key < cur.key { cur.left .= pool.push(Node { key: key }); }
        else { cur.right .= pool.push(Node { key: key }); }
        return;
    }
}

recursive fn in_order(n: Node&, out: i32[>..]&) {
    let l = n.left;
    if l { in_order(l, out); }
    out.push(n.key);
    let r = n.right;
    if r { in_order(r, out); }
}

fn main() {
    var pool: Node[>..] = [];
    for k in [50, 30, 70, 20, 40, 60, 80, 30] { insert(pool, k as i32); }
    var keys: i32[>..] = [];
    in_order(pool[0], keys);
    print(keys, " ", pool.len, " nodes, ", bytes_of(pool).len, " bytes, ", pool[1].count);
}
```

```
[20, 30, 40, 50, 60, 70, 80] 7 nodes, 112 bytes, 2
```

每个节点占 16 字节：两个 `i32` 和两个 4 字节链接。`cur.left .= pool.push(…)` 会推入节点并存储偏移量，而且这次 `push` 不会使 `cur` 失效，因为已有元素不会移动。存储自相对链接要求编译器能够确认引用与目标**精确地派生自同一个根数组**：这里两者都来自 `pool`。只有在数组跨度可能超出位宽所能表示的范围时，才会在存储时对偏移量进行范围检查。

<a id="copying-values-that-hold-self-relative-links"></a>
### 复制包含自相对链接的值

自相对偏移量仅在其被存储的位置才有意义，因此*包含*自相对链接的值不能以任何方式被复制：无论是从变量赋值、作为按值传递的参数或匹配绑定器、作为元素复制，还是通过 `copy(x)`。这类值只能就地构造（将字面量推入数组）、按引用绑定（如报错提示所示，用 `let n .= f();` 而不是 `let n = f();`）、使用 `&`-绑定器进行匹配，以及在 case 函数和 `format` 重载中按引用接收。其所有链接均为 `in pool` 的值则可像普通值一样复制，因为它的偏移量不依赖于自身所处的位置。唯一始终允许的复制是默认值的复制：字面量中的 `..` 会将相对字段设为 null。规范 §3.9。

函数或尾随块可以返回一个包含相对链接的新鲜值。它会直接在接收方处构造，即便跨函数调用或在关闭优化时也是如此：

```klang
struct Node { value: i64, next: Node&<u32>? }
fn linked(first: Node&, value: i64) -> Node {
    Node { value: value, next: first }
}
fn add<F>(nodes: Node[>..]&) { nodes.push(F()); }
fn main() {
    var nodes: Node[>..] = [Node { value: 1 }];
    nodes.push(linked(nodes[0], 2));
    let first .= nodes[0];
    add(nodes) { Node { value: 3, next: first } };
    let next = nodes[2].next;
    if next { print(next.value); }  // 1
}
```

每次调用都必须将结果放置在与所链接元素相同的根中。例如，`let n = linked(nodes[0], 2)` 是错误的：新的局部变量属于独立的存储。按值返回已存在的节点仍然是被禁止的复制。需要与接收方处于同一根中的结果也不能用作 `resize` 的填充值，或通过临时数组表示进行转换。

<a id="pool-relative-links-and-self"></a>
### 池相对链接与 `self`

`T&<u32 in pool>` 中的 `pool` 指定一个全局 `var` 数组（包括已弃用的 `reusable var`）或全局 `var` 根结构体的可变数组字段，例如 `T&<u32 in pool.items>`。该数组必须是只增的（grow-only）且元素类型为 `T`；所属的池是类型标识的一部分。存储的值是从池基址算起的偏移量加 1，因此在可选形式中 0 表示 null，而单个 `u32` 链接可覆盖 4 GB 的池空间。无论该链接从何处读取，加载操作都精确地以 `pool` 为根。这使得存放链接的容器在普通引用容器会产生歧义的场景下依然可用。

非可选的相对引用没有 null 值，因此无法直接构造链接指向自身的值：此时还没有任何可供指向的对象。作为此类字段完整初始化器编写的 **`self`**，表示字面量正在构造的值本身。循环链表正是借此创建哨兵节点。此后，没有任何链接会是 null，且每次插入和删除都只是同样的四次存储：

```klang
import slot_pool;

struct Node { val: i64, prev: Node&<u32 in pool.items>, next: Node&<u32 in pool.items> }

var pool = slot_pool<Node> {};
let head .= pool.alloc_with() { Node { val: 0, prev: self, next: self } };   // the sentinel

fn insert_after(p: Node&, v: i64) -> Node& {
    let n .= pool.alloc_ref(Node { val: v, prev: p, next: p.next });
    n.next.prev .= n;
    p.next .= n;
    return n;
}

fn remove(n: Node&) {
    n.prev.next .= n.next;
    n.next.prev .= n.prev;
    pool.free(pool.items.index_of(n));
}

fn is_end(n: Node&) -> bool { n .== head }

fn main() {
    insert_after(head, 1);
    let two .= insert_after(head.prev, 2);   // .=: `let two = …` would copy the node
    insert_after(head.prev, 3);
    remove(two);
    var line: u8[>..] = [];
    var n .= head.next;
    while !is_end(n) { format(line, n.val, " "); n .= n.next; }
    print(line, "| ", pool.items.len, " slots, ", 8 + 4 + 4, " bytes a node");
}
```

```
1 3 | 4 slots, 16 bytes a node
```

注意 `two` 上的 `.=`：`Node` 是定长类型，因此 `let two = insert_after(…)` 会把节点从池中复制出来，随后 `remove(two)` 就会被要求释放一个局部副本的槽位，而编译器会拒绝这种做法。

`self` 仅在结构体或变体字面量中合法，且只能用于非可选的、其指向类型正是当前所构造类型的相对引用字段；对于 `in pool` 字段，还要求该字面量是在该数组内部构造的（通过 `push`、元素存储或 `slot_pool.alloc_with` 构造器）。`alloc_with` 会在调用构造器之前先选定目标槽位，因此 `self` 指向该槽位。`alloc_ref` 则接收一个已构造好的值；在不需要依赖目标槽位进行构造时使用它。这两种操作将在[第 14 节](#14-reusable-pools)中说明。

<a id="which-form"></a>
### 如何选择形式

* **自相对**：适用于位置无关的数据块（例如需要整体保存、内存映射或移动的紧凑树），以及存放在局部或参数池中的结构（这类池没有可在类型中写出的名称）。
* **`in pool`**：适用于全局池中需要频繁重链接的结构，此时存储只需从寄存器做一次减法；适用于从*其他*数组指向该池的链接（自相对形式无法表达这一点）；以及需要把偏移量当作索引使用的场景，因为 `&pool[i]` 会编码为 `i * sizeof(T) + 1`，且 `index_of` 可以直接将其还原为索引。

参数是否指向某个池会在每个调用点确定，因此执行重链接的函数会按池进行特化，无需额外传入池参数。函数的返回值不能声明为相对引用：请声明为 `T&` 或 `T?`，接收它的相对槽位会自动对其进行编码。

<a id="an-index-for-a-sequential-array"></a>
### 顺序数组的索引

变长元素的数组无法直接通过下标索引（[第 5 节](#element-restrictions)），但可以额外使用第二个存放 4 字节链接的数组来索引它：

```klang
enum Mark { Circle { r: f64 }, Label { text: u8[varint] }, Dot }

var marks: Mark..[>..] = [];
var mark_at: (Mark..&<u32 in marks>)[>..] = [];

fn main() {
    mark_at.push(marks.push(Mark.Label { text: "hello, klang" }));
    mark_at.push(marks.push(Mark.Circle { r: 1.5 }));
    mark_at.push(marks.push(Mark.Dot));
    for i in [2, 0, 1] {
        match mark_at[i] {
            Circle c => print(i, ": circle ", c.r),
            Label l => print(i, ": label \"", l.text, "\""),
            Dot => print(i, ": dot"),
        }
    }
    print(bytes_of(marks).len, " bytes of marks, ", mark_at.len, " links of 4 bytes");
}
```

```
2: dot
0: label "hello, klang"
1: circle 1.5
24 bytes of marks, 3 links of 4 bytes
```

`push` 返回指向其所构造元素的引用。当该引用被存进 `mark_at` 时，它会变为该标记相对于 `marks` 基址的偏移量。`in` 偏移量所参照的数组必须是全局数组；局部数组可以用普通引用的数组 `(Mark..&)[>..]` 以同样方式建立索引，每项占八个字节。

---

<a id="12-lifetimes-roots-stores-and-writability"></a>
## 12. 生命周期：根、存储与可写性

这就是 K 语言“借用检查器”的全部核心规则：

> 引用或切片的存活时间绝不能超过拥有其目标的变量，且绝不能以错误的类型观察其目标。

语言中没有任何地方需要生命周期注解，也没有别名（aliasing）或独占性规则：两个引用指向同一个对象完全没问题，因为在没有共享内存并发的情况下，单纯的别名不会破坏这里的类型安全。规范 §9.1。

<a id="roots"></a>
### 根

每个引用和切片都拥有一个或多个静态**根（root）**。根是一个局部或全局变量，或者一个临时值，用于界定该引用目标的生命周期。当该变量自身的存储直接包含目标时，根是**精确的（exact）**；当它仅界定目标的生命周期时，则是不精确的。根由编译器推导，无需手动书写：`&x` 精确地以 `x` 为根，函数参数的切片以调用方传入的对象为根，从容器中读取的引用则以所有其存储可能容纳该目标的变量为根。函数会按不同的根进行特化，从而在不引入任何生命周期语法的情况下实现与生命周期参数同等的精度。规范 §9.2。

<a id="the-rules"></a>
### 规则

* **存储（Store）。** 只有当引用的根比目标位置的拥有者存活更久时，该引用才能存入该位置（变量、字段、元素、全局变量）。变量声明相当于将其值存入所声明的变量中。
* **返回（Return）。** 返回引用的根必须对调用方可见：即调用方传入的参数、全局变量，或函数自身就地构造的结果。函数自身局部变量的一部分会随函数返回而消亡。
* **分支（Branches）。** `if`、`match`、`block` 或循环的值是在其分支所开启的作用域结束之后才被接收的，因此它不能以分支内部声明的变量为根。
* **合并值（Merged values）。** 可能来自多个位置的值（`if` 的各分支、`var` 引用的多次绑定、函数的多个返回点）拥有所有来源的根，并且必须对其中的每一个根都满足每条规则。
* **重绑定（Rebinding）。** 引用变量在首次绑定时即确定其根：`.=` 可以在同一根内或处于同一作用域深度的根之间重新绑定它，这是遍历容器时的常见用法。其他情况则需要声明新变量。
* **临时值（Temporaries）。** 字面量、函数调用结果、`copy` 或控制结构的值会持续存活到所在语句结束。指向临时值的视图可以向下传递，但不能存入生命周期超过该语句的变量，也不能被返回：`let s = f()[..];` 是错误的，而 `let t = f(); let s = t[..];` 是合法的。
* **持有者（Holders）。** 持有者（包含引用字段的结构体、切片数组、包含引用的变体负载）以其内部所存入值的根为根。检查器会记录每一次此类存储，从而也能捕获会使持有者内部引用悬空的缩容或作用域退出。

生命周期错误会同时指明两端：引用的根以及它的目标位置。

```klang
fn longest(a: u8[:], b: u8[:]) -> u8[:] { if a.len >= b.len { a } else { b } }

fn main() {
    var outer: u8[>..] = [];
    outer.append("outer");
    var w = outer[..];
    {
        var inner: u8[>..] = [];
        inner.append("inner text");
        w = longest(outer, inner);   // error: storing a reference rooted at inner
    }
    print(w);
}
```

`longest` 的结果可能指向任一参数，因此它同时以两者为根，而 `inner` 活不过 `w`。把 `w` 的声明移到块内，或把文本复制出来，即可通过编译。

```klang
fn make() -> u8[>..] { str("made") }

fn main() {
    let t = make();
    let s = t[..];                   // fine: t lasts
    let u = make()[..];              // error: a view of a temporary
    print(s, " ", u);
}
```

<a id="writability"></a>
### 可写性

除非类型另有说明，否则一切都是可写的。`const T` 表示内容不可写的值：`const i64[3]` 不能对其元素赋值，而 `const T&` 和 `const T[:]` 分别是无法通过它们对目标进行写入、增长或缩容的引用和切片。与根一样，可写性也是按实例化*推导*的：

* **只读**：字符串字面量；`const` 值、其字段或按值绑定的 `for` / `match` 绑定上的 `&x` 或 `x[..]`；指向临时值的视图；`bytes_of` 视图；以及从声明为 `const` 的槽位中读取出的任何内容。
* **可写**：其他所有情况，包括 `var` 或普通 `let` 上的 `&x` 和 `x[..]`。
* **参数与返回值对常量性（constness）是泛型的。** 声明为 `u8[:]` 的参数既可接收可写实参，也可接收只读实参，函数体则按实际传入的可写性进行检查：若通过参数写入而调用方传入了字面量，会在该调用处报错，并附带完整调用链。声明为 `const u8[:]` 的参数无论传入什么实参都是只读的，这也是函数用于表明自身只读语义的方式。隐式添加 `const` 随处允许；去掉 `const` 则绝不允许。
* **槽位决定其持有内容的属性。** 字段、元素、全局变量或带类型注解的变量只有在类型带有 `const` 时才持有只读引用：`struct Named { name: const u8[:] }` 可以持有字面量，`struct Buf { bytes: u8[:] }` 则不能。未加注解的变量沿用其初始化器的常量性，因此当 `line` 为只读时，`var t = trim(line);` 是一个 `const u8[:]` 游标。
* `const` 是**浅层的**：从 `const Box&` 的字段中读出的引用，其可写性仍由该字段自身的类型决定。

```klang
fn upper_first(s: u8[:]) { if s.len > 0 && s[0] >= 'a' { s[0] -= 32; } }

struct Named { name: const u8[:] }

fn main() {
    var buf: u8[>..] = [];
    buf.append("klang");
    upper_first(buf);                  // a writable argument: fine
    let n = Named { name: "literal" }; // a const slot takes a literal
    let who = if buf.len > 3 { n.name } else { buf[..] };   // read-only, since one branch is
    print(buf, " ", who);
    // upper_first("literal");         // error: writes through a read-only parameter
}
```

```
Klang literal
```

规范 §9.5。

<a id="where-a-reference-read-out-of-a-container-may-point"></a>
### 从容器中读取的引用可能指向何处

容器标识了一个作用域，但并不一定指明其内容所指向的具体存储。因此，当从容器中读取引用时，检查器会重新推导它的根。作用域内每个存储能够容纳被指向类型的变量都是一个**候选者**。只有一个候选者时，该引用精确地以它为根；有多个候选者时，则不精确地以所有这些候选者为根。

某些操作需要数组的精确身份：存储相对链接、调用 `index_of`，或者确定通过该引用进行的缩容会释放哪个数组。如果检查器无法唯一确定，它会报告候选列表：`"`n` was read out of `slots` and may point into `pool` or `spare`"`。有两条规则可以收窄候选集：相对链接 `in pool` 精确地指向 `pool`；而所有存储操作对检查器都已知的局部持有者，则以存入其中的值为根。规范 §9.5。

<a id="reading-the-errors"></a>
### 理解错误消息

当检查器拒绝某段代码时，请完整阅读整条报错信息。它会同时指明引用*及其*绑定位置、持有者*及其*存入该引用的代码行、实例化点*及其*产生该实例化的调用链。根据这两端的位置信息来决定修改哪一端：在缩容前完成对视图的最后一次使用、移动缩容位置，或移动变量声明的位置。

---

<a id="13-growing-shrinking-and-assigning-whole-arrays"></a>
## 13. 数组的增长、缩容与整体赋值

增长很简单：`push` 和 `append` 会推进栈顶，有限容量数组则在其容量范围内增长。各类数组真正的区别在于缩容，因为缩容会释放掉某些东西可能仍在指向的元素。

<a id="grow-only-arrays-shrink-where-nothing-points-in"></a>
### 只增数组可在没有指向其中的引用时缩容

“只增（Grow-only）”指的是一种保证，而非操作集限制：**只要仍有指向 `T[>..]` 的引用或切片处于存活状态，它就绝不会缩容。** 只要拥有者还存活，栈顶之下的内存就绝不会移动或被复用，因此指向该数组的每个引用在能被访问到的期间都始终有效。当且仅当编译器能够确认没有任何以该数组为根的引用仍在使用中时，缩容操作（`pop`、`pop_n`、向下 `resize`、`clear` 或整体赋值数组）才是合法的：

* 缩容之后（同一代码块内、外层代码块中，或外层循环的后续迭代中），不再使用任何以该数组为根的引用或切片*变量*；
* 仍在使用的*持有者*（带有引用字段的结构体、切片数组）中没有任何一个曾经存入过指向该数组的引用；
* 缩容所在的那条语句本身没有使用同一语句中更早求值得到的指向该数组的引用（`use(a[0], a.pop())` 是错误的；`a[0] + a.pop()` 则不是，因为它会先读取左操作数）。

检查依据的是活跃性（liveness），而非词法作用域：若某个切片的最后一次使用在缩容之前，它就已经死亡，此时的缩容完全合法。因此，在每次迭代中重新填充的暂存缓冲区可以自由地向外分发自身的切片。当触发错误时，报错信息会指明持有者以及向其存入指向该数组引用的那一行代码。

```klang
fn reset(buf: u8[>..]&) { buf.clear(); }

fn main() {
    var line: u8[>..] = [];
    for i in 3 {
        reset(line);                        // nothing points into line here
        format(line, "line ", i + 1);
        let head = line[..4];
        print(head, "|", line[4..]);        // head's last use
    }
    var holder: u8[:][>..] = [];
    holder.push(line[..]);
    // line.clear();                        // error: holder may hold a reference into line,
                                            //        stored at the push above
    print(holder.len);
}
```

```
line| 1
line| 2
line| 3
1
```

通过引用参数或对全局变量进行缩容时，函数内部无法直接看到调用方的变量，因此会在每一层调用中进行检查。每个函数都会记录它会对哪些全局变量以及哪些参数的目标进行缩容，调用处则会如同在调用方本地执行了这些缩容一样接受检查。`pop`、`pop_n` 和 `resize` 还要求元素为定长类型，因为顺序数组无法定位其最后一个元素。规范 §5.1。

<a id="grow-shrink-arrays-keep-their-references-in-variables"></a>
### 可增可缩数组将其引用保留在变量中

`T[>..<]` 存放定长元素，并且可以在任何地方缩容：无论是局部变量、通过引用参数、全局变量，还是作为结构体的尾部字段。代价在于**指向它的引用和切片只能保存在变量中**。它们可以绑定到局部变量、向下传递以及作为返回值返回，但绝不能存入结构体字段、数组元素、全局变量或字面量中。一次 `pop` 之后，栈顶被释放的内存可能会被不同类型的数据复用，因此指向它的任何引用都不能活过下一次缩容。由于这类引用不会进入持久存储，检查器只需扫描作用域内的变量即可。只要任何此类变量在缩容之后仍被使用，该缩容就是错误的；报错信息会指出该变量及其绑定位置。

```klang
fn main() {
    var stack: i64[>..<] = [];
    for i in 5 { stack.push(i * 10); }
    let top .= stack[4];
    print(top);                             // last use of top
    stack.pop();                            // fine
    var popped = 0;
    while stack.len > 2 { popped += stack.pop(); }
    print(stack, " popped ", popped);
}
```

```
40
[0, 10] popped 50
```

如果一个函数在调用期间的数组长度从未低于调用开始时的长度——例如函数先记录 `let mark = xs.len`，执行推入操作，并在结束时调用 `xs.resize(mark)`——则称该调用对该数组是**平衡的（balanced）**。平衡调用不会释放任何在调用前获取的视图所指向的内容，因此在调用点不会算作缩容。递归正是借此共享同一个暂存栈（[第 19 节](#19-recursion)）。规范 §5.2。

<a id="limited-arrays"></a>
### 有限容量数组

`T[..k]` 和 `T[..]` 可以在其容量范围内自由增长和缩容，且允许持有指向它们的引用并能跨越 `pop` 操作存活：因为容量和元素类型都是固定的，所以槽位一旦写入就会永远保持为该元素类型的有效值。过期的引用读到的是该槽位当前持有的值，绝不会破坏内存。超出容量则会中止程序。规范 §5.3。

<a id="assigning-a-whole-array"></a>
### 整体赋值数组

左值（lvalue）在其大小无法改变或能够吸收大小变化时，可以在构造之后重新赋值：

* 定长的 `var` 左值：直接覆盖；
* 整个可调整大小的数组，或尾部为该类数组的值：赋值意味着*先清空，再构造*。右侧表达式会在旧元素原本所在的位置构造，因此右侧完全不能使用该数组本身，甚至连它的长度也不能读取（`x = f(x)` 是错误的；请先在另一个变量中构建新内容，再赋以它的 `copy()`）；
* 有限容量数组，只要新长度能装下；
* 固定模式（fixed-mode）枚举左值，可赋以任意变体；
* **不可**赋值：可变类（variable-class）左值（`T[]` 字段、可变模式枚举值、`varint` 字段），它们在构造时即被冻结。请改为重建其外层容器。

```klang
struct Rec { name: u8[], score: i64 }

fn main() {
    var xs: i64[>..] = [1, 2, 3];
    xs = [4, 5];                            // a shrink, then the new contents
    var ys: i64[>..] = [xs[0] + 1];
    xs = copy(ys);                          // computed from other storage: fine
    var recs: Rec[>..] = [];
    recs.push(Rec { name: "a", score: 1 });
    for r in recs { r.score = 2; }          // a fixed-size field: assignable
    // for r in recs { r.name = "bb"; }     // error: a variable-class field is frozen
    var one = Rec { name: "a", score: 1 };
    // one = Rec { name: "bb", score: 3 };  // error: and so is a variable-class local
    recs = [Rec { name: "bb", score: 3 }];  // rebuild the container instead
    print(xs, " ", recs, " ", one.score);
}
```

```
[5] [Rec { "bb", 3 }] 1
```

规范 §4.4。

<a id="construction-in-progress"></a>
### 构造进行中

当一个值正在数组内部就地构造时（正在推入的元素、正在追加的函数调用结果、整体赋值的新内容），任何代码都不得使该数组增长：无论是正在构造的表达式本身，还是它调用的函数都不行。若 `f` 会向其参数中推入元素，则 `xs.push(f(xs))` 会被拒绝。规范 §1.3，§4.2。

---

<a id="14-reusable-pools"></a>
## 14. 可复用池

有些数据的生命周期并非嵌套关系：缓存条目、被编辑的图节点、按任意顺序删除的文件。对于单个槽位可使用标准库的 [`slot_pool<T>`](stdlib.md#slot_pool)，对于连续区段则可使用 [`slice_pool<T>`](stdlib.md#slice_pool)。它们各自都是一个根结构体，在单次声明中打包了 `items` 数组及其复用元数据。拥有者必须是局部或全局变量；函数以引用形式接收它们。

**`reusable` 与 `reusable[]` 已被弃用。** 为兼容现有代码以及解决[剩余迁移缺口](design/root_structs.md#separate-follow-up-reusable-pools-in-the-stdlib)，这些内置形式暂时仍可使用，将在后续步骤中移除。

导入 `slot_pool` 即可使用以下操作：

| 操作 | 含义 |
|---|---|
| `a.alloc_index(v) -> i64` | 返回持有 `v` 的槽位索引：若有已释放槽位则复用之，否则在末尾推入新槽位 |
| `a.alloc_ref(v) -> T&` | 同上，但以引用形式返回 |
| `a.free(i)` | 将槽位 `i` 标记为可复用；若 `i` 越界则中止 |

`free` 不会释放内存，也不会结束任何生命周期。**每个元素在任何时刻都保持为其类型的有效存活值**，因此指向已释放并被复用槽位的引用读到的是*同一正确类型下的另一个值*：这与过期索引属于同类逻辑错误，绝不会导致内存破坏、类型混淆或越界访问。这类逻辑错误就是不安全性的全部边界，而这一设计使得修改树结构的负载无需通用分配器即可运行。

```klang
import slot_pool;
struct Item { id: i32, weight: f32 }

fn main() {
    var pool = slot_pool<Item> {};
    let s0 = pool.alloc_index(Item { 10, 1.0 });
    let s1 = pool.alloc_index(Item { 11, 1.0 });
    pool.free(s0);
    let s2 = pool.alloc_index(Item { 12, 2.0 });   // takes slot s0 back
    let node .= pool.alloc_ref(Item { 13, 3.0 });
    pool.free(pool.items.index_of(node));          // a reference turned back into its slot
    print("slots ", s0, " ", s1, " ", s2, ", pool length ", pool.items.len);
}
```

```
slots 0 1 0, pool length 3
```

池既可以是局部变量，也可以是全局变量。其 `items` 数组绝不会通过池 API 缩容；`items.len` 包含已释放的槽位。普通数组访问请直接使用 `items`，传递拥有者参数时请使用 `slot_pool<T>&`。元素必须是定长类型。`alloc_index` 和 `alloc_ref` 接收可正常复制的值。对于带有自相对链接或池相对 `self` 的新鲜值，请使用 `pool.alloc_with() { Node { value: 1, me: self } }`：构造器会直接在选定的槽位中运行一次，并返回指向它的引用。切勿直接修改其 `free_slots` 元数据或直接缩容 `items`。支持的类型和生命周期规则请参阅[库契约](stdlib.md#slot_pool)。

<a id="slice-pools"></a>
### 切片池

`slice_pool<T>` 分配的是元素的*连续区段*而非单个槽位，并维护一棵能够合并相邻空闲区段的空闲区间树：

| 操作 | 含义 |
|---|---|
| `a.alloc_slice(n) -> T[:]` | 从第一个能容纳 `n` 个元素的空闲区间中分配，否则从数组末尾分配并使数组增长；返回的每个元素都会被重置为默认值 |
| `a.free_slice(s)` | 记录区段 `s` 以供复用 |
| `a.realloc_slice(s, n) -> T[:]` | 将 `s` 调整为 `n` 个元素，若能就地调整则就地完成，否则释放旧区段并将内容复制到 `alloc_slice(n)` 分配的新位置 |

这非常适合用于 n 叉树的子节点列表：每个节点的子节点条目构成一段连续区间，可作为普通切片进行遍历。

```klang
import slice_pool;
fn main() {
    var pool = slice_pool<i64> {};
    let a = pool.alloc_slice(3);
    a[0] = 1; a[1] = 2; a[2] = 3;
    let b = pool.alloc_slice(2);
    b[0] = 7; b[1] = 8;
    pool.free_slice(a);
    let c = pool.alloc_slice(2);            // from the freed run, reset to defaults
    let d = pool.realloc_slice(b, 4);       // b ends the array: grows in place
    print(c, " ", d, " ", pool.items.len, " ", pool.items);
}
```

```
[0, 0] [7, 8, 0, 0] 7 [0, 0, 3, 7, 8, 0, 0]
```

释放和调整大小也接受已分配区段的子范围。非空切片必须对齐且完全位于 `items` 之内；当检查器无法静态证明这一点时，调用会在运行时进行检查。空切片或默认切片可以无害地释放，扩大空切片则会分配一个新区间。双重释放仍属于逻辑错误；不存在占用状态检查。扩容后的切片可能会被移动，因此持有自相对链接的元素类型无法使用 `realloc_slice`。不过，分配和释放操作支持可默认构造的自相对元素。传递拥有者请使用 `slice_pool<T>&`，索引和切片操作请使用其 `items` 数组。[库契约](stdlib.md#slice_pool)详细说明了默认值、子范围、空切片、高水位存储与重定位。

<a id="deprecated-builtin-pools"></a>
### 已弃用的内置池

旧式声明 `reusable var pool: T[>..] = [];` 与 `reusable[] var pool: T[>..] = [];` 仍直接在数组上提供相应操作，并附带隐藏的元数据。它们的参数类型为 `T[>..]&`；池溯源信息（pool provenance）会在调用间传递这些元数据。原有的 `push` 和 `append` 操作会在数组末尾追加元素，而不会使用已释放的区间。其保持不变的语义见[规范 §5.4](k_spec.md#54-reusable-arrays-safe-allocation-escape-hatch)。

要进行迁移，请导入相应的模块，声明 `slot_pool<T> {}` 或 `slice_pool<T> {}`，将拥有者参数更新为对应的根结构体引用，并使用 `pool.items` 执行数组操作。全局索引类型或池相对类型可以直接命名 `pool.items`。对于需要在接收槽位中就地构造的值，请将 `alloc_ref(value)` 替换为 `alloc_with() { value }`；如有需要，可用 `pool.items.index_of` 获取其索引。普通值分配与切片操作均保留原有方法名。将拥有者转移给工作线程（worker）仍是当前的一处迁移缺口；需要工作线程快照的代码请暂时保留已弃用的内置池。工作线程本地的库级池已经可以正常使用。

---

<a id="15-index-types-and-checkfree-functions"></a>
## 15. 索引类型与 checkfree 函数

每次数组索引都会经过边界检查，编译器会移除它能证明不必要的检查（[第 16 节](#bounds-checks)）。任何分析都无法移除的一种检查，是从*另一个*数组中读取出来的索引，例如 `dist[adj[k]]`，因为索引是否越界取决于一个数组的内容与另一个数组长度之间的比较。索引类型将这项检查前移到索引进入程序的那一刻。

**索引类型（index type）** `uN in a`（其中 `uN` 为 `u8`、`u16`、`u32` 或 `u64`）是一个无符号整数，无论在何处被观察到都保证小于 `a.len`。**边界类型（bound type）** `uN upto a` 是一个至多等于 `a.len` 的无符号整数，用于表示区间的结束位置。`a` 指定一个全局 `var` 数组（包括已弃用的 `reusable var`）或全局 `var` 根结构体的可变数组字段，例如 `u32 in graph.nodes`。它必须是元素为定长类型的只增数组，并且该数组是类型标识的一部分：`u32 in nodes` 既不是 `u32`，也不是 `u32 in edges`。其底层值就是普通整数。

```klang
struct Node { lo: u32 upto edges, hi: u32 upto edges, weight: i32 }
var nodes: Node[>..] = [];
type NodeId = u32 in nodes;          // an alias reads better at every use
var edges: NodeId[>..] = [];
```

* **构造。** 在所有构建模式下，`x as (uN in a)` 除非满足 `0 <= x < a.len`，否则都会中止程序，报错信息与 `a[x]` 越界时相同。这相当于把边界检查移动到了此处，而边界检查消除 pass 在能够消除 `a[x]` 检查的地方同样会消除这里的检查。`as!` 不能用来构造索引类型，字面量也绝不会自动适配为索引类型。
* **使用。** 用 `a` 的索引类型去索引 `a` 无需任何边界检查。索引值可隐式转换为其对应的普通整数类型，因此对它做算术运算得到的是普通整数；对索引类型的位置做复合赋值是错误的，对包含索引类型的结构体或定长数组做逐成员算术运算同样也是错误的。
* **循环。** 当 `for i in lo..hi` 的边界属于同一数组的索引类型或边界类型时，`i` 会获得该数组的索引类型；当 `n` 属于该数组的索引或边界类型时，`for i in n` 亦然。
* **透传。** 声明、参数、返回值、字段、变体负载以及数组元素都会保留该类型；如果分支结构的所有分支都具有同一个数组的索引类型，结果也会保留该类型（取最宽位宽）。指向此类存储的引用类型是 `(u32 in a)&`，而绝不是 `u32&`。
* **永不缩容。** 被索引类型命名的数组在任何地方都不可缩容，因此一旦处于范围内的值将永远处于范围内。索引类型没有默认值。`from_bytes` 会针对图像数据所属的数组校验其中的每一个索引。

索引类型不接受直接加后缀修饰，因此它的数组应写作 `(u32 in nodes)[>..]`，或者使用别名写作 `NodeId[>..]`。写在 `for` 绑定器上的索引类型同样需要加括号：`for v: (u32 in nodes) in …`。

```klang
struct Node { lo: u32 upto edges, hi: u32 upto edges, weight: i32 }
var nodes: Node[>..] = [];
type NodeId = u32 in nodes;
var edges: NodeId[>..] = [];

fn add_node(w: i32) -> NodeId {
    let none = 0 as (u32 upto edges);
    nodes.push(Node { lo: none, hi: none, weight: w });
    return (nodes.len - 1) as NodeId;
}

fn main() {
    for w in [5, 3, 8, 1] { add_node(w as i32); }
    for v in nodes.len {                                   // v links to v + 1 and v + 2
        nodes[v].lo = edges.len as (u32 upto edges);
        for d in 1..3 { if v + d < nodes.len { edges.push((v + d) as NodeId); } }
        nodes[v].hi = edges.len as (u32 upto edges);
    }
    var total = 0;
    for v in nodes.len {
        let n = nodes[v];
        for k in n.lo..n.hi { total += nodes[edges[k]].weight; }   // no checks left
    }
    let w = edges[0];
    let plain: u32 = w;                                    // as a number
    print(total, " ", w, " ", plain + 1);
}
```

```
21 1 2
```

规范 §3.11，`docs/design/index_types.md`。

<a id="checkfree-fn"></a>
### `checkfree fn`

`checkfree` 要求编译器在边界检查消除之后，若函数文本中的任何位置（包括每一次特化以及内联到调用方中的每一份副本）仍残留有边界检查，就拒绝编译该程序。它不会改变程序的计算行为。该函数调用的其他函数以及传入的代码块，在其中运行的位置也会受同一规则约束，而无需额外声明为 `checkfree`：不做任何索引操作的辅助函数无需注解，而保留了边界检查的辅助函数则会触发错误并指明通向它的调用链。其他中止检查仍会保留，尤其是 `assert`，`checkfree` 函数正是利用它在循环外一次性声明前置条件：

```klang
var nodes: i32[>..] = [];
type NodeId = u32 in nodes;

checkfree fn sum_at(ids: NodeId[:], weights: i32[>..]&) -> i64 {
    assert(weights.len >= nodes.len);          // once; the loop is then check-free
    var t = 0;
    for id in ids { t += nodes[id] + weights[id]; }
    return t;
}

fn main() {
    for w in [1, 2, 3] { nodes.push(w as i32); }
    var weights: i32[>..] = [10, 20, 30];
    let ids: NodeId[2] = [2 as NodeId, 0 as NodeId];
    print(sum_at(ids, weights));
}
```

```
44
```

在 `checkfree` 函数内部，编译器至少必须能够证明以下检查是不必要的：

* 对定长数组的常量索引；
* 使用 `a` 的索引类型去索引 `a`；
* 使用 `a` 的索引类型去索引 `b`，且此前已断言 `b.len >= a.len`，并且之后两个数组均未发生变化；
* 以数组长度为次数的 `for` 循环索引去索引该数组；
* 在条件已确立 `0 <= i < a.len` 之后执行 `a[i]`；
* 重复执行已经通过的同一检查。

若未通过，错误信息会指明残留的检查及其原因。`checkfree` 位于 `export` 之后，且位于 `inline`、`simd`、`recursive` 或 `fn` 之前；嵌套函数会自动继承该属性。规范 §7.14。

---

<a id="16-expressions-and-control-flow"></a>
## 16. 表达式与控制流

这门语言是面向表达式的：`if`、`match`、`block` 以及裸 `{ … }` 都是表达式，块的值就是其末尾表达式的值。赋值、`++` 和 `--` 是语句。运算符来自 C 和 Rust，并采用 Rust 的优先级（[附录 A](#appendix-a-keywords-and-operators)）。

<a id="if"></a>
### `if`

```klang
let hp = 30;
let kind = if hp > 50 { "healthy" } else if hp > 0 { "hurt" } else { "down" };
if hp > 0 { print(kind); }               // as a statement: no else needed
```

作为值使用时，`if` 需要带有最终的 `else`，且各个分支会合并为同一类型。字面量分支会适配另一分支的类型：`if c { x } else { 0 }` 具有 `x` 的类型——当 `x` 为 `u8` 时即为 `u8`；而当 `x` 为 `f32` 时，`if c { x } else { 0.5 }` 的类型为 `f32`。整数分支与浮点数分支并列时，整数会转换为该浮点数类型。若一个构造的所有分支都是字面量，它本身也具有类似字面量的性质，并在遇到目标类型时进行适配。相同元素类型的数组和切片会合并为一个*切片*：当一个分支是字符串字面量而另一个分支是 `u8[..16]` 字段时，结果是一个观察该字段的 `const u8[:]`，不会发生任何拷贝。规范 §6.4。

```klang
struct Item { name: u8[..16], count: i64 }
fn main() {
    let item = Item { name: "bolt", count: 0 };
    let name = if item.count == 0 { "none" } else { item.name };   // a const u8[:]
    let k: u8 = if item.count > 0 { 1 } else { 2 };               // literals adapt to u8
    print(name, " ", k);
}
```

```
none 2
```

无法提供值的末尾构造属于语句：没有 `else` 的 `if`、某个分支以 `return` 结尾的 `match`，或者没有任何 `break` 携带值的 `loop`。因此，结果类型靠推断的函数可以以此类构造结尾，不产生任何值。

<a id="block-break-with-a-value-and-bare-blocks"></a>
### `block`、带值的 `break` 与裸块

`block { … }` 无需循环即可为 `break` 提供跳转目标，从而让一连串检查能够带着值提前退出。裸 `{ … }` 仅构成作用域：其内部的 `break` 会离开外层的循环或 `block`。同一个构造中的所有 `break E` 必须在 `E` 的类型上达成一致。

```klang
struct P { alive: bool, hp: i64 }
fn main() {
    let p = P { alive: true, hp: 0 };
    let verdict = block {
        guard p.alive else { break "gone"; }
        guard p.hp > 0 else { break "dead"; }
        "standing"
    };
    let doubled = { let t = p.hp + 1; t * 2 };
    var n = 0;
    let found = loop { n += 7; if n % 5 == 0 { break n; } };
    print(verdict, " ", doubled, " ", found);
}
```

```
dead 2 35
```

<a id="guard"></a>
### `guard`

`guard c;` 仅在 `c` 成立时继续执行其所在块的其余部分，而 `guard c else { … }` 在不成立时转而执行 `else` 块。它相当于内外翻转的 `if`，自身并不会向别处跳转。直接位于函数体中时，“块的其余部分”就是函数的其余部分；位于循环体中时，跳过其余部分会直接进入下一次迭代；位于函数值的主体中时，则会结束该次调用。离开超过当前块的范围是 `else` 块的职责：`guard c else { return; }` 会离开函数，`guard c else { break; }` 会离开循环。在 `guard` 之后的代码中，条件已知成立，这也包括收窄可选类型：在 `guard r else { return; }` 之后，`r` 的类型收窄为 `T&`。

```klang
fn half(n: i64) -> i64 { guard n % 2 == 0 else { return -1; } n / 2 }
fn tenfold(p: i64?) -> i64 { guard p else { return 0; } p * 10 }

fn main() {
    for i in 5 { guard i % 2 == 0; print("even ", i); }
    let v = 4;
    print(half(8), " ", half(7), " ", tenfold(v), " ", tenfold(null));
}
```

```
even 0
even 2
even 4
4 -1 40 0
```

规范 §6.4。

<a id="defer"></a>
### `defer`

`defer s;` 与 `defer { … }` 会在其所在块结束时执行主体，无论该块如何结束：自然执行到末尾、`break`、`continue`、`return`、块经由高阶函数触发的 `return`，或穿过该函数的 `return … from`。循环体中的 `defer` 会在每次迭代结束时执行。一次退出会按后写先出的顺序执行在它之前写下的 `defer`，这样清理代码就能紧邻它所清理的对象，且当该清理目标从未执行到时会被跳过。`defer` 的主体是延后执行的普通代码：它看到的是退出时刻变量的状态；在发生 `return` 时，它会在返回值构建完成之后、块的存储释放之前运行。

```klang
var depth = 0;

fn describe(items: i64[:]) -> i64 {
    depth++;
    defer depth--;                   // every way out of describe restores it
    print("depth ", depth);
    var count = 0;
    for x in items {
        defer count++;               // once per iteration, continue included
        if x < 0 { continue; }
        if x > 100 { return -1; }
    }
    return count;
}

fn main() {
    print(describe([1, 2, -3]), " ", describe([5, 500, 7]), " ", depth);
    {
        defer print("block done");
        print("in the block");
    }
}
```

```
depth 1
depth 1
3 -1 0
in the block
block done
```

`defer` 的主体会在其书写位置进行检查，因此它只能引用在它之前声明的名称；在它之前被收窄的 `var` 可选变量在主体内部不再保持收窄状态，因为退出前的赋值可能会撤消这种收窄。有三种操作是它不允许做的，每一种都会导致编译错误：

* **向外离开。** 主体内的任何 `return`、`break` 或 `continue` 都不能以主体外部为目标，包括传入主体内部某次调用的块中的 `return`。
* **使用远距离返回。** 它不能调用执行 `return … from`、传播此类返回或捕获此类返回的函数，因为主体可能会在此类返回正在进行的过程中运行（[第 20 节](#20-long-distance-return-and-errors)）；报错信息会列出引入该机制的调用链。
* **引用函数的具名结果。** 函数按名称返回的局部变量是直接在调用方接收它的位置构建的（[第 17 节](#results)），且在 `defer` 主体运行时已构建完毕。

`defer` 只能出现在函数中：全局初始化器没有作用域退出。当根本没有发生正常退出时，不会运行任何 `defer`：`abort`、失败的 `assert` 或边界检查、`exit`，以及受控 Worker 的中止都会直接结束代码而不运行任何 `defer`，此时将由进程或 Worker 的结束来回收主体原本要释放的资源。规范 §6.6。

<a id="errdefer"></a>
### `errdefer`

`errdefer` 是上下文关键字，语法形式与体内限制均与 `defer` 相同，区别在于它仅当所属作用域沿错误展开路径退出（`return … from` 或 `exception::throw`）时才会执行，而在正常贯穿、普通 `return`、`break` 与 `continue` 时跳过。在同一作用域内，`errdefer` 与 `defer` 统一遵循后进先出（LIFO）顺序执行；`errdefer` 体内消费 `own` 句柄也不会阻止正常返回路径继续持有或返回该句柄：

```klang
import exception;

fn step(fail: bool) -> i64 {
    errdefer print("rollback");
    defer print("close");
    if fail { exception::throw(1); }
    return 42;
}

fn main() {
    var v: i64 = 0;
    fn ok_call() { v = step(false); }
    exception::try<i64>(ok_call) { _ => print("caught"); };
    print("ok=", v);
    fn bad_call() { step(true); }
    exception::try<i64>(bad_call) { _ => print("caught"); };
}
```

```
close
ok=42
close
rollback
caught
```

<a id="using"></a>
### `using`

类似 C# 的 `using` 声明与语句块在不引入隐式析构器的前提下提供确定性的资源释放语义。它直接脱糖为变量绑定与紧随其后的 `defer x.dispose();`，在作用域任何退出路径（正常离开、提前 `return`、循环 `break`/`continue`、`return … from`，并完整配合 `own` 句柄义务检查）上按后进先出（LIFO）顺序调用 `.dispose()`：

* **`using let x = expr;` / `using var x = expr;`**（C# 8+ 风格声明语句）：在当前块内绑定 `x`，并在当前块退出时自动调用 `x.dispose()`。
* **`using (let x = expr) { … }` / `using (var x = expr) { … }`**（作用域块形式）：将 `x` 作用域限制在花括号块内，离开块时调用 `x.dispose()`。
* **`using (expr) { … }`**（匿名资源守卫）：求值 `expr` 并在匿名作用域块退出时调用 `.dispose()`。

```klang
struct File { id: i64 }
fn open(id: i64) -> File { File { id: id } }
fn dispose(f: File) { print("close ", f.id); }

fn main() {
    using let a = open(1);
    using (let b = open(2)) {
        print("use ", b.id);
    }
    print("after inner ", a.id);
}
```

```
use 2
close 2
after inner 1
close 1
```

`using` 为上下文关键字：在非 `using` 声明/语句位置上仍可作为普通标识符或函数名使用（如 `let using = 10;`），与 Cordis 扩展语法兼容。

<a id="loops"></a>
### 循环

<!-- fragment -->
```klang
for i in 3 { }              // 0, 1, 2
for i in 10..13 { }         // 10, 11, 12: half-open
for i: i32 in 0..10 { }     // i is an i32; each bound must be, or widen to, one
for i: u8 in 0..256 { }     // every u8, the end one past the type's last value
for x in xs { }             // copies, for fixed-size elements; references for others
for &x in xs { x *= 2; }    // references: the mutation form
for x, i in xs { }          // with the index, an i64
for x, i: u8 in grid { }    // the index at a narrower type, where the array's length fits it
while cond { }
loop { if done { break; } }
```

* 区间的上下界会像操作数那样统一类型，而 `i` 就以该类型迭代；`for i in n` 等价于 `0..n`。上下界只求值一次；空区间或逆序区间什么也不做。
* `for x in arr` 对定长元素执行拷贝，对其他元素按引用绑定。拷贝出的元素不可写入（`x.f = 1` 会报错并提示使用 `&x`），因为写入只会修改副本。如果元素本身*就是*一个引用，则会直接绑定为它所持有的那个引用。
* `for &x in arr` 将每个元素都按引用绑定。在 `T[>..<]` 上，该绑定是一个活跃引用，因此循环内部不允许发生缩容。
* 循环会原地遍历其序列。可调整大小的数组的长度会在每次迭代前重新读取，因此在按值遍历的 `for` 循环中执行推入（push）会将新元素加入本次遍历；而切片则保持自身的长度。无论循环原地遍历的是什么，它在整个循环体期间都处于活跃状态，因此其所在的数组在循环体内不得缩容。
* `continue` 会跳转到下一个元素；`break` 会离开最内层的循环或 `block`。K 语言没有循环标签。

```klang
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    for &x in xs { x *= 10; }
    for x, i in xs { if i == 0 { xs.push(x + 1); } }   // the push extends the walk
    var count: u8 = 0;
    for i: u8 in 0..256 { count += 1; }
    print(xs, " ", count);
}
```

```
[10, 20, 30, 11] 0
```

（正如 `u8` 的行为那样，`count` 在递增 256 次后回绕到了 0。）规范 §6.5。

<a id="const-for-expansion-at-specialization-time"></a>
### `const for`：特化期展开

`const for` 会按编译期序列中的每一项特化一次代码片段：序列可以是结构体的字段、枚举的变体、类型列表或常量列表。这就是 K 语言的反射机制，没有运行时迭代器也没有元数据：展开后的每一份主体都是普通代码。

```klang
struct Point { x: i64, y: i64, label: u8[..8] }

fn show_fields<T>(v: T&) {
    const for <N, A> in fields<T> { print(N, " = ", v.A); }
}

fn scaled<T, F>(v: T&) -> T {
    T { const for <N, A> in fields<T> { A: F(N, v.A), } }
}

fn main() {
    var p = Point { x: 2, y: 3, label: "pt" };
    show_fields(p);
    const for <U> in types<i8, u16, f32> { print(default<U>()); }
    const for <C> in [1, 2, 4] { let a: u8[C] = [0; C]; print(a.len); }
    let q = scaled(p) { n, x => x };
    print(q);
}
```

```
x = 2
y = 3
label = pt
0
0
0.0
1
2
4
Point { 2, 3, "pt" }
```

`fields<T>` 会绑定每个字段的名称（一个字符串常量）以及一个*访问器*，它既可以写成 `v.A`，也可以作为该结构体类型字面量中的标签 `A:`。`types<…>` 绑定一个类型，而 `[…]` 绑定一个常量（字面量、具名常量，以及数组大小允许使用的整数表达式）。语句片段对每一项而言都是独立的作用域，构造器片段则负责提供初始化器。`break` 和 `continue` 不能跨越展开边界。

`variants<E>` 会绑定每个变体的名称及变体 `V`，该 `V` 可以写在任何原本书写变体名称的位置：将 `V` 用作类型（并用 `fields<V>` 获取其载荷）、用 `V { … }` 构造该变体、在 `match` 分支中写作 `V c` 或 `V &c`，以及在没有载荷时直接将 `V` 用作值。`match` 片段用于提供分支，这些分支会像手写分支一样接受检查：每个变体恰好出现一次，`_` 位于最后。

```klang
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }

fn show<T>(value: T) {
    match value {
        const for <N, V> in variants<T> {
            V v => {
                print(N);
                const for <F, A> in fields<V> { print("  ", F, " = ", v.A); }
            },
        }
    }
}

fn main() {
    let s: Shape = Shape.Rect { w: 3, h: 4 };
    show(s);
    const for <N, V> in variants<Shape> { print(N); }
}
```

```
Rect
  w = 3
  h = 4
Circle
Rect
Dot
```

规范 §6.5，`docs/design/const_for.md`。

<a id="const-if-selection-at-specialization-time"></a>
### `const if`：特化期选择

`const if` 会在函数特化期间选择它的其中一个片段列表，而完全不生成其余分支的代码。它出现在 `const for` 能够出现的位置：语句列表、构造器的初始化器列表，或 `match` 的分支列表，且每个分支内部包含与该列表同类的片段。条件分支链用 `else const if` 继续；在 `const if` 之后直接写普通的 `else if` 会报错。

```klang
struct Item { name: u8[..8], price: i64, tax: i64 = 20, discount: i64 = 0 }
enum Shape { Circle { r: i64 }, Dot }

fn required<T>() {
    const for <N, A> in fields<T> { const if !has_default<A> { print("required: ", N); } }
}

// A copy of src with every field that declares a default reset to it.
fn reset_defaults<T>(src: T&) -> T {
    T { const for <N, A> in fields<T> { const if !has_default<A> { A: src.A, } }, .. }
}

fn describe<T>(v: T) {
    const if same<T, i64> { print("an integer ", v); }
    else const if same<T, Shape> { print("a shape"); }
    else { print("something else"); }
}

fn main() {
    required<Item>();
    var item = Item { name: "pen", price: 5, tax: 7, discount: 3 };
    print(reset_defaults(item));
    describe(1);
    describe(2.5);
    const for <C> in [1, 2, 4] {
        const if C > 1 { print(C, " is wide"); } else { print(C, " is narrow"); }
    }
}
```

```
required: name
required: price
Item { "pen", 5, 20, 0 }
an integer 1
something else
1 is narrow
2 is wide
4 is wide
```

条件是特化期的常量：`true`、`false` 或具名 `bool` 常量；查询操作；由 `!`、`&&`、`||` 和括号组合的条件；或者同一类两个常量的比较，这里的常量即 `const for` 列表所接受的常量（字面量、具名常量、数组大小允许的整数表达式，以及外层 `const for` 绑定）。整数会在它们统一后的类型上比较，整数与浮点数比较时会先转换，而布尔值和字符串只允许使用 `==` 和 `!=`。局部变量、函数调用、字段读取或任何在运行时才计算的内容都会报错，而不会变成运行时分支。

查询操作仅在 `const if` 条件中有效。当来自 `fields<T>` 的访问器 `A` 所指代的字段显式声明了 `= …` 时（包括默认值恰好等于该类型零值的情况），`has_default<A>` 为 `true`；它不会对任何表达式求值。当两个类型在替换后完全相同（包括 `const` 限定在内）时，`same<T1, T2>` 为 `true`，因此可以用 `same<V, Shape.Dot>` 挑出某个变体，用 `same<T, i64>` 挑出某个泛型实例化。

未被选中的分支只会被解析而绝不会被类型检查，这正是它的意义所在：针对字段的 `const if` 可以在一个分支中写入该字段而在另一个分支中不碰它，而基于相同谓词的运行时 `if` 则必须对每个字段同时类型检查两个分支。在语句列表中，选中的分支是一个嵌套块而非展开体：其中的 `break`、`continue` 和 `return` 含义与在 `if` 中相同。在构造器中，选中的条目会并入初始化器列表；在 `match` 中，选中的分支会并入分支列表，从而让遍历 `variants<E>` 的 `const for` 可以把部分变体留给手写分支处理。

规范 §6.5，`docs/design/const_for.md` 第 11 节。

<a id="functions-that-never-return"></a>
### 永不返回的函数

`abort(msg)` 与 `exit(code)` 永不返回，检查器会将这一点纳入分析：以它们结尾的分支不提供值，它们之后的语句不会被检查，并且这一性质会沿着调用传播，因此函数可以以对它们的调用结尾，而无需编写哑元返回值。

```klang
fn fail(what: u8[:]) { abort(str("cannot ", what)); }

fn value(ok: bool) -> i64 {
    if ok { 42 } else { fail("compute") }
}

fn main() { print(value(true)); }
```

```
42
```

未知的递归调用会被假定为会返回。规范 §7.3、§9.3。

<a id="bounds-checks"></a>
### 边界检查

对数组或切片的每次索引访问都会经过检查，每一次向索引类型的转换也是如此（[第 15 节](#15-index-types-and-checkfree-functions)）。检查失败会中止程序。编译器会消除它能证明无需检查的边界检查，并且这门语言在设计上就为这种分析提供了所需的信息：

* `T[>..]` 只会在分析器可见的 `pop`、`pop_n`、`resize` 或 `clear` 处缩容，因此在 `push` 之前建立的边界在 `push` 之后依然成立；
* `%` 是欧几里得取模，因此归约结果始终在范围之内；
* 固定大小与容量都是常量；
* 每个调用点都会记录它对其参数所能证明的性质。

最有帮助的惯用写法是在核心计算函数顶部对长度做一次 `assert`：

```klang
let W = 8;
fn blur_row(above: u8[:], here: u8[:], below: u8[:], out: u8[:]) {
    assert(above.len == W && here.len == W && below.len == W && out.len == W);
    for x in 1..W - 1 {                     // every index is x plus a constant: no checks
        let sum = above[x - 1] as i64 + above[x] + above[x + 1] +
                  here[x - 1] + here[x] + here[x + 1] +
                  below[x - 1] + below[x] + below[x + 1];
        out[x] = (sum / 9) as! u8;
    }
}
```

`k --bce-lines` 会按行报告哪些检查残留了下来。在每次构建中仍会保留的其他中止条件包括：除以零、有限容量数组的容量溢出、在空数组上继续缩容、超出位宽的相对引用偏移、失败的 `assert`，以及数据栈耗尽。它们全都是干净的中止，会附带错误信息和失败检查所在的行号；标准库函数内部的检查失败会报告调用该库的程序代码行号。规范 §9.3、§10.5。

---

<a id="17-functions"></a>
## 17. 函数

<!-- fragment -->
```klang
fn name(a: i64, var b: f64, xs: i32[:]) -> i64 { … }
fn generic<T>(a: T, b: T) -> T { … }
fn also_generic(a, b) { … }              // untyped parameters are generic
fn scaled(a: i32, b: i32 = 0, c: f32 = 1.0) -> f32 { … }   // scaled(2) is scaled(2, 0, 1.0)
```

函数都是自由函数。语言中没有方法也没有 `impl` 块，但对任何函数来说，**`x.f(a)` 都会调用 `f(x, a)`**，因此在读起来更顺畅的地方可以使用方法调用语法。解析时会先查找字段和内置成员，然后再查找函数，并从 `x` 所属类型的命名空间开始（[命名空间](#namespaces)）。顶层声明与书写顺序无关。规范 §7.1。

<a id="parameters"></a>
### 参数

* 参数默认是一个 `let` 绑定：它不能被重新赋值，但其内容的可写性取决于其自身的类型。`var b: f64` 则使按值传递的参数成为可重新赋值的局部变量。
* 没有类型标注的参数是泛型参数（[第 18 节](#18-generics-and-function-values)）。
* 引用参数 `n: i64&` 无需书写 `&` 即可绑定到目标类型的左值。切片参数可接受每一种数组。按值传递的非定长参数 `xs: u8[>..]` 会在参数槽位中就地构造，并像局部变量一样由被调函数拥有：`f(copy(xs))` 或 `f(make())`。
* **默认值。** 末尾的参数可以声明默认值 `c: f32 = 1.0`，调用时可以省略末尾任意数量带有默认值的参数。默认值会在每次此类调用时，在已写出的实参之后作为该参数类型的值求值。它可以引用全局变量并调用函数，但不能引用调用方的局部变量。泛型参数的默认值会适配实参所绑定的类型（`fn scale<T>(x: T, by: T = 2)` 对 `u8` 和 `f64` 同样适用），并且默认值本身绝不会决定类型变量。

```klang
var counter = 0;
fn bump(r: i64& = counter, by: i64 = 1) { r += by; }
fn limit(x: i64, lo: i64 = 0, hi: i64 = 100) -> i64 {
    if x < lo { lo } else if x > hi { hi } else { x }
}
fn scale<T>(x: T, by: T = 2) -> T { x * by }

fn main() {
    bump(); bump();
    var mine = 10;
    bump(mine, 5);
    let b: u8 = 21;
    print(counter, " ", mine, " ", limit(250), " ", limit(-5, -10), " ", 7.limit(0, 5), " ",
          scale(b), " ", scale(1.5));
}
```

```
2 15 100 -5 5 42 3.0
```

函数调用实参支持在位置参数前缀之后使用具名参数（`name: expr`）。具名参数可以重排参数顺序并跳过中间带默认值的参数；所有无默认值的参数仍须提供实参，且位置参数不能出现在具名参数之后：

```klang
fn window(w: i64, h: i64 = 720, scale: i64 = 1, border: i64 = 0) -> i64 {
    w * scale + h + border
}
fn main() {
    print(window(1280, border: 8), " ", window(scale: 2, w: 640));
}
```

```
2008 2000
```

<a id="results"></a>
### 返回值

函数在 `->` 之后声明返回值，多个返回值之间用逗号分隔。多返回值是一种调用约定，而不是元组类型：在普通值上下文中的调用只取第一个返回值，`let a, b = f();` 会绑定全部返回值，`x, xs[i] = f();` 会将它们赋值给已存在的位置，而 `return f()` 会转发所有返回值。按惯例，末尾的 `bool` 或错误值用来表示其余返回值是否有意义。

```klang
fn divide(a: i64, b: i64) -> i64, bool {
    if b == 0 { return 0, false; }
    return a / b, true;
}

fn main() {
    let q, ok = divide(7, 2);
    var r = 0;
    var flag = false;
    r, flag = divide(1, 0);
    print(q, " ", ok, " ", r, " ", flag, " ", divide(9, 3));
}
```

```
3 true 0 false 3
```

只有 `return` 和调用能够携带多个值：分支不能以 `a, b` 结尾，因此每个分支必须各自独立返回。

凡是可以推断出返回值类型的地方都可以省略它。此时返回值类型取自第一个 `return`（或函数体的最后一个表达式），就像未带类型标注的 `let` 从其初始化器取得类型一样。若返回的是非函数自身局部变量的非定长左值，或显式写成 `&x`，则结果为引用；而指向定长值的引用则会作为拷贝返回。跨越递归环时，每个函数签名都必须完整写出。

非定长结果会**直接在目标位置中构造**（[第 4 节](#built-in-place)）：调用方的变量、正在推入的元素，或另一次调用的参数槽位。当此类结果的每一个 `return` 都返回同一个局部变量时，该局部变量从声明起*就是*那个目标位置，因此 `return xs` 不会拷贝任何东西（如果第二个被返回的局部变量阻碍了第一个局部变量直接分配在目标位置，编译器会发出警告）。函数自身的局部变量会被 `return` *移动*；它的一部分或临时值的一部分会在调用结束时消亡，因此必须以 `copy(x)` 的形式返回。规范 §7.1、§7.3。

<a id="overloading"></a>
### 重载

函数按参数类型进行静态重载解析。优先选择唯一的精确匹配，其次是泛型精确匹配，然后是需要转换的匹配（数组转切片、字面量适配类型、隐式拓宽）。候选函数的优先级取决于其最差实参的匹配等级，平局则报错。默认值不计入实参，因此 `f(a, b = 0)` 与 `f(a)` 并存会让所有单实参调用产生歧义。只有当没有任何普通候选匹配时，才会尝试标签分发（[第 9 节](#case-functions)）。需要将整数实参转换为浮点数的候选排在最后：`f(2)` 在 `f(i32)` 与 `f(f64)` 并存时会调用 `f(i32)`，而对整数 `n` 调用 `sqrt(n)` 会在两种浮点位宽之间产生歧义（请写 `sqrt(n as f64)`）。预期结果类型绝不参与重载选择。

```klang
fn describe(x: i64) -> u8[>..]     { str("int ", x) }
fn describe(x: f64) -> u8[>..]     { str("float ", x) }
fn describe(xs: i64[:]) -> u8[>..] { str(xs.len, " ints, first ", xs[0]) }

fn main() {
    let xs: i64[3] = [4, 8, 15];
    print(describe(7), "; ", 2.5.describe(), "; ", xs.describe(), "; ", describe(xs[1..]));
}
```

```
int 7; float 2.5; 3 ints, first 4; 2 ints, first 8
```

规范 §7.1。

<a id="nested-functions"></a>
### 嵌套函数

函数可以声明在另一个函数内部，并按照外部函数局部变量的常规规则将它们作为自由变量来读写。名称在嵌套函数声明处解析。同一个块中的嵌套函数可以不按声明顺序相互调用，就像递归下降解析器中的函数那样；但外层代码只能在嵌套函数声明之后才能调用它。同一个块中同名的嵌套函数像顶层函数一样支持重载，因此分支函数也可以嵌套声明。嵌套函数是编译期实体：它只作为函数值传递，绝不会逃逸。

```klang
struct Employee { name: u8[..12], dept: u8[..8], salary: i32 }

fn main() {
    var staff: Employee[>..] = [];
    staff.push(Employee { name: "ada", dept: "eng", salary: 120 });
    staff.push(Employee { name: "linus", dept: "ops", salary: 90 });
    staff.push(Employee { name: "grace", dept: "eng", salary: 135 });
    var budget = 0;
    var seen = 0;
    fn spend(e: Employee&) {          // writes main's locals
        budget += e.salary;
        seen++;
    }
    for &e in staff { if e.dept == "eng" { spend(e); } }
    print("eng budget ", budget, " over ", seen, " people");
}
```

```
eng budget 255 over 2 people
```

规范 §7.5。

<a id="function-qualifiers"></a>
### 函数限定符

| 限定符 | 含义 | 章节 |
|---|---|---|
| `recursive fn` | 递归环的入口 | [19](#19-recursion) |
| `extern fn`、`extern fn … = "sym"` | C 函数 | [24](#24-calling-c-and-being-called-from-c) |
| `export fn`、`export "sym" fn` | 可从 C 调用的 K 函数 | [24](#24-calling-c-and-being-called-from-c) |
| `thread_fn` | Worker 的入口点 | [22](#22-threads) |
| `checkfree fn` | 函数内不得残留边界检查 | [15](#checkfree-fn) |
| `inline fn` | 无论大小，在每次调用处内联 | 见下文 |
| `simd fn` | 按每个指令集级别分别构建一次 | 见下文 |
| `deprecated fn` | 调用或作为值使用时会发出警告 | 见下文 |

限定符必须按固定顺序书写：首先是 `deprecated` 及其可选提示消息，然后是 `export` 或 `extern`，接着是 `checkfree`、`simd`、`inline`、`recursive`，最后是 `fn` 或 `thread_fn`。

`deprecated "use copy_from" fn copy_into(...)` 会保留旧函数，并在直接调用处以及将函数具名作为值使用处发出警告，但位于拉取的外部包或内置包中的使用点除外——这些包应由其作者自行修改。警告会复述该提示字符串：`copy_into is deprecated: use copy_from`。若未提供字符串，则输出 `copy_into is deprecated`。通过已经被警告过的值进行的调用不会再次警告。在不是该限定符的所有位置，`deprecated` 都是一个普通名称。规范 §7.15。

`inline` 要求在每次调用处内联该函数；不写它时，编译器会根据函数大小与调用次数自行决定。它不会改变程序的计算结果，且不能与 `extern`、`export`、`simd`、`recursive` 或 `thread_fn` 组合使用。

`simd` 要求为目标平台的基准指令集构建一次该函数，再为其支持的每个更宽指令集各构建一次（在 x86-64 上为 AVX2 和 AVX-512）。在每次调用时，会选择 CPU 所具备的最宽版本。它适合包含可供 C 编译器向量化的热点循环的函数：字节变换、编解码器、数组上的内核计算。每个版本的计算结果都与基准版本完全一致，因此程序输出不依赖于此。后端可以忽略该限定符，就像除 clang 之外的编译器下的 C 后端以及进程内 TinyCC 后端所做的那样。唯一的例外是 `math` 中的 `mul_add(x, y, z)`：在为 FMA 构建的代码中 `x * y + z` 只舍入一次，而在未启用 FMA 时会舍入两次。AVX2 和 AVX-512 版本只舍入一次，基准版本则遵循其目标平台的舍入行为。规范 §7.12、§7.13。

```klang
simd fn checksum(xs: u8[:]) -> u32 {
    var h: u32 = 0;
    for x in xs { h += x as u32 * 31 + 7; }
    return h;
}

inline fn clamp01(x: f32) -> f32 { if x < 0 { 0 } else if x > 1 { 1 } else { x } }

fn main() { print(checksum("klang"), " ", clamp01(1.5)); }
```

```
16310 1.0
```

---

<a id="18-generics-and-function-values"></a>
## 18. 泛型与函数值

<a id="type-parameters"></a>
### 类型参数

未标注类型的参数就是泛型参数。显式的 `<T>` 用于声明两个参数必须使用同一类型，或者命名一个未出现在任何参数中的类型。一切都会进行单态化：泛型函数体只在所有类型均为具体类型的实例化处接受类型检查，且错误会连同产生该实例化的编译期调用链一起报告在出错的实例化点。类型变量绝不会进行抽象检查，因此只要具体类型允许，泛型函数体就可以做任何操作，无需 trait 约束。

```klang
fn twice(x) { x + x }
fn span<T>(a: T, b: T) -> T { if a > b { a - b } else { b - a } }
fn zero<T>() -> T { default<T>() }

fn main() {
    print(twice(21), " ", twice(1.5), " ", span(3, 8), " ", span(2.5, 1.0), " ", zero<f64>() + 0.5);
}
```

```
42 3.0 5 1.5 0.5
```

类型实参会根据实参类型推断得出。当多个实参提及同一个类型变量时，带类型的实参先绑定该变量，字面量随后适配：对于 `u32` 类型的 `n`，`max(n, 0)` 会得到 `u32` 实例化。浮点字面量优先于整数字面量进行绑定，而 `[]` 与 `null` 不绑定任何类型。显式列表 `f<i64>(x)` 按顺序绑定开头的类型参数；当没有任何实参提及某个参数时（`qget<i64>()`、`zero<f64>()`）就必须给出该列表。只有当 `<…>` 之后紧跟着 `(`、`{` 或 `.Variant {` 时，`f<` 才会开启类型实参列表；否则 `<` 表示比较运算符。

在泛型代码内部，对非引用位置使用 `.=` 的赋值效果与 `=` 相同。这使泛型代码能够用 `.=` 移动类型参数所对应类型的完整值。当 `T` 是引用类型时，`.=` 移动的是引用本身（此时 `=` 会通过引用写入目标）；当 `T` 是值类型时，它就是普通赋值。标准库中的 `swap_at` 就是这样编写的，因此它既能交换引用数组（包括相对引用）中的元素，也能交换其他任何数组中的元素。

当只有字面量绑定某个类型变量时，该参数在特化体内就是一个*字面量参数*：一个未知值的常量，每当遇到具体类型时便会适配。例如对于 `fn push_n<A, T>(xs: A&, v: T, n: i64)`，`push_n(flags, 1, n)` 无需在调用处强转即可向 `u8[>..]` 推入一个 `u8`，而 `push_n(flags, 300, n)` 则会报错并指出接受 `u8` 的 `push`。

**浮点上下文。** `f32` 目标位置可以细化泛型调用中原本未受约束的浮点计算：对于整数 `width` 和 `height`，`let scale: f32 = max(0.5, min(width / 960.0, height / 600.0));` 会以 `f32` 计算这两次调用。该规则适用于返回裸 `T` 且其 `T` 参数均为裸值的函数，条件是传入的实参为字面量或由字面量与整数组成的浮点表达式。只要传入带具体类型的实参，就会固定该类型。规范 §7.7。

<a id="function-values"></a>
### 函数值

静态函数值是作为泛型参数或无类型不可变块参数传递的编译期实体。函数实参可以是函数名、已绑定的泛型函数参数，或块字面量；参数上的 `: fn(…)` 约束是可选的文档标注。对函数值的每一次调用都是直接且可内联的，函数值不能逃逸：将其存储起来、作为返回值返回，或放入数据结构中都会导致编译错误。高阶函数可以编译为直接循环。[具体闭包](#concrete-closures)则是自带已存储捕获环境的可调用值。

<!-- fragment -->
```klang
fn foo<F: fn(i64)>(a: i64) { F(a); }
foo(1) { print(it) }                 // the trailing-block form, with the implicit `it`
foo(1) { x => print(x) }             // named block parameters
fn h(v: i64) { print(v) }
foo(1, h)                            // a named function
```

* 跟在调用括号之后的尾随块 `{ … }` 是最后一个实参。它的参数可以是隐式的 `it`，或者写在 `=>` 之前的具名参数。K 语言每次调用只允许一个尾随块；第二个函数值需要通过名称传递。
* 块可以像嵌套函数一样读写外层函数的局部变量。
* 高阶函数内部的调用可以向其块传递一个函数值。只要所捕获的作用域依然存活，该块就可以调用所提供的操作或将其转发给另一个函数。参见[带作用域的文件读取器](stdlib.md#scoped-file-readers)。
* 块中的 `return` 会从**词法上包围它的具名函数**返回，而不是从调用该块的函数返回，因此手写的迭代器与 `for` 具有同等的控制流表达能力。以此方式返回的引用可以是调用该块的函数交给它的引用，该引用指向调用点实参所指的位置。它不能指向调用该块的那个函数自身的局部变量或块自身的局部变量，因为这些变量会随着返回而消亡。
* 具名函数作为值传递时会保留其参数默认值；块的参数则没有默认值。当除带有默认值的参数之外其余实参都已提供时，作为实参传递的函数值可以跟在比被调函数形参总数更少的实参之后。
* 若一个名称具有多个重载，传递该名称时会一并传递所有重载，并在每次调用该函数值时于这些重载之间进行解析。
* 块的值类型会在不考虑其所在调用目标类型的情况下进行推断，因此块的值不能是 `[]`。接收该值的存储位置仍然是已知的，并用于检查新结果中的相对链接。

```klang
fn each_pair<T, F>(xs: T[:]) {
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}

fn each_step<F>(lo: i64, hi: i64, step: i64 = 1) {
    var i = lo;
    while i < hi { F(i); i += step; }
}

fn first_multiple_of_7(lo: i64, hi: i64) -> i64 {
    each_step(lo, hi, 3) { if it % 7 == 0 { return it; } };   // returns from first_multiple_of_7
    return -1;
}

fn main() {
    let xs: i64[5] = [4, 8, 15, 16, 23];
    var gaps: i64[>..] = [];
    each_pair(xs) { a, b => gaps.push(b - a); };
    let evens = xs.filter() { it % 2 == 0 };
    let total = fold(xs, 0) { acc, x => acc + x };
    let big = xs.find() { it > 10 };
    sort(gaps) { a, b => a > b };
    print(gaps, " ", evens, " ", total, " ", first_multiple_of_7(10, 40), " ",
          xs.map() { it * 2 }, " ", big);
}
```

```
[7, 7, 4, 1] [4, 8, 16] 66 28 [8, 16, 30, 32, 46] 15
```

标准库的 `filter` 和 `map` 会直接把结果构建在接收它的变量中，不产生中间数组。高阶函数从不限定其函数值的返回类型：`map` 的结果元素类型就是 `F(x)` 在该次实例化中的返回类型。规范 §7.6、§7.7。

<a id="concrete-closures"></a>
### 具体闭包

`fn[capture: Type](parameter: Type) -> Result { … }` 创建一个带有固定大小捕获存储的值。`capture: Type` 从定义作用域中绑定同名实体。`capture: Type = expression` 则在定义处求值一个初始化器，并将结果绑定到该名称。捕获初始化器在创建闭包时按源码顺序运行一次。若环境为空，可省略方括号。参数必须带有显式类型，而返回值类型可以推断。

```klang
fn make_adder(bias: i64) {
    return fn[bias: i64](x: i64) -> i64 { return bias + x; };
}

fn main() {
    let add = make_adder(3);
    print(add(4));
}
```

```
7
```

闭包体可以访问其捕获项、参数、局部声明和全局变量。`return` 从闭包的本次调用返回。值捕获按其类型进行拷贝。引用捕获或切片捕获会借用原存储，必须遵守其生命周期与访问规则。捕获项是私有的。

每个闭包字面量都有各自的具体类型，其中包含闭包所使用的任何外层类型参数。绑定或返回闭包时应由编译器推断该类型。它可以存储在记录或数组中，并通过普通泛型参数传递。调用闭包会直接使用其具体布局，且捕获存储无需隐式堆分配。
[规范 §7.6.1](k_spec.md#761-concrete-closures)

<a id="borrowed-callables"></a>
### 借用可调用对象

`fn&(i64) -> i64` 是一种运行时值类型。使用 `borrow_callable` 可将具名 K 函数或已有闭包绑定到精确签名上。不同的捕获布局只要签名相同，就可以共享该类型并放入同一个数组或字段中。

```klang
fn main() {
    var bias: i64 = 3;
    let add = fn[bias: const i64&](x: i64) -> i64 { x + bias };
    let scale = fn[factor: i64 = 2, extra: i64 = 1](x: i64) -> i64 {
        x * factor + extra
    };
    let a: fn&(i64) -> i64 = borrow_callable(add);
    let b = borrow_callable<fn&(i64) -> i64>(scale);
    let choices = [a, b];
    print(choices[0](5), " ", choices[1](5));
    bias = 7;
    print(choices[0](5));
}
```

```
8 11
12
```

借用闭包需要一个稳定的具名环境，且其中的捕获项必须可拷贝。它可以持有指向已知固定存储的只读引用。该环境的存活期必须长于该可调用对象及其所有拷贝。当别名替换该环境时，可调用对象会看到替换后的内容，编译器也会检查替换后内容的捕获根。具名顶层 K 函数无需环境。
参数接受无引用的定长值、定长无引用数据的只读视图，或运行时可调用对象。返回值则是无引用的定长值。当前的同步调用契约禁止依赖可变外部绑定或改变所借用存储的布局。
[规范 §7.6.2](k_spec.md#762-borrowed-callables)

<a id="owned-callables"></a>
### 拥有所有权的可调用对象

`fn[N](../A) -> R` 会为捕获环境预留 `N` 个字节。`own_callable` 会把一个具体闭包拷贝到该存储中，并检查其签名。所得的值可以离开创建它的函数。

```klang
fn make_adder(by: i64) -> fn[8](i64) -> i64 {
    return own_callable<fn[8](i64) -> i64>(
        fn[by: i64](x: i64) -> i64 { x + by });
}

fn main() {
    let callback = make_adder(12);
    print(callback(5));
}
```

```
17
```

容量是一个非负的编译期字节数，其中包含具体环境中的填充字节。具名函数或无捕获的闭包可以使用 `fn[0]`。调用该值时会在具类型存储中恢复其环境，因此外层记录可以把可调用对象放置在任意字节偏移处。转换与调用都不需要堆分配。

可拷贝的捕获项可以包含指向已知固定存储的只读引用，其所指对象的存活期必须长于该可调用对象的每一次拷贝。参数、返回值和目标副作用适用与借用可调用对象相同的同步契约。
[规范 §7.6.3](k_spec.md#763-owned-callables)

<a id="typed-metadata-decorators"></a>
### 类型化元数据装饰器

装饰器声明了带类型的配置字段，以及接受该装饰器的函数种类。配置实参使用编译期常量。`decorator_metadata<Decorator>(function_name)` 用于读取附加在函数上的记录。

```klang
decorator Route(provider: const u8[:], capacity: u32) on fn(i64) -> i64;

@Route("v2", 2 + 2)
fn request(value: i64) -> i64 { value + 1 }

fn main() {
    let route = decorator_metadata<Route>(request);
    print(route.provider, " ", route.capacity, " ", request(5));
}
```

```
v2 4 6
```

对异步处理函数可使用 `on async fn`。省略 `fn` 后的参数与返回值列表，即可接受所声明种类的任意签名，包括泛型函数。若指定了签名，则必须精确匹配。装饰器可以导入，也可以用命名空间限定。一个函数可以携带多个不同的装饰器。

查询函数时，会选择带有该装饰器的唯一重载。结果是一个字段只读的普通记录，因此注册代码既可以接受记录字面量，也可以接受装饰器产生的记录。
[规范 §7.6.4](k_spec.md#764-typed-metadata-decorators)

<a id="synchronous-decorator-wrappers"></a>
### 同步装饰器包装器

同步装饰器可以添加一个 `wrap` 块。调用 `next()` 会转发当前调用的参数，并返回下一层的返回值。在一组叠加的装饰器中，最上层的包装器最先进入。

```klang
decorator Trace(label: const u8[:]) on fn {
    wrap {
        print("enter ", label);
        defer print("leave ", label);
        next()
    }
}

@Trace("outer")
@TraceInner
fn square(x: i64) -> i64 { x * x }

decorator TraceInner() on fn {
    wrap {
        print("enter inner");
        defer print("leave inner");
        next()
    }
}

fn main() { print(square(5)); }
```

```
enter outer
enter inner
leave inner
leave outer
25
```

原始调用的实参和默认值只求值一次。每个包装器都遵守被装饰函数的返回值契约，包括从原始函数体推断出的返回值。装饰器配置字段名在包装器内可用。每次调用 `next()` 都会应用所有权与引用检查。每一层的 `defer` 会在该层退出时运行，非局部返回也不例外。
[规范 §7.6.5](k_spec.md#765-synchronous-decorator-wrappers)

<a id="asynchronous-decorator-wrappers"></a>
### 异步装饰器包装器

在声明为 `on async fn` 的装饰器中使用 `await next()`。它的包装器会在任务被轮询时运行，并且可以在继续执行前后挂起。继续执行会将原始输入转发给下一层，且在每条执行路径上最多运行一次。

```klang
decorator TraceAsync(label: const u8[:]) on async fn {
    wrap {
        print("enter ", label);
        defer print("leave ", label);
        await yield_now();
        return await next();
    }
}

@TraceAsync("square")
async fn square(x: i64) -> i64 {
    await yield_now();
    return x * x;
}

fn main() { print(square(5).join()); }
```

```
enter square
leave square
25
```

原始函数决定返回值类型，包括推断类型的情况。若在 `await next()` 期间发生取消，子任务会先完成排空，随后再运行包装器中已激活的 `defer`。如果包装器在转发拥有所有权的输入之前就退出，任务会在已激活的包装器 `defer` 运行之后再清理原始函数的输入。
[规范 §7.6.6](k_spec.md#766-asynchronous-decorator-wrappers)

<a id="struct-enum-and-field-metadata"></a>
### 结构体、枚举与字段元数据

使用 `on struct`、`on enum` 或 `on field` 声明装饰器。将其应用写在类型声明之前或具名字段之前。这些应用会附着常量记录，并保留声明本身的身份、字段与布局。

```klang
decorator Table(name: const u8[:]) on struct;
decorator Topic(name: const u8[:]) on enum;
decorator Column(number: u8) on field;

@Table("packets")
struct Packet<T> {
    @Column(1) payload: T,
}

@Topic("events")
enum Event {
    Received { @Column(2) payload: i64 },
    Closed,
}

fn main() {
    let table = decorator_metadata<Table, Packet>();
    let topic = decorator_metadata<Topic, Event>();
    let first = decorator_metadata<Column, Packet<u32>>(payload);
    let second = decorator_metadata<Column, Event.Received>(payload);
    print(table.name, " ", topic.name, " ", first.number, " ", second.number);
}
```

```
packets events 1 2
```

泛型声明及其所有特化共享相同的元数据。类型别名会解析为同一声明。字段选择器是指定结构体或枚举变体中的一个名称，因此无需构造被标注类型的任何值即可完成查找。
[规范 §7.6.7](k_spec.md#767-struct-enum-and-field-metadata)

<a id="enumerating-decorated-declarations"></a>
### 枚举被装饰的声明

使用 `const for <Name, Declaration, Metadata> in decorated<D, namespace Scope>` 可以在某个命名空间中发现被装饰的函数和类型。`namespace ::` 选择全局命名空间。每次迭代会绑定限定名称、该声明及其类型化元数据记录。

```klang
decorator Route(path: const u8[:]) on fn(i64) -> i64;

@Route("/double")
fn service::double(value: i64) -> i64 { return 2 * value; }

fn main() {
    const for <Name, Handler, Metadata> in decorated<Route, namespace service> {
        print(Name, " ", Metadata.path, " ", Handler(21));
    }
}
```

```
service::double /double 42
```

每个匹配的重载都有各自独立的绑定。类型绑定指向原始名义声明，若为泛型则可接受类型实参。命名空间别名会选中其目标命名空间。该查询使用已加载到程序中的声明，若没有匹配项则会展开为零项。

搭配 `on field` 装饰器与具体结构体或枚举变体使用 `decorated<D, T>` 时，绑定会按字段声明顺序给出字段名称、其访问器以及元数据。访问器在成员访问或构造器标签中的用法与 `fields<T>` 完全相同。
[规范 §7.6.8](k_spec.md#768-enumerating-decorated-declarations)

---

<a id="19-recursion"></a>
## 19. 递归

递归需要显式启用并加以标注。每个递归环路的入口函数都须标记为 `recursive fn`，环路中的每个函数都须具备完全显式的签名，其核心限制如下：

> 环路中的任何函数，在其某个非定长局部变量处于作用域内时，都不得再次调用进入该环路。

可增长或可变大小的局部变量从声明处起直至其所属代码块结束，都会占用一个数据栈；若在递归调用时仍有此类变量处于作用域内，每次递归激活就需要各自独立的数据栈。因此，递归函数在跨越递归调用时不得拥有任何可增长数据，必须在调用后继续存活的数据须在环路外部持有并传入：可以是引用、切片、对象池，或包含若干表引用的结构体。在调用*之前*结束的代码块中声明的临时缓冲区不受此规则约束。同一规则也适用于调用语句的临时值：在进入环路的调用点上，不得有非定长的临时值处于存活状态，这排除了将 `str(…)` 作为调用参数，或将该调用置于 `print` 内部。这样一来，递归深度就仅使用原生调用栈，栈耗尽时会干净地中止程序。

```klang
struct Node { key: i32, left: Node&<u32>?, right: Node&<u32>? }

recursive fn show(n: Node&, depth: i64) {
    {                                      // scratch in a block that ends before the calls
        var line: u8[>..] = [];
        for i in depth { line.append("  "); }
        format(line, n.key);
        print(line);
    }
    let l = n.left;
    if l { show(l, depth + 1); }
    let r = n.right;
    if r { show(r, depth + 1); }
}

recursive fn count(n: Node?) -> i64 {
    guard n else { return 0; }
    return 1 + count(n.left) + count(n.right);
}

fn main() {
    var pool: Node[>..] = [];
    let a .= pool.push(Node { key: 1 });
    a.left .= pool.push(Node { key: 2 });
    a.right .= pool.push(Node { key: 3 });
    show(a, 0);
    print(count(a), " nodes");
}
```

```
1
  2
  3
3 nodes
```

<a id="sharing-scratch-across-the-recursion"></a>
### 在递归间共享临时空间

按引用传入递归环路的可增缩数组可以同时服务于所有激活帧：每一层先记录 `let mark = scratch.len`，在调用方数据的上方推入自己的条目，并在结束时调用 `scratch.resize(mark)`。此类调用是*平衡的*（见[第 13 节](#grow-shrink-arrays-keep-their-references-in-variables)），因此激活帧对自己那部分数据的视图在跨越这些调用后仍然可用。

```klang
recursive fn walk(scratch: i64[>..<]&, depth: i64) -> i64 {
    let mark = scratch.len;
    scratch.push(depth);
    let mine = scratch[mark..];
    var total = 0;
    if depth < 3 { total = walk(scratch, depth + 1); }
    total += mine[0];
    scratch.resize(mark);
    return total;
}

fn main() {
    var scratch: i64[>..<] = [];
    print(walk(scratch, 0), " ", scratch.len);
}
```

```
6 0
```

当程序的状态由若干张可增长表（节点、文本、栈）组成时，可将每张表作为一个非递归驱动函数的局部变量来持有，然后向递归函数传入一个包含这些表引用的结构体。或者，也可以将这些递归函数*嵌套*写在驱动函数内部，让它们像示例中的解析器那样直接作为自由变量访问这些表，完全无需结构体。

<a id="the-rules-in-detail"></a>
### 详细规则

* **签名。** 跨越环路时不推断任何返回值类型，也不得将任何返回值类型推断为引用。
* **参数。** 环路中的任何函数都不得拥有按值传递的非定长参数：须改为传递引用或切片。
* **存储。** 根源为环路函数自身局部变量的引用可以向下传递，但不得存储到任何地方或返回，因为同一个局部变量的不同激活帧无法区分。根源在环路之外的引用不受此限制：按引用传入的对象池、外层非递归函数的局部变量或全局变量均可。每次递归调用都必须像入口调用那样将该对象池继续传递下去。
* **返回值。** 环路函数可以返回递归调用的结果；此类返回值的根源会在环路上通过多轮迭代求得。
* **多态递归。** 递归调用可以用不同的类型实参实例化被调函数，但实例化数量必须是有限的：如果某次调用导致类型在每一轮中不断增长，一旦某个函数正在进行的特化数量将超过 16 个，就会被拒绝。

规范 §7.8。

---

<a id="20-long-distance-return-and-errors"></a>
## 20. 远距离返回与错误

K 语言没有异常机制，没有 `Result` 类型，也没有强制性的错误处理约定。对于只有两种结果的调用，末尾附带一个 `bool` 就足够了；对于“找到或未找到”，则使用 `T?`，它是指向输入的引用，因此不会产生任何拷贝。这两种都是普通的返回值（见[第 17 节](#results)）。第三种惯用法则用于处理嵌套调用深处发生、且应在单一位置统一处理的失败情况。

<a id="return--from"></a>
### `return … from`

`return E from f` 会将 `E` 作为当前最内层活跃的 `f` 调用的返回值，并展开其间的所有栈帧。中间的各层函数保持其原有的简洁签名：它们既不需要接收错误参数，也不需要在每次调用处写 `?` 或任何其他代码来传播错误。

```klang
struct User { name: u8[..16], age: i32 }

fn load(text: u8[:]) -> User[>..], u8[] {
    var users: User[>..] = [];
    var lineno = 0;
    each_split(text, '\n') {
        lineno++;
        if it.len > 0 { users.push(parse_record(it, lineno)); }
    };
    return users, "";
}

fn parse_record(line: u8[:], lineno: i64) -> User {
    let comma = find(line, ",");
    guard comma >= 0 else { return [], str("line ", lineno, ": expected name,age") from load; }
    let age, ok = parse_int(trim(line[comma + 1..]));
    guard ok else { return [], str("line ", lineno, ": bad age") from load; }
    return User { name: trim(line[..comma]), age: age as i32 };
}

fn report(text: u8[:]) {
    let users, err = load(text);
    if err.len > 0 { print("error: ", err); return; }
    print("loaded ", users.len, " users");
}

fn main() {
    report("ada, 36\nlinus, 20\n");
    report("ada, 36\nlinus\n");
    report("ada, x\n");
}
```

```
loaded 2 users
error: line 2: expected name,age
error: line 1: bad age
```

* **静态检查。** 每个包含 `return … from load` 的函数调用都必须位于 `load` 的调用之内，全程序编译器会按调用图顺序验证这一点，因此这类返回在运行时绝不会未被捕获。
* **它是隐藏的判别标志，而非栈展开器。** 路径上的每个栈帧在调用返回后检查一个标志，若处于传播路径中则立即返回；正常的尾声代码（epilogue）都会执行，因此每个数据栈都会自行恢复。这里没有展开表，也没有析构函数；沿途唯一会执行的代码是正在离开的代码块中的 `defer` 体（见[第 16 节](#defer)）。
* **值直接落入 `load` 调用方期望的位置。** `str(…)` 直接在 `load` 的返回目标位置构造消息，绕过正在展开的中间栈帧。
* `f` 在编写该返回语句的函数所在处解析：先查找作用域内的嵌套函数，否则按该名称的重载集解析，而 `from ns::parse` 则显式指定命名空间。仅是恰好同名的无关函数绝不会捕获该返回。
* 当前编译器只允许写在具名函数中的远距离返回携带指向全局变量和静态数据的引用；应如上例那样按值返回拥有的数据。

同一套机制也允许代码块通过高阶函数从其外层函数 `return`（见[第 18 节](#function-values)）。标准库的 `exception` 模块将其封装为 `exception::try<E>(body) { error => … }` 和 `exception::throw(value)`，并以 `defer` 实现 `finally`（见 [stdlib.md](stdlib.md#exception)）。规范 §7.9。

<a id="stopping-the-program"></a>
### 终止程序

`assert(c)` 在 `c` 不成立时中止程序，并会对传入的可选值进行类型收窄；`abort(msg)` 带着一条消息中止程序；`exit(code)` 以指定的退出码结束程序。后两者永不返回，检查器会利用这一性质（见[第 16 节](#functions-that-never-return)）。它们都不可被捕获。规范 §9.3。

---

<a id="21-program-structure"></a>
## 21. 程序结构

<a id="files-and-imports"></a>
### 文件与导入

程序从命令行指定的*根文件*开始整体编译。`import a.b.c;` 会将文件 `a/b/c.k` 引入一次。路径解析顺序如下：首先相对于导入文件所属包的根目录（对于程序自身的文件，即根文件所在目录），然后作为根文件所声明的某个包的模块，最后在标准库目录中查找。带前导点的 `import .a.b;` 则改为相对于当前导入文件进行解析。导入仅加载文件，别无他用：它既不会创建命名空间，也不会在文件声明的内容之外向作用域引入任何名称。标准库的模块按名称导入：`import std;`、`import dictionary;`。

<a id="packages"></a>
### 包

根文件中的 `import name from "source";` 会声明包 `name`（一个包含其入口文件 `name.k` 的目录）并导入该入口。`source` 可以是相对于根文件的目录（`"../mylib"`），也可以是指定标签或提交的 git 仓库（GitHub 上的 `"owner/repo@v1"`、`"host/owner/repo@ref"` 或 URL），编译器会获取并构建一次。只有根文件可以声明包。在任何其他文件（包括库自身的文件）中出现同一行，则构成一项必须由根文件满足的*依赖要求*，从而使程序的依赖关系保持扁平、版本固定且集中在一处。[packages.md](packages.md) 提供了完整细节，包括程序自带的 C 代码以及如何制作包。

<a id="namespaces"></a>
### 命名空间

放在文件顶部所有声明之前的 `namespace image;` 会将这些声明置于命名空间 `image` 中；没有该声明的文件会声明到全局命名空间，且多个文件可以共享同一个命名空间。在其他地方，这类声明写作 `image::Pixel`、`image::brightness(p)`、`image::Shape.Circle { r: 1.0 }`。`::name` 用于指代被命名空间声明遮蔽的全局声明，或内置函数。声明本身也可以自带命名空间，例如 `fn image::brightness(…)`，或在带命名空间的文件中写作 `fn ::main()`。

```klang
import std;
namespace geo;

struct Pt { x: i64, y: i64 }
fn norm1(p: Pt) -> i64 { abs(p.x) + abs(p.y) }     // std's abs, found globally
fn format(out: u8[>..]&, p: Pt) { format(out, "(", p.x, ",", p.y, ")"); }

fn ::main() {                                       // declared in the global namespace
    let p = geo::Pt { -3, 4 };
    print(p, " ", geo::norm1(p), " ", p.norm1());   // the format hook and UFCS follow the type
}
```

```
(-3,4) 7 7
```

未限定的名称首先按词法作用域解析（局部变量、类型参数、嵌套函数），然后是当前声明所在的命名空间，最后是全局命名空间和内置符号。函数名的重载集取自按此顺序最先声明了该名称的命名空间。重载集绝不会跨命名空间合并，因此命名空间内的 `hash` 重载要访问全局版本须写作 `::hash(x)`。UFCS 调用会越过词法作用域，优先在接收者结构体或枚举类型所在的命名空间中查找（只要该命名空间声明了该名称）。因此，对于 `geo::Pt`，无论在哪个命名空间（即使该命名空间有自己的 `norm1`），`p.norm1()` 都会找到 `geo::norm1`，泛型函数体中的 `x.norm1()` 也是如此；而普通调用 `norm1(p)` 则不会。`format` 钩子同样跟随类型：在任何地方 `print(p)` 都会为 `geo::Pt` 找到 `geo::format`。全局变量和函数可以同名，因为调用会越过变量去解析函数。命名空间仅影响名称解析、类型标识和生成的 C 名称；它没有访问控制（隐私性）、没有重导出，也没有命名空间嵌套。规范 §11.1，`docs/design/namespaces.md`。

放在文件声明之前的 `namespace g = k_gfx2d;` 允许该文件在任何限定名称中将 `k_gfx2d::Sprite` 简写为 `g::Sprite`。该别名仅在当前文件内有效，因此库的别名绝不会泄露到导入它的文件中，甚至可以占用另一个命名空间的名称而不影响任何其他文件。其目标必须是某个文件确实声明了的命名空间。报错信息和 `--dump` 输出仍使用目标名称：`k_gfx2d::Sprite`。

<a id="globals-and-initialization"></a>
### 全局变量与初始化

全局变量在 `main` 之前按文件内的声明顺序初始化，被导入的文件优先。因此，初始化表达式可以使用当前文件所导入模块中的全局变量，也可以调用函数。在某个全局变量的初始化表达式运行之前就引用该全局变量是错误的，无论是在更早的初始化表达式中，还是在该初始化表达式所调用的函数中。将值转换为全局数组的索引类型，或使用 `from_bytes` 验证此类数组时，都会读取该数组的长度，因此也算作引用该数组。整个程序运行在一个拥有所有全局变量的隐式最外层作用域内：它们是生命周期规则的最外层根源。全局可增长数组在程序的整个生命周期内拥有一个数据栈的底部，使其成为天然的全程序内存池（arena）。具有编译期初始化表达式的扁平定长类型 `const` 全局变量位于静态数据区，共享且只读。

```klang
var log: u8[>..];                        // a var with only a type: empty
let NAMES = ["ada", "grace"];            // static data
let FIRST = NAMES.len - 1;               // a named constant
var counts: i64[NAMES.len] = [0; NAMES.len];

fn main() {
    log.append("started");
    counts[FIRST] += 1;
    print(log, " ", NAMES[FIRST], " ", counts);
}
```

```
started grace [0, 1]
```

<a id="main"></a>
### `main`

根文件全局命名空间中的 `fn main() { }`（或 `async fn main() { }`）是程序入口点。独立脚本也可以省略 `fn main()`，直接在文件顶层书写语句；编译器会按源码顺序将其合成为隐式入口（支持直接使用顶级 `await` 与 `async [-> T] { ... }` 块，并会在退出前自动等待裸 `async { ... }` 语句完成）。在同一文件中同时混用顶级语句与显式 `fn main()` 会触发编译错误。

```klang
async fn fetch(v: i64) -> i64 {
    await yield_now();
    return v * 2;
}

async fn main() {
    let a = await fetch(21);
    let b = await async -> i64 {
        let step = await fetch(5);
        return step + 1;
    };
    print(a, " ", b);
}
```

```
42 11
```

被导入文件中的 `fn main` 会被完全忽略，因此可执行文件也能兼作库使用：只需定义 `fn main_x()` 和一个 `fn main() { main_x(); }` 包装器，导入方就可以调用 `main_x`。程序的命令行参数通过 `os` 模块获取（`args()`、`arg_count()`），`exit(code)` 则用于设置进程退出码。

<a id="embed_file"></a>
### `embed_file`

`embed_file("path")` 在*编译*程序时读取相对于发起调用的源文件路径下的文件，并将其字节作为 `const u8[:]` 放入静态数据区：例如字体、查找表或着色器。路径必须是字符串字面量或持有字符串字面量的 `let` 全局变量，每个不同的文件只读取一次。`embed_shader` 对 GLSL 执行相同操作，为图形模块编译着色器（见 [stdlib.md](stdlib.md#shaders)）。规范 §12。

<a id="src_file-and-src_line"></a>
### src_file 与 src_line

`src_file()` 返回包含该调用的源文件的基本文件名（base name），类型为 `const u8[:]`。`src_line()` 返回该调用所在的行号（从 1 开始）。两者都读取解析器已经打上的调用节点信息。默认参数在声明它的函数中解析，而省略该参数的调用点则报告调用方的位置，因此

```klang
fn info(text: u8[:], file: u8[:] = src_file(), row: i64 = src_line()) {}
```

看到的是调用方的文件和行号。调用方显式写入的参数则会被保留。在 `info` 函数体内部调用 `src_file()` 或 `src_line()` 会返回 `info` 自身所在的文件和该调用所在的行号。基本文件名指的是最后一段路径，而非中止时打印的完整路径。同名的局部变量会遮蔽该内置函数，正如局部变量会遮蔽任何函数一样。规范 §12，`docs/design/call_location.md`。

---

<a id="22-threads"></a>
## 22. 线程

K 语言没有共享可变内存。工作线程函数声明为 `thread_fn`，编译器会将其以及它所调用的一切编译为一个**独立程序**，拥有各自的数据栈和所使用全局变量的独立副本。这些副本在派生线程的那一刻从派生方程序复制而来。工作线程递增一个全局变量时递增的是它自己的副本，主线程的副本仍保持原值。因此不存在数据竞争，语言也没有锁、原子操作或内存序。

| 内置函数 | 含义 |
|---|---|
| `thread_spawn(worker, args…) -> i64` | 启动一个工作线程；返回其永不复用的 ID |
| `thread_spawn_contained(worker, args…) -> i64` | 启动一个受控工作线程，其中的中止仅结束该工作线程本身 |
| `thread_wait(id) -> bool` | 阻塞直至该工作线程结束且其存储被释放；若为受控线程且异常中止则返回 `false` |
| `thread_wait(id, msg) -> bool` | 与 `thread_wait(id)` 相同，且首次此类等待会将受控工作线程的中止消息追加到 `msg` |
| `hardware_threads() -> i64` | 合理的工作线程数量，绝不超过数据栈所允许的上限 |
| `qput(v) -> bool` | 将 `v` 放入其类型对应的队列；若队列已满则立即返回 `false` |
| `qget<T>() -> T` | 取出下一个 `T`，阻塞等待 |
| `qget<T>(ms) -> T, bool` | 同上，但最多等待 `ms` 毫秒；超时则返回 `T` 的零值和 `false` |
| `qpoll<T>() -> T, bool` | 若有则取出一个；否则返回 `T` 的零值和 `false` |
| `qlimit<T>(n)` | 将 `T` 类型队列的容量限制为最多 `n` 条消息 |

值通过**类型化队列**在程序之间传递，每种类型对应一个队列。任何跨越线程传递的数据（包括工作线程的参数）都必须是**扁平的**：在任何深度上都不包含引用或切片。这非常高效，因为扁平的 K 值是连续存放的，因此一个任务或结果只需一次拷贝即可携带完整数据，包括其中的可变长部分。`thread_fn` 的参数不能有默认值。

所用的队列取决于值自身所拥有的类型：例如像 `Shape.Circle { r: 1.0 }` 这样的变体字面量具有其变体自身的类型，而 `7` 是 `i64`，因此两者分别不会送达 `qget<Shape>()` 或 `qget<i32>()`。如果程序中没有任何 `qput` 发送某种类型，却对其调用 `qget` 或 `qpoll`，编译器会给出警告。

<!-- check-only -->
```klang
struct Job    { n: i32 }
struct Result { n: i32, digits: i32 }

fn count_digits(n: i32) -> i32 { var d: i32 = 1; var v = n; while v >= 10 { v /= 10; d++; } d }

thread_fn worker() {
    loop {
        let job = qget<Job>();                   // blocks on the queue for Job
        guard job.n >= 0 else { break; }         // -1 means stop
        qput(Result { n: job.n, digits: count_digits(job.n) });
    }
}

fn main() {
    let n = max(1, hardware_threads());
    for i in 8 { qput(Job { n: (1 << (i * 3)) as i32 }); }
    for i in n { qput(Job { n: -1 }); }          // one quit job per worker
    var ids: i64[>..] = [];
    for i in n { ids.push(thread_spawn(worker)); }
    var total = 0;
    for i in 8 { let r = qget<Result>(); total += r.digits; }
    for id in ids { thread_wait(id); }
    print(total, " digits in total");
}
```

在用 `qlimit` 设定限制之前，队列没有消息数量上限。一旦队列已满，`qput` 会直接返回 `false` 而不等待，这样服务端就可以直接拒绝工作，而不至于无限堆积队列。将 `qput(v);` 作为语句调用则会忽略返回值。要限制队列占用的内存上限，只需将其限制设为该内存上限除以单条典型消息的大小。

工作线程返回时，无论是否有人等待它，都会释放它所拥有的一切资源。已入队的消息的生命周期长于发送者。当 `main` 返回时，仍在运行的工作线程会被终止。在 `thread_fn` 可达的任何函数中，使用非扁平的全局变量都是错误的，除非它是一个所有引用都指向静态数据的 `let` 变量（就像字符串字面量那样）。包含线程的程序需要真正的 C 编译器（`k build`），因为进程内 TinyCC 后端无法放置线程局部存储（TLS）。规范 §11.2。

<a id="contained-workers"></a>
### 受控工作线程

使用 `thread_spawn_contained` 启动的工作线程中，若发生检查失败或调用 `abort(msg)`，仅会终止该工作线程本身，并像正常返回一样释放其所有资源。错误消息会发往等待方而非 stderr：对该工作线程的每次 `thread_wait` 都会返回 `false`，且首次 `thread_wait(id, msg)` 会将带有源码位置的中止消息追加到 `msg` 中。后续的等待将不再收到消息，因此只需获取一次并保存好即可。监管者可以记录该消息并启动另一个工作线程：

<!-- check-only -->
```klang
thread_fn serve(attempt: i64) {
    assert(attempt >= 2);                        // a bug the first two hit
    print("attempt ", attempt, " served");
}

fn main() {
    var attempt = 0;
    var why: u8[>..] = [];
    while !thread_wait(thread_spawn_contained(serve, attempt), why) {
        print("attempt ", attempt, " failed: ", why);
        why.clear();
        attempt++;
    }
}
```

栈溢出、`exit(code)` 以及主线程上的中止仍会结束整个程序。受控工作线程内部用 `thread_spawn` 启动的工作线程不受控。通过 `extern fn` 打开的文件和其他资源仍保持打开状态。规范 §11.2。

---

<a id="23-serialization"></a>
## 23. 序列化

链接为自相对偏移量的数据结构无论位于内存何处都具有相同的含义，因此保存它只需写出其字节，加载它只需读入字节。三个内置函数为任意数组提供了这一能力：

| 内置函数 | 含义 |
|---|---|
| `to_bytes(a) -> u8[>..]`、`to_bytes(a, out)` | 数组的带帧字节映像：先是一个 varint 字节计数，随后是其元素区域；返回新数组，或追加到 `out` |
| `bytes_of(a) -> const u8[:]` | 仅元素区域，作为只读视图：零拷贝 |
| `from_bytes<T[>..]>(b: u8[:]) -> T[>..], bool` | 从不受信任的映像构造经过**验证**的数组；若格式无效则返回 `false` 和空数组 |

来自程序外部的字节是唯一可能未经过编译器证明就引入引用的入口，因此 `from_bytes` 在将其变为值之前会先进行验证。它会检查帧格式、每个标签、每个长度、varint 是否为规范编码，以及每个链接是否都落在该同一映像的某个元素起始位置上。损坏或恶意构造的文件只会返回 `false`，绝不会产生野指针。它验证的是*安全性*，而非数据完整性：被篡改的数据字节仍可能描述一个格式良好的结构，添加校验和应由程序自行负责。

```klang
struct Node { word: u8[..8], count: i32, left: Node&<u32>?, right: Node&<u32>? }

fn insert(pool: Node[>..]&, word: u8[:]) {
    if pool.len == 0 { pool.push(Node { word: word, count: 1 }); return; }
    var cur .= pool[0];
    loop {
        let c = compare(word, cur.word[..]);
        if c == 0 { cur.count += 1; return; }
        let next = if c < 0 { cur.left } else { cur.right };
        if next { cur .= next; continue; }
        if c < 0 { cur.left .= pool.push(Node { word: word, count: 1 }); }
        else { cur.right .= pool.push(Node { word: word, count: 1 }); }
        return;
    }
}

fn lookup(pool: Node[>..]&, word: u8[:]) -> i32 {
    guard pool.len > 0 else { return 0; }
    var cur .= pool[0];
    loop {
        let c = compare(word, cur.word[..]);
        if c == 0 { return cur.count; }
        let next = if c < 0 { cur.left } else { cur.right };
        guard next else { return 0; }
        cur .= next;
    }
}

fn main() {
    var index: Node[>..] = [];
    each_word("the quick fox and the lazy dog and the fox") { insert(index, it); };
    var image: u8[>..] = index.to_bytes();          // a framed image of the array
    var loaded, ok = from_bytes<Node[>..]>(image);  // verified before it becomes a value
    print(ok, " ", loaded.len, " nodes, 'the' x", lookup(loaded, "the"), ", ",
          bytes_of(index).len, " bytes");
    image[image.len - 3] = 200;                     // tamper with a link ...
    var bad, bok = from_bytes<Node[>..]>(image);    // ... and it is false and an empty array
    print(bok, " ", bad.len);
}
```

```
true 6 nodes, 'the' x3, 126 bytes
false 0
```

还原回来的是普通数组：它可以被索引和增长，指向其中的引用也是普通引用。`from_bytes` 还可以构造 `T[>..<]` 和 `T[]` 系列数组，但元素中包含相对引用的 `T[>..<]` 除外：因为收缩结果会释放其自身链接仍在引用的内存。对于元素为索引类型的数组，它会对照目标数组检查索引合法性。指向元素内部*字段*的链接以及 `in pool` 链接不可序列化。`bytes_of` 视图永远不可写，因为通过它写入的字节可能是未经检查器证明的链接。字节格式规范见 `docs/design/serialization.md`。规范 §3.9、§9.4、§12。

---

<a id="24-calling-c-and-being-called-from-c"></a>
## 24. 调用 C 与被 C 调用

<a id="owning-c-handles"></a>
### 拥有型 C 句柄

`own Handle` 承担着释放一个定长 C 句柄记录的义务。它使用与 `Handle` 相同的 C ABI 布局。声明为 `own` 的外部函数返回值会引入该义务，而声明为 `own` 的外部函数参数则会接管该义务。`borrow handle: Handle` 声明了一个外部函数参数，允许其在调用期间检视一个拥有型句柄。C 函数必须遵守该生命周期约束。

<!-- fragment -->
```klang
struct Handle { token: u64 }

extern fn acquire() -> own Handle;
extern fn read(borrow handle: Handle) -> i32;
extern fn release(handle: own Handle);

fn open_reader() {
    let handle = acquire();
    return fn[handle: own Handle]() -> i32 {
        defer release(handle);
        return read(handle);
    };
}

fn main() {
    let read_once = open_reader();
    print(read_once());
}
```

绑定、返回、传递和捕获拥有型值都会转移其所有权。编译器会检查每条可达的退出路径是否都妥善履行了释放义务，包括可能通过调用展开的路径。释放该值的 `defer` 会为该作用域的所有退出路径认领该义务。拥有资源的闭包在调用时会发生移动，从而将其捕获的值转移进调用栈帧。[规范 §9.6](k_spec.md#96-owning-c-handles)

<a id="extern-fn"></a>
### `extern fn`

不带函数体的声明会将一个 K 签名绑定到 C 函数。在原本写函数体的位置，写在 `=` 后的字符串即为 C 符号名；若省略，则直接使用 K 函数名。旧式写法 `extern "cbrt" fn cube_root(...)`（字符串紧跟在 `extern` 后）已弃用并会触发警告。此类声明处于顶层，具有完整类型且非泛型，并与普通函数一样参与重载解析。调用它会直接编译为一次 C 函数调用。

```klang
struct Stats { lo: i32, hi: i32, mean: f64 }

extern fn cube_root(x: f64) -> f64 = "cbrt";    // libm, by its C name
extern fn hypot(x: f64, y: f64) -> f64;
extern fn crc32_bytes(s: const u8[:]) -> u32;   // a slice crosses as { data, len }
extern fn stats_of(xs: i32[:], out: Stats&);    // a struct filled through a pointer
extern fn c_version(out: u8[>..]&);             // a builder C appends to
```

能够跨越边界的类型恰好就是那些具有纯 C 形态的类型：

* 整数、浮点数标量以及 `bool`；
* 任意扁平定长值（结构体、定长和静态容量数组、定长模式枚举），按值以其紧凑 C 类型传递；
* 指向上述 `T` 的 `T&`，作为指针传递；
* 上述 `T` 的切片 `T[:]`，作为 `{ data, len }` 结构体传递；
* 上述 `T` 的 `T[>..]&`，作为构造器传入，C 代码可向其中写入和追加完整元素（`k_bld_append` 可向 `u8[>..]&` 追加字节）。

返回值只能是一个标量或扁平定长值，或者无返回值。其他所有类型（`varint`、结构体内的引用、按值传递的可变或可调整大小的值、可选值）都会在声明处被拒绝。参数的可写性与类型声明一致，因此只读实参需要将其声明为 `const`。C 代码不得在调用结束后继续保留借用的参数。程序自身的 C 代码放在 `foo.k` 旁的配套文件 `foo.build` 中（`c_sources = ["impl.c"];`）。在进程内运行的程序（`k run`）仅链接到编译器自身，编译器提供了两种宽度的 C99 `<math.h>` 函数（除 `nexttoward` 外的所有函数），除此之外几乎没有其他 C 标准库函数；使用其他 C 库函数需要通过 `k build`、程序自带的 C 代码或 `extern "lib"` 块（见 [command_line.md](command_line.md#2-running-a-program)）。通过该配套文件中的 `c_headers` 或 `--include header.h` 提供的头文件可以直接使用 K 生成的类型；参见[在 C 头文件中使用 K 类型](packages.md#using-k-types-in-a-c-header)。

<a id="extern-lib----a-shared-library-bound-by-name"></a>
### `extern "lib" { … }`：按名称绑定的共享库

`extern` 块指定一个共享库并声明在程序启动时从中绑定的函数，无需编译或链接任何 C 代码。程序会打开该库并查找符号；若库或符号缺失，则会发生运行时错误，并指出尝试查找的名称。成员是省略了 `extern` 关键字的 `extern fn`；若有 `=` 后的字符串，则为其 C 符号名。

```klang
extern "SDL2" {
    fn SDL_Init(flags: u32) -> i32;
    fn SDL_Quit();
    fn SDL_GetTicks64() -> u64;
    fn delay(ms: u32) = "SDL_Delay";
}
```

`"SDL2"` 会按加载器的搜索路径打开 `SDL2.dll`、`libSDL2.dylib` 或 `libSDL2.so`。`"bin/steam_api64"` 则在程序资源目录（`resource_dir()`）下打开该文件，并对其最后一个路径段应用相同的命名规则；程序正是通过这种方式随附分发共享库。带有版本号后缀的名称（如 `"libSDL2-2.0.so.0"`）按原样使用。因平台或安装环境而异的库名应写在配套配置文件中，而非源码中：

<!-- fragment -->
```klang
// myprog.build
dylibs_linux = ["SDL2=libSDL2-2.0.so.0"];     // the soname every distro installs
dylibs_windows = ["SDL2=bin/SDL2.dll"];       // shipped with the program
```

包的配套配置文件将其自身的名称映射作为默认值，而程序的配套配置文件则按平台逐一覆盖其中提到的名称。

由于中间没有适配层，每个成员都须用 K 的内存形态来声明该库的真实 C 原型：

* **标量**对应其 C 类型：`i32` 为 `int`，`u32` 为 `unsigned`，`i64` 为 `int64_t`，`f32` 为 `float`，`f64` 为 `double`，`bool` 为单字节，`size_t` 为 `u64`。C 的 `enum`（包括 `SDL_bool`）在 C 中是 `int`，因此应声明为 `i32`，而非 `bool`。C 的 `long` 在 Linux 和 macOS 上是 `i64`，而在 Windows 上是 `i32`，因此包含 `long` 的函数需要按平台分别声明或编写适配层。
* **不透明句柄**，例如库返回并传回的 `SDL_Window *`，表示为 `u64`：K 仅传递该令牌，无法窥探其内部。
* **指向 K 所拥有数据的指针**写作 `T&`（其中 `T` 为扁平定长类型）：`fn SDL_GetWindowSize(w: u64, x: i32&, y: i32&)`。当 C 需要 `(ptr, count)` 时，分别将 `xs[0]` 和 `xs.len` 作为两个参数传入；当 C 需要以 NUL 结尾的字符串时，在末尾加上 `\0` 并按引用传入第一个字节：

  ```klang
  extern "SDL2" {
      fn SDL_CreateWindow(title: const u8&, x: i32, y: i32, w: i32, h: i32, flags: u32) -> u64;
  }
  let title = "K\0";
  let win = SDL_CreateWindow(title[0], 100, 100, 640, 480, 0);
  ```

* **结构体**按 K 的紧凑布局跨边界传递，仅当字段本来就处于 C 所放置的偏移位置时才与 C 布局一致。`SDL_Rect { int x, y, w, h }` 与 `struct Rect { x: i32, y: i32, w: i32, h: i32 }` 匹配。放在 `i32` 前面的 `u8` 需要在 C 进行对齐填充的位置显式写出 `pad 3`；而 C 会向上对齐总大小的结构体（如 `{ a: i32, b: u8 }`），若要放入供 C 索引的数组中，则须在末尾加上 `pad 3`。带有 `pad` 的结构体只能按引用传递。若按值传递，其填充字节会按照调用约定被实际传递，而 C 的填充字节根本不会被传递，因此 `{ x: f32, pad 4, y: f64 }` 会进入错误的寄存器。按引用传递结构体参数 `const Rect&` 时需要一个变量，因为字面量不能作为引用目标：应写作 `let r = Rect { 0, 0, 10, 10 }; SDL_HasIntersection(r, s)`，而不能直接将字面量写在调用中。
* **库所拥有的内存**，例如库返回的 `const char *` 或库持有的数组，同样作为 `u64` 返回。`ffi::cstr(p)` 和 `ffi::bytes(p, n)`（见 [stdlib.md](stdlib.md#ffi)）会将其中的数据拷贝到 K 的存储中：`print(ffi::cstr(SDL_GetError()))`。
* **仍需适配层处理的情况**：回调函数（库期望的函数指针）和可变参数函数完全无法直接绑定。两者都需要在原生部分编写几行 C 代码作为桥接。

只有被调用路径触及的成员才会被绑定，而没有任何成员被调用的库绝不会被打开，因此一个代码块可以声明比单个程序实际用到的更宽泛的接口。唯一依赖的 C 代码仅为此类共享库的程序，即使未安装 C 工具链，也能在进程内后端下运行。规范 §7.10，[packages.md](packages.md#the-native-part)。

<a id="export-fn"></a>
### `export fn`

`export fn` 使一个 K 函数可以被 C 调用：编译器会以该函数名或指定的字符串名称生成一个 C 包装函数。该函数必须位于顶层且非泛型，具有显式参数类型，且最多有一个带类型的返回值。边界处不支持默认参数、`return … from`、对象池参数和构造器。标量和扁平定长值按值返回，而 `const u8[:]` 返回值则会通过返回指针外加一个额外的 `int64_t *` 长度参数返回（字节不以 NUL 结尾）。

```klang
export fn hello_world() -> const u8[:] { return "Hello, world!" }
export "c_add" fn add(a: i32, b: i32) -> i32 { a + b }

fn main() { print(hello_world(), " ", add(20, 22)); }     // runs once, inside k_init
```

```
Hello, world! 42
```

`k --standalone --header hello.h -o hello.c hello.k` 会为 C 宿主程序构建对应的 C 代码。生成的 C 代码本身不含 `main`，并附带一个声明了 `k_init(argc, argv)` 的头文件，宿主程序在调用任何其他导出函数之前须先调用一次该函数。`k_init` 会初始化运行时和全局变量并运行 K 的 `main`（如果存在），因此库通常将启动代码放在 `main` 中。导出的函数并非线程安全的：必须在调用 `k_init` 的同一线程上调用它们。只能由宿主在调用 K 的间隙调用导出函数：被 K 代码调用的 C 代码在其自身返回前又回调导出函数（例如工具包的事件处理器或 `qsort` 比较器）属于运行时错误。当 K 和 C 必须交替执行时，应让 C 返回到 K，而非由 C 回调 K。`k_exit()` 会结束程序并释放启动时建立的资源，从而使同一进程能够再次调用 `k_init` 来重启它或启动它的另一个构建版本；当程序的某个工作线程仍在运行时调用 `k_exit()` 属于运行时错误。`k build --shared --header hello.h hello.k` 会直接将相同的 C 代码编译为共享库（`libhello.so`、`libhello.dylib` 或 `hello.dll`），并在源码旁生成头文件（见 [command_line.md §3](command_line.md#3-building-an-executable)）。规范 §7.11。

---

<a id="25-builtins"></a>
## 25. 内置函数

这些是语言本身提供的函数。其余功能均位于
标准库中，使用 K 语言编写于 `stdlib/` 目录下，并在
[stdlib.md](stdlib.md) 中进行了文档说明。

| 内置函数 | 含义 | 章节 |
|---|---|---|
| `print(a, b, …)` | 将各参数的文本与换行符作为一次写入输出 | [6](#rendering-text) |
| `str(a, b, …) -> u8[>..]` | 将文本作为新字符串，直接构建于目标位置 | [6](#rendering-text) |
| `format(out, a, b, …)` | 将文本追加到 `out`（任意可增长的 `u8` 数组） | [6](#rendering-text) |
| `assert(c)` | 若 `c` 不成立则中止程序；`c` 可为 `bool` 或可选类型（并将其收窄） | [20](#stopping-the-program) |
| `abort(msg)`, `exit(code)` | 终止程序；两者均不返回 | [20](#stopping-the-program) |
| `copy(x) -> T` | 创建 `x` 所指存储值的一份新副本 | [4](#copies-are-visible) |
| `default<T>() -> T` | 定长类型 `T` 在写入任何内容前的初始值，并应用已声明的默认值 | [8](#field-defaults) |
| `hardware_threads()`, `thread_spawn(worker, args…)`, `thread_spawn_contained(worker, args…)`, `thread_wait(id)`, `thread_wait(id, msg)` | 工作线程 | [22](#22-threads) |
| `qput(v)`, `qget<T>()`, `qpoll<T>()`, `qlimit<T>(n)` | 类型化队列 | [22](#22-threads) |
| `to_bytes(a)`, `to_bytes(a, out)`, `bytes_of(a)`, `from_bytes<T[>..]>(b)` | 序列化 | [23](#23-serialization) |
| `embed_file(path)`, `embed_shader(…)` | 在编译期读取的文件字节或已编译的着色器 | [21](#embed_file) |
| `src_file()`, `src_line()` | 调用处的文件名与行号；作为默认参数时，报告省略该参数的调用处 | [21](#src_file-and-src_line) |

数组成员 `.len`、`.cap`、`.push`、`.append`、`.pop`、`.pop_n`、
`.resize`、`.clear`、`.index_of`、`.range_index`、`.check_length` 和 `.move_from` 见
[第 5 节](#members)。标准库池 API 以及已弃用的内置操作
`.alloc_index`、`.alloc_ref`、`.free`、`.alloc_slice`、`.realloc_slice` 和
`.free_slice` 见 [第 14 节](#14-reusable-pools)。
规范 §12。

---

<a id="appendix-a-keywords-and-operators"></a>
## 附录 A. 关键字与运算符

**关键字**：`import namespace struct enum type fn thread_fn export simd
recursive let var extern const pad reusable if else while for in loop
block guard defer match return break continue as true false null self bool
varint i8 i16 i32 i64 u8 u16 u32 u64 f32 f64`。

**上下文关键字**（在其它任何地方均为普通标识符）：`from`
（位于 `return` 值之后）、`upto`（位于类型中的无符号整数类型之后）、
`using`（作用域资源绑定与清理块）、`errdefer`（仅在错误展开时执行的清理语句或代码块）、
`async`（`async fn` 声明与 `async { … }` 任务块）、`await`（等待并恢复异步任务）、
`inline` 与 `checkfree`（位于 `fn`、`recursive`、`simd` 或 `thread_fn` 之前）、
`deprecated`（位于其他函数限定符之前，可选地后接一个字符串）、
`fields`、`variants` 与 `types`（位于 `const for` 头部中）、
`has_default` 与 `same`（位于 `const if` 条件中）。

**运算符**（按优先级从高到低排列；优先级与 Rust 相同）：

| 优先级 | 运算符 |
|---|---|
| 后缀 | `f(…)`, `a[i]`, `a[x..y]`, `.field`, `?.field`, `x as T`, `x as! T` |
| 一元 | `-`, `!`, `~`, `&` |
| 乘除 | `*`, `/`, `%` |
| 加减 | `+`, `-` |
| 移位 | `<<`, `>>` |
| 按位与 | `&` |
| 按位异或 | `^` |
| 按位或 | `\|` |
| 比较 | `<`, `<=`, `>`, `>=` |
| 相等性 | `==`, `!=`, `.==`, `.!=` |
| 逻辑与 | `&&` |
| 逻辑或 | `\|\|` |
| 空合并 | `??` |

赋值语句：`=`、`.=`，以及所有二元运算符的复合赋值形式
（`+=`、`<<=`、`&=` 等）；作用于整数左值的 `++` 与 `--`。范围表达式
`a..b` 仅出现在切片方括号、`for` 头部以及 `match` 分支中。
规范 §6.1，附录 D。

<a id="appendix-b-glossary"></a>
## 附录 B. 术语表

* **平衡调用（Balanced call）**：在调用期间数组长度绝不会小于调用开始时的长度，因此调用前获取的视图在调用后依然有效。§13。
* **候选根（Candidate）**：自身存储空间可能容纳从容器中读出的引用目标的变量；此类引用会以它的候选根为根。§12。
* **分支函数（Case function）**：为每个变体类型提供一个重载，并通过枚举进行调用；相当于作为重载集的 `match`。§9。
* **数据栈（Data stack）**：带有一个递增指针（bump pointer）的大块地址空间保留区；所有非定长值都存放在这里。§4。
* **精确根（Exact root）**：自身存储空间直接持有引用目标的根，相对于仅限定其生命周期的根而言。§12。
* **定长、可变长、可调整大小（Fixed, variable, resizable）**：三种大小类别：常量大小、构造时确定的大小、大小可发生改变。§4。
* **扁平（Flat）**：在任意深度都不包含引用、切片或相对引用；能够跨线程传递的数据类型。§4、§22。
* **函数值（Function value）**：作为泛型参数传递的具名函数或代码块；它是编译期实体，绝非运行时数据。§18。
* **只增数组、可增可缩数组（Grow-only, grow-shrink）**：`T[>..]`（仅在没有任何指向其内部的指针时才会收缩）与 `T[>..<]`（其内部引用只能保存在变量中）。§5、§13。
* **持有者（Holder）**：包含引用的值：例如带有引用字段的结构体、切片数组。§12。
* **索引类型、上界类型（Index type, bound type）**：`uN in a` 表示严格小于 `a.len` 的整数；`uN upto a` 表示至多为 `a.len` 的整数。§15。
* **受限数组（Limited array）**：`T[..k]` 或 `T[..]`，长度限定在内联容量之内。§5。
* **具名常量（Named constant）**：带有常量初始化式的无类型 `let` 或 `const` 全局常量，用法与字面量相同。§2。
* **类型收窄（Narrowing）**：在 `if`、`while`、`guard` 或 `assert` 之后，可选类型被收窄为普通引用的作用域区域。§10。
* **对象池（Pool）**：用于存放链式数据或可独立复用数据的存储结构。标准库的 `slot_pool` 和 `slice_pool` 将只增数组与复用元数据捆绑在一起；语言层面的 `reusable` 修饰符已弃用。§11、§14。
* **相对引用（Relative reference）**：以窄偏移量存储的链接，可以是自相对的，也可以相对于具名池的基址计算。§11。
* **根（Root）**：限定引用目标生命周期的变量；由编译器推断，从不显式写出。§12。
* **顺序数组（Sequential array）**：元素大小各异的数组，可以迭代遍历但不能按索引随机访问。§5。
* **槽位（Slot）**：具有声明类型的位置（字段、元素、全局变量、带类型注解的变量），决定了它所持有的内容是否只读。§12。
* **临时值（Temporary）**：生命周期持续到所属语句结束的值：字面量、函数调用结果、`copy` 结果或控制流结构的求值结果。§12。
* **透明性（Transparent）**：引用的特性，指它在所有值上下文中表现得与它的目标值完全一致。§10。
* **可变模式、定长模式（Variable mode, fixed mode）**：枚举的两种存储形式：精确按当前变体大小存储（`Shape..`），或按标签加上足以容纳最大变体的空间存储（`Shape`）。§9。
