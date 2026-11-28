<a id="k-for-c-and-rust-programmers-translating-common-patterns"></a>
# 面向 C++ 与 Rust 程序员的 K 语言指南：常见模式转换

2026 年 10 月 22 日 · @Ben

一旦你不再问“我该在哪里分配它？”，而是开始问“哪个作用域拥有它，它住在哪个数组里？”，大多数 C++ 和 Rust 代码就能直接转换到 K。本指南选取你每天都在写的常见模式，展示它们在 K 中的对应写法，并解释其背后的差异原因。

本文内容整理自官方[教程](tutorial.md)、[语言参考](reference.md)、[标准库](stdlib.md)和设计笔记。文中的示例使用这些文档所示的语法；如果某段代码无法编译，请查阅语言参考。大多数示例假设文件顶部已有 `import std;`（用于 `trim`、`split`、`sort`、`filter`、`remove_at` 等函数），而 `dictionary` 示例还需要 `import dictionary;`。

<a id="the-one-shift-memory-is-a-few-stacks"></a>
## 核心转变：内存只是几个栈

K 没有 `malloc`，没有 `free`，没有 GC，没有引用计数，也没有析构函数。内存由原生栈加上编译器为你的程序确定大小的若干个**数据栈**（data stack）组成。由此引出四条规则，下文几乎每一种模式都是它们的直接推论：

1. **每个动态值都由一个变量拥有。** 可以是局部变量、全局变量、按值传递的参数，或者是它们的字段或元素。没有任何东西游离在堆上。
2. **离开作用域即释放。** 当拥有它的作用域结束时，栈指针直接回退。只需一次存储操作，无需遍历，也无需析构函数。
3. **一切永不移动。** 可增长数组始终位于自身所在数据栈的栈顶，因此 `push` 绝不会重新分配内存。指向其中的引用在经历一百万次 `push` 之后依然有效，这是 `Vec` 或 `std::vector` 无法承诺的。
4. **值内联嵌套。** 结构体里的字符串再放进数组里，本质上就是字节里套着字节里套着字节，中间没有任何指针。

编译器会静态证明这一切。没有生命周期标注；引用携带由编译器推导出的*根*（root，即拥有其目标的变量），并且不能比它活得更久。

在此之上，类型系统是封闭世界的（closed-world）：没有继承，没有 trait，没有虚表（vtable），没有函数指针，也没有可逃逸的闭包。动态多态只有代数数据类型（ADT），别无其他。

**如何阅读本指南。** 每一节都会点出一种 C++/Rust 习惯，先展示原写法，再展示 K 的版本。紧接着的速查表是全文索引。

<a id="quick-reference"></a>
## 速查表

| 你在 C++ / Rust 中写的是 | 你在 K 中写的是 | 对应章节 |
| --- | --- | --- |
| `new T`, `make_unique`, `Box::new` | 局部变量，或 `push` 进生命周期比你更长的数组 | 堆分配 |
| `vector<T>`, `Vec<T>` | `T[>..]`（只增不减）；若需弹出元素则用 `T[>..<]` | 容器与字符串 |
| `string`, `String` | 构建用 `u8[>..]`；构建完成用 `u8[]`；内联用 `u8[..16]` | 容器与字符串 |
| `string_view`, `&str`, `span`, `&[T]` | `T[:]` 切片（带检查，不会悬垂） | 容器与字符串 |
| `x.clone()`，vector 的拷贝构造函数 | `copy(x)`，始终显式写出 | 堆分配 |
| 虚函数，`dyn Trait` | `enum` + `match`，或 case 函数 | 虚函数与 trait 对象 |
| `vector<unique_ptr<Base>>` | `Shape[>..]`（定长模式）或 `Shape..[>..]`（紧凑模式） | 虚函数与 trait 对象 |
| trait 约束，C++ concept | 无类型约束 / `<T>` 泛型，按实例化位置检查 | Trait、concept 与方法 |
| `impl Hash for K`, `operator<<` | 自由函数 `hash(k: K)` 或 `format(out, v: K)` 重载 | Trait、concept 与方法 |
| 方法，`impl` 块 | 自由函数；`x.f(a)` 就是 `f(x, a)` | Trait、concept 与方法 |
| `std::function`，存储 `Box<dyn Fn>` | 编译期函数值；需存储的行为变成 enum 标签 | 回调与闭包 |
| `Box<Node>` 树，`Vec<Node>` + `u32` 索引 | `Node[>..]` 对象池 + `Node&<u32>?` 链接 | 链式结构 |
| `shared_ptr`, `Rc<RefCell<T>>`, 对象池 | 标准库 `slot_pool<T>` + `alloc_ref` / `free` | 非嵌套生命周期 |
| 拥有三个 `Vec` 的结构体 | `root struct`，其三个字段可独立增长 | 拥有多个动态数组的结构体 |
| `Result<T, E>`, `?`, `throw` | 末尾 `bool`、`T?`、`return E from f`、`exception::try` | 错误处理 |
| 可选链与空合并 | `x?.field`、`opt ?? fallback` | 日常习惯的变化 |
| RAII 析构函数 | 离开作用域（内存）；`defer`、`errdefer`、`using` + `own` / `release`（外部句柄） | RAII 与析构函数 |
| `Arc<Mutex<T>>`, `std::thread` + 共享数据 | `thread_fn` + 扁平值的类型化队列 | 线程 |
| serde, protobuf | `to_bytes` / `from_bytes<T>` / `bytes_of` | 序列化 |
| `std::optional<T&>`, `Option<&T>`, `nullptr` | `T?`，通过 `if` / `guard` 收窄 | 日常习惯的变化 |
| `std::tuple`, `(a, b)` 返回值 | `-> A, B` 多返回值（无元组类型） | 日常习惯的变化 |

<a id="heap-allocation-there-is-no-heap"></a>
## 堆分配：根本没有堆

在 C++ 和 Rust 中，当一个值的大小是动态的，或者它必须比当前调用活得更久时，你会进行堆分配。在 K 中，第一种情况直接就是局部变量，第二种情况则是按值返回，或者被 `push` 进更上层作用域拥有的数组中。

<a id="i-need-a-buffer-of-runtime-size"></a>
### “我需要一个运行时大小的缓冲区”

```
// C++
auto buf = std::make_unique<uint8_t[]>(n);
// Rust
let mut buf = vec![0u8; n];
```

```
// K：局部变量。它位于数据栈上，在右花括号处释放。
var buf: u8[>..] = [];
buf.resize(n, 0);
```

循环中的临时缓冲区每次迭代的释放成本只是一次指针存储，因此每次都重新填充一个新的局部变量是地道写法，并不浪费。

<a id="i-build-something-and-hand-it-back"></a>
### “我构建了一个值并把它返回”

C++ 程序员常通过传出参数（out-parameter）来避免拷贝；Rust 返回 `Vec` 并依赖 move 语义。K 按值返回，结果**直接在调用方的目标位置原地构建**，无论中间经过多少层调用。既没有临时对象，也没有 move。

```
fn squares(n: i64) -> i64[>..] {
    var out: i64[>..] = [];
    for i in n { out.push(i * i); }
    return out;           // `out` 从声明起“就是”调用方的变量
}

let sq = squares(10);     // 无拷贝

// 也适用于正在 push 的元素字段（来自教程）：
book.push(Order { id: 1001, customer: "alice",
                  items: parse_items("SKU-441:2:1999;SKU-7:1:500") });
```

