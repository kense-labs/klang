<a id="k-by-example"></a>
# K 语言示例教程

<details>
<summary>目录</summary>

- [1. 核心思想](#1-the-core-idea)
- [2. 运行程序](#2-running-something)
- [3. 熟悉的语法](#3-familiar-syntax)
- [4. 值的存储位置](#4-where-values-live)
- [5. 数组家族](#5-the-array-family)
- [6. 字符串即字节数组](#6-strings-are-byte-arrays)
- [7. 永不悬垂的切片](#7-slices-that-cannot-dangle)
- [8. 嵌套数据保持内联](#8-nested-data-stays-inline)
- [9. 两种尺寸的枚举](#9-enums-in-two-sizes)
- [10. 不用指针链接对象](#10-linking-things-without-pointers)
- [11. 非嵌套的生命周期](#11-lifetimes-that-do-not-nest)
- [12. 错误处理与 `return … from`](#12-errors-and-return--from)
- [13. 泛型与会消失的代码块](#13-generics-and-blocks-that-disappear)
- [14. 不共享数据的线程](#14-threads-that-share-nothing)
- [15. 以字节形式保存和加载数据](#15-saving-and-loading-data-as-bytes)
- [16. 调用 C 代码](#16-calling-c)
- [17. 关于性能](#17-about-the-speed)
- [18. 局限性](#18-limitations)
- [19. 从 C 调用 K](#19-calling-k-from-c)
- [20. 延伸阅读](#20-where-to-go-next)

</details>

本教程通过示例介绍 K 语言。它假设你具备 C、C++、Rust、Go 或类似语言的使用经验，并重点介绍 K 与它们的不同之处。建议从这里开始，随后阅读按主题全面涵盖整门语言的[语言参考](reference.md)。如需查看精确规则，请参阅[语言规范](k_spec.md)；完整程序示例请见 [`samples/`](samples.md)。

K 的大部分语法都很熟悉：花括号、`let`、`fn`、`struct` 和 `match`。主要区别在于数组如何存储和管理数据。我们先从值的存储位置讲起，因为这解释了该语言的许多其他设计选择。

---

<a id="1-the-core-idea"></a>
## 1. 核心思想

下面是一个字符串数组。

```klang
var words: u8[][>..] = [];
for i in 5 { words.push(str("word", i)); }
```

在 C++ 中，这是一个 `vector<string>`：vector 本身需要一次堆分配，每个字符串各需要一次堆分配，每个元素还要带一个指针和容量，并在离开作用域时遍历释放所有元素。在 Rust 中，这是一个具有相同结构的 `Vec<String>`。

而在 K 中，它是**一块连续的字节**：先是一个长度，然后是五个首尾相接的 `(length, characters)` 序列。这块内存通过递增一个指针（bump pointer）创建，当外层作用域结束、该指针回退时，它就会直接消失。不需要堆分配器，也不需要逐个释放字符串，而且这块内存中不包含任何指针。

```
words:  05 00 00 00 | 05 w o r d 0 | 05 w o r d 1 | 05 w o r d 2 | ...
```

该内存模型有四个主要特性：

* 内存由屈指可数的几个大型**数据栈（data stack）**组成。增长只是一次指针递增。
* 每个动态值都**由一个变量拥有**。唯一的释放方式是作用域结束时重置指针。
* 值采用**内联（inline）**嵌套。数组中的结构体里的字符串，就是字节里的字节里的字节。
* 你仍然可以获取**指向正在增长的容器的引用**，并且编译器能证明它们永不悬垂，而无需 `'a` 这类标注，也不需要 `Rc`。

这一模型有其局限性。它们与强加这些限制的规则以及相应的应对方法一并列于[第 18 节](#18-limitations)。它也带来了可量化的收益：在 16 项基准测试中，K 的运行速度约为**地道 C++ 的 3.3 倍**，与手工调优的 C++ 和最佳安全 Rust 大致持平，同时内存占用比 C++ **少 1.9 倍**，比 Rust 少 1.2 倍（[`bench/summary.md`](bench/summary.md)）。这些提升大多源于数据布局和内存管理的差异。

---

<a id="2-running-something"></a>
## 2. 运行程序

K 将整个程序编译为 C 代码，并调用手头可用的任何 C 编译器，将其构建为源文件旁的可执行文件：

```bash
k build hello.k && ./hello
```

`k -o hello.c hello.k` 会写出 C 代码供你自行编译，`k build --dry-run hello.k` 则会打印执行的命令。[command_line.md](command_line.md) 介绍了 `k build` 的其余选项以及其他命令，[packages.md](packages.md) 则说明了程序如何使用来自其他仓库的库。

内置的 TinyCC 后端无需单独安装 C 编译器。不带子命令，或使用 `k run` 时，程序会被编译进编译器自身进程并在其中运行，这正是你阅读本教程时最方便的方式：

```bash
k hello.k
```

下面是一个最简程序：

```klang
fn main() {
    print("hello, klang");
}
```

短小脚本也可以省略 `fn main()` 直接编写顶级语句（包括顶级 `await`）；显式入口点也可以写为 `async fn main()`（详见[语言参考 §17.9、§21](reference.md#21-program-structure)）。

`print` 接收任意数量、任意类型的参数，将每个参数渲染为文本并作为单行输出。它使用单次 `write` 调用，因此两个线程的输出绝不会交错。没有格式字符串，也没有 `<<`。

```klang
print("answer=", 42, " ratio=", 0.75, " yes=", 3 > 2);
```

不仅是标量，每个值都可以被打印：数组打印为 `[1, 2, 3]`，结构体打印为 `Point { 1, 2 }`，`u8` 数组则按其字节内容打印。示例代码中频繁使用 `print`，因为打印是检查值的便捷方式。

---

<a id="3-familiar-syntax"></a>
## 3. 熟悉的语法

`let` 创建不可重新赋值的绑定；`var` 允许重新赋值。两者都不会使内容变成只读：需要只读请使用 `const`。类型跟在名称后面，通常可以自动推导：

```klang
let answer = 42;
var count: i32 = 0;
let ratio = 0.75;
count += 1;
count++;              // 一条语句，而非表达式
```

结构体字面量既可以显式指定字段名，也可以按顺序列出字段，并且可以省略声明了默认值的字段。以 `..` 结尾的字面量可以省略任意字段：没有声明默认值的字段会获得其类型的默认值（零、空数组、空字符串或 null）。全局变量的声明方式与局部变量相同，只给出类型的 `var` 全局变量在初始时就具有这些默认值：

```klang
struct Unit { kind: i32, hp: i32 = 100, x: f32, y: f32, name: u8[] }

let scout = Unit { kind: 2, x: 1.5, .. };   // hp 为 100，y 为 0，name 为 ""
let elite = Unit { hp: 200, ..scout };      // 从 scout 复制其余定长字段
let { kind, hp } = elite;                   // 结构体字段解构绑定
var selected: Unit;                         // kind 为 0，hp 为 100，x 为 0，y 为 0，name 为 ""
var log: u8[][>..];                         // 空数组
```

函数调用也支持具名实参（`limit(250, hi: 200)`），可选值支持可选链 `?.` 与空合并 `??`（`node?.parent?.tag ?? 0`），且在目标枚举类型已知时支持前导点变体简写 `.Variant`。

函数都是自由函数。没有方法，也没有 `impl` 块，但 `x.f(a)` 会调用 `f(x, a)`，因此只要读起来更顺畅，你就可以使用类似方法调用的语法：

```klang
fn total(xs: i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    return t;
}
```

`if`、`match`、`block` 和 `{ }` 都是**表达式**：代码块的值是它末尾的表达式。因此没有三元运算符，也无需先声明一个变量再在每个分支里赋值：

```klang
let kind  = if hp > 50 { "healthy" } else { "hurt" };
let level = match hp { 0 => "dead", 1..50 => "low", _ => "fine" };
```

`guard` 声明了其后整个代码块所依赖的条件。`guard c;` 仅在 `c` 成立时继续执行代码块剩余部分，而 `guard c else { … }` 会在不成立时转而执行 `else` 块。若想跳出当前代码块之外的层级，只需在该 `else` 块中放入 `return`、`break` 或 `continue`。`block` 能在没有循环的情况下为 `break` 提供目标，从而让一连串检查可以带值提前退出：

```klang
let verdict = block {
    guard p.alive else { break "gone"; }
    guard p.hp > 0 else { break "dead"; }
    "standing"
};
```

`defer` 会在外层代码块结束时运行一条语句或一个代码块，无论以何种方式结束：正常到达块尾、`break`、`continue`、`return`，还是穿过该块的 `return … from`。清理逻辑可以紧挨着它要清理的资源编写，并在后续代码未提及的退出路径上依然执行：

```klang
guard gfx::open("demo", 640, 480, 0) else { return; }
defer gfx::close();                 // 从此处起离开本块的所有路径都会执行
```

循环体中的 `defer` 会在每次迭代结束时执行，而退出时只会执行已经历过的 `defer`，按后进先出（最晚注册的先执行）的顺序运行。`defer` 代码块内部不能向外跳转，也不能使用长距离返回（`return … from`），因为它可能正是在另一次长距离返回经过时执行的。仅在错误展开路径清理时可使用 `errdefer`，搭配 `using let h = ...;` 或 `using (let h = ...) { ... }` 可在作用域结束时自动调用 `.close()` / `release()`（详见[语言参考 §16.4](reference.md#16-expressions-and-control-flow)）。

循环包括 `for x in range_or_array`、`while` 和 `loop`：

```klang
for i in 3 { }              // 0, 1, 2
for i in 10..13 { }         // 10, 11, 12
for i: i32 in 0..n { }      // i 为 i32；n 必须能拓宽为 i32
for i: u8 in 0..256 { }     // 遍历全部 u8 值
for x in xs { }             // 对定长元素进行拷贝
for &x in xs { x *= 2; }    // 引用：用于修改元素的写法
for x, i in xs { }          // 同时获取索引
```

多返回值是一种调用约定，而不是元组类型（K 没有元组类型）。按惯例，末尾的 `bool` 表示其余返回值是否有意义：

```klang
fn divide(a: i64, b: i64) -> i64, bool {
    if b == 0 { return 0, false; }
    return a / b, true;
}

let q, ok = divide(7, 2);
```

若要将结果赋值给已存在的变量、字段或元素，请使用 `=`：`q, ok = divide(9, 3);`。

只有 `return` 和函数调用的返回值才能携带多个值，因此分支不能以 `a, b` 结尾。`return if c { 1, 2 } else { 3, 4 };` 是错误的；相反，应当像 `divide` 中那样由每个分支自行返回。

末尾参数可以有默认值，调用时可以省略：

```klang
fn limit(x: i64, lo: i64 = 0, hi: i64 = 100) -> i64 {
    if x < lo { lo } else if x > hi { hi } else { x }
}

print(limit(250), " ", limit(-5, -10));   // 100 -5
```

每次调用省略了某参数时，其默认值都会重新求值。默认值可以引用顶层声明所能引用的同等功能（全局变量、常量和函数），但绝不能引用调用方的变量，即使名称相同也不行。

有三条算术规则需要特别注意：

**算术运算按操作数自身的位宽进行。**`u8 + u8` 是 8 位加法。较窄的类型会隐式拓宽为较宽的类型；若不使用 `as`（在 debug 模式下检查范围）或 `as!`（从不检查），则不会发生任何窄化转换。`u32 + i32` 会引发编译错误并询问你的意图；语言不会静默地将两者都提升为 64 位。操作由所声明的位宽决定。

整数会隐式转换为浮点数，因此当 `x: f32` 时，`n * x` 是 `f32` 乘法；而浮点数只能通过 `as` 转为整数。浮点字面量本身没有固定位宽，由浮点字面量和整数组成的表达式也是如此：`n * 0.5` 在传入 `f32` 参数时按 `f32` 计算，而在 `let h = n * 0.5;` 中则是 `f64`。若 `if` 或 `match` 的所有分支都是字面量，它会像字面量一样适配上下文。如果某个分支是字面量，它会采用其他分支的类型：当 `x: f32` 时，`if big { x } else { 0.5 }` 的类型为 `f32`。

不会改变任何东西的类型转换会触发警告，因此编译器会指出被这些规则省去的多余转换。对于 `b: u8`，`let w: i64 = b as i64;` 会给出警告 ``redundant `as i64`: a u8 converts to i64 implicitly here``。而用于选定类型或运算位宽的转换，如 `let id = b as i32;` 或 `(a as i64) * b`，则不是多余的，不会触发警告。

**`%` 是欧几里得取模（Euclidean）。** 对于每一种整数类型，其结果都在 `[0, |b|)` 范围内且永不为负。因此对于*任意* `x`，`x % n` 都是长度至少为 `n` 的数组的有效索引，编译器从而可以省去边界检查，而不必强求你加上明知多余的守卫判断。

```klang
print(-7 % 3, " ", 7 % -3);       // 2 1
```

整数 `/` 向零截断：`-7 / 3` 的结果是 `-2`。对于负被除数，它并不与欧几里得 `%` 配对。导入 `std` 可使用与 `/` 配对的余数函数 `rem(a, b)`，以及与 `%` 配对的商函数 `div_euclid(a, b)`。例如，在宽度为 10 的瓦片网格中，坐标 `-1` 所在的瓦片为 `div_euclid(-1, 10) == -1`，瓦片内偏移为 `-1 % 10 == 9`。

**无符号算术在所有构建模式下都会回绕（wrap）**，这是语言定义所决定的，因为哈希和伪随机数生成器（PRNG）所需的正是回绕的无符号算术。有符号溢出在 debug 模式下会中止程序，在 release 模式下则会回绕。

---

<a id="4-where-values-live"></a>
## 4. 值的存储位置

一个 K 程序拥有原生调用栈、一些静态数据以及 **N 个数据栈**，其中 N 是编译器为你的程序计算出的一个小整数。数据栈是一段带有递增指针（bump pointer）的数 GB 级保留地址空间。除此之外没有其他内存系统：没有 `malloc`、没有空闲链表、没有 GC、没有引用计数，也没有析构函数。

因此每个动态值都由一个变量拥有，使这套机制得以成立的规则是：

1. 值按生命周期顺序压入栈，并在所属作用域退出时**批量**弹出。
2. 每个数据栈上同时最多只有一个*可调整大小（resizable）*的值处于存活状态，且它在其整个生命周期内都位于该栈的顶端。因此扩容只需递增指针而无需容量检查，且该值**永不移动**。

编译器会在静态阶段证明上述全部条件。除了递增指针本身，没有任何运行时簿记开销。

“每个栈一个可调整大小的值”听起来似乎很苛刻，但别忘了“N 个数据栈”是复数：同时存活的三个可增长数组会分别获得三个栈，一旦某个作用域释放了一个栈，它就会被立即复用。N 取决于程序最深调用点处的同时需求，在编译期一次性算好。未使用的栈仅占用保留的虚拟地址空间。你永远无需手写这些内容：语言中没有任何涉及数据栈的语法。

<a id="leaving-a-scope-frees-its-data"></a>
### 离开作用域即释放其数据

```klang
for round in 3 {
    var scratch: u8[>..] = [];
    for i in 100000 { scratch.push((i % 256) as! u8); }
    print("round ", round, ": ", scratch.len, " bytes of scratch");
}
```

每次迭代都会获得一块全新的 100 KB 缓冲区，到达右花括号时，栈顶就会回退到原位。没有析构函数运行，也没有链表需要遍历，因此“释放”这块缓冲区只需一次存储写入指令。

<a id="nothing-moves-so-references-survive-growth"></a>
### 永不移动，因此引用在扩容后依然有效

```klang
struct Item { id: i32, weight: f32 }

var items: Item[>..] = [];
let first .= items.push(Item { 1, 0.5 });          // 指向第 0 个元素的引用
for i in 2..1000001 { items.push(Item { i as i32, 0.0 }); }
first.weight = 99.5;                               // 一百万次 push 之后依然有效
print(items.len, " items, first is ", items[0]);
```

```
1000000 items, first is Item { 1, 99.5 }
```

`vector<T>` 和 `Vec<T>` 都无法做出这种保证：两者都会重新分配内存，从而使指向内部元素的引用失效。正是这一特性，使得大量 K 代码直接持有带类型的引用，而等效的 Rust 代码往往得在 arena 中保存 `u32` 索引——这也是 K 在基准测试中获得大幅性能优势的重要原因。

`push` 会返回指向它刚创建的元素的引用，这是将正在构建的数据连接起来的惯用方式。

<a id="copies-are-visible"></a>
### 拷贝是显式可见的

定长值（标量、结构体、定长数组、引用、切片）在赋值时会像 C 一样静默拷贝。**非定长值绝不会隐式拷贝**，因为这种拷贝的开销是 O(size)，应当让你明确看到：

```klang
var a: i64[>..] = [1, 2, 3];
var b = copy(a);      // 一次显式、可见的 O(n) 拷贝
b.push(4);
```

如果不写 `copy`，`var b = a;` 会将 `b` 按引用绑定到 `a`。函数调用也是如此：无论是声明为 `fn f(xs: i64[:])`、`fn f(xs: i64[>..]&)` 还是无类型的 `fn f(xs)`，调用 `f(xs)` 时都会按引用传递可增长数组，而不会拷贝其元素。需要自己那份副本的函数需显式调用 `copy`。

---

<a id="5-the-array-family"></a>
## 5. 数组家族

K 没有单一的“数组类型”。它有好几种数组类型，它们之间唯一的区别在于大小能否变化以及如何变化。

| 类型 | 含义 | 元数据 |
|---|---|---|
| `T[k]` | 固定大小，编译期已知 | 无 |
| `T[]` | 构造时确定大小，此后冻结不变 | 一个长度（默认为 `u32`） |
| `T[varint]` | 同上，但长度采用 [`varint`](#8-nested-data-stays-inline) | 不超过 127 个元素占 1 字节，不超过 16383 个占 2 字节 |
| `T[..k]` | 预留 `k` 个元素的空间，实际存放 0 到 `k` 个 | 一个长度，取能容纳 `k` 的最小无符号类型 |
| `T[..]` | 同上，`k` 在构造时决定 | 一个容量和一个长度 |
| `T[>..]` | *可调整大小*，**只增不减**：主力类型 | 一个长度，由所有者保存 |
| `T[>..<]` | *可调整大小*，**可增可减**：栈、队列、堆 | 一个长度，由所有者保存 |

除最后两种可调整大小的数组外，其余类型都以 `[metadata][elements...]` 的形式存储，直接内联在容纳它们的载体（变量、结构体字段、另一个数组的元素）中且紧凑排列，没有指向独立元素块的指针。`T[..k]` 无论位于何处，都会预留足量的 `k` 个元素空间。它的长度采用能计数到 `k` 的最小无符号类型：不超过 255 用 `u8`，不超过 65535 用 `u16`。这并不限制 `k` 的大小：`u8[..300]` 的长度类型是 `u16`。

而可调整大小数组的元素则位于数据栈顶端（第 4 节），在那里可以自由增长，由拥有它的变量保存当前长度。

```klang
var fixed: i32[4] = [1, 2, 3, 4];
let zeros: f64[8] = [0.0; 8];      // 填充写法
let frozen: i64[] = [7, 8, 9];     // 构造一次定型，永不改变

var grow: i64[>..] = [];           // arena、构建器、对象池、树形存储
for i in 5 { grow.push(i * i); }

var stack: i64[>..<] = [];         // 支持弹出的那种
stack.push(1); stack.push(2); stack.push(3);
let top = stack.pop();
stack.pop_n(2);                    // 一次丢弃末尾两个元素

var small: u8[..8] = [];           // 可内联存放在任何定长值能放的地方
small.push('h'); small.push('i');
print(small.len, " of ", small.cap);
```

```
2 of 8
```

有些地方的工作方式与 `std::vector` 或 `Vec` 不同：

* **长度与容量。** `.len` 是数组当前保存的元素个数，类似 `size()`。有限容量数组 `[..k]` 和 `[..]` 还有 `.cap`，类似 `capacity()`，只不过它的容量永不改变：超出容量继续 push 会直接中止程序，而不是重新分配。可调整大小的数组没有 `.cap`。它们会一直增长，直到所属数据栈的保留地址空间耗尽为止。
* **内存对齐。** 不会插入任何填充字节。1 字节长度后紧跟 `i64` 元素会导致这些元素未对齐，而 K 的读写操作会妥善处理这种情况——这在 x86-64 和 ARM64 上几乎没有额外开销。必须匹配外部内存布局的结构体可添加显式的 `pad` 字段（[规范 §3.2](k_spec.md#32-structs)）。
* **定型可调整大小的数组。** 编译器不会自行推断某个数组已经不再增长：可调整大小的数组会一直占用它的数据栈，直到所有者作用域结束。由于其他值会使用别的数据栈，并没有别的数据等着压在它上面。若希望构建完成的数据结构紧凑且不受下面提到的“每栈一个”规则约束，可以将其拷贝进 `T[]`：`let done: i64[] = copy(grow);`。

各类型适用场景：

* **`[>..]` 是默认首选。** 用于 arena、对象池、字符串构建器、树形存储，以及在各阶段之间反复清空重用的临时缓冲区。“只增不减”保证了指向数组内部的引用始终有效。只要编译器能证明没有任何东西指向它，你依然可以使用 `pop` 和 `clear`。
* **`[>..<]`** 用于栈或优先队列。其限制在于，指向它的引用只能保存在变量中，绝不能存入数据结构中，以便编译器在检查缩容操作时能够追踪这些引用。
* **`[..k]` / `[..]`** 用于数组必须直接内联在其他数据结构内部的情况，例如结构体里的 16 字节名称，或数组元素中的小型列表。
* **`[]` / `[varint]`** 用于构建完成的数据：记录中的字符串、对象池中的节点，以及任何一次构建后只供读取的数据。

只有 `[>..]` 和 `[>..<]` 属于“可调整大小”，且“每个栈一个”的规则也仅适用于这两种类型。其余类型可以放在任何地方。

---

<a id="6-strings-are-byte-arrays"></a>
## 6. 字符串即字节数组

K 没有专门的字符串类型。字符串就是任何以 `u8` 为元素的数组家族类型，你选用哪一种就表达了你的用途：

* `u8[>..]` —— 构建器。追加内容只需递增指针。
* `u8[]` / `u8[varint]` —— 成品字符串，内联存储在容纳它的地方。
* `u8[..16]` —— 位于结构体内部的短字符串，无任何间接寻址。
* `u8[:]` —— 指向他人字节数据的*视图*。字符串字面量就是这种类型，具体来说是 `const u8[:]`，因为字面量存放在只读静态数据区。

```klang
var s: u8[>..] = [];
s.append("hello");
s.push(',');
format(s, " world! ", 6 * 7);       // format 可将任意值文本追加到目标
print(s, " [", s.len, " bytes]");
```

```
hello, world! 42 [16 bytes]
```

跨越多行的文本写作 `"""` 字符串。它是原始字符串，不进行转义，并且每一行都会剥离与末尾 `"""` 相同的缩进，因此多行字符串可以按周围代码的缩进对齐书写：

```klang
print("""
    usage: wc [-l] file
      -l   count lines only
    """);
```

```
usage: wc [-l] file
  -l   count lines only
```

`str(...)` 会将格式化结果*直接就地构建*到目标位置的新字符串中：

```klang
words.push(str("word", i));         // 直接写入新元素的位置
```

这里不会先生成临时字符串再拷贝进数组。零拷贝构造保证（[规范 §4.3](k_spec.md#43-the-in-place-construction-guarantee)）规定，被构造的值总是直接构建在其最终位置（该位置会沿调用链向下传递），且编译器不得退回到“先在别处构建再拷贝”的做法。

切分文本不会产生任何拷贝，因为切片只是一个指向已存在字节的指针和长度计数：

```klang
let line = "  key = some value  ";
let body = trim(line);
let eq   = find(body, "=");
print("'", trim(body[..eq]), "' -> '", trim(body[eq + 1..]), "'");
```

```
'key' -> 'some value'
```

`split` 会返回一个**指向输入文本的切片数组**。每一段只占几个字节，不拷贝任何文本，而大多数语言在此都会返回一串新分配的字符串列表：

```klang
let parts = split("alice,30,paris", ',');
print(parts.len, " parts: ", parts);      // 3 parts: ["alice", "30", "paris"]
```

`words` 则按连续空白符切分，并丢弃空片段，这正是解析命令行所需要的行为：

```klang
let cmd = words("  go   north ");
print(cmd.len, " words: ", cmd);          // 2 words: ["go", "north"]
```

结构体也可以内联持有字符串，因此这种结构体的数组就是一整块连续内存，你可以像普通数据一样对它排序和拷贝：

```klang
struct Person { name: u8[..16], age: i32 }

// 定义 `format` 重载可让 print/str/format 按你的方式渲染 Person。
fn format(out: u8[>..]&, p: Person) { format(out, p.name, " (", p.age, ")"); }

var people: Person[>..] = [];
people.push(Person { name: "ada", age: 36 });
people.push(Person { name: "grace", age: 45 });
var p = Person { name: "", age: 20 };
format(p.name, "linus");        // 在容量上限内，有限数组同样可用作构建器
people.push(p);
print(people);
```

```
[ada (36), grace (45), linus (20)]
```

如果一个值可能是若干字符串之一，它就会是指向最终所选那个字符串的视图。当其中一个分支是切片（例如字符串字面量）时，给出内联字符串字段的另一分支会直接产生指向整个该字段的切片，不产生任何拷贝，整个 `if` 表达式的类型为 `const u8[:]`：

```klang
let who = if people.len > 3 { "a crowd" } else { people[1].name };
print(who);                             // grace
```

---

<a id="7-slices-that-cannot-dangle"></a>
## 7. 永不悬垂的切片

`T[:]` 是一个引用加上一个元素计数，类似于 C++ 的 `span`/`string_view` 或 Rust 的 `&[T]`。它能让函数处理任意数组类型中的一段范围，而无需拷贝其元素。

```klang
fn total(xs: i64[:]) -> i64 { var t = 0; for x in xs { t += x; } return t; }

total(fixed);         // T[k]
total(grow);          // T[>..]
total(grow[1..4]);    // 子范围
total(grow[^2..]);    // ^k 表示从末尾倒数
```

不仅仅是函数参数，任何切片类型的目标位置都能以这种方式接收数组：变量、结构体字段、数组元素或返回值。该切片就像 `fixed[..]` 一样直接观察数组本身，并且当数组本身是 `const` 时，切片也是 `const`：

```klang
let view: const i64[:] = fixed;   // fixed 本身，而非拷贝
fixed[0] = 99;
print(view[0]);                   // 99
```

与 `string_view` 的区别在于，这里的切片是经过静态检查的。每个引用和切片都携带一个静态的**根（root）**：即界定其所指目标生命周期的那个变量。整套生命周期系统只由一条规则构成：

> 引用不得比拥有其目标的变量活得更久，且绝不能以错误的类型观察其目标。

**整门语言中没有任何生命周期标注。** 根由编译器自动推导，函数则会按不同的根进行单态化（monomorphization）。通过这种单态化，你无需书写任何生命周期语法即可获得 Rust 生命周期般的精确度。同样也没有别名（aliasing）或排他性（exclusivity）规则：允许两个引用指向同一事物，因为在没有共享内存并发的情况下，别名本身不会破坏 K 的类型安全。检查器追踪的是生命周期和合法访问；它不要求引用具有排他性。

生命周期错误会同时指明引用的根及其目标去向：

```klang
var line: u8[>..] = [];
// ...
let w = line[..5];
print(w);
line.clear();        // 合法：w 的最后一次使用在 clear 之前
```

每轮迭代都会被重新填充的临时缓冲区可以自由地向外分发自身的切片。可复用的临时缓冲区与允许被外部指向的数据结构拥有完全相同的类型。

正因为切片从不拷贝，它天然适合作为*键*。标准库中以 `u8[:]` 为键的字典不存储任何字符串内容，只保存指向你已有文本的地址和长度：

```klang
let text = "the quick brown fox jumps over the lazy dog the fox";
var counts = dictionary<const u8[:], i32> {};
each_split(text, ' ') { counts.update(it, 0) { it += 1; }; };
print(counts.count, " distinct words");
```

整套词频统计就这么简单，没有任何拷贝发生。整个文件只需一次读入单个缓冲区，而每一个键、表格中的每一行以及最终报告中的每一行都直接指向那块缓冲区（[`15_word_freq`](../../samples/15_word_freq.k) 对整本书做了这件事）。

`const` 限定符描述的是可写性。可写性是推导而非强制标注的：字符串字面量是只读的，而对 `var` 取 `&x` 则是可写的。*槽位（slot）*（字段、元素或全局变量）必须说明它持有哪一种。`dictionary<const u8[:], i32>` 表示“这些键可以是只读视图”，从而允许字面量和其他只读视图作为键。函数参数对常量性是泛型的，因此你几乎永远不需要写 `const`，除非是在必须接收只读数据的槽位上，或者在你希望从文档角度明确标明只读的参数上。

---

<a id="8-nested-data-stays-inline"></a>
## 8. 嵌套数据保持内联

结构体可以包含变长部分，且它们按声明顺序**内联**排列。将其与 `varint` 字段（一种 LEB128 整数，数值需要多少字节就占多少字节）结合起来，一条记录就成了一段没有任何间接寻址的连续字节：

```klang
struct Item  { sku: u8[varint], qty: varint, cents: varint }
struct Order { id: varint, customer: u8[varint], items: Item[varint] }
```

在大多数语言中，`Order` 是一个拥有 `String` 和 `Vec<Item>` 的结构体，而每个 `Item` 又各自拥有一个 `String`。在 K 中，一个订单就是它的头部紧跟着它的各条订单项，首尾相连地排成一排；而整本订单簿就是由这类订单组成的单个数组。

```klang
// "SKU:qty:cents;..." -> items，直接构建到接收它的字段中
fn parse_items(field: u8[:]) -> Item[>..] {
    var items: Item[>..] = [];
    each_split(field, ';') {
        let p = split(it, ':');
        let qty, ok1 = parse_int(p[1]);
        let cents, ok2 = parse_int(p[2]);
        assert(ok1 && ok2);
        items.push(Item { sku: p[0], qty: qty, cents: cents });
    };
    return items;
}

var book: Order[>..] = [];
book.push(Order { id: 1001, customer: "alice",
                  items: parse_items("SKU-441:2:1999;SKU-7:1:500") });

for o in book {
    var t = 0;
    for it in o.items { t += it.qty * it.cents; }
    print(o.customer, " owes ", t, " cents over ", o.items.len, " items");
}
```

```
alice owes 4498 cents over 2 items
```

算一算字节数很有价值，因为真实程序里可能有数百万条这样的记录。上面这条订单（id 为 1001、客户 `alice`，以及两个分别带 5 字符和 7 字符 SKU 的订单项）的占用如下：

| | 字节数 | 分配次数 |
|---|---:|---:|
| K（按上述声明） | **29** | **0** |
| C++ `std::string` + `std::vector<Item>` | 160 | 1 |
| Rust `String` + `Vec<Item>` | 153 | 4 |

C++ 和 Rust 的数值是在 MSVC 和 rustc 下测得的 `sizeof` 加上实际分配的内存块大小；其中尚未计入分配器自身的每块头部和对齐取整开销，否则还会更多。C++ 在这里之所以只需要 1 次分配，仅仅是因为这三个字符串都短到足以触发短字符串优化（SSO）；只要某个 SKU 再多一个字符，C++ 同样需要 4 次分配。

更紧凑的布局减少了遍历订单簿时的缓存流量。而且 `id`、`qty` 和 `cents` 各自只占 1 个字节而非 8 个字节，因为 `varint` 只占用其数值所需的字节宽度。

还要注意，`parse_items` 按值返回了一个可增长数组，却没有任何拷贝：被调用方在编译时就已经知道目标位置，因此会直接把订单项写入 `book` 里的订单字段中。在 K 中，返回一个构建好的值与通过输出参数写入的开销完全相同，这也是代码中极少出现输出参数的原因。

变长元素的代价是数组会变成**顺序访问（sequential）**的：元素偏移量依赖于数据本身，因此你可以遍历它，但不能按索引访问它。

```klang
for o in book { print(o); }     // 合法
print(book[0]);                 // 错误：变长元素的数组
                                // 不能索引，只能遍历
```

对于需要遍历的数据（如记录、解析节点、日志行），可以使用变长元素。对于需要索引和排序的数据，则使用定长元素；若元素需要字符串，可用 `u8[..k]`。当两者都需要时，可以另建一个引用数组作为索引：你可以对这些引用进行索引和排序，而变长元素本身则原地紧凑保持不动。第 10 节末尾有一个相应的示例。

---

<a id="9-enums-in-two-sizes"></a>
## 9. 两种尺寸的枚举

代数数据类型（ADT）是这门语言中唯一的动态多态机制。
K 没有继承、trait 或虚函数表（vtable）。

```klang
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }
```

与众不同的是，每个 ADT 类型都可以用**两种方式**存储，并在使用时选择：

**固定模式（Fixed mode）**（`Shape`）与 Rust 的 `enum` 和 C++ 的 `variant` 相同：一个标签加上最大负载所需的空间，在此处每个值（包括 `Dot`）都占 1 + 16 = 17 字节。作为回报，固定模式值的数组可以建立索引，并且你可以用不同的变体就地覆盖某个元素。

```klang
var shapes: Shape[>..] = [];
shapes.push(Shape.Circle { r: 1.0 });
shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
shapes.push(Shape.Dot);
shapes[2] = Shape.Rect { w: 1.0, h: 1.0 };      // 就地替换为另一个变体
```

**可变模式（Variable mode）**（`Shape..`，注意末尾的两个点）为每个值只分配其自身变体所需的精确大小：`Dot` 占 1 字节，`Circle` 占 9 字节，`Rect` 占 17 字节。缺点是元素彼此紧挨着紧密排列，因此数组只能顺序访问（除非你在旁边单独维护一个索引，见[第 10 节](#an-index-for-variable-size-elements)），并且一个值永远不能更改其变体。作为回报，你可以获取指向负载*内部*的引用：

```klang
var packed: Shape..[>..] = [];
packed.push(Shape.Circle { r: 1.0 });
packed.push(Shape.Dot);
packed.push(Shape.Rect { w: 2.0, h: 3.0 });

for s in packed {
    match s {
        Rect &r => { r.w += 1.0; },     // 在数组内部就地修改负载
        _ => {},
    }
}
```

这两种模式提供了不同的保证。固定模式的值可以被另一个变体覆盖，因此不允许有任何东西指向其内部。可变模式的值可以被指向内部，因此它永远不能被覆盖。这保证了两种模式都是类型安全的，也是为什么 `match` 中的 `&` 绑定器只对可变模式的负载合法。

对于主要由小节点构成的树来说，内存差异非常巨大。这是基准测试中节省内存的最大单一来源：`records` 基准测试的内存用量比 Rust `enum`（其中每个元素都按其 `String` 变体的大小分配）少 4.0 倍，并且出于同样的原因，运行速度也快 2.0–2.3 倍，因为缓存的工作量更少了。

<a id="match-and-match-written-as-functions"></a>
### `match`，以及写成函数形式的 match

内联形式正如你所期望的那样：

```klang
fn name(s: Shape) -> u8[:] {
    match s {
        Circle c => "circle",
        Rect r => if r.w == r.h { "square" } else { "rectangle" },
        _ => "other",
    }
}
```

一个分支可以列出多个变体，例如 `Circle, Dot => "round"`。这样的分支不绑定负载，因为每个变体的负载都是各自独立的类型。

对整数进行 `match` 时支持字面量、半开区间以及命名常量（`let` 全局常量，或来自其他命名空间的 `ns::NAME`），因此程序自己的状态码也可以按名称出现在 `match` 中。一个分支可以列出多个此类项。待匹配表达式（scrutinee）的类型决定了裸名称是变体还是常量：

```klang
let GROUND = 0;
let SHALLOW = 1;
let DEEP = 4;
let LAVA = 7;

fn terrain(t: i64) -> u8[:] {
    match t {
        GROUND => "ground",
        SHALLOW..DEEP => "water",
        LAVA, 10..12 => "hot",
        _ => "rock",
    }
}
```

另一种形式是**分派函数（case functions）**：为每个变体编写一个重载，然后用枚举值来调用。这就是无需虚函数表的虚调用惯用法：

```klang
fn area(s: Shape.Circle) -> f64 { 3.14159 * s.r * s.r }
fn area(s: Shape.Rect) -> f64   { s.w * s.h }
fn area(s: Shape.Dot) -> f64    { 0.0 }

for s in shapes { print(s, " area ", area(s)); }
```

该调用会通过跳转表按标签进行分派，并且会像 `match` 一样检查穷尽性：如果你添加了一个新变体，这段代码就会停止编译，直到新变体也有了对应的函数实现。变体类型（`Shape.Circle`）是你可以命名和四处传递的普通类型。

---

<a id="10-linking-things-without-pointers"></a>
## 10. 不使用指针的链接方式

`T&` 是一个机器地址。它占 8 字节，不允许算术运算或类型转换，并且永远不会悬垂。对于包含大量链接的数据结构来说，每个链接占 8 字节往往超出你的需求，而且地址也无法直接保存到文件中。

因此 K 还提供了**相对引用（relative references）**，将链接存储为更窄的偏移量：

* `T&<u32>` —— *自相对（self-relative）*：从该字段自身到目标的偏移量，目标必须位于同一个外层数组中。这类链接与位置无关，因此由它们构成的结构可以整体重定位。第 15 节将解释这如何支持序列化。
* `T&<u32 in pool>` —— *池相对（pool-relative）*：相对于某个具名全局池基址的偏移量。存储它只需减去一个已在寄存器中的基址。与自相对链接不同，这类链接允许其他数组持有指向该池*内部*的链接。

偏移量宽度可以是 `u8`、`u16`、`u32`、`u64` 或 `varint`。读取这两种形式中的任意一种都会得到一个普通的 `T&`，而将 `T&` 存入其中时会重新将其编码。`T&<u32>?` 使用偏移量 0 表示空（null），因此一个链接连同其空值表示一共只占 4 字节。

下面是一个存储在只增数组中的二叉搜索树，使用 4 字节自相对子节点链接：

```klang
struct Node { key: i32, count: i32 = 1, left: Node&<u32>?, right: Node&<u32>? }

fn insert(pool: Node[>..]&, key: i32) {
    if pool.len == 0 { pool.push(Node { key: key }); return; }
    var cur .= pool[0];                  // .= 绑定元素；= 会复制元素
    loop {
        if key == cur.key { cur.count += 1; return; }
        if key < cur.key {
            let l = cur.left;
            if l { cur .= l; continue; }             // 经 `if` 窄化后：l 的类型为 Node&
            cur.left .= pool.push(Node { key: key });   // push 不会移动 cur
            return;
        }
        let r = cur.right;
        if r { cur .= r; continue; }
        cur.right .= pool.push(Node { key: key });
        return;
    }
}
```

这个例子用到了三项引用特性：

**`.=` 用于绑定和重新绑定引用。** 引用是*透明的*：表示引用的表达式在任何地方都表现得像它的目标一样，没有 `*` 也没有 `->`。因此必须有一种方式来表达“我指的是引用本身”，`.=` 就是做这件事的。`var cur .= pool[0];` 将 `cur` 绑定到该元素；`cur .= l;` 将其重新指向新的目标。（`.==` 是对应的同一性测试：`==` 比较两个节点的值，而 `.==` 询问它们是否是同一个节点。）

**可空类型是可为空的引用，通过控制流进行窄化。** `Node?` 是可空的 `Node&`：空值就是地址 0，因此不占用额外空间。在经过 `if`、`while`、`guard` 或 `assert` 将其窄化之前你无法使用它，而在窄化后的作用域内，它*就是*一个 `Node&`。

**插入操作只是一次存储。** `cur.left .= pool.push(...)` 压入节点并写入偏移量，而且由于没有任何东西发生移动，`push` 不会使 `cur` 失效。在安全 Rust 中，到了这一步你就得改用 `Vec<Node>` 加 `u32` 索引了。

该节点占 16 字节：两个 `i32` 和两个 4 字节链接，而由 `Box<Node>` 或 `unique_ptr<Node>` 构成的树每个节点需要 24 字节的节点体加上一个分配头。随机插入一千次的结果如下：

```
442 nodes, 7072 bytes of tree, no allocator involved
```

<a id="self-and-sentinels"></a>
### `self` 与哨兵节点

非可空相对引用没有空值，因此一个链接指回自身的结构原本永远无法被构造出来，因为当时还没有可以指向的目标。`self` 的含义是“当前字面量正在构造的这个值”，且它仅能合法地作为此类字段的完整初始化表达式：

```klang
import slot_pool;

struct Node { val: i64, prev: Node&<u32 in pool.items>, next: Node&<u32 in pool.items> }

var pool = slot_pool<Node> {};
let head .= pool.alloc_with() { Node { val: 0, prev: self, next: self } };
```

这就是一个循环双向链表的哨兵节点，在 `main` 运行之前就已经分配好了。正因为哨兵存在，链表中的任何链接都永远不会为空。每一次插入和删除都是完全相同的四次存储，没有任何特殊情况；每一次读取链接也都是一次普通的加法，而不是加法外加一次空值检查。`alloc_with` 会在运行构造块之前先选好槽位，因此 `self` 指代位于最终位置上的该节点。对于链接已经指向现有元素的节点，使用普通带值参数的 `alloc_ref` 即可：

```klang
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
```

一个节点占 8 + 4 + 4 = 16 字节，而在 C++ 版本的同一结构中，每个节点是一个 `std::list` 节点外加一个 map 节点。这一差异正是 `lru` 基准测试内存用量减少 3.2 倍的主要原因。

<a id="an-index-for-variable-size-elements"></a>
### 为变长元素建立索引

第 8 节说过，变长元素的数组可以遍历但不能直接索引：每个元素从哪里开始取决于它前面的每一个元素。既然其他数组可以持有指向池中的链接，你就可以自己用第二个数组来维护索引，在压入元素的同时，在索引数组中为每个元素压入一个 4 字节链接：

```klang
enum Mark {
    Circle { r: f64 },
    Rect { w: f64, h: f64 },
    Label { text: u8[varint] },
    Dot,
}

var marks: Mark..[>..] = [];
var mark_at: (Mark..&<u32 in marks>)[>..] = [];

fn main() {
    mark_at.push(marks.push(Mark.Label { text: "hello, klang" }));
    mark_at.push(marks.push(Mark.Circle { r: 1.5 }));
    mark_at.push(marks.push(Mark.Dot));
    mark_at.push(marks.push(Mark.Rect { w: 2.0, h: 3.0 }));
    mark_at.push(marks.push(Mark.Label { text: "x" }));
    for i in [3, 0, 4, 1, 2] {
        match mark_at[i] {
            Circle c => print(i, ": circle ", c.r),
            Rect r => print(i, ": rect ", r.w, " by ", r.h),
            Label l => print(i, ": label \"", l.text, "\""),
            Dot => print(i, ": dot"),
        }
    }
}
```

```
3: rect 2 by 3
0: label "hello, klang"
4: label "x"
1: circle 1.5
2: dot
```

`push` 会在 `marks` 的末尾就地构造每个标记并返回指向它的引用。当该引用被存入 `mark_at` 时，它就变成了该标记相对于 `marks` 基址的偏移量。此后 `mark_at[i]` 只是一次加载和一次加法，因此可以按任意顺序访问这些标记。

看看 `marks` 里存了什么。一个 `Label` 由它的标签、一个 1 字节长度以及它的文本组成，全都内嵌在元素内部：带有 `hello, klang` 的元素占 14 字节，而下一个标记就从这段文本结束的地方开始。这五个标记连同它们的字符串一共只占用一块 44 字节的连续内存，没有任何指针，也没有任何堆分配。Rust 或 C++ 中的枚举只有一种固定大小，因此字符串变体会持有一个固定大小的字符串对象，把放不下的文本存到别处去，而且每个元素都和最大变体一样大。在这里，每个值都精确等于其内容的长度，而索引仍然只需每个元素 4 字节就能访问其中任意一个。

`in` 偏移量所参照的基址数组必须是全局变量。函数局部数组也可以用普通引用 `(Mark..&)[>..]` 以同样的方式建立索引，每个条目占 8 字节。[`05_shapes`](../../samples/05_shapes.k) 就以这个例子结尾。

---

<a id="11-lifetimes-that-do-not-nest"></a>
## 11. 不嵌套的生命周期

有些数据的生命周期并不嵌套：例如缓存条目、可变图节点，或按任意顺序删除的文件。K 通过标准库的 `slot_pool<T>` 来支持这类场景：它是一个根结构体，捆绑了一个 `items` 数组和一个空闲列表（freelist）。将其所有者声明为局部变量或全局变量，并以 `slot_pool<T>&` 传递给函数。

```klang
import slot_pool;
struct Item { id: i32, weight: f32 }

var pool = slot_pool<Item> {};

let s0 = pool.alloc_index(Item { 10, 1.0 });
let s1 = pool.alloc_index(Item { 11, 1.0 });
pool.free(s0);
let s2 = pool.alloc_index(Item { 12, 2.0 });   // 重新拿回槽位 s0
```

如果有空闲槽位，`alloc_index` / `alloc_ref` 会取用一个空闲槽位；否则压入一个新元素。`free(i)` 会把一个槽位记录下来以供复用。这听起来像是个分配器，因此有必要精确说明 `free` 做什么、不做什么：

> **所有元素在任何时候都保持有效。** `free` 不释放任何内存，也不结束任何生命周期；它只是把一个索引添加到空闲列表中。之后该槽位仍然是一个存活的、类型完备的 `Item`，它依然属于这个池，而这个池依然属于它的宿主作用域。

槽位复用保留了内存安全与类型安全。指向一个已被释放并复用的槽位的引用，读到的是*另一个 `Item`*：它本身是有效的，但不是你心里想的那个。这与“保存了指向某个随后已被覆盖的数组的索引”属于同一类错误：一个逻辑 bug，而且你可以在局部范围内对其推理。内存绝不会被按非写入时的类型访问，也不会有任何东西越界，因为根本没有任何内存被释放。

在同一个池内，程序独立于作用域之外来管理槽位的复用。归还一个槽位之后，就应当停止使用指向其旧内容的引用：它们依然是内存安全的，但可能会读到被替换进去的新值。

在这样的池上运行前面的链表代码会得到：

```
built: 10 20 30 40 50   (6 slots)
evens removed: 10 30 50   (6 slots)
two more, both from the freelist: 10 30 50 7 8   (6 slots)
```

[`slice_pool<T>`](stdlib.md#slice_pool) 模块分配的不是单个槽位，而是元素的*连续片段（runs）*——`alloc_slice(n)`、`realloc_slice(s, n)`、`free_slice(s)`——内部维护一个会合并相邻空闲区间的片段空闲列表。在构建 n 叉树的子节点列表时就需要用到它：每个节点的条目都希望是一段连续的区间，从而可以作为普通切片来遍历。两种池类型都通过 `pool.items` 暴露其底层数组；其长度包含已释放的存储空间。

**`reusable` 和 `reusable[]` 已被弃用**，建议改用这些库类型。它们目前仍保留以兼容现有代码；彻底移除将是后续步骤。[`26_file_tree`](../../samples/26_file_tree.k) 将这两种库池类型与指向其全局 `items` 成员的紧凑链接结合在了一起。将整个池复制到工作线程中仍然需要使用已弃用的内建机制；工作线程局部的库池则是受支持的。

---

<a id="12-errors-and-return--from"></a>
## 12. 错误处理与 `return … from`

K 没有异常机制，没有 `Result` 类型，也没有规定统一的错误约定：由应用程序自己选择。对于只有两种结果的调用，你已经见过的尾部 `bool` 就足够了（`let age, ok = parse_age(s);`）。对于“找到或未找到”，则使用 `T?`，它不会产生任何拷贝，因为它只是指向输入的引用。这两种都是普通的返回值。

**`return E from f`** 会将 `E` 作为最内层活跃的 `f` 调用的结果返回，并展开中间的每一个栈帧。它提供了轻量级异常。如果只用上面两种惯用法，解析器在八层调用深处失败时，就得要求这八个函数全部在签名里带上错误，哪怕它们只是顺路传递而已。

```klang
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
    let age, ok = parse_age(line[comma + 1..]);
    guard ok else { return [], str("line ", lineno, ": bad age") from load; }
    return User { name: trim(line[..comma]), age: age };
}
```

```
loaded 2 users
error: line 2: expected name,age
error: line 1: bad age
```

看看 `parse_record` *没有*什么。它的返回类型是 `User`，而不是 `Result<User, E>` 或 `(User, bool)`。它没有错误参数，对它所调用的函数也不需要写 `?`，没有任何东西需要手动传播。它与 `load` 之间的所有函数也都是如此，不管中间隔了多少层。整个程序里的全部错误处理，就只是生成错误信息的两个 `guard` 和负责报告错误的那一个 `if err.len > 0`。

有三个特性使其开销极低：

* **它是静态检查的。** 合法性是一个编译期属性：每个包含 `return … from load` 的函数的调用点都必须处于某次 `load` 调用的动态作用域之内，全程序编译器会按调用图顺序验证这一点。运行时不存在“未捕获”的情况。
* **实现方式是一个隐藏的判别标志**，而不是栈展开器（unwinder）。路径上的每个栈帧都会增加一个标志，说明“这个结果归我”还是“继续向上传播给 `load`”，调用者在第二种情况下只需检查该标志并立即返回。正常的尾声代码（epilogue）照常执行，因此每个数据栈的水位线都会自动恢复。没有展开表，没有 `setjmp`，也没有析构函数需要运行——因为本来就没有析构函数。
* **结果直接构造在 `load` 的调用者想要的位置。** `str("line ", …)` 会把消息直接构造到 `load` 的返回目标处，无需经过正在展开的中间栈帧。

因此在成功路径上，唯一的开销只是返回时每层栈帧一次标志位检查。这也是为什么示例中的解析器会把这一机制用于*每一个*语法错误，而不是只留给灾难性错误（[`17_calc`](../../samples/17_calc.k)、[`18_json`](../../samples/18_json.k)：非法词法单元、缺失括号和错误转义都只需一行）。

[`exception` 模块](stdlib.md#exception) 将这一机制封装为 `exception::try<E>(body) { error => ... }` 和 `exception::throw(value)`。它可以捕获所有权文本或应用自定义的错误值，而无需在每个抛出点点名应用的外层函数。有关所有权与递归限制，请参阅该模块的参考文档。它的 `finally` 对应 `defer`，而 `errdefer` 则仅在通过 `return … from` 或 `exception::throw` 错误展开时执行清理、正常返回时跳过。

同一套机制还允许代码块从其外围函数 `return`，而不是从调用它的高阶函数（HOF）返回（[第 13 节](#13-generics-and-blocks-that-disappear)）。那是你会遇到它的另一处地方。

有些调用会直接终止程序而不是报告错误：用于不变量的 `assert(c)`、带消息的 `abort(msg)`，以及 `exit(code)`。类型检查器知道后两者永远不会返回，因此以它们结尾的分支不会产生值。这一知识也会通过普通函数调用传播，包括对 `exception::throw` 的封装函数：返回值的函数可以在末尾调用一个永不返回的函数，而无需在后面补一个伪造的返回值。已证明退出的位置之后的语句和尾部表达式不会被类型检查。未知的递归调用则会保守地假定会返回。

---

<a id="13-generics-and-blocks-that-disappear"></a>
## 13. 泛型，以及消失的代码块

未标注类型的参数就是泛型的：

```klang
fn twice(x) { x + x }
fn span<T>(a: T, b: T) -> T { if a > b { a - b } else { b - a } }
```

`<T>` 要么表示两个参数必须具有相同的类型，要么命名一个没有任何参数携带的类型。类型实参通常从函数实参中推导，例如 `foo(1)`。像 `foo<i64>(1)` 这样的显式列表可以提供靠前的类型实参；对于没有任何实参能决定的类型变量，则必须显式提供：

```klang
fn zero<T>() -> T { default<T>() }   // T 的默认值

let half = zero<f64>() + 0.5;
```

所有泛型都会单态化（monomorphized），因此没有什么抽象层需要做类型检查：泛型函数体只在每一个类型都已具体化的实例化点上才被检查，报错时也会给出产生该错误的编译期调用链。

重载根据实参类型进行解析，而切片参数可以接受任意种类的数组：

```klang
fn describe(x: i64) -> u8[>..]     { str("int ", x) }
fn describe(x: f64) -> u8[>..]     { str("float ", x) }
fn describe(xs: i64[:]) -> u8[>..] { str(xs.len, " ints, first ", xs[0]) }

print(describe(7), "; ", 2.5.describe(), "; ", xs.describe(), "; ", describe(xs[2..]));
```

静态函数值是**编译期实体**。它们作为泛型参数传递，每次调用都是直接调用且可内联，并且它们不能逃逸：将它存入变量、作为返回值或放进数据结构中都会导致编译错误。高阶函数可以编译成一个直接的循环：

```klang
fn each_pair<T, F>(xs: T[:]) {
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}

var gaps: i64[>..] = [];
each_pair(xs) { a, b => gaps.push(b - a); };     // 写入外围局部变量
```

跟在调用后面的 `{ ... }` 是尾随块语法糖（trailing-block sugar），隐式参数为 `it`，也可以用 `a, b =>` 写出具名参数。标准库通篇采用这种写法，而且 `filter`/`map` 会直接在接收它们的变量里构造结果，既没有中间数组，也没有额外分配：

```klang
let evens   = xs.filter() { it % 2 == 0 };
let squares = xs.map()    { it * it };
let total   = fold(xs, 0) { acc, x => acc + x };
sort(xs) { a, b => a > b };
```

代码块也可以从**词法外围函数** `return`，而不是从调用它的高阶函数返回。这使用了同一套 `return from` 机制，意味着手写迭代器的代码块里的 `return` 与 `for` 循环体里的 `return` 行为完全一致：

```klang
fn each_step<F>(lo: i64, hi: i64, step: i64) {
    var i = lo;
    while i < hi { F(i); i += step; }
}

fn first_multiple_of_7(lo: i64, hi: i64) -> i64 {
    each_step(lo, hi, 3) { if it % 7 == 0 { return it; } };   // 从 first_multiple_of_7 返回，
                                                              // 而不是从 each_step 返回
    return -1;
}
```

嵌套函数可以使用其外围函数的局部变量。示例中的解析器就是利用这一点来共享它们的输入、游标和对象池：它们全部是 `parse` 的局部变量，供嵌套在其内部的递归下降函数直接访问。这避免了在每次调用中来回传递一个状态结构体（[`17_calc`](../../samples/17_calc.k)、[`18_json`](../../samples/18_json.k)）。

递归需要显式启用并加以标注。递归环的入口是 `recursive fn`，环中的每个函数都需要显式签名，并且**环中的任何函数在调用回到该环期间都不能持有可增长数据**，否则每一次激活记录都需要自己单独的数据栈。因此，递归的构建函数要把所增长的池作为参数传入：

```klang
recursive fn in_order(n: Node&, out: i32[>..]&) {
    let l = n.left;
    if l { in_order(l, out); }
    out.push(n.key);
    let r = n.right;
    if r { in_order(r, out); }
}
```

只要临时空间的作用域在递归调用之前结束，环中的函数仍然可以拥有在递归前就用完的临时数据。下面这段代码在块内构造并打印每一行，因此在递归调用开始时，它所用的栈空间已经重新释放了：

```klang
recursive fn show(n: Node&, depth: i64) {
    {
        var line: u8[>..] = [];
        for i in depth { line.append("  "); }
        line.append(str(n.key));
        print(line);
    }
    let l = n.left;
    if l { show(l, depth + 1); }
    let r = n.right;
    if r { show(r, depth + 1); }
}
```

这是对你如何编写递归代码的真实约束，同时也是每一层递归只消耗原生栈的主要原因。

<a id="several-tables-one-context"></a>
### 多个表，一个上下文

编译器或仿真器往往将状态保存在多个可增长的表中（节点、符号、输出），并希望把它们打包在一起传递。结构体不能同时拥有多个这样的表，因为一个结构体最多只能持有（作为其最后一个字段的）一个可调整大小的数组，而且递归函数也不能在递归调用之间保留它们。相反，你可以把每个表都设为某个驱动函数的局部变量，然后把一个由这些表的引用组成的单一结构体传给操作这些表的代码：

```klang
struct Node { at: i64, first: i64, next: i64 }   // 源码偏移、第一个子节点、下一个兄弟节点

struct Ctx {                 // 一个切片和三个引用：固定大小，传递开销极低
    source: const u8[:],
    nodes: Node[>..]&,
    text: u8[>..]&,
    scratch: i64[>..<]&,     // 递归的工作栈
}

recursive fn walk(c: Ctx&, n: i64, depth: i64) -> i64 {
    let mark = c.scratch.len;
    c.scratch.push(depth);                 // 本次激活记录的条目，压在调用者条目之上
    c.text.push(c.source[c.nodes[n].at]);
    var total = c.scratch[mark];
    var e = c.nodes[n].first;
    while e >= 0 {
        total += walk(c, e, depth + 1);
        e = c.nodes[e].next;
    }
    c.scratch.resize(mark);                // 返回前再次清理掉
    return total;
}

fn compile(source: const u8[:]) -> i64 {
    var nodes: Node[>..] = [];
    var text: u8[>..] = [];
    var scratch: i64[>..<] = [];
    nodes.push(Node { at: 0, first: 1, next: -1 });   // 带两个子节点的根节点
    nodes.push(Node { at: 1, first: -1, next: 2 });
    nodes.push(Node { at: 2, first: -1, next: -1 });
    var c = Ctx { source: source, nodes: nodes, text: text, scratch: scratch };
    let r = walk(c, 0, 1);
    print(text, " ", scratch.len);
    return r;
}
```

`compile("abc")` 会打印 `abc 0` 并返回 5。`walk` 通过 `c` 对各个表执行压入、索引和收缩，就像通过单独的引用参数操作一样。这些表是 `compile` 的局部变量，因此它们的所有权位于递归环之外，生命周期长于 `walk` 的每一次激活记录。可增减的 `scratch` 是整个递归过程共享的一块栈：每次激活记录都将自己的内容压在调用者条目之上，并在返回前截断回自己的标记点 `mark`。

该上下文的生命周期仅限于一次 `compile` 调用。在同一进程中针对另一个文件进行的第二次调用，会从全新的表开始；而当每次调用的作用域退出时，它的所有表都会一次性全部释放，因此没有全局状态需要重置。当操作这些表的代码小到足以放进驱动函数内部时，嵌套函数可以直接把这些表当作自由变量来使用，根本不需要结构体，就像示例里的解析器那样。上下文结构体适用于代码分散在许多函数或文件中的情形。

---

<a id="14-threads-that-share-nothing"></a>
## 14. 互不共享的线程

线程之间什么都不共享。工作线程函数声明为 `thread_fn`，编译器会将它以及它调用的一切编译成一个**独立程序**，拥有各自的数据栈，并拥有它所使用到的全局变量的独立副本。

线程之间通过带类型的队列传递值，每种类型对应一个队列，由类型本身决定选用哪个队列。能够跨线程传递的值必须是**扁平的（flat）**：在任何深度上都不包含引用和切片。复制它们非常廉价，因为扁平的 K 值在内存中是连续的，把它拷贝进队列只是一次 memcpy。

```klang
struct Job    { n: i32 }
struct Result { n: i32, digits: i32 }

thread_fn worker() {
    loop {
        let job = qget<Job>();           // 阻塞等待
        guard job.n >= 0 else { break; } // -1 表示停止
        qput(Result { n: job.n, digits: count_digits(job.n) });
    }
}

fn main() {
    let n = max(1, hardware_threads());
    for i in 8 { qput(Job { n: (1 << (i * 3)) as i32 }); }
    for i in n { qput(Job { n: -1 }); }              // 每个工作线程一个退出任务
    var ids: i64[>..] = [];
    for i in n { ids.push(thread_spawn(worker)); }
    var total = 0;
    for i in 8 { let r = qget<Result>(); total += r.digits; }
    for id in ids { thread_wait(id); }
    print(n, " workers, ", total, " digits in total");
}
```

因为“扁平值”可以包含变长部分，所以一个任务或结果可以直接携带数据本身，而不仅仅是指向数据的句柄。`struct Row { y: i32, cells: u8[] }` 会作为一整份拷贝跨越队列，连同它的像素数据一起传递，这正是 [`23_mandelbrot_threads`](../../samples/23_mandelbrot_threads.k) 的做法。

工作线程如果递增某个全局变量，它递增的是*它自己的副本*，`main` 中的副本保持不变。K 托管的数据结构绝不会在线程间共享内存。这一模型使得 K 代码中不需要锁、原子操作或内存序。

标准库在访问有状态的操作系统功能时是线程安全的。

对于非标准的 FFI 和 C 代码，一切就无法保证了：K 的保证并不延伸到那部分代码。即使 K 托管的数据绝不共享内存，C 代码仍然可以访问共享的原生状态。编译器无法检查 C 代码，也无法核验它如何使用那些状态。

社区惯例是：除非另有说明，否则默认将 C 代码视为线程安全的。包应当明确记录任何非线程安全的代码，而调用者必须遵守该包的同步与生命周期规则。

（线程功能需要外部 C 编译器：进程内的 TinyCC 后端无法放置线程局部存储，因此请使用 `-o` 编译多线程程序。）

---

<a id="15-saving-and-loading-data-as-bytes"></a>
## 15. 将数据以字节形式保存与加载

如果一个结构体的链接是自相对偏移量，那么无论它位于内存中的何处，这个结构体的含义都完全相同。因此保存它只需把字节写出去，加载它只需把字节读进来。没有序列化器，没有模式定义（schema），没有指针修正，也没有按节点进行的内存分配。

```klang
struct Node { word: u8[..24], count: i32, left: Node&<u32>?, right: Node&<u32>? }

var index: Node[>..] = [];
// ... 构建树 ...

var image: u8[>..] = index.to_bytes();      // 数组的一个带帧头的字节镜像
var loaded, ok = from_bytes<Node[>..]>(image);
print("loaded ", ok, ", ", loaded.len, " nodes, 'fox' = ", lookup(loaded, "fox"));
```

`bytes_of(a)` 做的是同样的事，但不产生拷贝：它返回元素区域的只读*视图*，你可以直接将它传给 `write_file`。

更有趣的一半是 `from_bytes`。来自程序外部的字节流，是引用唯一可能在未经编译器证明有效的情况下进入程序的地方，因此 `from_bytes` **会在字节变成值之前进行校验**。它会检查帧结构、每一个标签、每一个长度，并验证每一个链接都指向同一个镜像中某个元素的起始位置。损坏或恶意的文件会返回 `false` 和一个空数组，绝不会产生野引用。

```klang
image[image.len - 3] = 200;                  // 篡改一个链接
var bad, bok = from_bytes<Node[>..]>(image);
print("accepted: ", bok, ", nodes ", bad.len);
```

```
loaded true, 9 nodes, 'fox' = 4
accepted: false, nodes 0
```

它校验的是*安全性*，而不是完整性。一个数据字节被修改过的镜像依然描述了一个格式良构的结构，因此要确认文件就是你当初写入的那个，靠的是校验和（checksum），而不是校验器。加载回来的就是一个普通数组：你可以对它建立索引、对它进行扩展，指向其内部的引用就是普通引用，生命周期系统也不需要为它增加任何新规则。

---

<a id="16-calling-c"></a>
## 16. 调用 C

`extern fn` 将一个 K 函数签名绑定到某个 C 符号上。`math` 和 `os` 模块都使用了这一外部函数接口（foreign-function interface）。

```klang
extern fn cube_root(x: f64) -> f64 = "cbrt";
extern fn hypot(x: f64, y: f64) -> f64;

extern fn crc32_bytes(s: const u8[:]) -> u32;   // 传入切片，传出标量
extern fn stats_of(xs: i32[:], out: Stats&);    // 通过引用填充的结构体
extern fn c_version(out: u8[>..]&);             // 供 C 追加内容的构建器
```

能够跨越边界的类型，恰恰就是那些具有纯粹 C 形态的类型：标量类型和 `bool`、扁平的定长结构体与数组（按值传递，或通过引用作为指针传递）、这些类型的切片（表现为 `{ data, len }` 结构体），以及 `u8[>..]&` 构建器。任何其他类型都会在声明处被直接拒绝，而不会在调用时被错误地封送（mis-marshalled）。此类调用会被编译为直接的 C 调用，没有任何额外的调用约定开销。你可以通过 `--include header.h` 引入自己的 C 头文件。

---

<a id="17-about-the-speed"></a>
## 17. 关于性能

K 的性能同时取决于其内存模型和编译器优化。区分这两者有助于你选择数据布局和调优循环。

**内存模型的优势。** 任何执行路径上都没有分配器。也没有析构清理（teardown）：退出作用域只是一次指针存储操作，而不是遍历释放。数据是连续存放的，因此缓存负担更轻：一个记录数组就是*一整块*内存，而不是每条记录各自占一块。链接字段很窄：在指针需要占 8 字节的地方，这里只需 2 字节或 4 字节偏移量。并且借助可变模式枚举（variable-mode enums），主要由小节点构成的树不再需要为每个节点都预留最大变体的空间。

**它确实需要优化器的地方**，以及你可以在何处助其一臂之力。每次索引访问都会进行边界检查，而编译器能证明其中大部分检查是不必要的并将其移除。但“大部分”并非“全部”，任何编译器无法证明不必要的检查都会保留在程序中。能起到帮助的做法，是给分析器提供一个它可以使用的长度信息。下面是一个作用于行切片的模糊（blur）核心函数：

```klang
let W = 512;

// 图像的每一行都是一个切片；调用方为每个输出行传入三行源数据和
// 一行目标数据。
fn blur_row(above: u8[:], here: u8[:], below: u8[:], out: u8[:]) {
    assert(above.len == W && here.len == W && below.len == W && out.len == W);
    for x in 1..W - 1 {
        let sum = above[x - 1] as i64 + above[x] + above[x + 1] +
                  here[x - 1]  + here[x]  + here[x + 1] +
                  below[x - 1] + below[x] + below[x + 1];
        out[x] = (sum / 9) as! u8;
    }
}
```

`k check --stats` 会报告它移除了哪些检查：

```
bce: elided 11/11 index and 0/4 slice checks
```

每行加上一条 `assert` 后，像素循环内的每一次索引访问就变成了“循环计数器加一个常量”，且与已知长度进行比较。因此全部 11 处检查都被移除了，C 后端得到的循环代码与直接用 C 编写完全一致。如果没有这条断言，一处检查也不会被移除，循环也无法向量化。`--bce-lines` 会逐行告诉你还残留了哪些检查；当某个核心循环比你预期的慢时，可以使用它来排查。

还有两条语言规则也能帮助编译器进行该分析。只增数组（grow-only array）只会在编译器可见的 `pop`/`pop_n`/`resize`/`clear` 调用处缩短，因此在 `push` 之前成立的边界条件在 `push` 之后依然成立。此外，由于 `%` 采用欧几里得取模（Euclidean），约减运算的结果始终落在有效范围内。

**基准测试数据**（[`bench/summary.md`](bench/summary.md)、[`bench/results.md`](bench/results.md)）：在十六项基准测试中，K 的运行速度达到地道 C++ 的 3.3 倍、手工优化 C++ 的 1.16 倍、最优安全 Rust 的 1.05–1.11 倍，同时内存占用分别少 1.9 倍/1.3 倍/1.2 倍。落后的项目也同样列出了并附有原因说明。`particles` 是一个扁平浮点计算核心，其设计本身从未预期会产生优势，结果也确实如此。`calc` 相对于 Rust 为 0.82 倍，是汇总报告中唯一归因于 K 自身设计而非后端的落后项，报告也指出了导致这一结果的三个原因。每一项测量都是包含程序收尾清理在内的全进程挂钟时间（wall-clock time），清理过程在其他语言中会消耗时间，而在 K 中耗时为零。

---

<a id="18-limitations"></a>
## 18. 局限性

以下这些局限你都会遇到。每一条都链接到了前文展示应对方法的小节（如有），以及施加该限制的规范条款。

* **你必须思考数据存放在哪里。** 不必时刻操心，但现在你需要明确回答“谁拥有这份数据，它的作用域持续多久”这个问题。大多数情况下，答案是“构建它的那个函数”，除此之外无需再做任何事。当生命周期无法嵌套时，请使用标准库池（[第 11 节](#11-lifetimes-that-do-not-nest)；[规范 §1.2](k_spec.md#12-memory-model)、[§5.4](k_spec.md#54-reusable-arrays-safe-allocation-escape-hatch)）。
* **递归函数不能跨递归调用保留可增长数据。** 在递归调用之前结束的语句块中使用临时缓冲（scratch）是可以的；任何生命周期更长的数据都需要作为参数传入。这会改变递归下降解析器的组织方式：状态会变成非递归入口函数的局部变量，或者是指向这些局部变量的引用结构体。这可以说反而更清晰，但确实是一种写法上的改变（[多张表，同一个上下文](#several-tables-one-context)；[规范 §7.8](k_spec.md#78-recursion)）。
* **可变大小元素的数组不能按索引访问。** 对于每个容器，你需要在“紧凑且可遍历”与“可索引”之间做出选择，或者在紧凑容器旁额外花一份内存维护一个引用索引（[第 10 节](#an-index-for-variable-size-elements)；[规范 §3.3](k_spec.md#33-the-array-family)）。
* **定长模式枚举无法获取指向内部的指针；可变模式枚举无法被覆写。** 你需要在每个使用点选择对应的模式（[第 9 节](#9-enums-in-two-sizes)；[规范 §3.5](k_spec.md#35-algebraic-data-types)、[§8.1](k_spec.md#81-match)）。
* **具体闭包（concrete closures）会保留其捕获类型。** 闭包可以带着其拥有的值或有效借用逃逸作用域。ADT 标签可以分派到不同的行为。Cordis 在显式宿主边界处支持原生组件。[闭包](reference.md#concrete-closures) · [Case 函数](k_spec.md#82-case-functions-match-as-an-overload-set) · [Cordis](design/cordis_microkernel.md)
* **非标准 FFI 包不受工作线程隔离保护。** K 托管的数据结构绝不在线程间共享内存。标准库同样不共享内存；它对有状态操作系统功能的访问是线程安全的。非标准包则可能访问没有此类保证的 C 代码。按约定，除非另有说明，这类代码至少应具备线程安全性，且包应当明确标注非线程安全的代码（[第 14 节](#14-threads-that-share-nothing)）。
* **全程序编译（Whole-program compilation）。** 不支持分离编译，也不存在 K 代码的共享库（[规范 §10.1](k_spec.md#101-whole-program-call-graph-order)）。
* **池（pool）对其元素的管理较为宽松。** 不会释放任何内存，也不会出现内存不安全，但如果你在归还某个槽位后仍继续使用它，你读到的将是下一个持有者写入的内容。这在数组上等同于悬垂/过期索引（stale index），需要由你自己来避免（[第 11 节](#11-lifetimes-that-do-not-nest)；[规范 §9.4](k_spec.md#94-type-safe-reuse-and-stale-references)）。
* **v1 版本的省缺项**（你在使用时会注意到）：可变长类型不支持移动语义（move）、不支持嵌套的自有 `root struct` 或其数组、不支持带标签的 `break`、不支持命名空间私有性（[规范 §12](k_spec.md#12-builtins-the-standard-library-and-what-v1-leaves-out)），标准库中也暂未提供子进程或网络功能（[`os`](stdlib.md#os)）。

普通结构体仍然最多只能包含一个可变长尾部字段。[`root struct`](reference.md#root-structs) 则可以将带有独立存储字段的状态打包在一起，包括多个可变长数组。将所有者声明为局部变量或全局变量，并按引用传递它即可。

---

<a id="19-calling-k-from-c"></a>
## 19. 从 C 中调用 K

`export fn` 与 `extern fn` 相反：它让 K 函数可以被 C 调用，从而使 K 模块能够作为库链接进 C 或 C++ 程序中。

```klang
var calls: i32 = 0;

export fn hello_world() -> const u8[:] {
    calls += 1;
    return "Hello, world!"
}

export "c_add" fn add(a: i32, b: i32) -> i32 { a + b }

fn main() {
    calls = 100;    // 在 k_init 内部运行一次
}
```

`export fn` 本质上是一个普通的 K 函数；编译器会为它生成一个 C 包装器。可选的字符串用于指定导出的 C 符号名；如果不写，则使用 K 函数名。当你要求生成头文件时，编译器会为 C 宿主程序构建对应的 C 代码，其中不包含自身的 `main` 函数：

```sh
k --standalone --header hello.h -o hello.c hello.k
cc hello.c app.c -o app
```

```c
#include "hello.h"
#include <stdio.h>

int main(int argc, char **argv) {
    int64_t length;
    k_init(argc, argv);
    const uint8_t *message = hello_world(&length);
    fwrite(message, 1, (size_t)length, stdout);   // 非 NUL 结尾
    putchar('\n');
    printf("%d\n", c_add(20, 22));
    return 0;
}
```

宿主程序必须在调用其他任何函数之前先调用一次 `k_init`。它会初始化运行时与全局变量，然后运行你的 K 函数 `fn main()`，因此 `main` 正是放置库自身启动代码的地方。如果库没有任何需要启动初始化的内容，也可以省略 `main`。当宿主程序用完该程序时，`k_exit()` 会释放 `k_init` 所建立的一切状态，之后 `k_init` 还可以再次启动它：这正是宿主程序重新加载已重编译库的机制。`const u8[:]` 类型的返回值会以一个指针外加一个用于接收长度的额外 `int64_t *` 参数的形式返回；标量与扁平定长值则按值返回。生成的函数不是线程安全的：请从调用 `k_init` 的同一线程调用它们。允许跨越边界的签名与[第 16 节](#16-calling-c)相同，另有少量额外限制；详见[规范 §7.11](k_spec.md#711-exporting-k-functions-to-c)。

---

<a id="20-where-to-go-next"></a>
## 20. 下一步去哪里

* **[`docs/reference.md`](reference.md)** — 涵盖整门语言的参考手册，按主题组织，每条规则都配有可编译的示例：学完本教程后就可以前往这里，日常也可以在这里查阅细节。
* **[`samples/`](samples.md)** — 按阅读顺序编排的 31 个完整程序，每个都附有注释说明其演示的内容。可以从 `01_tour` 和 `02_memory` 开始，然后直接跳到与你问题相关的示例。其中 `13_linked_list`、`14_bst` 和 `18_json` 详细展示了数据结构；`26_file_tree` 则同时使用了两种池。
* **[`docs/stdlib.md`](stdlib.md)** — 标准库参考文档。包含 14 个模块：`std`、`dictionary`、`vec`、`math`、`os`、`binary`、`base64`、`csv`、`json`、`regex`、`audio`、`gfx`、`physics` 和 `ui`，其 K 接口文件位于 `stdlib/` 目录下。
* **[`docs/k_spec.md`](k_spec.md)** — 详尽的语言规范，包括程序为何会无法通过编译。本教程中所有的“规范 §”链接都指向该文档。
* **[`bench/`](bench/summary.md)** — 基准测试数据及其背后的设计说明。
* **[`docs/implementation.md`](implementation.md)** — 编译器的工作原理，如果你想参与改造编译器，可以阅读这份文档。

最后一条建议：当检查器拒绝某段代码时，请把错误信息完整读完。它会同时指出问题的两端：引用*以及*它绑定的位置、持有者*以及*往里面存值的那一行，或者实例化位置*以及*产生它的调用链。借助这些位置信息，就能判断需要修改的是哪一处绑定、存储还是作用域。