因此优先使用返回值而不是传出参数；两者开销完全相同，而返回值可读性更好。`str(...)` 也遵循同样规则：`names.push(str("item", i))` 会直接格式化到新元素的位置中。

<a id="i-need-my-own-copy"></a>
### “我需要属于自己的拷贝”

固定大小的值（标量、字段全为定长的结构体、`T[k]`、引用、切片）在 `=` 赋值时会像 C 一样静默拷贝。任何大小在编译期未知的值**绝不会隐式拷贝**：`let b = a;` 会把 `b` 按引用绑定到 `a`。你需要写出 `copy(a)` 来获得一次 O(n) 拷贝，因此每一次昂贵的拷贝都在源码中清晰可见。

```
var a: i64[>..] = [1, 2, 3];
let alias = a;        // 同一个数组
var mine = copy(a);   // 独立的拷贝
```

<a id="the-object-must-outlive-this-function"></a>
### “这个对象必须比当前函数活得更久”

这个问题就变成了*应该由哪个作用域拥有它*。通常答案是调用方：调用方声明数组并以 `T[>..]&` 传给被调方；被调方往里 `push` 并从 `push` 取回一个引用。当生命周期真正交错、没有任何作用域能匹配时，使用 `reusable` 池（见“非嵌套生命周期”）。

<a id="containers-and-strings-pick-the-array-by-what-its-size-does"></a>
## 容器与字符串：按大小变化方式选择数组

K 中没有单一的 `vector`。整个数组家族的区别仅仅在于大小如何变化，而且每个成员都以内联方式存储 `[metadata][elements]`，没有指向独立内存块的指针。

| 你的需求 | C++ / Rust | K |
| --- | --- | --- |
| 可增长列表，且需要持续持有指向元素的引用 | `vector<T>` / `Vec<T>`（增长后引用失效） | `T[>..]` 只增数组：默认选择 |
| 栈、队列或堆（heap） | `vector` + `pop_back`, `BinaryHeap` | `T[>..<]` 可增可减数组，配合 `std` 中的 `heap_push` / `heap_pop` |
| 结构体内部的小型有界列表 | `std::array` + 计数, `ArrayVec`, `SmallVec` | `T[..k]`（有界容量，内联） |
| 构建一次后只读 | 停止修改的 `vector`, `Box<[T]>` | `T[]` 或 `T[varint]` |
| 编译期固定大小 | `std::array<T, N>`, `[T; N]` | `T[k]` |
| 查看别人的元素 | `span<T>`, `&[T]` | `T[:]` |

有两条规则决定了你怎么使用它们。一个结构体中最多只能包含一个*可调整大小*的值（`T[>..]` 或 `T[>..<]`），并且它必须作为最后一个字段。此外，数组的元素本身不能是可调整大小的。

<a id="vector-and-vec"></a>
### `vector<string>` 与 `Vec<String>`

```
var words: u8[][>..] = [];
for i in 5 { words.push(str("word", i)); }
```

这是一整块连续内存：先是一个计数，然后紧接着五个 `(length, bytes)` 片段。没有逐字符串的独立分配，也没有逐元素的指针。代价是：变长元素使这个数组变成**顺序访问**（sequential）的——你可以迭代它，但不能用下标索引它（`words[2]` 会导致编译错误）。

如果你需要下标索引或排序，可以用内联容量把每个元素做成固定大小：

```
var names: u8[..16][>..] = [];   // 每个名字最多 16 字节，可索引、可排序
```

或者保留紧凑数组，并在旁边维护一个引用索引数组（“链式结构”一节会展示这一做法）。

<a id="string-building-ostringstream-format-stringpush_str"></a>
### 字符串构建（`ostringstream`、`format!`、`String::push_str`）

K 没有专门的字符串类型：字符串就是 `u8` 数组。`print`、`format` 和 `str` 可以接受任意数量、任意类型的参数。

```
var s: u8[>..] = [];
s.append("hello");
s.push(',');
format(s, " world! ", 6 * 7);       // 追加任意值的文本表示
let t = str("x=", x, " y=", y);    // 新字符串，直接在目标位置构建
print("answer=", 42, " ratio=", 0.75);   // 无需格式化占位符串
```

`u8[..16]` 字段在容量范围内同样可以作为构建器使用：`format(p.name, "linus")`。

<a id="string_view-str-split"></a>
### `string_view`、`&str`、`split`

切片没有额外开销且带有边界检查，因此库函数返回的是视图而不是新字符串。`split` 返回一个指向输入文本的切片数组；不拷贝任何文本。

```
let body = trim(line);
let eq   = find(body, "=");
let key  = trim(body[..eq]);
let parts = split("alice,30,paris", ',');
each_split(text, '\n') { handle(trim(it)); };   // 甚至完全不构造数组
```

切片非常适合作为哈希键：`dictionary<const u8[:], i32>` 只存储指向你已持有文本的地址与长度。加上 `const` 是因为字符串字面量和 `split` 的结果都是只读视图。

<a id="erasing-from-the-middle-retaining-dedup"></a>
### 从中间删除、保留（retain）、去重（dedup）

这些都是以引用接收数组的库函数：`remove_at`、`swap_remove`、`insert_at`、`retain`、`dedup`。只增数组只能在编译器能确认没有任何引用或切片仍指向它的时候缩小；如果你持有一个稍后还会用到的切片指向该数组，调用会被拒绝，报错信息会直接指出那个切片。

<a id="virtual-functions-and-trait-objects-become-enums"></a>
## 虚函数与 trait 对象变成 enum

K 没有继承、没有 trait、也没有虚表。它唯一的运行时多态机制是 `enum`（代数数据类型）。这覆盖了绝大多数常见场景：一组已知的、封闭的种类，把它们存放在一起并统一进行操作。

<a id="the-classic-shape-hierarchy"></a>
### 经典的图形继承体系

```
// C++
struct Shape { virtual double area() const = 0; virtual ~Shape() = default; };
struct Circle : Shape { double r; double area() const override { return 3.14159*r*r; } };
struct Rect   : Shape { double w, h; double area() const override { return w*h; } };
std::vector<std::unique_ptr<Shape>> shapes;

// Rust
trait Shape { fn area(&self) -> f64; }
let shapes: Vec<Box<dyn Shape>> = ...;
```

```
// K
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

// 方式 1：case 函数，每个变体一个重载。这就是虚调用的替代。
fn area(s: Shape.Circle) -> f64 { 3.14159 * s.r * s.r }
fn area(s: Shape.Rect) -> f64   { s.w * s.h }
fn area(s: Shape.Dot) -> f64    { 0.0 }

// 方式 2：一个带 match 的函数。同样的派发，内联写法。
fn name(s: Shape) -> u8[:] {
    match s {
        Circle c => "circle",
        Rect r   => if r.w == r.h { "square" } else { "rectangle" },
        _        => "other",
    }
}

var shapes: Shape[>..] = [];
shapes.push(Shape.Circle { r: 1.0 });
shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
for s in shapes { print(s, " area ", area(s)); }
```

用整个 `Shape` 调用 `area(s)` 会通过基于标签的跳转表（jump table）进行派发。编译器会检查穷尽性：一旦新增一个 `Triangle` 变体，每一组 case 函数和每一个没有 `_` 分支的 `match` 都会停止编译，直到你处理它为止。这和纯虚函数给出的保证一样，只是把强制检查放到了调用操作处，而不是类的定义处。

其权衡也是和类型（sum type）一贯的取舍。新增一种*操作*很容易（在任何地方编写新的 case 函数即可）。新增一种*类型变体*则需要修改每一个操作。如果你需要一个第三方代码在运行时扩展的开放集合（如插件），K 无法直接表达；参见最后一节。

<a id="base-class-fields-shared-by-every-kind"></a>
### 所有种类共享的基类字段

把公共数据放进一个结构体，把变化的部分放进一个 enum 字段：

```
enum Kind { Player { lives: i32 }, Enemy { aggro: f32 }, Pickup { value: i32 } }
struct Entity { x: f32, y: f32, hp: i32, kind: Kind }

fn tick(e: Entity&) {
    match e.kind {
        Enemy en => { e.x += en.aggro; },
        _ => {},
    }
}
```

<a id="pick-the-storage-mode-at-the-use-site"></a>
### 在使用处选择存储模式

每个 enum 都可以用两种方式存储，由你在使用处写下的类型决定，而不是声明处决定：

|  | 定长模式 `Shape` | 变长模式 `Shape..` |
| --- | --- | --- |
| 大小 | 标签 + 最大载荷，每个元素相同（此处为 17 字节） | 恰好是各自变体的大小（Dot 1，Circle 9，Rect 17） |
| 它们的数组 | 可索引 | 顺序访问：仅可迭代，或在旁维护一个索引数组 |
| 原地修改元素的变体 | 可以：`shapes[2] = Shape.Rect { .. }` | 绝不可以 |
| 指向载荷的引用（`Rect &r =>`） | 不可以；`match` 绑定的是拷贝 | 可以，支持原地修改 |
| 变长载荷（例如字符串） | 不允许 | 允许 |

这就是取代“指向不同大小堆对象的指针数组”的方案。需要索引和原地替换时用定长模式；需要紧凑的树、记录和只需遍历的日志时用变长模式。

```
var packed: Shape..[>..] = [];
packed.push(Shape.Circle { r: 1.0 });
packed.push(Shape.Rect { w: 2.0, h: 3.0 });
for s in packed {
    match s {
        Rect &r => { r.w += 1.0; },   // 原地修改数组内部的载荷
        _ => {},
    }
}
```

在定长模式下，`match` 绑定的变量是只读拷贝。要修改某一个变体的字段，需要构造新的变体并给整个元素重新赋值。

<a id="visitor-pattern-double-dispatch"></a>
### 访问者模式与双重派发

访问者模式本来就是为了模拟模式匹配而存在的，因此它直接消失了：直接写 `match` 即可。对于双重派发（例如 A 与 B 碰撞），嵌套两个 `match`，或者写一组 case 函数并在函数体里对第二个参数做 `match`。case 函数派发只针对单个参数位置。

<a id="generic-code-over-every-variant"></a>
### 遍历每个变体的泛型代码

如果你习惯用 derive 宏或反射（“打印每个变体的所有字段”），请使用 `const for <N, V> in variants<T>` 和 `const for <F, A> in fields<V>`，它们会在编译期展开成普通代码。

<a id="traits-concepts-and-methods-become-overloads-and-generics"></a>
## Trait、concept 与方法变成重载和泛型

K 的静态多态机制类似于没有 concept 的 C++ 模板：泛型函数体只在每个具体实例化处进行检查，并且所有东西都会单态化（monomorphize）。不需要声明 trait 约束。trait 或 concept 所*命名*的能力，你在 K 中通过自由函数的重载来提供。

<a id="generic-functions"></a>
### 泛型函数

```
fn twice(x) { x + x }                         // 未标注类型的参数即为泛型
fn span<T>(a: T, b: T) -> T { if a > b { a - b } else { b - a } }   // <T> 要求 a 和 b 类型一致
fn zero<T>() -> T { default<T>() }           // 无法推导时显式传入类型参数
let half = zero<f64>() + 0.5;
```

类型参数永远是类型，绝不能是尺寸：`fn f<N>(a: i64[N])` 是错误的。改用切片 `T[:]`，每种数组形式都可以转换过去。

错误会报告在实例化位置，并附带产生该实例化的完整调用链，很像 C++ 模板报错，但会把整条链条清晰列出来。

<a id="impl-hash-for-mykey-stdhash"></a>
### `impl Hash for MyKey`、`std::hash<MyKey>`

编写一个 `hash` 重载。`dictionary` 会通过重载决议找到它。

```
struct key { a: i64, b: i64 }
fn hash(k: key) -> u64 { hash_combine(hash(k.a), hash(k.b)) }

var seen = dictionary<key, bool> {};    // 集合就是 dictionary<K, bool>
```

相等性比较是结构化且内置的（`==` 会逐成员比较结构体），因此不需要 derive `PartialEq`。

<a id="impl-display-operator"></a>
### `impl Display`、`operator<<`

编写一个 `format` 重载。之后 `print`、`str` 和 `format` 都会在任何地方按这种方式渲染你的类型，包括它位于数组或其他结构体内部时。

```
struct Person { name: u8[..16], age: i32 }
fn format(out: u8[>..]&, p: Person) { format(out, p.name, " (", p.age, ")"); }

print(people);    // [ada (36), grace (45)]
```

即使不写重载，所有类型依然可以按结构直接打印（`Point { 1, 2 }`），调试时通常已经够用。

<a id="ordering-ord-operator-comparators"></a>
### 排序与大小比较（`Ord`、`operator<`、比较器）

大小比较运算符（`<`、`>`、`<=`、`>=`）只存在于数字上。排序接收一个表示“a 排在 b 前面”的代码块：

```
sort(people) { a, b => a.age < b.age };
let oldest = max_by(people) { it.age };   // 最大元素的索引
```

<a id="operator-overloading-on-math-types"></a>
### 数学类型的运算符重载

K 没有运算符重载。取而代之的是，`+ - * / %` 以及一元 `-` 会自动逐成员应用于叶子全为整数或全为浮点数的任意结构体或定长数组，且 `*` 与 `/` 支持标量广播（broadcast）。标准库里的 `vec3<T>` 就是这样工作的。

```
struct V3 { x: f32, y: f32, z: f32 }
var c = V3 { 1, 2, 3 } + V3 { 0.5, 0.5, 0.5 };
c *= 2;
```

任何超出逐成员算术的运算（例如矩阵乘法、点积）都是具名函数：`mul(a, b)`、`dot(a, b)`。

<a id="methods-and-impl-blocks"></a>
### 方法与 `impl` 块

K 里没有方法和 `impl` 块，但对任何函数来说 `x.f(a)` 都完全等价于 `f(x, a)`，因此方法调用语法到处都能用：`xs.sort()`、`d.insert(k, v)`、`2.5.describe()`。把与某类型相关的函数放在同一文件或同一命名空间即可。另外 v1 没有命名空间私有性控制，因此封装靠约定。

<a id="constructors"></a>
### 构造函数

结构体字面量就能胜任。字段可以声明默认值（每次构造时求值，可以调用函数），并且字面量末尾加上 `..` 会用各自类型的默认值填充所有未指定的字段。如果需要维护不变量，写一个普通的 `make_thing(...)` 函数返回结构体即可；它会在调用方原地构造。

<a id="callbacks-and-closures-passed-down-never-stored"></a>
## 回调与闭包：向下传递，绝不存储

K 中的函数值是编译期实体。你可以把函数或代码块传给另一个函数，其中的每次调用都是直接调用且可内联的。你不能把它存进变量、字段或数组里，也不能返回它。没有 `std::function`，没有 `Box<dyn Fn>`，也没有函数指针。

<a id="higher-order-functions-and-lambdas"></a>
### 高阶函数与 lambda

尾随代码块（trailing-block）语法取代了 lambda。`it` 是隐式参数，也可以用 `a, b =>` 给参数命名。代码块可以直接读取并修改外层局部变量。

```
// Rust: let evens: Vec<_> = xs.iter().filter(|x| *x % 2 == 0).collect();
let evens   = xs.filter() { it % 2 == 0 };
let squares = xs.map()    { it * it };
let total   = fold(xs, 0) { acc, x => acc + x };

var gaps: i64[>..] = [];
each_pair(xs) { a, b => gaps.push(b - a); };   // 修改外层局部变量
```

`filter` 和 `map` 会直接把结果构造到接收变量中；不存在迭代器链，也不存在中间集合。

<a id="writing-your-own-hof"></a>
### 编写自己的高阶函数（HOF）

将函数作为类型参数接收并调用它：

```
fn each_pair<T, F>(xs: T[:]) {
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}
```

代码块内部的 `return` 会直接从**你书写该代码块的外层函数**返回，而不是从高阶函数返回。因此自定义迭代器拥有与内置 `for` 完全相同的控制流能力：

```
fn each_step<F>(lo: i64, hi: i64, step: i64) {
    var i = lo;
    while i < hi { F(i); i += step; }
}

fn first_multiple_of_7(lo: i64, hi: i64) -> i64 {
    each_step(lo, hi, 3) { if it % 7 == 0 { return it; } };   // 直接退出 first_multiple_of_7
    return -1;
}
```

这取代了 Rust 的 `try_for_each`/`ControlFlow` 体操，以及 C++ 里“从 lambda 返回一个 bool 来中止遍历”的写法。

<a id="nested-functions-and-closures"></a>
### 嵌套函数与闭包

嵌套 `fn` 可以把外层函数的局部变量作为自由变量来读写。示例中的递归下降解析器就是这样写的：输入、游标和输出池都是 `parse` 的局部变量，而文法规则是一个个嵌套函数。

```
var budget = 0;
fn spend(e: Employee&) { budget += e.salary; }
for &e in staff { if e.dept == "eng" { spend(e); } }
```

如果要返回或存储可调用对象，请使用带显式捕获的闭包。
泛型函数可以把具象闭包当作普通参数接收。
[具象闭包](reference.md#concrete-closures)

<a id="stored-callbacks-event-handlers-command-tables-strategy-objects"></a>
### 存储回调：事件处理器、命令表、策略对象

这是变化最大的一种模式。当 C++ 保存一个 `vector<std::function<void()>>` 处理器列表或 `map<string, Command*>` 时，这组行为几乎总是在编译期就已经确定。把*要执行哪种*行为存成数据，然后用 enum 派发。

```
// C++: std::vector<std::function<void(World&)>> on_tick;
enum Action { Spawn { kind: i32 }, Heal { amount: i32 }, Log }

fn run(a: Action.Spawn, w: World&) { /* ... */ }
fn run(a: Action.Heal,  w: World&) { /* ... */ }
fn run(a: Action.Log,   w: World&) { /* ... */ }

var on_tick: Action[>..] = [];
on_tick.push(Action.Heal { amount: 5 });
for a in on_tick { run(a, world); }   // 按标签派发；其余参数透传
```

载荷里携带着闭包原本会捕获的数据。正因为它保存的是数据而不是代码，这个列表还可以保存到磁盘或发送给工作线程。

按名称索引的命令表，可以写成一连串把名称转成 `Action` 的 `if`/`else` 链（`match` 只接受 enum 和整数，不接受字符串；字符串用 `==` 比较），或者用一个 `dictionary<const u8[:], i32>` 把名称映射到 id，然后再对 id 做 `match`。

对于 GUI 回调，`ui` 库采用的是立即模式（immediate-mode）：`if ui::button_label(ctx, "Reset") { volume = 0.5; }`。“回调”就是这个 `if` 语句体，在按钮被点击的那一帧直接执行。

<a id="linked-structures-nodes-live-in-an-array-links-are-references"></a>
## 链式结构：节点住在数组里，链接就是引用

C++ 在这里使用 `unique_ptr<Node>` 子节点，安全 Rust 无奈只能退回到 `Vec<Node>` 加 `u32` 索引，而 K 把节点放进同一个只增数组中，再用**类型化引用**将它们链接起来。由于 `push` 绝不会移动已有节点，你从 `push` 拿到的引用永远有效，并且编译器会静态检查它。

<a id="binary-search-tree"></a>
### 二叉搜索树

```
// Rust，常见的 arena 变通方案
struct Node { key: i32, left: Option<u32>, right: Option<u32> }
let mut pool: Vec<Node> = Vec::new();
```

```
// K
struct Node { key: i32, count: i32 = 1, left: Node&<u32>?, right: Node&<u32>? }

fn insert(pool: Node[>..]&, key: i32) {
    if pool.len == 0 { pool.push(Node { key: key }); return; }
    var cur .= pool[0];                     // .= 绑定引用；= 会拷贝
    loop {
        if key == cur.key { cur.count += 1; return; }
        let next = if key < cur.key { cur.left } else { cur.right };
        if next { cur .= next; continue; }  // `if` 会收窄可选类型
        if key < cur.key { cur.left .= pool.push(Node { key: key }); }
        else             { cur.right .= pool.push(Node { key: key }); }
        return;                             // 这次 push 没有让 cur 失效
    }
}
```

这里有三个引用特性在发挥作用：

- **引用是透明的。** `cur.key` 直接通过 `cur` 读取字段；没有 `*` 或 `->`。要表示引用本身时，使用 `.=` 进行绑定或重绑定，使用 `.==` 比较地址。
- **`T?` 是可空引用**，没有任何空间开销。使用前必须通过 `if`、`while`、`guard` 或 `assert` 将其收窄；在收窄后的作用域内，它就直接是一个 `T&`。
- **`Node&<u32>` 是相对引用**：占用 4 字节偏移量，而不是 8 字节指针。读取它时会得到一个普通的 `Node&`。这里的节点只占 16 字节，相比之下 `Box`/`unique_ptr` 树需要 24 字节外加分配器头部。

相对引用的位宽可以是 `u8`、`u16`、`u32`、`u64` 或 `varint`。如果你不在意体积，直接用普通的 `T&`（8 字节）。

<a id="self-relative-vs-pool-relative-links"></a>
### 自相对链接 vs 池相对链接

| 形式 | 偏移量基准 | 适用场景 |
| --- | --- | --- |
| `T&<u32>` | 字段自身；目标必须在同一个数组中 | 池是局部变量或参数，或者你想把整个结构直接作为字节保存/移动 |
| `T&<u32 in pool>` | 具名全局数组 `pool` 的基址，或全局 root struct 字段如 `pool.items` 的基址 | 其他数组也需要指向这个池，或者你需要频繁改写链接 |

持有自相对链接的值不能被拷贝（否则偏移量会指向别处）。用 `push` 原地构建它，并用 `.=` 绑定它。

<a id="doubly-linked-list-with-a-sentinel"></a>
### 带哨兵的双向链表

`self` 可以把一个非可选链接初始化为指向正在构造的值本身，因此循环链表可以拥有哨兵节点而完全不需要任何空链接。节点保存在 `slot_pool` 中（见[非嵌套生命周期](#non-nested-lifetimes-shared_ptr-and-rc-become-pools)），它通过 `alloc_index_with` 在槽位内原地构造哨兵：

```
import slot_pool;

struct Node { val: i64, prev: Node&<u32 in pool.items>, next: Node&<u32 in pool.items> }

var pool = slot_pool<Node> {};
let head = pool.alloc_index_with() { Node { val: 0, prev: self, next: self } };

fn insert_after(p: Node&, v: i64) -> Node& {
    let n .= pool.alloc_ref(Node { val: v, prev: p, next: p.next });
    n.next.prev .= n;
    p.next .= n;
    return n;
}

fn remove(n: Node&) {
    n.prev.next .= n.next;
    n.next.prev .= n.prev;
    pool.free_ref(n);
}
```

<a id="recursion-over-a-tree"></a>
### 树的递归遍历

递归需要显式启用：将递归环的入口标记为 `recursive fn`，并把环中每个函数的完整签名都写出来。累加器按引用传入（原因见“拥有多个动态数组的结构体”）：

```
recursive fn in_order(n: Node&, out: i32[>..]&) {
    let l = n.left;
    if l { in_order(l, out); }
    out.push(n.key);
    let r = n.right;
    if r { in_order(r, out); }
}
```

<a id="expression-trees-and-asts"></a>
### 表达式树与 AST

把变长模式 enum 与窄位宽相对链接结合起来。每个节点只占用它所属变体那么大的空间：

```
enum Expr { Num { v: f64 }, Add { l: Expr..&<u16>, r: Expr..&<u16> } }

fn eval(e: Expr.Num&) -> f64 { e.v }
recursive fn eval(e: Expr.Add&) -> f64 { eval(e.l) + eval(e.r) }

var pool: Expr..[>..] = [];
let a = pool.push(Expr.Num { v: 1.5 });
let b = pool.push(Expr.Num { v: 2.0 });
let sum = pool.push(Expr.Add { l: a, r: b });   // 整棵树只占 23 字节
```

<a id="graphs-and-indexing-a-packed-array"></a>
### 图与紧致数组的索引

对于邻接表，`index types` 特性可以把 `u32` 节点 id 变成编译器已知在界内的类型：`type NodeId = u32 in nodes;`。用 `as NodeId` 转换时只检查一次；之后每一次 `nodes[id]` 都不再需要边界检查。

变长元素的数组（如 `Shape..[>..]`、`u8[][>..]`）不能直接索引。在旁边维护第二个由链接构成的数组，索引那个数组即可：

```
var marks: Mark..[>..] = [];
var mark_at: (Mark..&<u32 in marks>)[>..] = [];
mark_at.push(marks.push(Mark.Circle { r: 1.5 }));
match mark_at[i] { /* ... */ }
```

`in` 形式需要全局数组；对于局部数组，使用每项 8 字节的 `(Mark..&)[>..]`。

<a id="non-nested-lifetimes-shared_ptr-and-rc-become-pools"></a>
## 非嵌套生命周期：`shared_ptr` 与 `Rc` 变成对象池

有些数据确实不是按栈顺序消亡的：缓存条目、正在编辑的图节点、按任意顺序关闭的打开文档、游戏中的实体。C++ 会使用 `new`/`delete` 或 `shared_ptr`；Rust 会使用 `Rc<RefCell<T>>` 或 slab crate。K 的标准库为你提供了 **`slot_pool`**：一个只增数组 `items` 配上一个空闲链表（freelist）。它是一个 root struct（见[拥有多个动态数组的结构体](#structs-that-own-several-vectors-a-root-struct)），因此每个池都是局部变量或全局变量。

```
import slot_pool;

struct Item { id: i32, weight: f32 }
var pool = slot_pool<Item> {};

let s0 = pool.alloc_index(Item { 10, 1.0 });   // 槽位索引
let r  .= pool.alloc_ref(Item { 11, 1.0 });    // 或者直接拿到引用
pool.free(s0);                                 // 0 号槽进入空闲链表
let s2 = pool.alloc_index(Item { 12, 2.0 });   // 复用 0 号槽
pool.free_ref(r);                              // 把引用归还到对应槽位
```

通过 `pool.items` 读取并遍历元素：`pool.items[s2]`、`for item in pool.items`。语言早期的 `reusable` 和 `reusable[]` 池已弃用，由 `slot_pool` 和 `slice_pool` 取代。

**`free` 不会释放任何内存。** 它只是把索引放回空闲链表。每个槽位始终保持是一个存活且类型正确的值，因此指向已复用槽位的陈旧引用只会读到另一个 *不同的* `Item`，绝不会读到垃圾内存。这属于和 `Vec` 陈旧索引同一类的逻辑错误，而不是释放后使用（use-after-free）。内存安全与类型安全依然成立。

由此引出几条实用规则：

- 把已释放的槽位当作陈旧索引来对待：`free` 之后就停止使用指向它的引用。如果你需要检测陈旧引用，就在元素里存一个代际计数器（generation counter）并做比对，就像使用 Rust 的 slotmap key 一样。
- 池永远不会收缩；它的高水位线会一直保持已提交状态，直到所属作用域结束（如果是全局变量，则是程序结束）。`pool.items.len` 统计的是曾经追加过的所有槽位，包括空闲的槽位，遍历 `items` 时也会访问到它们。`os` 模块中的 `trim_memory()` 归还的是各栈顶之上的内存页，而不是已释放的槽位。
- 元素必须是固定大小的。如果每个槽位需要变长数据，请使用下面的 `slice_pool`。
- 引用计数没有直接等价物。选定一个拥有者（通常只有一处）并在那里释放；其他持有者保留引用并接受陈旧槽位语义，或自行检查代际号。

<a id="independently-growing-buffers-slice_pool"></a>
### 各自独立增长的缓冲区：`slice_pool`

当每个对象拥有各自的可增长缓冲区（例如文档的文本、节点的子节点列表）时，使用 `slice_pool`。它从自己的 `items` 数组中分配连续片段，并自动合并相邻的已释放块。

```
// C++: std::unordered_map<DocId, std::unique_ptr<Document>> open;
import slot_pool;
import slice_pool;

struct Document { id: u64, text: u8[:] }      // 文档当前占用的那一段字节

var docs = slot_pool<Document> {};
var bytes = slice_pool<u8> {};

fn open_doc(id: u64, size: i64) -> i64 {      // 返回 docs 中的槽位，相当于 DocId
    return docs.alloc_index(Document { id: id, text: bytes.alloc_slice(size) });
}
fn grow(slot: i64, extra: i64) {
    let d .= docs.items[slot];
    d.text = bytes.realloc_slice(d.text, d.text.len + extra);
}
fn close(slot: i64) {
    bytes.free_slice(docs.items[slot].text);
    docs.free(slot);
}
```

分发槽位索引而不是引用可以让 API 保持简洁：`free` 接收索引，而 `free_ref` 只接受编译器能够精确追溯到那个池的引用——而使用全局 `docs` 的函数中的 `Document&` 参数目前还无法通过这种追溯。`bytes.alloc_copy(text)` 可以在一步之内分配一段空间并从已有切片复制填充。

这是一个单数组内的首次适应（first-fit）分配器，有三个使用 `malloc` 时不会遇到的后果：`realloc_slice` 可能会移动数据段，因此游标请保存为偏移量而不是切片；所有文档共享同一个栈预留空间（默认 2 GB）；并且该数组的高水位线永远不会归还给操作系统。[设计笔记](design/owning_dynamic_data.md)讨论了其他替代方案，例如固定 `u8[..MAX]` 槽位或分块 rope。

<a id="when-a-pool-is-the-wrong-answer"></a>
### 什么时候池不是正确答案

许多“我需要 `shared_ptr`”的情况，真正的需求其实是“我需要它的生命周期比创建它的函数更长”。先检查调用方的作用域能否拥有这些数据，让被调方直接向传下来的数组里 `push`。这种做法零开销，也完全不需要释放。

<a id="structs-that-own-several-vectors-a-root-struct"></a>
## 拥有多个动态数组的结构体：`root struct`

C++ 的 `class Compiler { vector<Node> nodes; vector<Symbol> syms; string out; }` 或 Rust 的 `struct World { entities: Vec<E>, events: Vec<Ev>, ... }` 在 K 中对应 **`root struct`**。它的每个字段都有独立的存储空间，因此三个字段可以各自独立增长：

```
root struct Compiler {
    nodes: Node[>..] = [],
    syms: Symbol[>..] = [],
    out: u8[>..] = [],
}

fn emit(c: Compiler&, s: u8[:]) { c.out.append(s); }

fn compile(source: const u8[:]) -> i64 {
    var c = Compiler { .. };
    c.nodes.push(Node { kind: 1, first: -1, next: -1 });
    emit(c, source);
    return c.out.len;
}   // 三张表在此同时释放；无需清理任何全局状态
```

代价在于放置位置。它的拥有者必须是局部变量或全局变量：它不能是字段、数组元素、拷贝或返回值。函数通过 `Compiler&` 接收它，而且该引用可以像其他引用一样被存储、捕获或设为可选。上面的 `slot_pool` 和 `slice_pool` 就是标准库里的 root struct。

普通 `struct`（也就是能放进数组里的那种）**最多只能拥有一个**可调整大小的数组，而且只能作为它的**最后一个**字段（因为可调整大小的值必须位于自身所在数据栈的栈顶）。

<a id="what-fits-in-an-ordinary-struct"></a>
### 普通结构体里能放什么

- 任意数量的固定大小字段，包括内联有界数组（`u8[..32]`、`Item[..8]`）和定长模式 enum。
- 在构造时一次性建好的变长字段（`u8[]`、`T[varint]`）。
- 一个位于末尾的可增长数组：`struct Stack { name: u8[..8], items: i64[>..<] }`。

如果你的“动态数组”其实有一个不大的真实上界，`T[..k]` 可以把它们内联存放，从而让结构体保持固定大小，这样它也能放进数组里。

<a id="the-context-pattern"></a>
### 上下文（context）模式

当多张表已经作为独立变量存在，或者工作代码还需要访问它并不拥有的数据时，可以把每张表都声明为一个**驱动函数的局部变量**，然后只给工作代码传递一个由引用构成的普通结构体。由引用和切片构成的结构体是固定大小的，传递成本极低。

```
struct Ctx {
    source: const u8[:],
    nodes: Node[>..]&,
    text: u8[>..]&,
    scratch: i64[>..<]&,
}

fn compile(source: const u8[:]) -> i64 {
    var nodes: Node[>..] = [];
    var text: u8[>..] = [];
    var scratch: i64[>..<] = [];
    var c = Ctx { source: source, nodes: nodes, text: text, scratch: scratch };
    return walk(c, 0, 1);
}   // 三张表在此同时释放；无需清理任何全局状态
```

这种结构体适合分散在多个函数或文件中的代码。当代码足够小、可以全部放进驱动函数内部时，嵌套函数可以直接把这些表当作自由变量来访问，连结构体都不需要。

对于程序全局生命周期的状态，全局变量也能用（顶层写 `var log: u8[][>..];` 是合法的），但上下文模式可以免费为你提供每次调用互相隔离的全新状态。

<a id="recursion-cant-keep-growable-locals-across-the-recursive-call"></a>
### 递归不能跨越递归调用保留可增长局部变量

因为每次递归激活都需要自己独立的数据栈，所以编译器禁止处在递归环中的函数在向环内递归调用期间持有可增长数据。有三种绕开方式：

1. **把累加器传进来**：`recursive fn in_order(n: Node&, out: i32[>..]&)` 直接向调用方的数组 `push`。
2. **用作用域限制临时变量，使其在调用前结束**：在一个 `{ }` 块里拼好一行内容、打印出来，然后再递归。
3. **在递归期间共享同一个可增可减栈**，在返回前截断回退到标记位置：

```
recursive fn walk(c: Ctx&, n: i64, depth: i64) -> i64 {
    let mark = c.scratch.len;
    c.scratch.push(depth);            // 本次递归激活的条目
    var total = c.scratch[mark];
    var e = c.nodes[n].first;
    while e >= 0 { total += walk(c, e, depth + 1); e = c.nodes[e].next; }
    c.scratch.resize(mark);           // 返回前清理干净
    return total;
}
```

因此，递归下降解析器会把状态放在非递归入口函数拥有的 root struct 或局部变量中，并按引用向下传递，而不是在递归过程中不断创建解析器对象。

<a id="updating-a-variable-size-field"></a>
### 更新变长字段

像 `u8[]` 或 `T[varint]` 这样的字段，以及变长模式 enum 的值，一旦构建完成就是**冻结**的。如果一条记录里的字符串需要修改，要么给它一个固定容量字段（`u8[..32]`，可赋值），要么重建整个容器。同一条记录中的固定大小字段可以自由赋值。

<a id="errors-three-lightweight-tools-instead-of-result-or-exceptions"></a>
## 错误处理：三种轻量工具取代 `Result` 与异常

K 没有 `Result` 类型，没有 `?` 运算符，也没有栈展开（unwinding）异常。K 为你提供了三种工具，按错误传播的距离来选择。

| 场景 | C++ / Rust | K |
| --- | --- | --- |
| 两种结果，调用方当场处理 | `optional<T>`, `Result<T, E>` | 末尾追加 `bool`：`-> T, bool` |
| 找到或未找到 | `T*` / `Option<&T>` | `T?`，指向输入数据的可空引用 |
| 在深处失败，在很上层处理 | 异常，层层传递 `?` | `return E from f`，或 `exception` 模块 |
| Bug 或不变量破坏 | `assert`, `panic!` | `assert(c)`, `abort(msg)`, `exit(code)` |

<a id="trailing-bool"></a>
### 末尾 `bool`

```
fn divide(a: i64, b: i64) -> i64, bool {
    if b == 0 { return 0, false; }
    return a / b, true;
}
let q, ok = divide(7, 2);
guard ok else { return; }
```

多返回值是一种调用约定，而不是元组。库函数也遵循这一约定：`let n, ok = parse_int(s);`。

<a id="optional-references"></a>
### 可选引用

```
let big = xs.find() { it > 100 };   // 指向 xs 的 T?，没找到则为 null
if big { print(big); }               // 已收窄：块内 big 是 T&
guard big else { return 0; }         // 或者：当前块后续代码中均已收窄
```

<a id="return--from-the-long-distance-return"></a>
### `return … from`：跨层长距离返回

`return E from f` 会把 `E` 作为当前最内层活跃调用 `f` 的结果直接返回，跳过中间的所有栈帧。中间的函数对此毫无感知：签名里不需要写 `Result`，调用处也不需要写 `?`。

```
fn load(text: u8[:]) -> User[>..], u8[] {          // 返回 users 和错误消息
    var users: User[>..] = [];
    var lineno = 0;
    each_split(text, '\n') {
        lineno++;
        if it.len > 0 { users.push(parse_record(it, lineno)); }
    };
    return users, "";
}

fn parse_record(line: u8[:], lineno: i64) -> User {   // 普通 User，没有 Result
    let comma = find(line, ",");
    guard comma >= 0 else {
        return [], str("line ", lineno, ": expected name,age") from load;
    }
    let age, ok = parse_age(line[comma + 1..]);
    guard ok else { return [], str("line ", lineno, ": bad age") from load; }
    return User { name: trim(line[..comma]), age: age };
}
```

这是静态检查的：通往 `return … from load` 的每一条调用路径都必须处于对 `load` 的调用内部，因此运行时绝不会出现“未捕获”的情况。它的代价仅仅是在退出路径上每个栈帧检查一次标志位，没有任何栈展开表。这让它足够廉价，甚至可以用于解析器里的每一个语法错误，而不只是灾难性故障。

<a id="typed-trythrow"></a>
### 类型化 `try`/`throw`

当你不想在每个抛出点都点名目标函数时，`exception` 模块对同一套机制做了封装：

```
import exception;

fn load() { exception::throw(str("cannot load item ", 42)); }

exception::try<u8[]>(load) { error => print(error); };
```

错误类型在 `try` 处固定（用 `u8[]` 表示拥有所有权的文本，或整数，或你自己的结构体/enum）。抛出的错误只能携带指向全局或静态数据的引用，因此请把文本拷贝到拥有所有权的 `u8[]` 中。这里没有 `finally`，且 `abort`/`exit` 不可捕获。

<a id="dont-port-errno-style-or-sentinel-heavy-code"></a>
### 不要照搬 `errno` 风格或大量哨兵值

按照库约定，索引使用 `-1` 表示“无”（`find`、`position`），这没问题。对于任何更丰富的状态，优先使用末尾 `bool` 或 `T?` 而不是魔数，因为 `T?` 会强制调用方在使用前先收窄。

<a id="raii-and-destructors-memory-frees-itself-everything-else-is-explicit"></a>
## RAII 与析构函数：内存自动释放，其余一切显式管理

K 没有析构函数，也没有 `Drop`。对于内存来说你根本不需要它们：离开作用域就会重置数据栈，而且既然 K 内存里没有任何东西指向外部分配，也就没有任何东西需要遍历清理。拆毁一个包含一百万个元素的结构只需一次存储指令。

RAII 在 C++ 和 Rust 中还负责释放*非内存*资源：文件、GPU 缓冲区、套接字、锁。在 K 中，这些资源表现为句柄（handle），需要你显式释放。

| 资源 | C++ / Rust | K |
| --- | --- | --- |
| 文件内容 | 由析构函数关闭的 `ifstream` | `read_file(path, out)` / `write_file(path, data)`：整文件操作，没有需要关闭的句柄 |
| GPU 缓冲区、纹理、管线 | 带析构函数的封装类 | `gfx::Buffer { id }` 句柄；调用 `gfx::release(b)` |
| 物理世界、刚体 | 拥有所有权的对象 | `physics::destroy(w)` |
| 互斥锁守卫 | `lock_guard`, `MutexGuard` | 不需要：线程不共享内存 |
| 作用域计时器、“退出时撤销” | 守卫对象 | 自己在代码块末尾执行对应操作 |

库句柄携带代际号，因此在释放后继续使用会报告为误用，而不会崩溃。为了在所有退出路径上都能确定性地完成清理，K 提供了 `defer`（按 LIFO 顺序在块退出时执行）、`errdefer`（仅在通过 `return ... from` 或 `exception::throw` 错误展开时执行）以及类 C# 的 `using` 语法：

```klang
using let res = open_handle();        // 退出作用域时自动调用 close() / release()
using (let tmp = acquire()) { ... }   // 仅在语句块作用域内有效
```

配合 `own Handle` 线性包装类型（详见 `reference.md` 中的 C 句柄所有权章节），外部资源释放可与数据栈回收一起在编译期得到静态验证。

<a id="threads-no-shared-memory-only-typed-queues"></a>
## 线程：没有共享内存，只有类型化队列

K 中没有共享可变内存，因此也没有 `Mutex`、`Arc`、原子操作或内存序。工作线程是一个 `thread_fn`；编译器会把它以及它调用的所有内容编译成一个独立程序，拥有各自的数据栈以及**它所用到的每一个全局变量的独立副本**。值通过队列跨线程传递，每种类型对应一个队列。

```
// Rust: let results = Arc<Mutex<Vec<R>>>; 启动工作线程加锁并 push。
struct Job    { n: i32 }
struct Result { n: i32, digits: i32 }

thread_fn worker() {
    loop {
        let job = qget<Job>();             // 阻塞等待
        guard job.n >= 0 else { break; }   // -1 表示停止
        qput(Result { n: job.n, digits: count_digits(job.n) });
    }
}

fn main() {
    let n = max(1, hardware_threads());
    for i in 8 { qput(Job { n: (1 << (i * 3)) as i32 }); }
    for i in n { qput(Job { n: -1 }); }    // 每个工作线程一个停止任务
    var ids: i64[>..] = [];
    for i in n { ids.push(thread_spawn(worker)); }
    var total = 0;
    for i in 8 { let r = qget<Result>(); total += r.digits; }
    for id in ids { thread_wait(id); }
}
```

实际编写中的变化：

- **只有扁平值能跨线程传递**：任何深度下都不能包含引用或切片。因为扁平值可以包含变长部分，所以一个任务可以携带真实数据，如 `struct Row { y: i32, cells: u8[] }`，而跨线程传递只是一次 memcpy。
- **结果以消息形式返回**，而不是写入共享向量。在 `main` 中收集它们，并按 id 字段重新组装。
- **在工作线程里递增全局变量只会修改该工作线程自己的副本。** 共享计数器需要改成消息。
- **对大数组做并行 for 循环**意味着将它切分成块，把每一块（或其索引范围加上所需数据）作为任务发送出去，再合并结果。
- **带有主线程约束的库**（`gfx`、`ui`、`audio`、`physics`）如果从 `thread_fn` 可达，会在编译期被拒绝。
- 线程需要真正的 C 编译器构建（`k build` 或 `-o`）；进程内的 TinyCC 运行器无法放置线程局部存储（TLS）。

<a id="serialization-your-data-is-already-a-file-format"></a>
## 序列化：你的数据本身就是文件格式

在 C++ 和 Rust 中，保存带指针链接的结构体需要序列化器（serde、protobuf 或手写代码），因为进程外的地址毫无意义。在 K 中，使用自相对链接（`T&<u32>`）和内联嵌套值的数据是位置无关的，因此保存它就是直接写出它的字节。

```
struct Node { word: u8[..24], count: i32, left: Node&<u32>?, right: Node&<u32>? }
var index: Node[>..] = [];
// ... 构建树 ...

let image: u8[>..] = index.to_bytes();              // 带帧头的字节映像
write_file("index.bin", image);

var loaded, ok = from_bytes<Node[>..]>(image);      // 经过校验的加载
```

`from_bytes` 在交回普通数组之前会校验帧头、每一个标签、每一个长度以及每一个链接，因此损坏或恶意构造的文件只会返回 `false`，绝不会产生野引用。它检查的是安全性，而不是完整性：如果你需要确认文件确实是你当初写入的那份，请自行添加校验和。

`bytes_of(a)` 是元素字节的零拷贝只读视图，可直接用于 `write_file` 或上传到 GPU（`gfx::vertex_buffer(bytes_of(vertices))`）。

设计启示：如果某个结构将来需要保存，在布局时请优先选择自相对链接，而不是 `T&` 和 `in pool` 链接。对于外部格式，可以使用 `binary`（带检查的小端读取）、`json`（写入器、树解析器，或直接解析进自定义类型的流式读取器）、`csv` 和 `base64`。

<a id="smaller-habits-that-change"></a>
## 日常习惯的变化

这些细节常在上手第一天下午让人踩坑。

| 习惯 | 在 K 中 |
| --- | --- |
| `cond ? a : b` | `if` 本身是表达式：`let k = if hp > 50 { "ok" } else { "hurt" };` |
| 先声明变量，再在各分支赋值 | 把 `if`/`match`/`block` 当作表达式，或声明 `let n: i64;` 并保证仅赋值一次（静态检查） |
| 一连串 `if (!x) return` 提前退出 | `guard x else { return; }`；在 `block { … }` 中，用 `break value` 带值退出 |
| Rust / C++ 中 `let` / `const` 的含义 | `let` = 不能重绑变量名，但内容仍可写；`const T` = 内容只读；`var` = 可重绑 |
| `*p`, `p->x`, `&*p` | 引用是透明的：`p.x`。`.=` 绑定或重绑定，`.==` 比较地址 |
| `std::tuple`，返回 `(a, b)` | `-> A, B` 与 `let a, b = f();`。没有元组类型，且 `if` 分支不能产出 `a, b` |
| 在表达式里写 `x++` | `++` 和赋值都只能是语句 |
| 对整数写 `if (n)` | 编译报错：请写 `if n != 0`。`if` 用在可选值上会进行收窄 |
| `-7 % 3 == -1` | `%` 是欧几里得取模：`-7 % 3 == 2`，结果总在 `[0, abs(b))`。`std` 中的 `rem` 提供 C 语言行为 |
| 隐式提升为 `int` | 算术按操作数位宽执行；`u8 + u8` 仍是 8 位。`u32 + i32` 会报错。收窄转换需要 `as`（调试模式检查）或 `as!`（不检查） |
| 有符号溢出是 UB / 调试时 panic | 调试模式直接中止（abort），发布模式回绕（wrap）。无符号始终回绕 |
| `for (auto& x : v) x *= 2;` | `for &x in xs { x *= 2; }`。普通的 `for x in xs` 对固定大小元素给出只读拷贝 |
| `printf("%d %s", …)` | `print("n=", n, " s=", s)`；需要填充和精度时用 `format_int` / `format_flt` |
| `std::optional<T&>`, `Option::map`, `unwrap_or` | `T?` 通过 `if`/`guard` 收窄，支持可选链 `x?.field` 与空合并 `opt ?? fallback` |
| Rust `Foo { a: 1, ..base }` 与结构体解构 | `Foo { a: 1, ..base }`、`let { a, b } = foo;`、前导点枚举简写 `.Variant` |
| 具名函数实参 | `f(x, flag: true)`（参数名需与函数声明一致） |
| `fn main()` / `async fn main()` / 无 main 脚本 | 支持显式 `fn main()`、`async fn main()`，以及无 `main` 的顶级语句和顶级 `await` |
| 带标签的 `break 'outer` | v1 不支持标签。改用 `block`、带 `return` 的嵌套函数，或标志位 |
| 递归“直接写就行” | 将递归环标记为 `recursive fn`，写出完整签名，不要在递归路径上保留可增长局部变量 |
| 独立编译、`.so` 插件 | 全程序编译为 C。K 的库以源码包形式提供（`k build`） |

<a id="bounds-checks-and-speed"></a>
### 边界检查与性能

每一次索引都会进行边界检查，而编译器会消除它能证明安全的检查。就像你协助自动向量化器那样协助编译器：在计算核心循环顶部放一条针对长度的 `assert`，就能让它消掉循环里的所有检查。`k --bce-lines` 可以按行显示哪些检查保留了下来，而 `checkfree fn` 会把任何残留检查直接变成编译错误。对于 `dist[adj[k]]` 风格的间接索引，索引类型（`u32 in nodes`）会把检查前移到创建 id 的位置。

<a id="what-doesnt-translate-and-the-nearest-workaround"></a>
## 无法直接转换的设计及最接近的替代方案

K 的速度与安全性来源于封闭世界与静态所有权。少数设计依赖的恰恰是它所放弃的东西。

| 你的需求 | 为什么没有 | 最接近的 K 写法 |
| --- | --- | --- |
| 运行时插件、由外部模块扩展的开放类继承体系 | 没有函数指针、没有虚表、全程序编译 | 用 enum 列出已知种类；真正需要外部代码时，通过 `extern fn` / `extern "lib" { … }` 接入 C |
| 存进结构体或从函数返回的闭包 | 函数值必须留在编译期 | 存储一个 enum 标签加上捕获的数据；用 case 函数派发 |
| `Vec<Vec<T>>`，每个内层 vector 独立增长 | 数组元素不能是可调整大小的 | 使用来自 `slice_pool<T>` 的切片、扁平数组加偏移量，或在有界时用 `T[..k]` 行 |
| 对变长记录数组进行随机访问 | 每个元素的偏移量取决于前面所有元素 | 改用带内联容量的定长元素，或在旁维护一个由引用构成的索引数组 |
| 获取指向定长模式 enum 载荷的引用 | 它随时可能被另一个变体覆盖 | 改用变长模式（`T..`）存储，或者拷贝、修改后再重新赋值 |
| 原地改变紧凑（`T..`）元素的变体 | 它的邻居紧挨着排在它后面 | 改用定长模式，或重建容器 |
| 带自动最后拥有者清理的 `shared_ptr` | 没有引用计数，没有析构函数 | 选定一个拥有者；用 `slot_pool` + 显式 `free`；加代际字段检测陈旧引用 |
| 线程间的共享可变状态 | 设计上禁止共享内存 | 通过类型化队列发送消息 |
| 每个对象拥有多个可增长成员的对象数组 | root struct 必须是局部变量或全局变量 | 用一个 root struct 为每个成员各持有一个字段，对象只存槽位索引，或用 `slice_pool` 管理每对象片段 |
| 子进程、网络通信 | 不在 v1 标准库中 | 通过 `extern fn` 调用 C |

一条经验法则：当某段代码编不过时，请把整条报错读完。它会把两端都点出来，比如引用和它被绑定的位置，或者持有者和往里存值的那一行，这通常就能直接告诉你该移动哪个作用域或绑定。

<a id="sources"></a>
## 参考资料

- [K 语言文档索引](index.md)
- [K 语言实例教程（Tutorial）](tutorial.md)
- [K 语言参考手册](reference.md)
- [K 语言标准库](stdlib.md)
- [设计笔记：独立拥有的动态数据](design/owning_dynamic_data.md)
