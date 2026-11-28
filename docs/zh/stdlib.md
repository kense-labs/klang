<a id="the-k-standard-library"></a>
# K 标准库

<details>
<summary>目录</summary>

- [std](#std)
- [exception](#exception)
- [dictionary](#dictionary)
- [slot_pool](#slot_pool)
- [slice_pool](#slice_pool)
- [vec](#vec)
- [mat](#mat)
- [quat](#quat)
- [math](#math)
- [os](#os)
- [ffi](#ffi)
- [binary](#binary)
- [base64](#base64)
- [csv](#csv)
- [json](#json)
- [regex](#regex)
- [gfx](#gfx)
- [audio](#audio)
- [physics](#physics)
- [ui](#ui)

</details>

标准库在 `stdlib/` 下包含二十个模块：`std`、`dictionary`、`slot_pool`、`slice_pool`、`vec`、`mat`、`quat`、`math`、`os`、`ffi`、`binary`、`base64`、`csv`、`json`、`regex`、`audio`、`gfx`、`physics`、`ui` 和 `exception`。按模块名导入各个模块，例如 `import std;`。编译器会在其源码树中定位标准库；可使用 `--stdlib <dir>` 或 `K_STDLIB` 选择其他位置。除以下通过 `extern fn`（规范 §7.10）调用的部分外，其余全部用 K 编写：

* `os` 背后的 C 实现（`src/runtime/runtime_os.h`）；
* `std` 中 `find_any` 和 `find_pair` 背后的字节搜索（`src/runtime/runtime_impl.h`）；
* `std` 中的 `popcount`、`clz` 和 `ctz`，以及 `ffi` 的两份副本（`src/runtime/runtime_ext.h`）；
* `math` 背后的 libm；
* `gfx` 背后的图形层、`ui` 背后的 UI 层以及 `audio` 背后的 PCM 混音器，均位于 `packages/gfx` 包中；
* `physics` 背后的物理层，位于 `packages/physics` 包中。

这两个包都随 K 一同发布。构建时，它们的 CMake 工程会解析固定版本的原生依赖。原生层的说明见各包文档：[图形](design/gfx.md) · [UI](design/ui.md) · [音频](design/audio.md) · [物理](design/physics.md)。本文档为参考手册。

对于**文本渲染、字体和游戏 HUD**，请先查看基于 `gfx` 渲染的 [`ui`](#text-rendering-and-game-huds)。对于外部二进制文件格式，请使用 [`binary`](#binary)；`from_bytes` 用于读取 K 自身的序列化格式。文本格式各有对应的模块：[`json`](#json)、[`csv`](#csv) 和 [`base64`](#base64)，[`regex`](#regex) 则使用正则表达式搜索文本。

标准库遵循以下约定：

* 原地读取或修改元素的函数接收 `xs: T[:]`；所有数组种类和切片都会隐式转换为该类型：`sort(arr)`、`sort(arr[1..])`。
* 会改变长度的函数接收 `xs: A&`，即整个数组的引用，并针对传入的数组种类进行实例化，前提是该数组种类支持所用到的操作。只要调用方中没有任何指向只增数组的引用或切片（规范 §5.1），只增数组就可以通过该引用收缩，因此 `remove_at(xs, i)` 可用于 `[>..]` 局部变量；若存在可能被失效的活动引用或切片，检查器会拒绝该调用。
* 任何非固定大小的值都不会按值传递（规范 §4.1）：`f(xs)` 按引用绑定，需要自有副本的函数会调用 `copy(xs)`。
* 新建数组以 `T[>..]` 形式返回，并直接在调用方的目标位置构建：`let ys = filter(xs) { it > 0 };`、`out.append(map(xs) { … })`。
* “可能存在的元素”表示为 `T?`：指向输入中元素的引用，或为 null 表示不存在。可使用 `if`/`guard` 收窄其类型。位置类型为 `i64`，`-1` 表示不存在。双结果标量函数返回一个尾部 `bool`：`let v, ok = parse_int(s);`。
* 代码块用法：`xs.find() { it > 3 }`、`sort(xs) { a, b => a.age < b.age }`。比较器的语义始终是“a 排在 b 之前”。排序、堆以及极值函数均提供带代码块和不带代码块两种形式；不带代码块时使用 `<` 进行比较。
* 文本输出写入调用方传入的 `u8[>..]&` 构建器中；`str(…)` 是返回新字符串的形式。
* 适用 UFCS：`xs.sort()`、`d.insert(k, v)`、`r.rand_int(6)`。
* 按*值*接收元素的函数（`push_n`、`insert_at`、`fill`、`heap_push`）不能接收包含自相对引用的值，因为这些值无法复制（规范 §3.9）。请原地构造它们。
* 移动元素的函数（`swap_at`、`reverse`、`shuffle`、`sort`、`stable_sort`、`fill`、`copy_from`、`insert_at`、`remove_at`、`swap_remove`、`retain`、`dedup` 以及各堆函数）会移动元素自身（包括引用）：`reverse(refs)` 会重排引用数组，而它们所指的对象保持不变；指向不可复制值的引用数组（规范 §3.9）同样可以排序。这些函数按泛型代码的常规方式使用 `.=` 存储整个元素（规范 §3.8）。相对引用数组（自相对引用或 `in pool`）也以相同方式重排。`T` 局部变量或参数保存的是普通引用，每次将该引用存回元素时都会重新编码。对于自相对链接，这要求该链接指向所属元素的根对象内部（规范 §3.9），而在其自身数组内移动的链接满足这一条件。`swap(a, b)` 交换 `a` 和 `b` 所指的值。`remove_at`、`swap_remove` 和 `heap_pop` 返回被移除值的副本；若元素是引用，则返回其所指对象的副本。按值传递引用元素（`insert_at`、`heap_push`）时写作 `&x`，此时 `T` 为引用类型。`stable_sort` 可以对 `in pool` 链接数组排序，但其临时数组无法保存自相对链接的副本（规范 §3.9）。
* `std`、`dictionary`、`slot_pool`、`slice_pool`、`vec`、`mat`、`quat`、`math` 和 `os` 位于全局命名空间；`ffi`、`binary`、`base64`、`csv`、`json`、`regex`、`audio`、`gfx`、`physics`、`ui` 和 `exception` 使用各自的命名空间，同理还有供 `std` 和 `json` 读写浮点文本的辅助模块 `float_text`，以及 `std` 的 ASCII 字符分类模块 `ascii`。名为 `fill` 或 `count` 的局部变量会遮蔽同名的全局函数，因此调用该函数会报错；而同名的全局变量不会遮蔽它，因为函数调用中的名称会越过全局变量解析到函数（规范 §11.1）。切片分配器的实现类型与辅助函数位于 `slice_pool_detail` 中；应用程序应使用公开的池操作。

<a id="std"></a>
## std

<a id="scalars-and-bits"></a>
### 标量与位运算

```klang
fn min<T>(a: T, b: T) -> T          fn max<T>(a: T, b: T) -> T
fn clamp<T>(x: T, lo: T, hi: T) -> T
fn abs<T>(x: T) -> T                // integers; abs(f32) and abs(f64) overloads
fn rem<T>(a: T, b: T) -> T         // integers; pairs with `/`
fn div_euclid<T>(a: T, b: T) -> T  // integers; pairs with `%`
fn sign<T>(x: T) -> T               // -1, 0, 1 at x's type; f32/f64 overloads
fn lerp(a: f64, b: f64, t: f64) -> f64      // and f32
fn next_pow2(x: i64) -> i64         // smallest power of two >= max(x, 1); aborts past 2^62
fn popcount(x: u64) -> i64
fn clz(x: u64) -> i64               // 64 for 0
fn ctz(x: u64) -> i64               // 64 for 0
fn rotl<N>(x: u32, n: N) -> u32     // and u64; rotr rotates right
fn byte_swap(x: u32) -> u32         // and u16, u64
fn is_pow2<T>(x: T) -> bool         // integers; false for x <= 0
fn ilog2<T>(x: T) -> i64            // floor(log2(x)); -1 for x <= 0
fn isqrt<T>(x: T) -> T              // floor(sqrt(x)); aborts for x < 0
fn gcd<T>(a: T, b: T) -> T          // never negative; gcd(0, 0) == 0
fn lcm<T>(a: T, b: T) -> T          // never negative; 0 if either is 0
fn swap<T>(a: T&, b: T&)            // swap(x, y)
```

这些函数均不保留越界检查，因此 `checkfree` 函数（规范 §7.14）可以调用它们。

`popcount`、`clz` 和 `ctz` 在运行时中实现。只要 C 编译器提供对应指令（MSVC 的 `popcount` 需要 `/arch:AVX` 或更高版本），每个函数都会编译为单条指令，而在 TinyCC 下则是循环。`rotl`、`rotr` 和 `byte_swap` 同样由运行时实现，在 GCC、clang 和 MSVC 下各自编译为单条指令；其余函数均以 K 编写。

`rotl` 和 `rotr` 接收任意整数类型的位移计数并对位宽取模，因此所有计数均有定义，负计数则向相反方向旋转：对于 `u32`，`rotl(x, -1) == rotr(x, 1)` 且 `rotl(x, 32) == x`。更窄的整数值会同等地拓宽到两个重载，因此需要显式指定旋转位宽，例如 `rotl(b as u32, 4)`；`byte_swap` 同样按精确类型接收 `u16`、`u32` 或 `u64`。当对数不存在时，`ilog2` 返回 -1，就像搜索在无匹配位置时返回 -1 一样，因此正数 `x` 所需的位数为 `ilog2(x) + 1`。

`gcd` 和 `lcm` 接收任意整数类型并对绝对值进行计算：`gcd(-12, 18) == 6` 且 `lcm(-4, 6) == 12`。当结果超出类型表示范围时——例如 `gcd(i64.min, 0)`（2^63）或 `u8` 类型下 16 和 17 的 `lcm`——都会触发 `assert` 失败，包括无符号类型在内；回绕后的值既不是公约数也不是公倍数。`isqrt` 对所有 `u64` 值都能精确计算，无需经过浮点数。

`rem` 是截断除法的余数：`rem(-7, 3) == -1`，与 `-7 / 3 == -2` 配对。`div_euclid` 是与欧几里得取模 `%` 配对的商：`div_euclid(-7, 3) == -3` 且 `-7 % 3 == 2`。它对正除数向下取整，对负除数向上取整（`div_euclid(-7, -3) == 3`）。因此，坐标对应的图块和偏移量分别为 `div_euclid(x, width)` 和 `x % width`。

这两个函数都返回操作数的整数类型，并在除数为零时中止。`rem(MIN, -1)` 为零，包括 `i64` 在内；`div_euclid` 遵循 `/` 的溢出规则（`i64.min, -1` 总是中止；更窄的有符号位宽在调试构建下中止、在发布构建下回绕）。无符号类型的 `rem` 和 `div_euclid` 等价于普通的 `%` 和 `/`。这两个函数均不接收浮点数；浮点数的 `%` 本身就是截断余数（`fmod`）。当商可表示时，恒等式 `b * (a / b) + rem(a, b) == a` 和 `b * div_euclid(a, b) + a % b == a` 在数学上成立，但计算其中间乘积时可能会发生溢出。

<a id="hashing"></a>
### 哈希

```klang
fn hash(x: i8) -> u64               // ... one overload per integer type
fn hash(x: bool) -> u64             fn hash(x: f32) -> u64      fn hash(x: f64) -> u64
fn hash(s: u8[:]) -> u64            // 8 bytes per multiply; any u8 array coerces
fn hash_combine(seed: u64, h: u64) -> u64
```

这些哈希函数将所有输入混合到低 32 位中。标量重载和 `hash_combine` 不保留越界检查，因此 `checkfree` 函数（规范 §7.14）可以调用它们。`dictionary` 会掩码取这些位以定位主槽位，并将其保留为槽位的标签。浮点数按其位模式计算哈希，`-0.0` 按 `0.0` 处理，`f32` 按其值对应的 `f64` 处理。`dictionary` 也会识别用户键类型的 `hash` 重载，无论它声明在全局还是键类型的命名空间中（它会调用 `key.hash()`，§11.1），用户自定义重载也应当做到这一点，就像对各字段的哈希执行 `hash_combine` 一样：

```klang
struct key { a: i64, b: i64 }
fn hash(k: key) -> u64 { hash_combine(hash(k.a), hash(k.b)) }
```

<a id="random-numbers"></a>
### 随机数

```klang
struct rng { s: u64 }                    // splitmix64; the seed is the state
fn rand_u64(r: rng&) -> u64
fn rand_int(r: rng&, n: i64) -> i64      // uniform in [0, n), without modulo bias; asserts n > 0
fn rand_range(r: rng&, size: i64) -> i64            // uniform in [0, size); asserts size > 0
fn rand_range(r: rng&, lo: i64, size: i64) -> i64   // uniform in [lo, lo + size); asserts size > 0
fn rand_flt(r: rng&) -> f64              // uniform in [0, 1), 53 bits
fn rand_f32(r: rng&) -> f32              // uniform in [0, 1), 24 bits
fn rand_gaussian(r: rng&) -> f64         // standard normal distribution: mean 0, variance 1
fn shuffle<T>(xs: T[:], r: rng&)         // Fisher-Yates
```

```klang
var r = rng { os.random_seed() };   // or any fixed seed
let d = r.rand_range(6);            // 0 to 5
let roll = r.rand_range(1, 6);      // a die: 1 to 6
let height = 170.0 + 7.5 * r.rand_gaussian();
```

与切片的长度一样，`rand_range` 接收的是数量而不是结束位置，因此 `rand_range(r, lo, n)` 恰好有 `n` 种可能的结果；`lo + size - 1` 必须能容纳于 `i64`。不传 `lo` 时默认从 0 开始，与 `rand_int` 相同。`rand_flt` 和 `rand_f32` 取一次抽取的高 53 位或高 24 位，因此每个结果都是 2^-53 或 2^-24 的倍数且等概率出现。`rand_gaussian` 使用 Marsaglia 极坐标法从标准正态（高斯）分布中采样：它不断从 `rand_flt` 抽取二维点对直到落入单位圆内，平均大约需要 1.27 次尝试。该方法一次产生两个样本，但 `rng` 只保存种子状态，因此第二个样本会被丢弃。在任何 `libm` 的 `log` 给出相同结果的平台上，同一种子在每次运行中生成的序列都完全一致。

<a id="arrays-reading-and-searching"></a>
### 数组：读取与搜索

```klang
fn each_rev<T, F>(xs: T[:])                    // F(x), last to first
fn each_chunk<T, F>(xs: T[:], n: i64)          // F(chunk: T[:], i); asserts n > 0; last chunk may be short
fn find<T, F>(xs: T[:]) -> T?                  // first x with F(x), as a reference
fn find_index<T, F>(xs: T[:]) -> i64
fn position<T>(xs: T[:], v: T) -> i64          // first i with xs[i] == v
fn contains<T>(xs: T[:], v: T) -> bool
fn any<T, F>(xs: T[:]) -> bool                 fn all<T, F>(xs: T[:]) -> bool
fn count<T, F>(xs: T[:]) -> i64                // number of x with F(x)
fn min<T>(xs: T[:]) -> T                       // asserts xs.len > 0; min(xs) { a, b => ... } too
fn max<T>(xs: T[:]) -> T
fn min_by<T, F>(xs: T[:]) -> i64               // index of the smallest F(x); -1 if empty
fn max_by<T, F>(xs: T[:]) -> i64
fn last<T>(xs: T[:]) -> T&                     // asserts xs.len > 0
fn lower_bound<T, F>(xs: T[:]) -> i64          // first i with !F(xs[i]); F true on a prefix
fn binary_search<T>(xs: T[:], v: T) -> i64, bool   // sorted by < (or { a, b => ... }): position, found
fn find<T>(xs: T[:], sub: T[:]) -> i64         // first occurrence of a subsequence, -1 if none
fn rfind<T>(xs: T[:], sub: T[:]) -> i64
fn starts_with<T>(xs: T[:], p: T[:]) -> bool   fn ends_with<T>(xs: T[:], p: T[:]) -> bool
```

`find`/`rfind`/`starts_with`/`ends_with` 对元素类型是泛型的，因此它们也是字符串处理函数，例如 `find(line, "://")`。`find` 和 `rfind` 仅在首元素匹配的位置上比较整个子序列。（查找索引的函数命名为 `position` 而不是 `index_of`，因为 `index_of` 是将元素引用转换为其索引的内建操作。）`min(xs)`、`max(xs)`、`min_by` 和 `max_by` 不保留越界检查，因此 `checkfree` 函数（规范 §7.14）可以调用它们；只要比较器本身也不保留越界检查，其带比较器的形式同样可以调用。

```klang
let big = xs.find() { it > 100 };
if big { print(big); }
var row = 0;
each_chunk(pixels, width) { line, y => row += line.len; };
```

<a id="byte-sets"></a>
### 字节集

```klang
struct ByteSet { members: bool[256], ... }      // a set of bytes, prepared for searching
fn byte_set(members: bool[256]) -> ByteSet
fn byte_set(bytes: u8[:]) -> ByteSet            // the bytes listed: byte_set(",\"\n")
fn find_any(s: const u8[:], set: const ByteSet&) -> i64
                                                // first i with s[i] in the set, -1 if none
fn find_pair(s: u8[:], a: const ByteSet&, d: i64, b: const ByteSet&) -> i64
                                                // first i with s[i] in a and s[i + d] in b
                                                // (i + d < s.len), -1 if none; d >= 0
```

这些函数在运行时中执行扫描。在 x86-64 上，它们使用每颗此类 CPU 都支持的 SSE2 每次测试 16 个字节；若集合既不是最多三个字节、也不是单个连续区间或两者的补集，则会通过半字节（nibble）查表实现，这需要 SSSE3 支持，运行时会在程序启动时检查该特性。在 AArch64（小端序、使用 clang 或 gcc 构建）上，所有集合类型都使用每颗此类 CPU 都支持的 NEON，且当搜索长度足够时，`find_any` 每次可测试 64 个字节。在其他平台以及 JIT 运行模式下，它们逐字节进行测试，而单字节集合会使用 C 库的 `memchr` 搜索。`byte_set` 会遍历全部 256 个字节以决定测试方式，因此请在搜索循环外将集合构造并保存在变量中一次即可。`members` 表示集合成员，其余字段供搜索使用。`find_pair` 用于已知匹配中相距 `d` 的两个字节时的预过滤检查；[`regex`](#regex) 就用这种方式检查模式前导字节中最罕见的两个字节。

```klang
let special = byte_set(",\"\n");
var pos = 0;
loop {
    let k = find_any(text[pos..], special);
    if k < 0 { break; }
    pos += k;
    handle(text[pos]);
    pos += 1;
}
```

<a id="arrays-transforming"></a>
### 数组：变换

```klang
fn map<T, F>(xs: T[:])                          // -> U[>..], U the block's result type
fn filter<T, F>(xs: T[:]) -> T[>..]
fn fold<T, A, F>(xs: T[:], acc: A) -> A         // acc = F(acc, x)
fn sum<T>(xs: T[:]) -> T                        // from default<T>()
fn concat<T>(a: T[:], b: T[:]) -> T[>..]
```

```klang
let squares = xs.map() { it * it };
let evens = xs.filter() { it % 2 == 0 };
let total = fold(xs, 0.0) { a, x => a + x };
```

<a id="arrays-in-place"></a>
### 数组：原地操作

```klang
fn fill<T>(xs: T[:], v: T)
fn copy_from<T>(dst: T[:], src: T[:])           // equal lengths; first to last
fn copy_into<T>(dst: T[:], src: T[:])           // deprecated name of copy_from; a call warns
fn reverse<T>(xs: T[:])
fn sort<T>(xs: T[:])                            // by <; sort(xs) { a, b => ... } by the block
fn stable_sort<T>(xs: T[:])                     // natural merge sort; one temporary of xs.len elements
fn to_lower(s: u8[:])                           // ASCII, in place
fn to_upper(s: u8[:])
```

`copy_from` 保留了从头到尾逐元素复制的语义。`dst.copy_from(src)` 的写法与调用行为一致：从 `src` 复制到 `dst`。`copy_into` 是该函数的旧名称，参数不变，调用时会发出警告（规范 §7.15）并提示使用 `copy_from`。对于在切片重叠时仍能保留源值的带检查复制（要求元素为固定大小且可复制），请使用语言内建操作 `dst.move_from(src)`。

`sort` 是基于 pdqsort 和 Rust ipnsort 的模式击败快速排序（pattern-defeating quicksort）。它是不稳定排序（相等元素的相对顺序未指定），原地工作且不分配内存。其分区过程不依赖比较分支，连续相等键只需线性扫描而非 n log n；若分区持续划分不佳，则会退回堆排序，因此最坏时间复杂度为 O(n log n)。

`stable_sort` 是自然归并排序（natural merge sort）：它找出已经有序（或严格降序，找到后原地反转）的序列段，并在 `xs` 与位于新数据栈上的一个长度为 `xs.len` 的临时数组之间逐趟归并。

对于已有序或严格降序的输入，两者都只做单趟扫描；对于此类输入或长度不超过 20 的输入，两者均不分配内存。若比较器不是严格弱序（如 `<=`，或对含 NaN 的浮点数使用 `<`），元素顺序将未指定，但结果始终是输入的一个排列，且两个函数都不会中止。

可以对 `let` 数组进行排序，因为 `let` 禁止的是重新绑定而不是写入元素。`const` 数组或只读切片则不能排序（规范 §9.5）。

<a id="arrays-changing-the-length"></a>
### 数组：改变长度

```klang
fn push_n<A, T>(xs: A&, v: T, n: i64)           // n pushes
fn insert_at<A, T>(xs: A&, i: i64, v: T)        // shifts [i..) up; needs push
fn remove_at<A>(xs: A&, i: i64) -> T            // shifts down; needs pop
fn swap_remove<A>(xs: A&, i: i64) -> T          // O(1), reorders
fn retain<A, F>(xs: A&)                         // keeps x with F(x), in order; needs resize
fn dedup<A>(xs: A&)                             // drops adjacent duplicates
fn heap_push<A, T>(xs: A&, v: T)                // min-heap by <; block forms of all three
fn heap_pop<A>(xs: A&) -> T
fn make_heap<A>(xs: A&)                         // O(n)
```

`[>..<]` 是天然的堆与队列容器；定长上限的 `[..k]` 字段同样适用。

堆为二叉堆，`i` 的子节点位于 `2i + 1` 和 `2i + 2`。其上滤和下滤过程通过移动“空洞（hole）”而非交换元素实现：待放置的元素暂存在局部变量中，每一层将一个元素移入空洞，最后在空洞停下的位置写入一次。`heap_pop` 与 libstdc++ 和 Rust 的做法相同：先将空洞从根沿优先级更高的子节点一路下移到叶节点，再将末尾元素从该位置向上筛选。这样每层只需约一次比较，而从根直接下滤则需两次。对于满足严格弱序的比较器，每次操作后堆的布局都与教科书式的交换筛选完全一致（包括相等键的情况），因此相等元素出堆的顺序也完全相同。若比较器在运行期间读取堆内容，会看到某个槽位中存着其相邻元素的副本，而不是等待在外的那个元素。

```klang
var q: i64[>..<] = [];
q.heap_push(5); q.heap_push(1);
let smallest = q.heap_pop();
```

<a id="strings"></a>
### 字符串

```klang
fn format_int<B>(out: B&, v: i64, base: i64, width: i64, padc: u8)   // base 2..36, right-aligned; aborts outside it
fn format_uint<B>(out: B&, v: u64, base: i64, width: i64, padc: u8)   // B: u8[>..] or u8[..k]
fn format_flt(out: u8[>..]&, v: f64, decimals: i64)        // fixed decimals, the exact value rounded half up; inf/-inf/nan as print writes them
fn parse_int(s: u8[:], base: i64 = 10) -> i64, bool        // optional sign, whole string; false outside i64 or base 2..36
fn parse_uint(s: u8[:], base: i64 = 10) -> u64, bool       // optional '+', whole string; false on '-' or outside u64 or base 2..36
fn parse_int_bits(s: const u8[:], signed: bool, base: i64 = 10) -> u64, bool
fn parse_flt(s: u8[:]) -> f64, bool                        // sign, fraction, exponent, or inf/-inf/nan; correctly rounded
fn format_uleb(out: u8[>..]&, v: i64)                     // LEB128 (3.6), 1-10 bytes
fn parse_uleb(s: u8[:]) -> i64, i64                       // value and byte count; 0, 0 if malformed or not shortest
fn compare(a: u8[:], b: u8[:]) -> i64                      // bytewise: -1, 0, 1
fn trim(s: u8[:]) -> u8[:]                                 // ASCII whitespace; a sub-slice
fn trim_start(s: u8[:]) -> u8[:]                           fn trim_end(s: u8[:]) -> u8[:]
fn split(s: u8[:], sep: u8) -> const u8[:][>..]            // read-only slices into s; empty parts kept
fn split(s: u8[:], sep: u8[:]) -> const u8[:][>..]         // matched from the left; an empty sep: [s]
fn each_split<F>(s: u8[:], sep: u8)                        // F(part); no array built
fn each_split<F>(s: u8[:], sep: u8[:])
fn split_once(s: u8[:], sep: u8) -> const u8[:], const u8[:], bool    // before, after the first sep
fn split_once(s: u8[:], sep: u8[:]) -> const u8[:], const u8[:], bool // not found: s, "", false
fn words(s: u8[:]) -> const u8[:][>..]                     // split at runs of ASCII whitespace; no empty words
fn each_word<F>(s: u8[:])                                  // F(word)
fn join<T>(out: u8[>..]&, parts: T[:], sep: u8[:])         // T any u8 array/slice type
fn format_replaced(out: u8[>..]&, s: u8[:], old: u8[:], with: u8[:])
fn each_utf8<F>(s: u8[:])                                  // F(codepoint); malformed bytes give 0xFFFD
fn push_utf8(out: u8[>..]&, cp: i64)                       // 0xFFFD's bytes for cp outside 0..0x10FFFF
fn ascii::is_digit(c: u8) -> bool                          // '0'-'9'
fn ascii::is_alpha(c: u8) -> bool                          // 'a'-'z', 'A'-'Z'
fn ascii::is_alnum(c: u8) -> bool                          // either
fn ascii::hex_value(c: u8) -> i64                          // 0-15 for 0-9, a-f, A-F; -1 otherwise
```

内建函数 `format`/`str`/`print` 负责标量与字符串的常规格式化；这里的函数则提供对输出格式的精细控制以及解析功能。在十进制下，`format_int` 和 `format_uint` 写出的文本（在填充之后）与 `format` 相同，且运行速度一样快。当 `base` 超出 2..36 时，`format_int` 和 `format_uint` 会中止。遇到无效进制、格式错误或溢出时，整数解析函数返回 `0, false`。当需要由输入本身决定数值是否有符号时，请使用 `parse_int_bits`：它在有符号模式下采用与 `parse_int` 相同的语法与范围，在无符号模式下采用与 `parse_uint` 相同的语法与范围；在有符号模式下，通过 `bits as! i64` 即可恢复有符号值。`parse_flt` 返回与十进制文本最接近的 double 值，与 C 的 `strtod` 行为一致，因此由 `print` 或 `format` 输出的浮点文本可以无损读回为原值，包括它们为非有限值输出 `inf`、`-inf` 和 `nan` 的情况。字符串在 K 中即 `u8` 数组：输入为 `u8[:]`，输出写入 `u8[>..]&` 构建器，存储形式为 `u8[]` 或 `u8[..k]`。

`split_once` 在首次出现 `sep` 处切分 `s`，行为与 Go 的 `strings.Cut` 相同：返回分隔符前后的两部分（均为指向 `s` 的只读切片）以及 `true`。当 `s` 中不包含 `sep` 时，返回完整的 `s`、一个空切片和 `false`，因此无论是否找到，第一部分都代表整个字段。与 `split` 一样，空的 `sep` 永远不会被匹配。

这些字符分类函数仅限 ASCII，与 C locale 下的 `<ctype.h>` 以及 Rust 的 `u8::is_ascii_digit` 等函数一致：所有 0x80 及以上的字节都不属于任何分类。它们位于 `ascii` 命名空间中，该命名空间随 `import std;` 一并引入，这样像 `is_digit` 这类简短常用的名字在全局命名空间中仍保持自由可用。

```klang
let text = "1f8B rest";
var v = 0;
var i = 0;
while i < text.len && ascii::hex_value(text[i]) >= 0 {
    v = v * 16 + ascii::hex_value(text[i]);
    i++;
}
print(v, " ", i, " ", ascii::is_alpha(text[i + 1]));   // 8075 4 true
```

`each_utf8` 按 WHATWG 解码器规则解码。被截断的序列、超长编码形式、代理项（surrogate）或超过 0x10FFFF 的码点都会为每个最大子部分（Unicode §3.9）产出一个 0xFFFD，而打断序列的字节会作为下一序列的起始字节：`[0xC3, 'A']` 会依次给出 0xFFFD 和 `A`。`push_utf8` 会把代理项按其自身码值编码为三字节写出，`json` 正是通过这种方式保留孤立的 `\u` 转义。

```klang
var line: u8[>..] = [];
format(line, "x=");
format_int(line, x, 16, 8, '0');
each_split(text, '\n') { handle(trim(it)); };
let n, ok = parse_int(trim(field));
let key, value, has_value = split_once(arg, '=');   // "-o=out": "-o", "out", true
let cmd = words(input);                   // "take  the sword": ["take", "the", "sword"]
if cmd.len > 1 && cmd[0] == "take" { take(cmd[cmd.len - 1]); }
```

`format_uleb` 和 `parse_uleb` 读写序列化映像使用的长度前缀（`design/serialization.md`）。单独写出该前缀后，程序无需先复制到新映像中即可保存 `bytes_of` 视图；流式读取器通过读取该前缀即可获知后续负载的字节数。

```klang
let payload = pool.bytes_of();            // a view, nothing copied
var head: u8[>..] = [];
format_uleb(head, payload.len);
write_file(path, head); append_file(path, payload);
```

`format_flt` 对 `v` 的精确二进制值在任意大小和任意小数位数下执行四舍五入：`format_flt(out, 0.1, 20)` 输出 `0.10000000000000000555`，与 C 的 `printf("%.20f")` 相同；`format_flt(out, 2.675, 2)` 输出 `2.67`，因为最接近 2.675 的 double 值略小于 2.675。只有当数值恰好位于中点时，舍入结果才与采用“四舍六入五成双”的 `printf` 不同：`format_flt(out, 0.125, 2)` 输出 `0.13`。而普通的 `format` 会输出能无损读回为同一数值的最短文本。

<a id="exception"></a>
## exception

基于 `return ... from`（规范 §7.9）在 K 中实现的类型化异常，灵感来自 [Lobster 的 exception 模块](https://github.com/aardappel/lobster/blob/master/modules/exception.lobster)。通过 `import exception;` 导入；其函数位于 `exception` 命名空间中。

```klang
fn try<E, Body, Catch>()            // Body(); on an error, Catch(error)
fn try<E, Body>()                   // Body(); discard an error
fn try<E, Body, Catch, Finally>()   // as the first, then Finally() on every way out
fn throw(value)                    // exit the nearest enclosing handler
```

```klang
import exception;

fn load() {
    // This can be any number of calls below try.
    exception::throw(str("cannot load item ", 42));
}

exception::try<u8[]>(load) { error => print(error); };
```

`E` 是错误类型，需在 `try` 处显式指定：可用拥有所有权的文本 `u8[]`、整数，或应用自定义的结构体或枚举（变长模式枚举写作 `Error..`）。抛出的值必须适配类型 `E`，且 `E` 必须可作为数组元素使用。body 和 catch 均为静态函数值；K 只允许一个尾随代码块，因此请至少具名传入其中一个回调。body 的返回值会被丢弃；它可以更新所捕获的输出变量。

处理程序在成功时返回空的 `E[]`，失败时返回仅含该错误的单元素数组。这直接使用普通的数据栈，无需 `E` 具有默认值，并允许将 `0`、`false` 和 `""` 作为错误抛出。catch 在处理程序返回后运行，因此在 catch 中调用 `throw` 会抛向外层的 `try`；若要重新抛出拥有所有权的变长错误，请使用 `exception::throw(copy(error))`。同样，首次抛出已存在的非固定大小值时也需要 `copy`；新构造的值和字符串切片则可直接传入。拥有所有权的文本会在包含它的栈帧退出前完成复制。

语言的静态检查与限制依然适用：

* 每一处 `throw` 在所有编译期调用路径上都必须有对应的处理程序，包括从 catch 中抛出的异常。错误类型不兼容会导致编译错误。
* 远距离返回所携带的引用必须指向全局变量或静态数据。请使用拥有所有权的 `u8[]`（而非指向局部字符串的切片），并在错误记录中使用拥有所有权的字段。
* 递归代码可以向外层处理程序抛出异常，但 `try` 本身不能被递归重入：其非固定大小的处理结果会在递归调用期间持续占用数据栈（§7.8）。
* 检查器会传播永不返回到调用方的调用。声明了返回值的函数可以以 `exception::throw(error);` 结尾，抛出异常的分支也无需提供返回值。这并不会赋予 `throw` 值类型：请将其用作语句或分支，而不是写作 `return throw(error);` 或作为需要值的参数。递归推理是保守的（规范 §7.3）。
* 仅捕获对 `throw` 的调用。运行时错误、`abort` 和 `exit` 依然不可捕获。不提供栈追踪（stack-trace）机制。

`try<E>(body, catch, finally)` 会在 body 正常完成、捕获到抛出的异常之后，以及当抛出异常或指向外层函数的词法 `return` 穿过该调用离开时执行 `finally`。它在 try 之前等价于 `defer finally();`（参考手册，“defer”），因此 `finally` 遵循 defer 体的规则：它不能向外层返回，也不能抛出、捕获异常或调用任何会这样做的函数。

<a id="dictionary"></a>
## dictionary

```klang
struct dictionary<K, V> { count: i64, slots: dictionary_slot<K, V>[>..<] }
fn get<K, V>(d: dictionary<K, V>&, key: K) -> V?               // reference to the value, or null
fn contains<K, V>(d: dictionary<K, V>&, key: K) -> bool
fn insert<K, V>(d: dictionary<K, V>&, key: K, val: V) -> bool   // overwrites; true if new
fn get_or_insert<K, V>(d: dictionary<K, V>&, key: K, v0: V) -> V&
fn update<K, V, F>(d: dictionary<K, V>&, key: K, v0: V)         // F(val&); inserts v0 first if absent
fn remove<K, V>(d: dictionary<K, V>&, key: K) -> bool
fn clear<K, V>(d: dictionary<K, V>&)
fn reserve<K, V>(d: dictionary<K, V>&, n: i64)                  // room for n entries without a rehash
fn each<K, V, F>(d: dictionary<K, V>&)                          // F(key, val&); order unspecified
```

该哈希表采用线性探测的开放寻址法、2 的幂容量以及后移删除（backward-shift deletion）。键和值均为固定大小；键类型需要实现 `hash` 和 `==`。每个槽位保留其键哈希的 31 位作为标签，因此查找时仅在标签一致的位置比较键，扩容与删除时也无需重新对键求哈希（在最多 2^31 个槽位的表中）。`insert`、`update` 和 `get_or_insert` 无论键是否已存在都只查找一次。表在达到 2/3 负载因子时原地扩容翻倍；`reserve(n)` 会按该负载因子为 `n` 个条目预留容量。字符串键可以使用指向调用方所持文本的 `u8[:]` 切片（字面量或 `const` 数据的视图请用 `const u8[:]`，§9.5），或使用内联的 `u8[..k]`。集合可表示为 `dictionary<K, bool>`。

槽位数组是可增删数组，因此指向其中的引用（`get` 或 `get_or_insert` 的结果，或 `each` 的绑定变量）必须在下一次 `insert`、`update`、`remove` 或 `clear` 之前完成最后一次使用。否则检查器会在修改处报错，并指出该引用及其绑定位置（规范 §5.2）。请先结束该引用的使用，或使用在一次调用中完成查找与修改的 `update`：

```klang
var counts = dictionary<const u8[:], i32> {};
each_split(text, ' ') { counts.update(it, 0) { it += 1; } };
counts.each() { w, n => if n > 100 { print(w, " ", n); } };
let n = counts.get("the");
if n { print(n); }               // the last use of n; counts is free again after it
```

检查器关注的是活跃度（liveness）而非作用域：最后一次使用发生在修改之前的引用无需额外包裹在代码块中，而在修改之后仍被使用的引用无论声明在何处都会报错。上例的键类型为 `const u8[:]`，因为字符串字面量和 `split` 返回的只读视图需要该类型（§9.5）。保存可写切片的字典可以使用 `dictionary<u8[:], V>`，无论底层缓冲区是用 `let` 还是 `var` 绑定的。

只要是可选类型，值也可以是引用：例如 `Cell?`，或指向池的链接如 `Cell&<u32 in cells>?`。未使用的槽位保存 `default<V>()`，而非可选引用没有默认值。对已存在的键执行 `insert` 会存入新引用，而不是通过旧引用写入其指向的对象。`get` 和 `get_or_insert` 返回存储的引用本身，`update` 和 `each` 的代码块也接收该引用（引用的 `V?` 和 `V&` 即该引用本身，§3.8；若为相对引用则会先加载）。通过它写入会修改所指对象；若要替换该引用本身，则需调用 `insert`。对于值为 null 的键和不存在的键，`get` 均返回 null；可使用 `contains` 区分两者。此类引用指向的是其所指对象而非槽位数组内部，因此它可以跨越后续的字典修改继续保持活跃：

```klang
var owner = dictionary<i64, Cell?> {};
owner.insert(7, cells[3]);
let c = owner.get(7);
owner.insert(7, cells[4]);       // cells[3] is unchanged, and c still refers to it
if c { c.v += 1; }
```

<a id="migrating-from-reusable"></a>
## 从 reusable 迁移

`reusable` 和 `reusable[]` 已被弃用；它们的内建操作目前仍可使用。请按需导入 `slot_pool`（用于单个槽位）或 `slice_pool`（用于连续片段）。

| 现有写法 | 标准库写法 |
|---|---|
| `reusable var p: T[>..] = [];` | `var p = slot_pool<T> {};` |
| `reusable[] var p: T[>..] = [];` | `var p = slice_pool<T> {};` |
| 池参数 `p: T[>..]&` | `p: slot_pool<T>&` 或 `p: slice_pool<T>&` |
| `p[i]`, `p.len`, `for x in p` | `p.items[i]`, `p.items.len`, `for x in p.items` |
| `u32 in p`, `Node&<u32 in p>` | `u32 in p.items`, `Node&<u32 in p.items>` |
| `p.alloc_ref(value)` | 可复制值写法相同；原地构造使用 `p.alloc_with() { fresh_value }` |
| `p.alloc_index(value)` | 可复制值写法相同；原地构造使用 `p.alloc_index_with() { fresh_value }` |
| `p.free(i)` | 写法相同；当元素本身不是引用时也可使用 `p.free_ref(node)` |
| `p.alloc_slice(n)`, `p.free_slice(s)`, `p.realloc_slice(s, n)` | `slice_pool` 上方法名相同 |
| 分配一个切片，再向其中复制数据 | `p.alloc_copy(source)` |

这两个库都会维持稳定且已初始化的底层存储。`items.len` 是历史最高水位计数（high-water count），其中包含已释放的条目，迭代也会访问这些条目。已释放的引用和视图仍然有效，且能够观察到后续复用写入的新值；逻辑上的释放后使用（use-after-free）和重复释放（double-free）检测需要应用层自行维护记录。`recycle_all` 使所有存储空间重新变为可复用，但不会收缩存储或清除原有值。切勿直接收缩/替换 `items` 或修改分配器的内部记账状态。

通过 `free_ref` 释放来自错误池的槽位时，依然保留 `index_of` 的编译期检查。释放来自错误池的非空切片目前会在运行时触发包含性检查失败（`slice_pool.free_slice: slice is not from this pool`）；目前尚无编译期警告。来自任何来源的空切片均无害。与标准库中的所有检查一样，池的检查也会报告调用它们的程序源码行号。符合条件（eligible）的 `items` 可以单独序列化，但这会省略分配器状态。整个根所有者无法自动序列化或复制到 worker 中。对于扁平元素，[worker 快照示例](../../samples/32_pool_worker_snapshots.k) 展示了如何将 `items` 和分配器元数据显式复制到 worker 本地的根所有者中。其切片元数据类型属于实现细节，请使该做法与标准库保持同步，不要将其视为稳定的序列化格式。

池引用可以存储在普通结构体中、被闭包捕获、在所有者存活期间返回，以及声明为可选类型。这比内建池引用更加灵活。拥有数据的根对象本身仍仅限局部变量或全局变量；嵌套的根所有者目前尚不支持（deferred）。

<a id="slot_pool"></a>
## slot_pool

`import slot_pool;` 通过普通的根结构体字段和函数提供稳定、可复用的槽位。请使用它来替代已弃用的 `reusable` 语言特性。

```klang
root struct slot_pool<T> {
    items: T[>..] = [],
    free_slots: i64[>..<] = []
}

fn alloc_index<T>(pool: slot_pool<T>&, value: T) -> i64
fn alloc_ref<T>(pool: slot_pool<T>&, value: T) -> T&
fn alloc_with<T, F>(pool: slot_pool<T>&) -> T&
fn alloc_index_with<T, F>(pool: slot_pool<T>&) -> i64
fn free<T>(pool: slot_pool<T>&, i: i64)
fn free_ref<T>(pool: slot_pool<T>&, value: T&)
fn recycle_all<T>(pool: slot_pool<T>&)
```

声明一个局部或全局所有者，并通过 `items` 进行普通的索引、切片、迭代和 `index_of` 操作：

```klang
import slot_pool;

fn main() {
    var pool = slot_pool<i64> {};
    let first .= pool.alloc_ref(10);
    let index = pool.items.index_of(first);
    pool.free(index);
    assert(pool.alloc_index(20) == index);
    print(first);                         // 20: the same slot, now reused
}
```

元素必须是固定大小的。无需默认值：每次分配都会提供值。`alloc_index` 和 `alloc_ref` 接收可正常复制的值。每次分配会优先取出最近释放的索引，若无空闲槽位则追加到末尾。`alloc_index` 返回其索引；`alloc_ref` 返回指向该值的引用。对于引用类型的元素，`T&` 会照常折叠为 `T`；当需要标识槽位本身时，请使用 `alloc_index`。整元素赋值会替换引用类型元素的值，而不会通过该引用写入其所指对象。

`alloc_with` 和 `alloc_index_with` 接收一个返回 `T` 的零参数函数或尾随代码块；后者直接返回选定的索引。它们会先选定槽位，然后调用一次构建器，并将结果直接构造到该槽位上。这支持自相对链接以及池相对的 `self`：

```klang
import slot_pool;
struct Node { value: i64, me: Node&<u32>, next: Node&<u32>? }
fn main() {
    var nodes = slot_pool<Node> {};
    let first .= nodes.alloc_with() { Node { value: 1, me: self } };
    let second .= nodes.alloc_with() { Node { value: 2, me: self, next: first } };
    let index = nodes.items.index_of(second);
    nodes.free(index);
    nodes.alloc_with() { Node { value: 3, me: self, next: first } };
    assert(second.value == 3 && second.me .== second);
}
```

新生成的结果也可以来自其他函数。已存在的自相对值依然不可复制，且链接必须指向接收数组内部。尾随代码块会自动推导结果类型；对于引用类型的 `T`，请传入显式返回该引用类型的具名函数。

与按值传参不同，构建器在空闲索引被取出之后才会运行。构建器内部释放的槽位会保留在空闲列表中供后续分配使用。新追加的槽位仅在构造完成时才会并入 `items.len`；若在完成前发生非局部返回，则不会发布该槽位。复用不具备事务性：非局部退出不会恢复已弹出的索引，也不会撤销初始化过程中的副作用。常规的构造规则会禁止在构建器执行期间对 `items` 进行冲突的扩容操作。对回收索引的越界检查会在构建器运行前执行。

`free` 在所有构建模式下都会检查 `0 <= i < items.len`，然后记录该索引。它会保留已初始化的元素，不会收缩数组。现有引用依然有效，并能观察到后续替换写入的新值。与 `reusable` 一样，重复释放和使用逻辑上已释放的槽位属于应用程序错误；池本身不跟踪占用状态或代数（generation）。`items.len` 是曾追加过的槽位总数（包括已释放的槽位），迭代也会遍历这些槽位。

请将 `free_slots` 视为内部记账数据。不要收缩或替换 `items`，也不要直接编辑空闲列表：这样做会破坏池的逻辑状态。普通的数组越界检查和生命周期检查依然适用。`recycle_all` 按索引顺序使所有槽位重新变为可用，同时保留已初始化的值和底层存储；它不会收缩 `items`、销毁值或使现有引用失效。`free_ref` 将 `index_of` 与 `free` 结合，并保留前者的编译期来源检查；对于引用类型的元素，由于其引用指向的是被引用对象而非池槽位，请改用索引释放。符合条件的 `items` 成员可以单独序列化，但其序列化映像不包含空闲列表。

对于全局 `var pool`，紧凑链接和索引类型可以引用其数组：`Node&<u32 in pool.items>`、`u32 in pool.items` 和 `u32 upto pool.items`。使用普通链接的池相对值是可复制的，可以配合 `alloc_ref` 使用；自相对值和 `self` 初始化器则需使用 `alloc_with` 在分配的槽位上原地构造。根所有者无法复制到 worker 中；允许使用 worker 本地的池。参见[迁移计划](design/root_structs.md#separate-follow-up-reusable-pools-in-the-stdlib)。

<a id="slice_pool"></a>
## slice_pool

`import slice_pool;` 提供可复用的固定大小元素连续片段。`alloc_slice` 和 `realloc_slice` 要求元素类型具有默认值；`alloc_copy` 仅要求值可复制。所有者的声明形式为 `var pool = slice_pool<T> {};`。请使用它替代已弃用的 `reusable[]` 语言特性。这两个内置特性目前仍可用于既有代码，它们的移除将是未来单独的一步。

```klang
fn alloc_slice<T>(pool: slice_pool<T>&, n: i64) -> T[:]
fn alloc_copy<T>(pool: slice_pool<T>&, src: T[:]) -> T[:]
fn free_slice<T>(pool: slice_pool<T>&, s: T[:])
fn realloc_slice<T>(pool: slice_pool<T>&, s: T[:], n: i64) -> T[:]
fn recycle_all<T>(pool: slice_pool<T>&)
fn trim(pool: slice_pool<u8>&) -> bool
```

```klang
import slice_pool;
fn main() {
    var pool = slice_pool<i64> {};
    var entries = pool.alloc_slice(3);
    entries[0] = 7;
    entries = pool.realloc_slice(entries, 5);
    print(entries);                         // [7, 0, 0, 0, 0]
    pool.free_slice(entries);
    let again = pool.alloc_slice(2);
    print(again, " ", pool.items.len);       // [0, 0] 5
}
```

分配按索引顺序选择第一个能容纳的空闲区间（free span）。若没有合适的区间，则扩容底层数组，并优先利用位于尾部的任何空闲区间。空闲区间会与相邻的空闲区间合并。一棵由 512 字节节点构成的增强型 B+ 树通过树遍历和有界节点扫描来处理首次适应（first-fit）查找与邻接区间查询；清空后的节点会被复用。这棵树的代码沿用了运行时 `reusable[]` 的实现，但在副本中会对从 `nodes` 和 `tree` 读取的数据进行校验，因此其分配策略与之一致。

`alloc_slice` 会对其分出的每一个元素进行默认初始化，包括复用的元素。`alloc_copy` 直接从源切片初始化，不会先用默认值填充。它支持与 `items` 重叠的视图，且不会释放源切片。例如，`strings.alloc_copy(text)` 会分配并复制一个字符串。`realloc_slice` 保留前 `min(s.len, n)` 个元素，将缩减出的尾部归还给树，并对每个新增元素执行一次默认初始化。当处于数组末尾，或紧邻其后的空闲区间足够大（包括尾部扩展）时，扩容会原地进行。否则，它会释放旧片段并选择第一个适应位置，按重叠安全的顺序进行复制。默认表达式不得修改或重入同一个池。每一个已发布的底层数组元素都是已初始化的。

`free_slice` 和 `realloc_slice` 接受池片段的子范围。它们会检查非空切片是否对齐且完全位于 `items` 之内，即使其来源仅在运行时才能确定。空切片或默认切片可以安全地释放；扩容空切片会分配一个新片段。负数或过大的计数会在树被修改之前于所有构建版本中触发中止（abort）。`items.range_index(s)` 可恢复非空片段的索引；对所有空切片均返回零。

释放和缩减片段不会缩减 `items` 或使旧视图失效。保留的视图会观察到其原始地址上后续的替换内容；重定位不会改变其指向。重复释放、由无效释放引起的活跃分配重叠，以及访问逻辑上已释放的片段，均属于应用程序错误。`items.len` 是高水位长度，而非活跃元素的数量。请将 `nodes` 和 `tree` 视为内部账本数据，不要缩减或替换 `items`。常规的边界和生命周期检查仍然适用于这些公开字段。运行时会检查从它们读取的每一个节点编号和条目计数：若程序写错了其中之一，将触发中止并提示 `slice pool span tree is corrupt`。若某个区间被改写为起始位置超出 `items`，则会从提取它的操作中触发中止并提示 `span tree is corrupt`；其他被改写的区间可能会将元素重复分出两次，就像重复释放一样。`recycle_all` 将整个高水位范围变成一个空闲片段并回收所有元数据节点。它会保留已初始化的值、底层存储以及现有视图；下一次分配将从索引零开始。

`trim` 将 `u8` 池中空闲片段的物理内存归还给操作系统（在 Windows 上还会归还其提交内存量 commit charge），这与 `os::trim_memory` 对栈顶之上内存的处理方式一致：它会丢弃每个空闲区间内部的完整内存页，这些页重新读取时将返回零——这本就是 `alloc_slice` 赋予它们的值。由于空闲区间已与相邻区间合并，因此只有同时包含活跃元素的页才会被保留。活跃片段、视图和 `items.len` 均保持不变，地址范围也保持保留状态，因此进程的虚拟内存大小不会下降。复用了该区间的分配会重新获取其内存页。与 `trim_memory` 一样，它是显式调用的：释放操作仍保持与之前一样的低开销，长期运行的程序应在关闭大型缓冲区后调用 `trim`，而不是每次释放后都调用。读取已释放的片段是应用程序错误，但在没有调用 `trim` 时，这种读取在池复用该片段前仍会看到旧字节；而在调用 `trim` 后会看到零，因此添加 `trim` 调用可能会改变仍在读取已释放片段的程序的行为。在 Windows 上，通过陈旧视图读取已丢弃的页会重新提交（commit）该页，因此读取已释放片段会抵消这些页的精简效果。该方法仅限于 `u8`，因为丢弃后的内存页必须能够作为有效元素读回。返回 `false` 表示操作系统请求失败。

该所有者遵循与 `slot_pool` 相同的放置、序列化和工作线程（worker）限制：符合条件的 `items` 需单独序列化，并构造工作线程本地的所有者，而非转移根所有者。`alloc_slice` 原地构造默认值，因此分配和释放支持具有默认值的自相对（self-relative）元素；`realloc_slice` 要求元素通常可复制，与内置特性一致。全局索引/池相对类型可以引用 `pool.items`，与 `slot_pool` 相同。当 `T` 具有默认值时，支持指向单独所有存储的可选引用以及已声明的字段默认值。

关于与 `reusable[]` 的对比（包括保留函数调用边界的情况），请参阅 [slice-pool 基准测试报告](bench/slice_pool.md)。

<a id="vec"></a>
## vec

```klang
struct vec2<T> { x: T, y: T }       struct vec3<T> { x: T, y: T, z: T }
struct vec4<T> { x: T, y: T, z: T, w: T }
type float2/float3/float4 = vec*<f32>   double2/3/4 = vec*<f64>   int2/3/4 = vec*<i32>
let float3_0 = float3 { 0.0, 0.0, 0.0 };  // 所有九个别名均提供 _0 和 _1

fn dot<T>(a: vec3<T>, b: vec3<T>) -> T          // 支持所有维度
fn cross<T>(a: vec3<T>, b: vec3<T>) -> vec3<T>
fn length_sq<T>(v: vec3<T>) -> T
fn length<T>(v: vec3<T>) -> T                   // 浮点向量
fn normalize<T>(v: vec3<T>) -> vec3<T>
fn distance<T>(a: vec3<T>, b: vec3<T>) -> T
fn lerp<T>(a: vec3<T>, b: vec3<T>, t: T) -> vec3<T>
fn xy<T>(v: vec3<T>) -> vec2<T>                 // 亦提供 xy(vec4)、xyz(vec4)
```

这些函数均不保留边界检查，因此 `checkfree` 函数（规范 §7.14）可以调用它们。

语言内置了逐元素运算符 `+ - * /`（`a + b`、`p - q`），以及对浮点或有符号整数向量的一元负号（`-v`）。`*` 和 `/` 还支持任一顺序的标量广播：`v * 2.0`、`v / length(v)`、`1.0 / v`；`v *= scale` 和 `v /= scale` 会原地更新向量。标量会转换为分量类型，因此用 `f64` 变量缩放 `float3` 时需要显式写出 `as f32`。逐分量的 `min`/`max` 需手动展开编写（`min(a.x, b.x)`）。

<a id="mat"></a>
## mat

用于 2D 和 3D 变换的方阵，基于 `vec` 的向量构建。命名沿用 GLSL，GLSL 中没有的名称则采用 glm 的命名。

```klang
type float2x2 = float2[2]   float3x3 = float3[3]   float4x4 = float4[4]   // 亦提供 double2x2/3x3/4x4
let float4x4_identity                              // 所有六个别名均提供

fn mul<T>(a: vec4<T>[4], b: vec4<T>[4]) -> vec4<T>[4]   // GLSL a * b：先 b 后 a
fn mul<T>(m: vec4<T>[4], v: vec4<T>) -> vec4<T>         // GLSL m * v
fn mul<T>(v: vec4<T>, m: vec4<T>[4]) -> vec4<T>         // GLSL v * m
fn transpose<T>(m: vec4<T>[4]) -> vec4<T>[4]            // 以下函数支持所有三种尺寸
fn determinant<T>(m: vec4<T>[4]) -> T
fn inverse<T>(m: vec4<T>[4]) -> vec4<T>[4]
fn outer_product<T>(c: vec4<T>, r: vec4<T>) -> vec4<T>[4]
fn row<T>(m: vec4<T>[4], i: i64) -> vec4<T>             // 列为 m[i]
fn to3x3<T>(m: vec4<T>[4]) -> vec3<T>[3]                // GLSL mat3(m4)；亦提供 to2x2(3x3)
fn to4x4<T>(m: vec3<T>[3]) -> vec4<T>[4]                // GLSL mat4(m3)；亦提供 to3x3(2x2)

// 3D，以 4x4 表示
fn translate<T>(t: vec3<T>) -> vec4<T>[4]     fn scale<T>(s: vec3<T>) -> vec4<T>[4]
fn rotate<T>(axis: vec3<T>, angle: T) -> vec4<T>[4]     // 亦提供 rotate_x/y/z(angle)
fn perspective<T>(fovy: T, aspect: T, near: T, far: T) -> vec4<T>[4]
fn ortho<T>(left, right, bottom, top, near, far: T) -> vec4<T>[4]
fn ortho<T>(left, right, bottom, top: T) -> vec4<T>[4]  // near -1, far 1：用于 2D
fn look_at<T>(eye: vec3<T>, target: vec3<T>, up: vec3<T>) -> vec4<T>[4]
fn transform_point<T>(m: vec4<T>[4], p: vec3<T>) -> vec3<T>    // w = 1，并除以 w
fn transform_vector<T>(m: vec4<T>[4], v: vec3<T>) -> vec3<T>   // w = 0
// 2D，以齐次 3x3 表示
fn translate<T>(t: vec2<T>) -> vec3<T>[3]     fn scale<T>(s: vec2<T>) -> vec3<T>[3]
fn rotate<T>(angle: T) -> vec3<T>[3]                    // 逆时针方向
fn transform_point<T>(m: vec3<T>[3], p: vec2<T>) -> vec2<T>    // 亦提供 transform_vector
```

除 `row` 的索引会受检查外，其余函数均不保留边界检查，因此 `checkfree` 函数（规范 §7.14）可以调用它们。

与 GLSL 中一样，矩阵是其各列的定长数组：`m[i]` 是第 `i` 列，`m[3]` 是 4x4 矩阵的平移向量，而 `m[i].y` 是该列第 1 行的元素。4x4 矩阵的字节布局与 GLSL `mat4` 在 std140 或 std430 块中的布局一致（2x2 对应 std430 的 `mat2`）；而 GLSL 的 `mat3` 会将每列填充至 16 字节，因此请将 3x3 矩阵以 `to4x4(m)` 的形式发送，并按 `mat4` 读取。语言的逐元素运算符（规范 §6.1）提供了 `a + b`、`a - b`、`-m` 和 `m * 2.0`，与 GLSL 一致。但**两个矩阵的 `a * b` 同样是逐元素相乘**（即 GLSL 的 `matrixCompMult`）；矩阵乘法应使用 `mul`。

这些构造器返回一个变换矩阵，与 `gfx` 一致；glm 中的 `translate(m, t)` 在此对应 `mul(m, translate(t))`。`mul(a, b)` 先应用 `b`，因此模型矩阵为 `mul(translate(p), mul(rotate(axis, angle), scale(s)))`。`rotate` 沿着轴向原点看去呈逆时针方向旋转，并会对轴向量进行归一化。`perspective`、`ortho` 和 `look_at` 均为右手坐标系，映射到与 `gfx` 相同的 SDL_GPU 裁剪空间：y 轴朝上，深度从 `near` 处的 0 到 `far` 处的 1（对应 glm 的 `perspectiveRH_ZO`、`orthoRH_ZO`、`lookAtRH`）。四参数版本的 `ortho` 等同于 `near` 为 -1、`far` 为 1 的六参数版本，因此 2D 场景的 z = 0 位于深度 0.5 处，两侧均有余量；而为 GL 深度范围设计的 glm 则并非如此。当 `up` 与视线方向平行时，`look_at` 的结果为 NaN。

这些函数对 `f32` 和 `f64` 泛化，其具体类型由实参类型决定，绝不会由结果赋值目标的类型决定。字面量会适配其他实参的类型，但带有具体类型的 `f64` 值（例如 `radians(60.0)`）会使调用解析为 `f64`，并使其旁边的 `f32` 拓宽：即使 `aspect: f32`，`perspective(radians(60.0), aspect, 0.1, 100.0)` 也会得到 `double4x4`。若要获得 `float4x4`，请写作 `radians(60.0 as f32)`；当所有实参均为字面量时，写作 `rotate_z(0.5 as f32)`。

<a id="quat"></a>
## quat

用于 3D 旋转的四元数，采用 glm 的命名，以及与 `mat` 矩阵之间的相互转换（`quat` 导入了 `mat`）。

```klang
struct quat<T> { x: T, y: T, z: T, w: T }          // w 为标量部分
type floatq = quat<f32>     doubleq = quat<f64>
let floatq_identity         let doubleq_identity

fn mul<T>(a: quat<T>, b: quat<T>) -> quat<T>            // 先 b 后 a
fn rotate<T>(q: quat<T>, v: vec3<T>) -> vec3<T>
fn angle_axis<T>(angle: T, axis: vec3<T>) -> quat<T>
fn to_angle_axis<T>(q: quat<T>) -> T, vec3<T>
fn rotation<T>(from: vec3<T>, to: vec3<T>) -> quat<T>  // 最短弧旋转
fn quat_look_at<T>(direction: vec3<T>, up: vec3<T>) -> quat<T>
fn slerp<T>(a: quat<T>, b: quat<T>, t: T) -> quat<T>    // 亦提供 nlerp
fn from_euler<T>(e: vec3<T>) -> quat<T>                 fn euler_angles<T>(q: quat<T>) -> vec3<T>
fn mat3_cast<T>(q: quat<T>) -> vec3<T>[3]               // 亦提供 mat4_cast
fn compose<T>(t: vec3<T>, q: quat<T>, s: vec3<T>) -> vec4<T>[4]   // 缩放、旋转、平移
fn quat_cast<T>(m: vec3<T>[3]) -> quat<T>               // 亦支持 4x4 矩阵
fn conjugate<T>(q: quat<T>) -> quat<T>                  fn inverse<T>(q: quat<T>) -> quat<T>
fn dot, length, length_sq, normalize                    // 与 vec 相同
```

这些函数均不保留边界检查，因此 `checkfree` 函数（规范 §7.14）可以调用它们。它们与 `mat` 中的函数一样对 `f32` 和 `f64` 泛化。旋转由单位四元数表示。`a + b`、`-q` 和 `q * 2.0` 是语言的逐元素运算符，**`a * b` 亦然：哈密顿积（Hamilton product）是 `mul`**。`angle_axis` 的旋转方向与 `mat` 的 `rotate(axis, angle)` 一致，并会对轴向量归一化（注意这里采用 glm 的参数顺序，与 `rotate` 相反）。`from_euler` 和 `euler_angles` 对应 glm 的 `quat(vec3)` 和 `eulerAngles`：依次绕 x、y、z 轴旋转，单位为弧度。在偏航角（yaw）为 ±π/2 时，俯仰（pitch）和滚转（roll）绕同一轴转动，`euler_angles` 会随舍入情况将旋转量分配给两者，但这三个角始终能重建出原旋转，而 glm 的实现则不能。对旋转矩阵调用 `quat_cast` 会得到 `q` 或 `-q`，二者表示同一旋转；若矩阵包含缩放，则会得到非单位长度的四元数。`compose(t, q, s)` 等价于 `mul(translate(t), mul(mat4_cast(q), scale(s)))` 但无需执行矩阵乘法，用于生成表示物体位置、朝向和缩放的常见模型矩阵。与 `look_at` 类似，当 `up` 与 `direction` 平行时，`quat_look_at` 的结果为 NaN。

<a id="math"></a>
## math

`math` 提供两种位宽的 libm 函数（`sin(x)` 会根据实参类型选择 `sin` 或 `sinf`）：`sqrt sin cos tan asin acos atan atan2 exp log log2 log10 pow hypot cbrt exp2 expm1 log1p sinh cosh tanh floor ceil round trunc copysign`。这些函数如 C 一样返回浮点数（`as i64` 可执行转换）。`expm1(x)` 和 `log1p(x)` 分别计算 `exp(x) - 1` 和 `log(1 + x)`，不会在 `x` 很小时丢失有效位；`copysign(x, y)` 取 `x` 的绝对值并带 `y` 的符号位，可用于区分 `-0.0` 与 `0.0`。`sqrt` 在运行时中实现（`k_sqrt`、`k_sqrtf`）。它给出与 C 相同且正确舍入的平方根，负数返回 NaN，但不设置 C 的 errno，因此在 C 编译器允许的情况下它会编译为单条平方根硬件指令。其他在 C 中会设置 errno 的函数（从 `sin` 到 `tanh`）在运行时名称（`k_exp`、`k_expf`……）下对应 libm 的同名函数，且已告知 gcc 和 clang 它们仅依赖于输入参数。由于 K 没有 errno，调用它们不会迫使 C 编译器重新加载外层循环保存在寄存器中的值。两个编译器仍将其识别为 libm 函数，因此会对常量参数进行折叠，并在 C 库支持时将同一值的 `sin` 和 `cos` 合并为单次 `sincos` 调用。整数实参可同等地转换为两种位宽，因此调用时必须明确指定一种（`sqrt(n as f64)`）；`sqrt(n * 0.5)` 会与 `sqrt(0.5)` 一样调用 `f64` 版本。

两种位宽的 `mul_add(x, y, z)` 均计算 `x * y + z`：在针对 FMA 构建的代码中执行一次舍入，在其他情况下执行两次舍入。具体某次调用采用哪种方式由实现决定（规范 §7.12）。C 后端在 arm64 上、在针对 AVX2 或更高版本构建的 x86-64 上（`-march=x86-64-v3`、clang-cl 的 `/arch:AVX2`），以及在 `simd fn` 的 AVX2 和 AVX-512 版本中会执行一次舍入。在其他环境、MSVC 下以及 JIT 下会执行两次舍入。直接写出的 `x * y + z` 始终执行两次舍入。

这两种舍入方式可能产生截然不同的结果，而不仅仅是最后一位的差异：

- **相消（Cancellation）。** 当 `x * y` 与 `z` 接近抵消时，融合结果会保留乘积的低位，而非融合结果则会丢失这些位，因此可能出现一个为零而另一个不为零的情况。当 `x = 1 + 2^-30` 时，`mul_add(x, x, -(1.0 + 2.0 * (x - 1.0)))` 在融合时为 `2^-60`，未融合时为 `0.0`，基于它的条件分支会走向不同路径。
- **溢出（Overflow）。** `mul_add(1.0e308, 2.0, -1.0e308)` 在融合时为 `1.0e308`，但未融合时为 `inf`，因为乘积在加法之前就已经溢出。

在 `simd fn` 内部，只有 K 在该函数体自身中发出的调用才会融合：即它自身的代码以及 K 内联进其中的函数。`simd fn` 所调用的普通辅助函数中的调用会保持该辅助函数的舍入方式，即便 C 编译器随后内联了该辅助函数也是如此。K 是否内联小型辅助函数取决于优化级别，因此将 `mul_add` 移入辅助函数可能会在 `-O0` 与 `-O2` 之间同时改变结果和速度。将辅助函数标记为 `inline fn` 可在除 `-O0`（K 在此级别下不做任何内联）以外的所有优化级别中保持其调用在 `simd` 调用方中处于融合状态。函数值在编译期解析，因此将 `mul_add` 作为函数值传入 `simd fn`（`F(x, y, z)`，其中 `F` = `mul_add`）会像直接调用一样进行融合。

该模块还包含：

```klang
let PI = 3.141592653589793;   let TAU = 6.283185307179586;
let E = 2.718281828459045;    let INF = 1.0 / 0.0;   let NAN = 0.0 / 0.0;
fn radians(deg: f64) -> f64   fn degrees(rad: f64) -> f64      // 亦提供 f32
fn is_nan(x: f64) -> bool     fn is_inf(x: f64) -> bool        // 亦提供 f32
fn is_finite(x: f64) -> bool                                   // 非 inf 或 NaN；亦提供 f32
fn fract(x: f64) -> f64                                        // x - floor(x)；亦提供 f32
fn inverse_lerp(a: f64, b: f64, v: f64) -> f64                 // (v - a) / (b - a)；亦提供 f32
fn smoothstep(edge0: f64, edge1: f64, x: f64) -> f64           // 亦提供 f32
fn wrap_angle(x: f64) -> f64                                   // 归入 [-PI, PI)；亦提供 f32
```

`fract`、`smoothstep` 和 `inverse_lerp` 遵循 GLSL 和 `std` 的 `lerp`：

- `fract(x)` 为 `x - floor(x)`，因此绝不为负：`fract(-0.25)` 为 `0.75`。极小的负数 `x` 会与 GLSL 一样舍入到 `1.0`。
- `inverse_lerp(a, b, v)` 是 `lerp(a, b, t)` 的逆运算：在 `a` 处为 0，在 `b` 处为 1，且不做钳制，因此 `v` 超出 `b` 时结果大于 1。当 `a == b` 时结果为 NaN 或无穷大。
- `smoothstep(edge0, edge1, x)` 在 `edge0` 及以下为 0，在 `edge1` 及以上为 1，中间部分取钳制后 `t = inverse_lerp(edge0, edge1, x)` 的 `3t² - 2t³`。边界反向时则反向插值。当两个边界相等时（GLSL 对此未定义），低于边界返回 0，高于边界返回 1，恰好等于边界时返回 NaN。
- `wrap_angle(x)` 通过加减整数圈将弧度值归入 `[-PI, PI)`，等价于向下取整余数：`PI` 会回绕为 `-PI`，而 `-0.0` 保持为 `-0.0`。f32 形式的边界为舍入到 f32 后的 `PI`。

对于 NaN 实参，以上函数均返回 NaN；对于无穷大实参，`fract` 和 `wrap_angle` 返回 NaN。`NAN` 的符号未作规定。`INF` 和 `NAN` 会像 `PI` 一样采用上下文所要求的类型（`let inf32: f32 = INF;`）。

libm 函数是 `extern fn`，而上述函数不保留边界检查，因此 `checkfree` 函数（规范 §7.14）可以调用 `math` 中的任意函数。

<a id="os"></a>
## os

对 `src/runtime/runtime_os.h` 的小型封装接口：

```klang
fn read_file(path: const u8[:], out: u8[>..]&) -> bool     // 追加整个文件
fn with_read_file<Body>(path: const u8[:]) -> bool         // 向 Body 借出有界读取器
fn write_file(path: const u8[:], data: const u8[:]) -> bool
fn write_file_atomic(path: const u8[:], data: const u8[:]) -> bool   // 要么保留旧文件，要么写入全部新内容
fn append_file(path: const u8[:], data: const u8[:]) -> bool
fn file_exists(path: const u8[:]) -> bool                  // 为文件，而非目录
fn delete_file(path: const u8[:]) -> bool
fn rename_file(from: const u8[:], to: const u8[:]) -> bool // 原子地替换 `to`
fn make_dir(path: const u8[:]) -> bool                     // 若之后存在目录则返回 true
fn is_dir(path: const u8[:]) -> bool
fn delete_dir(path: const u8[:]) -> bool                   // 删除空目录
fn list_dir(path: const u8[:], out: u8[>..]&) -> bool      // 追加每个名称及 '\n'，已排序
fn read_line(out: u8[>..]&) -> bool                  // 从 stdin 读取，剥离换行符；末尾返回 false
fn read_stdin(out: u8[>..]&)                         // 读取直到输入结束的所有内容
fn read_stdin(out: u8[>..]&, max: i64) -> bool       // 最多读取 max 字节；末尾返回 false
fn write_stdout(s: const u8[:])   fn write_stderr(s: const u8[:])   fn flush_stdout()
fn arg_count() -> i64       fn arg(i: i64, out: u8[>..]&)
fn args() -> u8[:][>..]                              // 可索引；参数 0 为程序本身
fn env(name: const u8[:], out: u8[>..]&) -> bool
fn trim_memory() -> bool                            // 将未使用的 data-stack 页面归还给操作系统
fn resource_dir() -> u8[>..]                         // 绝对目录路径，带末尾分隔符
fn resource_dir(out: u8[>..]&) -> bool               // 追加路径；失败时保持不变
fn time() -> f64                                     // 自纪元以来的秒数
fn clock() -> f64                                    // 单调递增，高精度
fn time_ns() -> i64         fn clock_ns() -> i64
fn sleep(seconds: f64)      fn sleep_ms(ms: i64)
fn random_seed() -> u64                              // 熵源，用于 rng
fn random_bytes(out: u8[:]) -> bool                  // 密码学安全；false 表示操作系统未提供
```

<a id="scoped-file-readers"></a>
### 作用域文件读取器

`with_read_file(path) { read => … }` 在块的作用域期间打开一个二进制流。若打开失败则返回 `false`，此时块体不会执行。若打开成功，该作用域拥有该流，并在正常退出、外层函数返回，或可恢复的 `exception::throw` 离开该作用域时关闭流。块接收一个操作 `read(out: u8[>..]&, max: i64 = 65536) -> i64`，这是一个编译期函数值，可在作用域存活期间调用或转发。

正数返回值表示追加的字节数。每次调用最多追加 `min(max, 65536)` 个字节，并将流前进相应的已读量。当限制量为正时，零表示到达文件尾（EOF）。限制量为零时直接返回零而不进行读取。限制量为负或读取失败时返回 `-1`，并使构建器保持原样。在发生错误前可能会先返回部分读取结果，而错误将由后续调用报告。调用方通过选择何时清空或复用其构建器来控制缓冲。

<!-- fragment -->
```klang
import os;

fn copy_to_stdout(path: const u8[:]) -> bool {
    return with_read_file(path) { read =>
        var chunk: u8[>..] = [];
        loop {
            let n = read(chunk);
            if n < 0 { return false; }
            if n == 0 { break; }
            write_stdout(chunk);
            chunk.clear();
        }
    };
}
```

`trim_memory` 请求操作系统丢弃调用方线程程序中每一个当前数据栈顶之外的完整内存页，包括其全局数据和已结束作用域留下的临时空间。它会保留活跃值、借用视图、受限数组容量和虚拟地址保留区；后续的栈增长可正常工作。它不会影响其他工作线程。建议在大型工作负载之后（例如替换文档之后）调用，而不是在每次临时值之后调用。普通作用域退出会保留内存页以供低开销复用，而被精简的内存页在栈再次增长进入时会触发一次缺页异常。

这些内存页会被立即释放。Windows 会解除其提交（decommit），从而一并归还提交内存量（commit charge）；Linux 会立即将其从常驻集中移除。macOS 则会在相同地址处以全新的匿名内存替换每个保留区未使用的后缀部分。在三大平台上，再次增长的栈都会获得全新的清零页。macOS 的精简覆盖完整的未使用保留区，因此普通增长无需运行时追踪或信号处理。返回 `false` 表示至少有一个操作系统请求失败。

在所有平台上路径均采用 UTF-8；在 Windows 上，运行时会将其转换为 UTF-16 以调用系统的宽字符 API。环境变量值和程序参数也均为 UTF-8。在 Windows 上，程序参数会先经过 ANSI 代码页，因此仅保留该代码页所含的字符。若路径长度超过 4095 字节、包含 NUL 字节，或（在 Windows 上）不是有效的 UTF-8，则会被拒绝且调用返回 `false`，因为被截断或以其他方式解码的路径可能会指向不同的文件。

`random_bytes` 从操作系统的密码学安全生成器填充其实参，适用于密钥、随机数（nonce）和其他机密信息：在 macOS、Linux 和 BSD 上使用 `getentropy`（若内核不支持则使用 `/dev/urandom`），在 Windows 10 及更高版本上使用 `ProcessPrng`（更早版本使用 `RtlGenRandom`），并在运行时动态查找，使程序无需链接额外的库。返回 `false` 表示操作系统未能提供字节；此时不得使用 `out` 的内容。`random_seed` 以相同方式获取值，仅当失败时才会回退到时钟混合值，因此它不可用于机密信息。`std` 中的 `rng` 绝不可用于机密信息。

`read_file` 在失败时不追加任何内容。`read_stdin` 原样追加字节，在 Windows 上也是如此（C 库的文本模式会将 `\r\n` 转换为 `\n` 并在 Ctrl-Z 处停止）。`read_line` 会连同换行符一并剥离行尾换行符前的 `\r`。`read_stdin(out, max)` 最多追加 `max` 字节，并会等待直到读满或输入结束，因此大型管道可以分段读取。当没有剩余字节或 `max` 小于等于 0 时，它返回 `false` 且不追加任何内容。按行进行的交互式输入仍应使用 `read_line`。

`write_file_atomic` 用于绝不能被看到处于半写入状态的文件，例如游戏存档。它将数据写入与 `path` 同一目录下的一个新文件中，文件名在 `path` 后附加随机标签和 `.tmp`。随后它将该文件刷盘并重命名覆盖 `path`，在 POSIX 上保留 `path` 的权限。在任意时刻发生崩溃或失败，都将使 `path` 要么保留旧内容，要么包含全部新内容。若发生失败，新文件会被删除；若发生崩溃，则可能会留下一个以 `.tmp` 结尾的文件。
`rename_file` 对程序自身在同一卷内写入的文件执行同样的单步替换。在 Windows 上，当另一个程序正打开文件时该文件无法被替换，而杀毒软件会短暂打开刚写入的文件，因此这两个函数在返回 `false` 前都会等待大约最多一秒。

`list_dir` 会排除 `.` 和 `..`，并按字节序对文件名排序，因此列出结果不依赖于文件系统。无法写成单行 UTF-8 的名称（包含换行符的 POSIX 名称，或非有效 UTF-16 的 Windows 名称）也会被排除。失败时不追加任何内容。

```klang
var names: u8[>..] = [];
if list_dir("saves", names) {
    each_split(names, '\n') { if it.len > 0 { load_save(str("saves/", it)); } };
}
if !write_file_atomic("saves/slot1.sav", image) { write_stderr("not saved\n"); }
```

<a id="application-resources"></a>
### 应用程序资源

普通的相对路径与 C/C++ 一样相对于进程工作目录解析。K 在启动程序时不会改变该目录。对于随应用程序分发的数据，`resource_dir()` 返回一个带末尾路径分隔符的绝对 UTF-8 目录路径，可直接用于拼接：

```klang
var wad: u8[>..] = [];
if !read_file(str(resource_dir(), "data/map.wad"), wad) {
    abort("cannot read map.wad");
}
```

在 JIT 运行时，根目录是入口源文件所在的目录，无论由哪个导入模块调用该辅助函数。在 AOT 程序中，它是正在运行的可执行文件所在的目录，在运行时动态发现，而非在编译时硬编码记录。在常规的 macOS `.app` 捆绑包中，它是 `Contents/Resources/`。将数据部署在相对于该根目录的位置；将可执行文件及其数据一并移动可保持资源查找有效。AOT 程序不依赖于其原始源码树或启动器的工作目录。当 `extern "..." { }` 块以包含路径分隔符的名称（例如 `"bin/steam_api64"`）指定共享库时，也会相对于同一根目录打开（规范 §7.10），因此随程序一同分发的库可以与数据以相同方式找到。

若无法确定该目录，返回字符串的形态会触发中止；接受构建器的重载则会返回 `false` 且不追加任何内容。原生发现机制支持 Windows、Linux 和 macOS。该目录用于存放随附资产，不保证可写。用户提供的路径、输出和存档可继续使用普通文件操作及其选定位置。

`exit(code)` 和 `abort(msg)` 是内置函数，以便检查器知道它们不会返回（diverge）。v1 中暂不包含子进程和网络功能；当程序需要时，可通过 `extern fn` 引入。

<a id="ffi"></a>
## ffi

```klang
fn cstr(p: u64, out: u8[>..]&) -> bool       // 复制 p 处的 C 字符串（不含末尾 NUL）；空指针返回 false
fn cstr(p: u64) -> u8[>..]                   // 同上，返回新分配的结果；空指针返回空值
fn bytes(p: u64, n: i64, out: u8[>..]&) -> bool   // 复制 p 处的 n 个字节；空指针返回 false
fn bytes(p: u64, n: i64) -> u8[>..]
```

这些函数读取由 C 库拥有的内存。通过共享库（规范 §7.10，[reference.md §24](reference.md#24-calling-c-and-being-called-from-c)）绑定的函数会以 `u64` 形式返回 `const char *` 或由库保留的数组，而 K 无法直接解引用读取该地址。这些函数会将数据复制到 K 存储中，因此外部内存未经复制绝不会成为 K 值。就像每一次外部调用信任 C 一样，它们完全信任传入的指针和长度：错误的指针或长度属于与 C 中相同的错误，不会被捕获。空指针不追加任何内容并返回 `false` 结果。

```klang
import ffi;
extern "SDL2" {
    fn SDL_Init(flags: u32) -> i32;
    fn SDL_GetError() -> u64;
}
if SDL_Init(0x20) < 0 { abort(str("SDL: ", ffi::cstr(SDL_GetError()))); }
```

<a id="binary"></a>
## binary

针对外部格式（小端序和大端序）的带检查读取和追加式写入，由 K 语言编写（浮点数通过小型运行时辅助函数提取比特位）。`import binary;` 将 API 置于 `binary` 命名空间中。输入为 `const u8[:]`；返回的字节切片和定宽名称借用该输入，不进行复制或分配。它们的生命周期和只读访问权限均按常规方式检查。该 API 不解释 K 序列化镜像（`from_bytes`）。

<a id="reads-at-offsets"></a>
### 按偏移量读取

```klang
fn range_ok(data: const u8[:], at: i64, size: i64) -> bool
fn read_u8(data: const u8[:], at: i64) -> u8, bool
fn read_i8(data: const u8[:], at: i64) -> i8, bool
fn read_u16le(data: const u8[:], at: i64) -> u16, bool    // 亦提供 read_u16be
fn read_i16le(data: const u8[:], at: i64) -> i16, bool    // 亦提供 read_i16be
fn read_u32le(data: const u8[:], at: i64) -> u32, bool    // 亦提供 read_u32be
fn read_i32le(data: const u8[:], at: i64) -> i32, bool    // 亦提供 read_i32be
fn read_u64le(data: const u8[:], at: i64) -> u64, bool    // 亦提供 read_u64be
fn read_i64le(data: const u8[:], at: i64) -> i64, bool    // 亦提供 read_i64be
fn read_f32le(data: const u8[:], at: i64) -> f32, bool    // 亦提供 read_f32be
fn read_f64le(data: const u8[:], at: i64) -> f64, bool    // 亦提供 read_f64be
fn read_bytes(data: const u8[:], at: i64, size: i64) -> const u8[:], bool
fn read_name(data: const u8[:], at: i64, width: i64) -> const u8[:], bool
```

偏移量和大小均以字节为单位。负值、截断字段以及超出末尾的偏移量会失败并返回零或空切片以及 `false`；边界检查在极端偏移量或大小下不会发生溢出。位于末尾的空范围是有效的。在任一主机字节序下，读取均支持未对齐的偏移量。`le` 读取先取最低有效字节，`be` 读取先取最高有效字节。浮点读取精确保留值的比特位，包括 NaN 载荷。
`read_name` 校验整个定宽字段，并返回第一个 NUL 之前的字节，若无 NUL 则返回全部 `width` 字节。它不校验填充字节，也不将这些字节解释为特定文本编码。

<a id="cursor-and-bounded-subreaders"></a>
### 游标与有界子读取器

```klang
struct Reader { data: const u8[:], pos: i64 }
fn reader(data: const u8[:]) -> Reader
fn read_u8(r: Reader&) -> u8, bool
fn read_u16le(r: Reader&) -> u16, bool    // 每种按偏移量读取均提供游标形式
fn read_bytes(r: Reader&, size: i64) -> const u8[:], bool
fn read_name(r: Reader&, width: i64) -> const u8[:], bool
fn skip(r: Reader&, size: i64) -> bool
fn subreader(r: Reader&, size: i64) -> Reader, bool
```

游标仅保存其输入和字节位置。每次读取都返回自身的成功标志。成功时会推进 `pos`（对于名称，按完整宽度推进）；失败时返回零或空切片，并保持 `pos` 不变，因此随后可以尝试更短的读取。设置 `pos` 可进行跳转定位；负值或超出范围的位置会导致读取失败。调用方自行保存所需的任何文件偏移量或诊断上下文。游标没有粘滞错误状态。

`subreader` 从父游标中消费一段有界范围，并为其返回一个从零开始的新游标。失败时返回空读取器和 `false`，并使父游标保持在原位。在使用子游标或返回的切片期间，请将父输入保持在作用域内；当前的引用结果分析可能会将它们的生命周期与父对象绑定。

```klang
// data 拥有文件字节数据。前四个字节给出了载荷长度。
var r = binary::reader(data);
let size, size_ok = binary::read_u32le(r);
if !size_ok { abort("missing payload length"); }
var payload, payload_ok = binary::subreader(r, size);
if !payload_ok { abort("truncated payload"); }
let version, version_ok = binary::read_u16le(payload);
let name, name_ok = binary::read_name(payload, 8);
if !version_ok || !name_ok { abort("truncated payload header"); }
// name 借用自 data。payload 无法读取进入下一条记录。
```

随机访问请使用按偏移量读取，顺序记录请使用游标。应用程序在检查读取结果后，仍需自行校验签名、计数、索引和格式特定的限制。[`31_mini_doom.k`](../../samples/31_mini_doom.k) 中的 WAD 加载器展示了这两种风格。

<a id="writes"></a>
### 写入

```klang
fn write_u8(out: u8[>..]&, v: u8)
fn write_i8(out: u8[>..]&, v: i8)
fn write_u16le(out: u8[>..]&, v: u16)    // 亦提供 write_u16be
fn write_i16le(out: u8[>..]&, v: i16)    // 亦提供 write_i16be
fn write_u32le(out: u8[>..]&, v: u32)    // 亦提供 write_u32be
fn write_i32le(out: u8[>..]&, v: i32)    // 亦提供 write_i32be
fn write_u64le(out: u8[>..]&, v: u64)    // 亦提供 write_u64be
fn write_i64le(out: u8[>..]&, v: i64)    // 亦提供 write_i64be
fn write_f32le(out: u8[>..]&, v: f32)    // 亦提供 write_f32be
fn write_f64le(out: u8[>..]&, v: f64)    // 亦提供 write_f64be
```

每次写入按同名读取函数提取字节的顺序将值的字节追加到 `out`，因此写入后再用对应读取函数读取即可还原该值。浮点写入精确保留值的比特位，包括 NaN 载荷。写入操作不会失败；`out` 会像普通追加操作一样自动增长。

```klang
var out: u8[>..] = [];
binary::write_u32be(out, 0x89504E47);    // 文件签名，最高有效字节在前
binary::write_u16le(out, 3);
binary::write_f32le(out, 1.5);
let version, version_ok = binary::read_u16le(out, 4);
assert(version_ok && version == 3);
```

<a id="base64"></a>
## base64

RFC 4648 §4 所定义的 Base64：标准字母表（`A`-`Z`、`a`-`z`、`0`-`9`、`+`、`/`）和 `=` 填充。`import base64;` 将其置于 `base64` 命名空间中。

```klang
fn encode(out: u8[>..]&, src: const u8[:])            // 每 3 字节追加 4 个字符，带填充
fn decode(out: u8[>..]&, src: const u8[:]) -> bool    // 追加解码后的字节；若 src 无效则返回 false
```

`decode` 仅接受完整的四字符分组，最后一组可以以 `=` 或 `==` 结尾。它会拒绝换行符、空白字符、未填充的文本以及其他字母表（URL 安全字母表中的 `-` 和 `_` 均无效）。它不要求填充前剩余的比特位必须为零，因此 `Zh==` 的解码结果与 `Zg==` 相同；大多数解码器均允许如此。遇到无效输入时，它返回 `false` 并使 `out` 保持原样。

```klang
var text: u8[>..] = [];
base64::encode(text, "foobar");              // "Zm9vYmFy"
var bytes: u8[>..] = [];
if !base64::decode(bytes, text) { abort("not base64"); }
```

该编解码器通过计算而非查表来映射字母表，因此 C 编译器能够向量化两个循环。两者也都是 `simd fn`（规范 §7.12）：当 clang 为 x86-64 编译时，在支持的 CPU 上它们会以 AVX2 或 AVX-512 代码运行，速度比基线 SSE2 快数倍。

<a id="csv"></a>
## csv

RFC 4180 所定义的逗号分隔值格式。`import csv;` 将读取器和写入器的引号格式化功能置于 `csv` 命名空间中。

```klang
fn each_record<F>(text: const u8[:], sep: u8 = ',') -> bool    // 对每条记录调用 F(fields: const u8[:][:])
fn format_field(out: u8[>..]&, s: u8[:], sep: u8 = ',')         // s，在需要时加引号
```

`each_record` 按顺序将每条记录的字段以文本切片的形式传给其块。整个过程不发生复制，且字段切片归读取器所有，并在下一条记录时复用。字段可以加引号，带引号的字段可以包含分隔符、换行符以及双写引号（每个双写引号代表一个引号字符）。传给块的带引号字段会去掉外围引号，但保留内部的成对双写引号原样不变。由于未加引号的字段不可能包含引号，因此 `format_replaced(out, field, "\"\"", "\"")` 即可追加任意字段的文本内容。记录以 LF 或 CRLF 结尾，最后一条记录可以例外不带换行符。空行是一条包含单个空字段的记录，表头行与其它记录并无二致。字段周围的空格属于字段本身。`sep` 可以是除引号或换行符之外的任意字节，例如 `';'`，或者用 `'\t'` 表示制表符分隔值（TSV）。

`each_record` 在将此前所有合法记录传给块之后，于第一条格式错误的记录处返回 `false`；块不会看到该错误记录。若记录在未加引号的字段中出现引号、带引号的字段缺少闭合引号，或者闭合引号之后出现除分隔符或换行符以外的任何内容，则该记录格式错误。与所有块一样，块中的 `return` 会直接从调用 `each_record` 的函数中返回。

```klang
var total = 0.0;
let ok = csv::each_record(text) { fields =>
    let price, valid = parse_flt(fields[2]);
    if valid { total += price; }
};
if !ok { abort("malformed CSV"); }
```

`format_field` 原样追加 `s`；若 `s` 包含分隔符、引号或换行符，则加上外围引号并将内部引号双写。字段之间的分隔符以及记录末尾的换行符由调用方自行写入。

```klang
for row in rows {
    csv::format_field(out, row.name);
    out.push(',');
    format(out, row.count, "\n");
}
```

<a id="json"></a>
## json

RFC 8259 所定义的 JSON，提供三种使用方式：向构建器追加值的写入器、构建文档树的解析器，以及像 serde derive 一样将文本直接映射到调用方自身类型的读取器。`import json;` 将它们置于 `json` 命名空间中。

<a id="writing"></a>
### 写入

```klang
fn format_string(out: u8[>..]&, s: u8[:])    // 加引号，并转义 " \ 与控制字符
fn format_key(out: u8[>..]&, key: u8[:])     // 带引号的键及其冒号："key":
fn format_number(out: u8[>..]&, v: f64)      // 能精确读回 v 的最短形式
fn format_int(out: u8[>..]&, v: i64)
fn format_bool(out: u8[>..]&, b: bool)
```

调用方自行写入方括号、花括号以及值之间的逗号。字符串中从 0x80 起的字节均原样写入，因此 UTF-8 文本会保持 UTF-8 编码。`format_number` 写出能够精确读回 `v` 的最少有效数字，采用与 serde_json 相同的格式（`1.0`、`0.1`、`1e-7`、`1.5e300`、`0.30000000000000004`）。对于 JSON 中没有数字表示的 NaN 和无穷大，它会写出 `null`。

```klang
out.push('{');
json::format_key(out, "name");
json::format_string(out, player.name);
out.push(',');
json::format_key(out, "pos");
out.push('[');
json::format_number(out, player.x);
out.push(',');
json::format_number(out, player.y);
out.append("]}");
```

<a id="the-document-tree"></a>
### 文档树

```klang
struct Node { next: Node&<u32>?, first: Node&<u32>?, key: const u8[:], val: Value }
enum Value { Null, Bool { v: bool }, Int { v: i64 }, Num { v: f64 }, Str { s: const u8[:] },
             Arr { count: i64 }, Obj { count: i64 } }
fn parse(text: u8[:], doc: Node[>..]&, decoded: u8[>..]&) -> Node?, u8[>..]
                                             // 返回根节点，或返回 null 及错误信息
fn member(n: Node?, key: u8[:]) -> Node?     // 名为 key 的第一个成员；若无则返回 null
fn each<F>(n: Node&)                         // 按顺序对每个元素或成员调用 F(child)
fn as_f64(n: Node?) -> f64, bool             // Int 或 Num
fn as_i64(n: Node?) -> i64, bool             // Int
fn as_bool(n: Node?) -> bool, bool
fn as_string(n: Node?) -> const u8[:], bool
```

`parse` 按文本顺序为每一个值向 `doc` 压入一个节点。每个节点保存一个指向下一个兄弟节点的链接、一个指向第一个子节点的链接（对于容器）、键（在数组中为空）以及值。键和字符串均为 `text` 的切片。包含转义序列的键和字符串则改为指向解码后文本的切片，解码后的文本被追加到 `decoded` 中；除此之外不发生任何复制。不含小数部分或指数部分且在 `i64` 范围内的数字成为 `Int`。其他任何数字成为 `Num`，经过正确舍入，若超出 `f64` 范围则为无穷大。转义序列解码为 UTF-8：代理对（surrogate pair）变为单个码点，孤立代理项（lone surrogate）变为编码其自身码点的三个字节。不会校验字符串是否为有效 UTF-8。

遇到格式错误的文本时，`parse` 返回 null 以及一条指出发现错误所在字节位置的消息，例如 `json: expected ',' or '}' at byte 41`。在此之前解析的节点已经压入 `doc`。

该解析器是一个单循环，使用自有栈来维护打开的容器，因此可以解析任意深度的嵌套。`member` 和各访问器均接受 null 节点，因此可以链式查找。重复的键会保留在树中，`member` 返回第一个匹配项。

```klang
var doc: json::Node[>..] = [];
var decoded: u8[>..] = [];
let root, err = json::parse(text, doc, decoded);
guard root else { abort(err); }
let w, ok = json::as_i64(json::member(json::member(root, "window"), "width"));
let items = json::member(root, "items");
if items {
    json::each(items) { item =>
        let name, named = json::as_string(json::member(item, "name"));
        if named { print(name); }
    };
}
```

<a id="reading-into-your-own-types"></a>
### 读取到自定义类型

```klang
struct Reader { text: const u8[:], pos: i64, error: const u8[:], error_at: i64 }
fn reader(text: const u8[:]) -> Reader
fn each_member<F>(r: Reader&)                // 对游标处对象的每个成员调用 F(key)
fn each_element<F>(r: Reader&)               // 对游标处数组的每个元素调用 F()
fn read_number(r: Reader&) -> f64
fn read_int(r: Reader&) -> i64               // 无小数或指数部分，且在 i64 范围内
fn read_bool(r: Reader&) -> bool
fn read_string(r: Reader&, scratch: u8[>..]&) -> const u8[:]
fn skip(r: Reader&)                          // 跳过任意类型的值并进行语法检查
fn peek(r: Reader&) -> u8                    // 跳过空白后的下一个字节；到达末尾返回 0
fn finish(r: Reader&) -> bool                // 无错误，且剩余内容仅为空白字符
fn message(r: Reader&) -> u8[>..]            // "json: <what> at byte <n>"
```

读取器边解析边处理，不构建任何中间树。每个块使用与预期类型对应的函数读取向它提供的值，并使用 `skip` 跳过不需要的值。`read_string` 返回原文本的切片；若字符串包含转义序列，则返回 `scratch` 的切片。键会按文本原样传给块，包括其中的任何转义序列，因此带有转义的键与不带转义的同名键永远不相等。错误具有粘滞性：一旦读取发现不符合预期的文本，就会记录它期望的内容及位置，并将游标移至末尾，从而使后续每次读取均失败并结束所有循环。调用方只需在最后通过 `finish` 检查一次，该函数还会拒绝值后面除空白字符以外的任何内容。若要在值可选的位置处理 `null`，请检查 `peek(r) == 'n'` 并调用 `skip`。

```klang
struct Item { id: i64, price: f64 }

fn read_items(text: u8[:]) -> Item[>..] {
    var r = json::reader(text);
    var items: Item[>..] = [];
    json::each_element(r) {
        var it = Item { id: 0, price: 0.0 };
        json::each_member(r) { key =>
            if key == "id" { it.id = json::read_int(r); }
            else if key == "price" { it.price = json::read_number(r); }
            else { json::skip(r); }
        };
        items.push(it);
    };
    if !json::finish(r) { abort(json::message(r)); }
    return items;
}
```

<a id="regex"></a>
## regex

基于字节的正则表达式，语法与匹配规则与 Rust 的 `regex` crate 一致。使用 `import regex;` 可将其引入命名空间 `regex`。

```klang
fn compile(pattern: u8[:]) -> Regex                  // aborts on a malformed pattern
fn compile(pattern: u8[:], err: u8[>..]&) -> Regex   // appends the error to err instead
fn find(re: Regex&, text: u8[:]) -> i64, i64         // the first match's start and end; -1, -1
fn matches(re: Regex&, text: u8[:]) -> bool          // whether there is a match
fn count(re: Regex&, text: u8[:]) -> i64             // the matches each_match finds
fn each_match<F>(re: Regex&, text: u8[:])            // F(start, end) for each match
fn each_captures<F>(re: Regex&, text: u8[:])         // F(caps: Captures) for each match
fn format_replaced<F>(out: u8[>..]&, re: Regex&, text: u8[:])
                                                     // text, each match replaced by F(caps)
struct Captures { text: const u8[:], slots: const i64[:] }
fn group(c: Captures, k: i64) -> const u8[:]         // group k's text; empty if it took no part
```

匹配采用最左优先（leftmost-first）策略，与该 crate、Perl 及大多数正则引擎一致。首先选择起始位置最靠前的匹配；在同一起始位置的多个匹配中，选择模式最偏好的那一个：按顺序尝试分支选择（alternation），重复量词默认贪婪（greedy），除非显式指定为懒惰（lazy）。搜索是非锚定的（unanchored）。迭代函数从左到右查找互不重叠的匹配，并且绝不会在前一个匹配结束的位置再次匹配空串：例如 `a*` 在 `baaa` 上匹配 `0..0` 和 `1..4`。第 0 组（Group 0）表示整个匹配，其余捕获组按左括号的顺序编号。第 k 组跨越文本的 `slots[2k]..slots[2k + 1]` 范围；若某个组未参与匹配，则其两个槽位均为 -1。重复的捕获组保留其最后一次迭代的捕获结果。`group` 返回的切片生命周期锚定在传给代码块的 `Captures` 上。若某个组的视图需要在代码块之外继续存活（例如用作字典的键），则应改为直接通过槽位对原文本进行切片。

支持的语法包括：

* 字面量字节；
* `.`（仅在启用 `s` 标志时匹配换行符）；
* 字符类 `[a-z]` 和 `[^...]`，其中位于首位的 `]` 或位于首位或末位的 `-` 表示字面量字符；
* `\d \w \s` 及其补集 `\D \W \S`，也可用于字符类内部；
* 转义序列 `\n \t \r \f \v \xHH` 以及任意标点符号的转义（`\.`、`\\`）；
* 捕获组 `(...)`、非捕获组 `(?:...)` 以及命名捕获组 `(?P<name>...)` 或 `(?<name>...)`，其中组名会被接受但不会被使用；
* 标志 `i`（字母不区分大小写匹配）和 `s`，写作 `(?i)` 表示作用到当前组末尾，写作 `(?i:...)` 表示仅在括号内生效，写作 `(?-i)` 用于关闭对应标志；
* 分支选择 `|`；
* 量词 `* + ? {n} {n,} {n,m}`，在后面加上 `?` 即可转为懒惰匹配。

字符类与大小写折叠均按 ASCII 处理，与该 crate 关闭 Unicode 时的行为一致。不支持锚点与单词边界（`^ $ \A \z \b \B`）、字符类集合运算（`&& -- ~~`）、嵌套字符类、Unicode 字符类及其他标志，遇到这些情况时 `compile` 会报错：`regex: anchors are not supported at offset 0 in ^a`。该模块的头文件说明了与该 crate 的两处已知差异：当分支共享重复前缀时，会按书写顺序进行匹配（与 Perl 和 RE2 一致）；此外，位于模式末尾且带有 `{0}` 的捕获组会被保留。

`compile` 会预先构建完整的 DFA：一个用于确定匹配结束位置，另一个基于反转模式用于确定匹配起始位置。因此，搜索过程对所检查的每个文本字节只需一次查表。当每个匹配都带有罕见的引导字节，或在不可能包含某罕见字节的子模式之后紧跟该字节时，搜索会通过 `find_pair` 或 `find_any`（见上文）跳过无关文本，仅在可能存在匹配的位置进行检查。当模式能确定某个捕获组在匹配中的固定位置时，直接从该位置提取；否则通过仅在当前匹配范围内运行的回溯器来获取。`find`、`matches` 和 `count` 均不使用这两者。由于 DFA 可能需要指数级的状态数，如果某个模式的程序或 DFA 规模过大（例如 `[ab]*a[ab]{20}`，或数千次的计数重复），则会报错而非继续编译。请将 `Regex` 的创建放在使用它进行搜索的循环之外，只编译一次。

```klang
let re = regex::compile("(\\w+)=(\\d+)");
regex::each_captures(re, line) { c =>
    let n, ok = parse_int(regex::group(c, 2));
    if ok { settings.insert(line[c.slots[2]..c.slots[3]], n); }    // a key that outlives c
};

var err: u8[>..] = [];
let user = regex::compile(pattern_from_user, err);
if err.len > 0 { print(err); }

var out: u8[>..] = [];
let field = regex::compile("""\{\{\s*(\w+)\s*\}\}""");
regex::format_replaced(out, field, template) { caps =>
    let value = vars.get(regex::group(caps, 1));
    if value { value } else { "" }
};
```

<a id="gfx"></a>
## gfx

基于 SDL3 GPU API 的图形模块，底层通过 Direct3D 12、Vulkan 或 Metal 进行绘制：包含窗口及其输入、缓冲区、纹理、采样器、管线、渲染通道与计算通道，以及结果回读功能。可选的原生部分既可以编译进 `k` 编译器本身，也可以在 `k run` 或 `k build` 首次使用时按需构建。内置了 gfx 的编译器可通过 `k --gfx-link msvc|cc` 打印响应文件，用于直接链接生成的 C 代码。所有符号均位于命名空间 `gfx` 中。`samples/27_gfx_cube.k` 是一个小巧而完整的示例程序，包中的[图形设计](design/gfx.md)文档介绍了原生层的实现。

**文本渲染由 [`ui`](#text-rendering-and-game-huds) 模块提供**，包括字体、定位文本以及非交互式游戏 HUD。使用 `ui::label` 或 `ui::draw_text`，然后在 gfx 场景之上调用 `ui::render` 即可。仅当有特殊的艺术字效或渲染需求时，才需要单独的文本渲染器。

<a id="shaders"></a>
### 着色器

```klang
// Code shaders share, in a global each names as a part.
let scene = """
    layout(set = 1, binding = 0) uniform Scene { mat4 view_proj; };
    """;

// A shader's source after its stage, "vert", "frag" or "comp", or a file,
// .vert, .frag or .comp, relative to this one.
let vs = embed_shader("vert", scene, """
    layout(location = 0) in vec3 a_pos;
    void main() { gl_Position = view_proj * vec4(a_pos, 1.0); }
    """);
let fs = embed_shader("lit.frag");
```

`embed_shader` 是一个内建函数。GLSL 450 着色器会在编译程序时一并编译为 SPIR-V、MSL 和 HLSL，结果是一个静态数据切片 `const u8[:]`，可直接传给 `pipeline` 或 `compute_pipeline`。其源码通常是在调用处编写的 `"""` 字符串（见规范 §2）。也可以拆分为多个部分，每部分可以是一个字符串字面量，或是用字符串字面量初始化的 `let` 或 `const` 全局变量；这些部分会按顺序逐行拼接。未能通过编译的着色器会在调用处触发编译错误。如果存在出错的 GLSL 代码行，错误信息会指向程序或着色器文件中的对应行。`#include "x.glsl"` 相对于着色器文件所在路径解析；若源码直接写在程序中，则相对于程序文件所在路径解析。着色器方言遵循 cute_spirv（`third_party/cute_spirv`）：不支持 `double` 类型、几何着色器（geometry）和曲面细分着色器（tessellation）阶段，uniform 块不能有实例名，且每个存储缓冲区（storage buffer）块仅包含一个运行时数组（`buffer B { vec4 items[]; };`）。

资源必须放置在 SDL_GPU 期望的位置，编译器会对此进行检查：

| 阶段 | `set` | 包含的资源（每个 set 中的 binding 按此顺序从 0 开始编号） |
|---|---|---|
| vertex | 0 | 采样器（samplers），然后是存储纹理（storage textures），然后是只读存储缓冲区（storage buffers） |
| vertex | 1 | uniform 块 |
| fragment | 2 | 采样器（samplers），然后是存储纹理（storage textures），然后是只读存储缓冲区（storage buffers） |
| fragment | 3 | uniform 块 |
| compute | 0 | 采样器（samplers），然后是只读存储纹理，然后是只读存储缓冲区 |
| compute | 1 | 读写存储纹理，然后是读写存储缓冲区 |
| compute | 2 | uniform 块 |

`bind_*` 调用中的 `slot` 参数在同一种类内独立计数：无论前面有多少个采样器，第一个存储缓冲区的槽位始终是 0。而计算着色器的读写资源则按绑定顺序直接传给 `begin_compute`。

<a id="device-window-and-frames"></a>
### 设备、窗口与帧

```klang
fn open(title: const u8[:], width: i64, height: i64) -> bool        // and (..., flags)
fn open_headless(width: i64, height: i64) -> bool                   // no window; and (..., flags)
fn close()
fn frame() -> bool          // ends the frame drawn since the last call; false once asked to close or the device is lost
fn quit()                   // frame() returns false next
fn flush()                  // submit everything and wait for the GPU
fn error() -> u8[>..]       // why the last failing call failed
fn check()                  // abort if the program has misused gfx
fn driver() -> u8[>..]      // "direct3d12", "vulkan" or "metal"
fn set_title(title: const u8[:])
fn set_fullscreen(on: bool) -> bool   fn fullscreen() -> bool   fn maximize()
fn screen() -> Texture      fn screen_depth() -> Texture      fn screen_size() -> int2
fn pixel_density() -> f32   fn display_scale() -> f32
fn time() -> f64            fn delta_time() -> f64            fn frame_count() -> i64
fn native_window() -> u64   fn native_display() -> u64        fn native_kind() -> i64
fn presented(shown: bool)   fn needs_present() -> bool        // WINDOW_ONLY
```

`open` 的标志位包括：`WINDOW_RESIZABLE`、`WINDOW_HIDDEN`、`WINDOW_FULLSCREEN`、`NO_VSYNC`、`DEBUG`（在已安装验证层时启用；在原生层的调试构建中默认开启）、`WAIT_WHEN_IDLE` 以及 `WINDOW_ONLY`（见下文）。若环境变量中设置了 `K_GFX_HEADLESS=1`，`open` 将不创建窗口；测试运行器即使用此方式。屏幕是一个与窗口像素尺寸相同的 RGBA8 纹理。其深度纹理在首次调用 `screen_depth()` 时创建（`begin_screen` 会调用该函数）；不需要深度的 2D 程序可以使用 `begin_pass(screen(), Texture { 0 }, clear)` 进行绘制。一帧会绘制到屏幕纹理中，随后 `frame()` 将其显示出来。窗口具有传给 `open` 的宽高（以显示器自身的坐标为单位），并包含该区域内显示器的全部像素。在高像素密度显示器上，像素数会大于其逻辑宽高；`screen_size()` 返回的正是这些像素的尺寸，鼠标位置也按像素报告。

`pixel_density()` 返回屏幕每单位逻辑坐标对应的像素数：在大多数高分屏上为 2，在其他显示器上为 1（包括任意缩放设置下的 Windows）。`display_scale()` 则是该值乘以系统的内容缩放比例，即系统绘制自身文本与控件所用的比例：高分屏 Mac 上为 2，设置为 150% 缩放的 Windows 上为 1.5。两者均取自 SDL 为窗口报告的值；无头模式或未打开窗口时为 1。窗口在显示器之间移动时这些值会随之变化，因此需要跟随缩放的程序应在每一帧重新读取。它们仅用于查询报告：gfx 与 ui 都不会自动应用缩放，绘制尺寸由程序自行决定。若希望 ui 的大小与系统自身保持一致，可将 `display_scale()` 传给 `ui::set_scale`。

启用 `WAIT_WHEN_IDLE` 时，仅当自上次调用以来有工作使用了 GPU，或者窗口需要重绘时，`frame()` 才会呈现画面。若某一帧未做任何绘制，下一次 `frame()` 会等待事件到来，最长等待 1 秒（当有手柄打开且窗口拥有焦点时为 16 ms），除非已经调用了 `quit()`；`delta_time()` 不计入这段等待时间，因此在空闲期间运行的计时器应读取 `time()`。窗口调整大小会使屏幕变黑，因此程序应在 `screen_size()` 发生变化时重绘。

启用 `WINDOW_ONLY` 时，`open` 仅创建窗口并处理输入、事件与时间，而不创建 GPU 设备，以便由其他渲染器向窗口绘制；此时调用任何需要 GPU 设备的函数都属于误用。`native_window()` 返回用于绘制的原生句柄：Apple 系统上为 `CAMetalLayer`，Windows 上为 `HWND`，Android 上为 `ANativeWindow`（每次应用回到前台时为新句柄，在后台时为 0），Linux 上为 `wl_surface` 或 X11 窗口，无头模式下为 0。在 Linux 上，`native_display()` 返回 `wl_display` 或 `Display*`，`native_kind()` 指明类型：`NATIVE_WAYLAND` 或 `NATIVE_X11`。其他视频驱动或平台会导致 `open` 失败。若同时启用 `WAIT_WHEN_IDLE`，渲染器需在每帧调用 `presented(shown)`；若某一帧未呈现画面，下一次 `frame()` 将进入等待；当窗口需要重新获得图像时，`needs_present()` 为 true。未启用 `WINDOW_ONLY` 而调用 `presented()` 属于误用。

`set_fullscreen` 可将窗口切换为桌面分辨率下的无边框全屏，或恢复为普通窗口。若窗口无法切换，或处于无头模式下，则返回 false。由于 SDL 以异步方式执行切换，`fullscreen()` 的返回值可能会滞后一到两帧。`maximize` 可将窗口最大化以填满桌面。

```klang
guard gfx::open("demo", 1280, 720) else { abort(str("gfx: ", gfx::error())); }
while gfx::frame() {
    gfx::begin_screen(float4 { 0.1, 0.1, 0.1, 1.0 });
    // bind, uniforms, draw ...
    gfx::end_pass();
}
gfx::close();
```

错误处理：因程序外部原因可能失败的调用会返回 false 或零句柄，并通过 `error()` 说明原因。而程序本不应发起的非法调用会在发生时打印错误信息并被跳过，并在下一次调用 `frame()`、`check()`、回读函数或 `close()` 时中止程序。例如：未绑定管线就进行绘制、着色器需要采样的纹理未绑定、使用已释放的句柄，以及 uniform 结构体大小不匹配。

如果 GPU 设备丢失（Vulkan 下的 `VK_ERROR_DEVICE_LOST`），会打印一次提示信息，此后 `frame()` 始终返回 false（如同窗口已关闭一样），以便程序保存状态并退出。目前尚未识别 Direct3D 12 的设备移除（device removal）与 Metal 的设备丢失。

<a id="input"></a>
### 输入

```klang
fn key_down(name: const u8[:]) -> bool        // SDL's key names: "A", "Space", "Left", "Escape"
fn key_pressed(name: const u8[:]) -> bool     // went down since the last frame()
fn key_released(name: const u8[:]) -> bool
fn mouse_down(button: i64) -> bool            // MOUSE_LEFT, MOUSE_MIDDLE, MOUSE_RIGHT
fn mouse_pressed(button: i64) -> bool         fn mouse_released(button: i64) -> bool
fn mouse_pos() -> float2    fn mouse_delta() -> float2      // in the screen's pixels
fn mouse_wheel() -> f32     fn mouse_in_window() -> bool    // over the window, or dragging from it
fn focused() -> bool                                      // keyboard focus at the last frame()
fn set_mouse_relative(on: bool) -> bool    fn mouse_relative() -> bool
fn set_cursor(shape: i64) -> bool                         // a system cursor, CURSOR_*
fn set_cursor_visible(visible: bool) -> bool    fn cursor_visible() -> bool
fn inject_key(name: const u8[:], down: bool) -> bool           // as if typed, seen at the next frame()
fn inject_mouse(x: f32, y: f32, button: i64, down: bool)       // to pixel x, y; button 0 only moves
fn inject_text(text: const u8[:]) -> bool                      // an EVENT_TEXT per character
fn inject_focus(focused: bool) -> bool                        // simulate focus at the next frame()
fn events() -> Event[>..]           // the last frame's input, in the order it came
fn text_input(on: bool)             // typed text as EVENT_TEXT: off by default
fn scancode(name: const u8[:]) -> i64       fn key_name(scancode: i64) -> u8[>..]
fn clipboard() -> u8[>..]           fn set_clipboard(text: const u8[:]) -> bool
fn open_count() -> i64              // times gfx was opened: older handles are gone
```

状态类函数报告当前帧的瞬时状态；`events()` 则报告两帧之间发生的所有输入事件，文本编辑通常需要这种事件流。事件类型包括：

* 按键按下与释放，附带扫描码（scancode）、按键对应的字符（`keycode`）以及按住的修饰键（`MOD_*`）；
* 输入的字符；
* 鼠标移动；
* 鼠标按键（`MOUSE_*`，含 X1 与 X2）及其连击次数；
* 鼠标滚轮。

在无头模式下，gfx 使用自有的剪贴板而非系统剪贴板。

`mouse_pos` 从窗口打开那一刻起就会报告鼠标位置，即使鼠标尚未第一次移动。按 SDL 的报告规则，当鼠标位于窗口上方，或按住从窗口内按下并拖拽的按键时，`mouse_in_window` 为 true。如果窗口恰好在鼠标下方打开，SDL 最晚会在鼠标第一次移动时报告该状态。一旦鼠标离开窗口，`mouse_pos` 会停留在离开时的屏幕外坐标处，因此边缘滚动需要同时检查这两者。在无头模式下，只要最后一次注入的位置位于屏幕范围内，鼠标就被视为处于窗口内。

`set_mouse_relative(true)` 会将鼠标限制在窗口内并隐藏光标。此时即使越过窗口边界，`mouse_delta()` 仍会持续以屏幕像素为单位报告相对位移：第一人称视角转向应使用它，而不是 `mouse_pos()`。切换到不同的模式会丢弃已排队的移动事件并清零当前增量，以免先前累积的移动突然转动视角；再次以相同模式调用则会保留移动状态。调用失败（包括窗口没有键盘焦点时）会返回 false，`error()` 会给出原因。

`set_cursor_visible(false)` 用于为自行绘制光标的程序隐藏系统光标。该偏好设置仅在非相对模式下生效：相对模式总是隐藏光标，而 `cursor_visible()` 报告的是窗口内的实际有效可见状态。退出相对模式后会恢复先前的可见性偏好。在无焦点状态下隐藏光标会失败；只要 gfx 处于打开状态，显示光标总是能成功。

`set_cursor` 用于选择一种系统光标形状，以提示点击鼠标下方元素将会触发的操作：

| 常量 | 光标形状 |
|---|---|
| `CURSOR_DEFAULT` | 默认箭头 |
| `CURSOR_TEXT` | 文本输入光标（I 型） |
| `CURSOR_POINTER` | 手型，用于链接或按钮上方 |
| `CURSOR_CROSSHAIR` | 十字准星 |
| `CURSOR_WAIT`、`CURSOR_PROGRESS` | 忙碌，以及忙碌但仍可点击 |
| `CURSOR_MOVE` | 四向移动 |
| `CURSOR_NOT_ALLOWED` | 此处禁止操作 |
| `CURSOR_EW_RESIZE`、`CURSOR_NS_RESIZE` | 双向调整边大小 |
| `CURSOR_NWSE_RESIZE`、`CURSOR_NESW_RESIZE` | 双向调整角大小 |
| `CURSOR_N_RESIZE`、`CURSOR_E_RESIZE`、`CURSOR_S_RESIZE`、`CURSOR_W_RESIZE` | 从对应边调整大小 |
| `CURSOR_NW_RESIZE`、`CURSOR_NE_RESIZE`、`CURSOR_SE_RESIZE`、`CURSOR_SW_RESIZE` | 从对应角调整大小 |

传入任何其他值或调用失败时返回 false，可通过 `error()` 查看原因。光标形状会一直保持到下次修改为止，且可见性与相对模式状态保持不变。在无头模式下仅校验形状值，不会改动桌面环境。`close()` 会恢复默认光标。

`focused()` 报告键盘焦点，这与 `mouse_in_window()` 不同。`events()` 会按顺序在输入流中包含 `EVENT_FOCUS_GAINED` 和 `EVENT_FOCUS_LOST`；这两类事件不带附加负载。失去焦点时，会清空所有按住的键盘与鼠标按键以及鼠标/滚轮增量，关闭相对模式，并将光标重置为可见。失焦期间收到的输入将被忽略。获得焦点时仍保持关闭相对模式，以便游戏在失去焦点时暂停并等待用户点击后再重新捕获鼠标。在 `EVENT_FOCUS_LOST` 之前，按住的键和按钮还会触发释放边沿状态以及合成的抬起事件，因此事件驱动的 UI 不会在切换应用时残留按住状态或拖拽状态。`close()` 同样会释放鼠标并恢复光标。

无头模式下的 gfx 启动时默认拥有焦点、光标可见且关闭相对模式。各 setter 函数会在不触碰系统鼠标的情况下模拟相应状态。`inject_focus` 用于在测试中排队相同的焦点切换状态，但不会改变操作系统的真实焦点。关闭并重新打开 gfx 会重置所有这些状态。

<a id="gamepads"></a>
### 游戏手柄

```klang
fn gamepad_connected(slot: i64) -> bool
fn gamepad_changes() -> i64                     // counts connects and disconnects
fn gamepad_type(slot: i64) -> i64               // SDL's SDL_GamepadType, 0 for an empty slot
fn gamepad_down(slot: i64, button: i64) -> bool
fn gamepad_pressed(slot: i64, button: i64) -> bool      // went down since the last frame()
fn gamepad_released(slot: i64, button: i64) -> bool
fn gamepad_axis(slot: i64, axis: i64) -> f32    // sticks -1..1, triggers 0..1, no deadzone
fn gamepad_rumble(slot: i64, low: f32, high: f32, ms: i64) -> bool   // strengths 0..1
```

手柄连接时按顺序占用 `0` 到 `MAX_GAMEPADS - 1` 的槽位；断开连接时释放对应槽位，并将其当时按住的所有按键报告为已释放。可通过监视 `gamepad_changes` 来感知连接或断开事件。按键遵循 SDL 的标准布局，按物理位置命名：`GAMEPAD_SOUTH` 对应 Xbox 上的 A 与 PlayStation 上的 Cross，其余依次为 `GAMEPAD_EAST`、`GAMEPAD_WEST`、`GAMEPAD_NORTH`、`GAMEPAD_BACK`、`GAMEPAD_GUIDE`、`GAMEPAD_START`、`GAMEPAD_LEFT_STICK`、`GAMEPAD_RIGHT_STICK`、`GAMEPAD_LEFT_SHOULDER`、`GAMEPAD_RIGHT_SHOULDER`、`GAMEPAD_DPAD_UP`、`GAMEPAD_DPAD_DOWN`、`GAMEPAD_DPAD_LEFT`、`GAMEPAD_DPAD_RIGHT`。摇杆与扳机轴包括 `AXIS_LEFT_X`、`AXIS_LEFT_Y`、`AXIS_RIGHT_X`、`AXIS_RIGHT_Y`、`AXIS_LEFT_TRIGGER`、`AXIS_RIGHT_TRIGGER`。

对空槽位读取时，所有按键均为未按下，轴值为 0，震动返回 false（不支持震动的手柄同理）。超出上述范围的槽位、按键或轴编号属于误用。没有手柄支持的机器表现为没有手柄，这并非错误。

<a id="buffers"></a>
### 缓冲区

```klang
struct Buffer { id: u32 }
fn buffer(usage: i64, data: const u8[:]) -> Buffer             // and (usage, size, data): zeros after
fn vertex_buffer(data: const u8[:]) -> Buffer                    // data from bytes_of(vertices)
fn index_buffer(data: const u8[:]) -> Buffer
fn update_buffer(b: Buffer, data: const u8[:]) -> bool           // and (b, offset, data)
fn read_buffer<T>(b: Buffer, out: T[>..]&, count: i64) -> bool   // appends count elements of T
fn read_buffer(b: Buffer, offset: i64, out: u8[:]) -> bool
fn buffer_size(b: Buffer) -> i64
fn release(b: Buffer)
```

用途标志包括：`BUFFER_VERTEX`、`BUFFER_INDEX`、`BUFFER_INDIRECT`、`BUFFER_STORAGE`（供顶点和片元着色器读取）、`BUFFER_COMPUTE_READ`、`BUFFER_COMPUTE_WRITE`。更新与回读操作发生在渲染通道之间，并与前后的绘制调用保持顺序；回读操作会等待 GPU 执行完成。

<a id="textures-and-samplers"></a>
### 纹理与采样器

```klang
struct Texture { id: u32 }
struct TextureDesc { kind: i32 = 0, format: i32 = 1, usage: i32 = 1, width: i32, height: i32,
                     depth: i32 = 1, mips: i32 = 1, samples: i32 = 1 }   // TEXTURE_2D, RGBA8, SAMPLED
struct Region { mip: i32 = 0, layer: i32 = 0, x: i32 = 0, y: i32 = 0, z: i32 = 0,
                w: i32 = 0, h: i32 = 0, d: i32 = 0 }    // w == 0: the whole mip
fn texture(desc: TextureDesc) -> Texture                 // mips 0: the full chain
fn texture2d(width: i64, height: i64) -> Texture         // and (..., format, usage)
fn render_target(width: i64, height: i64, format: i32) -> Texture   // COLOR_TARGET | SAMPLED
fn depth_target(width: i64, height: i64) -> Texture                 // DEPTH, and SAMPLED
fn texture3d(width, height, depth, format, usage)        fn texture_cube(size, format, usage)
fn texture_array(width, height, layers, format, usage)
fn load_texture(path: const u8[:], flags: i64) -> Texture   // PNG or BMP; LOAD_MIPS, LOAD_SRGB
fn update_texture(t: Texture, data: const u8[:]) -> bool     // and (t, region, data)
fn read_texture<T>(t: Texture, out: T[>..]&) -> bool         // appends mip 0 as T: u8, float4, ...
fn read_texture(t: Texture, region: Region, out: u8[:]) -> bool
fn read_pixels(t: Texture) -> u8[>..]                        // mip 0's bytes, top row first
fn save_png(t: Texture, path: const u8[:]) -> bool           // RGBA8 or BGRA8
fn screenshot(path: const u8[:]) -> bool                     // the screen, as shown
fn generate_mips(t: Texture)
fn texture_info(t: Texture) -> TextureDesc
fn format_supported(format: i32, usage: i32) -> bool
fn release(t: Texture)

struct Sampler { id: u32 }
struct SamplerDesc { min_filter: i32 = 1, mag_filter: i32 = 1, mip_filter: i32 = 1,
                     wrap_u: i32 = 0, wrap_v: i32 = 0, wrap_w: i32 = 0,
                     max_anisotropy: f32 = 1.0, compare: i32 = 0 }  // LINEAR, REPEAT
fn sampler(desc: SamplerDesc) -> Sampler      fn sampler(filter: i32, wrap: i32) -> Sampler
fn shadow_sampler() -> Sampler                // for sampler2DShadow
fn release(s: Sampler)
```

纹理类型包括：`TEXTURE_2D`、`TEXTURE_2D_ARRAY`、`TEXTURE_3D`、`TEXTURE_CUBE`（六个面 +x、-x、+y、-y、+z、-z 依次对应层 0 到 5）。用途包括：`SAMPLED`、`COLOR_TARGET`、`DEPTH_TARGET`、`STORAGE_READ`、`COMPUTE_READ`、`COMPUTE_WRITE`、`COMPUTE_READ_WRITE`；多重采样纹理只能作为渲染目标。格式包括：`RGBA8`、`BGRA8`、`RGBA8_SRGB`、`R8`、`RG8`、`RGBA16F`、`RGBA32F`、`R16F`、`RG16F`、`R32F`、`RG32F`、`R32UI`、`RGBA8UI`、`RGB10A2`、`RG11B10F`，深度格式 `DEPTH16`、`DEPTH24`、`DEPTH32F`、`DEPTH24_STENCIL8`、`DEPTH`（在同时支持采样的设备上为 32 位浮点格式），以及块压缩格式 `BC1`、`BC3`、`BC7`、`ASTC_4x4`、`ASTC_5x5`、`ASTC_6x6`、`ASTC_8x8`：块压缩格式仅支持 `SAMPLED` 用途，每次只能整层更新一个 mip（region 的 `w` 为 0，数据按完整块对齐），且不支持回读或调用 `generate_mips`。`format_supported` 用于查询设备是否支持创建指定格式与用途的纹理；在使用压缩格式前应先调用该函数检查。过滤模式包括 `NEAREST`、`LINEAR`；寻址包裹模式包括 `REPEAT`、`MIRROR`、`CLAMP`；比较函数包括 `COMPARE_LESS` 等。

<a id="pipelines-and-drawing"></a>
### 管线与绘制

```klang
struct Pipeline { id: u32 }
struct PipelineDesc { primitive: i32 = 0, cull: i32 = 0, clockwise: bool = false,
                      wireframe: bool = false, depth_test: bool = false,
                      depth_write: bool = false, depth_compare: i32 = 2, blend: i32 = 0,
                      instance_location: i32 = -1, depth_bias: f32 = 0.0,
                      depth_bias_slope: f32 = 0.0, formats: u8[16] = [0; 16] }
fn pipeline(vs: const u8[:], fs: const u8[:]) -> Pipeline      // and (vs, fs, desc)
fn release(p: Pipeline)

struct PassDesc { color: Texture[4], resolve: Texture[4], depth: Texture, layer: i32 = 0,
                  mip: i32 = 0, clear_color: bool = true, clear_depth: bool = true,
                  color_value: float4 = float4 { 0.0, 0.0, 0.0, 1.0 }, depth_value: f32 = 1.0 }
fn begin_pass(desc: PassDesc) -> bool
fn begin_pass(target: Texture, depth: Texture, clear: float4) -> bool   // Texture { 0 }: no depth
fn begin_screen(clear: float4) -> bool
fn end_pass()
fn bind(p: Pipeline)
fn bind_vertex_buffer(b: Buffer)                 // and (slot, b, offset): slot 1 per instance
fn bind_index_buffer(b: Buffer, index_size: i64) // 2 or 4 bytes; and (b, size, offset)
fn bind_texture(stage: i64, slot: i64, t: Texture, s: Sampler)     // VERTEX, FRAGMENT, COMPUTE
fn bind_storage_texture(stage: i64, slot: i64, t: Texture)
fn bind_storage_buffer(stage: i64, slot: i64, b: Buffer)
fn uniforms<T>(stage: i64, slot: i64, u: T)     // a flat struct as a uniform block
fn push_uniforms(stage: i64, slot: i64, data: const u8[:])
fn viewport(x: f32, y: f32, w: f32, h: f32)     fn scissor(x: i64, y: i64, w: i64, h: i64)
fn draw(vertices: i64)                          fn draw_instanced(vertices: i64, instances: i64)
fn draw_indexed(indices: i64)                   fn draw_indexed_instanced(indices: i64, instances: i64)
fn draw(vertices, instances, first_vertex, first_instance)
fn draw_indexed(indices, instances, first_index, vertex_offset, first_instance)
```

带有 `resolve` 目标的渲染通道会将其多重采样 `color` 目标解析到该目标中，并丢弃多重采样颜色与深度目标上的样本数据：后续通道如果加载而非清除它们，属于误用。不带解析目标的通道则会保留这些样本，因此一帧可以跨越多个渲染通道进行绘制，并在最后一个通道执行解析。

管线并不绑定到特定的输出纹理格式；它在首次于某个通道中绑定时，会针对该通道的目标格式按需创建。顶点着色器的输入按 location 顺序依次从单个紧凑元素中读取，布局方式与 K 结构体字段布局一致：例如 `struct Vertex { pos: float3, uv: float2 }` 会对应传入 `layout(location = 0) in vec3` 和 `layout(location = 1) in vec2`。从 `instance_location` 开始的输入则按实例从 1 号顶点缓冲区读取，而 `formats[location]` 可覆盖对应位置的顶点格式（例如用 `VERTEX_UBYTE4_NORM` 表示四个字节的颜色、`VERTEX_HALF2` 以及其他 `VERTEX_*` 格式）。图元类型包括 `TRIANGLES`、`TRIANGLE_STRIP`、`LINES`、`LINE_STRIP`、`POINTS`；剔除模式包括 `CULL_NONE`、`CULL_FRONT`、`CULL_BACK`（除非指定 `clockwise`，否则正面为逆时针绕序）；混合模式包括 `BLEND_NONE`、`BLEND_ALPHA`、`BLEND_ADD`、`BLEND_PREMULTIPLIED`、`BLEND_MULTIPLY`。

uniform 块采用 std140 布局，而 K 结构体则是紧凑排列的。在 std140 中，vec3 或 vec4 成员的起始偏移必须是 16 字节的倍数，且整个块的大小也会向上对齐到 16 字节，因此对应的结构体需要手动填充（例如 `struct Place { rect: float4, depth: f32, pad 12 }`）。推送大小与着色器块不一致的结构体属于误用。在 Direct3D 12 上，着色器中的 `gl_InstanceIndex` 不会计入绘制调用的 `first_instance`，因此如果着色器读取了 `gl_InstanceIndex`，请保持 `first_instance` 为 0。

<a id="compute"></a>
### 计算着色器

```klang
struct ComputePipeline { id: u32 }
fn compute_pipeline(cs: const u8[:]) -> ComputePipeline
fn begin_compute(rw_buffers: const Buffer[:]) -> bool     // and (rw_buffers, rw_textures)
fn bind(p: ComputePipeline)
fn dispatch(x: i64, y: i64, z: i64)                        // workgroups of the shader's local size
fn end_compute()
fn release(p: ComputePipeline)
```

<a id="matrices"></a>
### 矩阵

```klang
struct mat4 { c0: float4, c1: float4, c2: float4, c3: float4 }   // column-major, as GLSL's mat4
let mat4_identity
fn mul(a: mat4, b: mat4) -> mat4          fn mul(m: mat4, v: float4) -> float4
fn translate(t: float3) -> mat4           fn scale(s: float3) -> mat4
fn rotate(axis: float3, angle: f32) -> mat4
fn perspective(fovy: f32, aspect: f32, near: f32, far: f32) -> mat4   // right-handed, depth 0..1
fn ortho(left, right, bottom, top, near, far: f32) -> mat4
fn look_at(eye: float3, target: float3, up: float3) -> mat4
```

在所有后端上，裁剪空间均遵循 SDL_GPU 的约定：y 轴朝上，深度范围为 0 到 1，纹理坐标原点 (0, 0) 位于左上角。

<a id="audio"></a>
## audio

基于 SDL3 的过程式 PCM 音效模块，独立于窗口和图形设备。使用 `import audio;` 引入，符号位于 `audio::` 命名空间中。它是 `gfx` 包中的一个模块：使用 `-DK_BUILTIN_LIBS=all` 编译编译器时会一并编入，否则在首次使用时按需构建；`k build` 会自动链接它。[音频混音器设计](design/audio.md)

| 函数 | 行为 |
|---|---|
| `available() -> bool` | 原生层已链接时返回 true；不探测音频设备。 |
| `open(sample_rate = 48000) -> bool` | 打开默认输出设备，采用立体声输出。 |
| `open_offline(sample_rate = 48000) -> bool` | 不打开设备或后台线程，以离线模式打开同一个混音器。 |
| `close()` | 停止所有播放、释放所有声音、使全部句柄失效；可安全重复调用。 |
| `create_sound(samples: const f32[:], sample_rate = 48000, channels = 1) -> Sound` | 将单声道或交错立体声 PCM 拷贝到自有存储中。 |
| `release_sound(sound) -> bool` | 停止该声音的所有声部并释放 PCM 数据。传入零句柄无副作用。 |
| `play(sound, volume = 1.0, pan = 0.0, pitch = 1.0, looping = false) -> Voice` | 启动一个独立声部，允许同一个声音重叠播放。 |
| `playing(voice) -> bool` | 该声部是否仍在混音。过期或零句柄返回 false。 |
| `stop(voice)`, `stop_all()` | 停止指定声部或全部声部。对已完成、过期或零声部调用无副作用。 |
| `set_voice(voice, volume, pan, pitch) -> bool` | 在不重新启动的情况下修改正在播放的声部参数。 |
| `voice_position(voice) -> f64` | 返回正在播放的声部在声音中的位置（秒）；未播放时返回 -1。 |
| `seek(voice, seconds: f64) -> bool` | 将正在播放的声部跳转到声音中对应的秒数位置。 |
| `render(out: f32[:]) -> bool` | 仅离线模式可用：写出交错立体声输出并推进播放进度。 |
| `error() -> u8[>..]` | 最近一次失败的原因；成功的调用不会清除它。 |
| `error_into(out: u8[:]) -> i64` | 尽可能多地将错误信息拷贝到缓冲区中，并返回其完整长度。 |

所有调用都必须在主线程上执行；从 `thread_fn` 可达的调用会被拒绝。`Sound { id: u64 }` 和 `Voice { id: u64 }` 都是显式管理的资源。创建声音时会立即拷贝源数据：随后 K 代码即可修改或释放其数组。释放声音会同时停止它的所有声部。已播放完毕的声部会自动回收。新创建的资源绝不会复用旧句柄，即使跨过 `close()` 和 `open()` 也是如此。

全局仅有一个混音器，最多容纳 `MAX_SOUNDS = 256` 个声音以及 `MAX_VOICES = 64` 个并发声部。资源耗尽时会返回零句柄，而不会抢占已有声部。支持的采样率范围为 8000..192000 Hz。PCM 数据必须非空、全为有限浮点数、位于 `[-1, 1]` 范围内，且包含完整采样帧（立体声每帧两个采样）。音量范围为 `0..1`；声像（pan）范围为 `-1..1`，以 0 为中心，向一侧偏移时会衰减另一声道；音高（pitch）范围为 `0.125..8`，会同时改变播放速度与持续时间。混音采用线性插值，并将最终叠加结果裁剪到 `[-1, 1]`。当多个音效重叠播放时，请注意预留动态余量（headroom）。

非法参数、过期声音、不可用设备以及资源耗尽均会返回 false 或零句柄，并设置 `error()`，不会中止程序。停止或查询已播放完毕的声部属于正常操作；对其执行更新则返回 false。离线 `render` 要求输出样本数为偶数（允许空输出），并精确推进 `out.len / 2` 帧。设备模式下播放会自动推进；`playing()` 反映的是混音器的状态，因此播放结束或停止后，设备缓冲区中可能仍残留少许已混音的音频。`voice_position` 已将这段缓冲考虑在内：在设备模式下，它会返回听众实际听到的位置（在设备回调之间做平滑插值），从而可用于让其他逻辑与声音保持同步；在离线模式下，它反映混音器自身的位置。对循环声部执行超过末尾的 `seek` 会回绕，对非循环声部则会停在最后一帧；传入负位置则会失败。本模块不提供文件解码、流式输入、录音或音频效果器功能。

```klang
import audio;
import math;
import os;

fn main() {
    if !audio::open() { print(audio::error()); return; }
    var pcm: f32[>..] = [];
    for i in 12000 {
        let t = i as f64 / 48000.0;
        let envelope = 1.0 - i as f64 / 12000.0;
        pcm.push((sin(TAU * 440.0 * t) * 0.2 * envelope) as f32);
    }
    let tone = audio::create_sound(pcm);
    let voice = audio::play(tone);
    while audio::playing(voice) { sleep_ms(5); }
    sleep_ms(100); // Let the device finish the already mixed tail.
    audio::close();
}
```

<a id="physics"></a>
## physics

基于 Box3D 的刚体物理模块：包含物理世界、刚体、Box3D 支持的所有类型形状、九种关节、接触/传感器/碰撞/移动及关节事件、射线与形状投射及重叠查询、角色移动器，以及录制与回放。与 `gfx` 类似，该模块是可选的：需要以 `third_party/box3d` 子模块构建编译器，且使用它的程序需链接 `k --physics-link msvc|cc` 打印的链接参数（`cl game.c @<it>`、`cc game.c -o game @<it>`；若程序同时使用了 `gfx`，则需一并附带 gfx 的响应文件）。未内置该模块的编译器仍能对此类程序进行类型检查并生成 C 代码，仅在进程内直接运行时会失败。所有符号均位于命名空间 `physics` 中；`samples/28_physics_boxes.k` 是一个完整的示例程序。[物理设计](design/physics.md)

本 API 将 Box3D 的接口封装为 K 风格命名：`b3Body_GetPosition` 对应 `position(body)`，`b3CreateRevoluteJoint` 对应 `create_revolute_joint`；读取函数直接以属性名命名，设置函数则加上 `set_` 前缀。Box3D 使用米、千克和秒作为单位，且没有内建的“上方”方向；默认重力沿 y 轴为 -10。在 Box3D 原生接收回调函数的地方，本 API 采用了其他形式：事件与查询结果会以新建数组返回（每种同时提供 `_into` 版本，可将结果拷贝到调用方提供的切片中并返回实际数量），而摩擦与恢复系数的混合方式则通过 `MIX_*` 规则指定。暂不包含自定义接触过滤器、求解前回调（pre-solve callbacks）、调试绘制以及独立的动态树接口。

物理模块运行在主线程上；从 `thread_fn` 可达的调用会触发编译错误。Box3D 会将单步模拟分配到其自身的工作线程上并行执行，线程数量由 `WorldDef.workers` 指定，并且无论使用多少工作线程，模拟结果都完全一致。

<a id="errors"></a>
### 错误

因程序外部原因可能失败的调用（例如文件不存在，或点集无法构成凸包）会返回 false 或零句柄，并通过 `error()` 说明原因。而程序本不应发起的非法调用会在发生时被报告并跳过，并在下一次调用 `step()`、`destroy(world)` 或 `check()` 时中止程序。例如：操作已销毁的刚体、在移动关节（prismatic joint）上调用旋转关节（revolute）的函数，以及将高度场添加到动态刚体上。每个句柄都带有代数（generation）校验，因此使用已销毁的对象会直接报错而不会导致崩溃。

```klang
fn error() -> u8[>..]           fn check()             fn misuse_count() -> i64
fn available() -> bool          fn version() -> Version          // Box3D's
fn byte_count() -> i64          // what Box3D has allocated
fn set_length_units_per_meter(units: f32)     // before anything else is made
fn length_units_per_meter() -> f32
fn world_count() -> i64         fn max_world_count() -> i64
```

<a id="values"></a>
### 值类型

```klang
struct Quat { x: f32, y: f32, z: f32, w: f32 }       // a rotation; w the scalar part
struct Transform { p: float3, q: Quat }              // rotate by q, then move by p
struct Mat3 { cx: float3, cy: float3, cz: float3 }   // by columns
struct AABB { lower: float3, upper: float3 }
struct Plane { normal: float3, offset: f32 }
struct MassData { mass: f32, center: float3, inertia: Mat3 }
struct Sphere { center: float3, radius: f32 }
struct Capsule { center1: float3, center2: float3, radius: f32 }
let quat_identity           let transform_identity
fn quat_axis_angle(axis: float3, angle: f32) -> Quat
fn mul(a: Quat, b: Quat) -> Quat            // b, then a
fn rotate(q: Quat, v: float3) -> float3     fn inv_rotate(q: Quat, v: float3) -> float3
fn conjugate(q: Quat) -> Quat               fn normalize(q: Quat) -> Quat
fn rotation_matrix(q: Quat) -> Mat3
fn transform_point(t: Transform, p: float3) -> float3      // and inv_transform_point
fn mul(a: Transform, b: Transform) -> Transform            fn inv_mul(a, b) -> Transform
```

<a id="worlds"></a>
### 物理世界

```klang
struct World { id: u32 }
struct WorldDef { gravity: float3 = { 0, -10, 0 }, restitution_threshold: f32 = 1.0,
                  hit_event_threshold: f32 = 1.0, contact_hertz: f32 = 30.0,
                  contact_damping_ratio: f32 = 10.0, contact_speed: f32 = 3.0,
                  maximum_linear_speed: f32 = 400.0, enable_sleep: bool = true,
                  enable_continuous: bool = true, workers: i32 = 1, friction_mixing: i32 = 0,
                  restitution_mixing: i32 = 0, user_data: u64 = 0, capacity: Capacity }
fn create_world() -> World                   // and (def)
fn destroy(w: World)                         fn is_valid(w: World) -> bool
fn step(w: World, time_step: f32, sub_steps: i64)    // 1/60 and 4 are the usual
fn gravity(w) -> float3        fn set_gravity(w, gravity: float3)
fn enable_sleeping(w, flag)    fn sleeping_enabled(w) -> bool     // and continuous, warm_starting
fn restitution_threshold(w) -> f32   fn hit_event_threshold(w) -> f32   // and set_...
fn maximum_linear_speed(w) -> f32    fn contact_recycle_distance(w) -> f32  // and set_...
fn set_contact_tuning(w, hertz: f32, damping_ratio: f32, push_speed: f32)
fn set_friction_mixing(w, rule: i64)          fn set_restitution_mixing(w, rule: i64)
fn worker_count(w) -> i64      fn set_worker_count(w, count: i64)   // 1 to MAX_WORKERS
fn explode(w, def: ExplosionDef)       // position, radius, falloff, impulse_per_area, mask_bits
fn bounds(w) -> AABB           fn awake_body_count(w) -> i64
fn profile(w) -> Profile       fn counters(w) -> Counters      fn max_capacity(w) -> Capacity
fn user_data(w) -> u64         fn set_user_data(w, data: u64)
```

同时最多可以存在 128 个物理世界。混合规则包括：`MIX_DEFAULT`（Box3D 默认规则：摩擦取几何平均，恢复系数取较大值）、`MIX_GEOMETRIC`、`MIX_MIN`、`MIX_MAX`、`MIX_AVERAGE`、`MIX_MULTIPLY`。

<a id="bodies"></a>
### 刚体

```klang
struct Body { id: u64 }
struct BodyDef { body_type: i32 = 0, position: float3, rotation: Quat = quat_identity,
                 linear_velocity: float3, angular_velocity: float3, linear_damping: f32 = 0.0,
                 angular_damping: f32 = 0.0, gravity_scale: f32 = 1.0,
                 sleep_threshold: f32 = 0.05, safety_factor: f32 = 0.5,
                 motion_locks: MotionLocks, enable_sleep: bool = true, is_awake: bool = true,
                 is_bullet: bool = false, is_enabled: bool = true,
                 allow_fast_rotation: bool = false, enable_contact_recycling: bool = true,
                 user_data: u64 = 0 }
fn create_body(w: World, def: BodyDef) -> Body
fn destroy(b: Body)          // with its shapes and joints
fn is_valid(b) -> bool       fn world(b) -> World
fn body_type(b) -> i32       fn set_type(b, body_type: i64)   // STATIC, KINEMATIC, DYNAMIC
fn name(b) -> u8[>..]        fn set_name(b, name: const u8[:])
fn user_data(b) -> u64       fn set_user_data(b, data: u64)
fn position(b) -> float3     fn rotation(b) -> Quat     fn transform(b) -> Transform
fn transforms(bodies: const Body[:], out: Transform[:])   // many in one call
fn set_transform(b, position: float3, rotation: Quat)      // a teleport
fn local_point(b, p) -> float3    fn world_point(b, p) -> float3    // and local_/world_vector
fn linear_velocity(b) -> float3   fn angular_velocity(b) -> float3  // and set_...
fn local_point_velocity(b, p) -> float3        fn world_point_velocity(b, p) -> float3
fn set_target_transform(b, target: Transform, time_step: f32, wake: bool)   // kinematic
fn apply_force(b, force: float3, point: float3, wake: bool)   fn apply_force_to_center(b, force, wake)
fn apply_torque(b, torque: float3, wake: bool)
fn apply_linear_impulse(b, impulse, point, wake)   fn apply_linear_impulse_to_center(b, impulse, wake)
fn apply_angular_impulse(b, impulse: float3, wake: bool)
fn mass(b) -> f32     fn inverse_mass(b) -> f32     fn rotational_inertia(b) -> Mat3
fn world_inverse_inertia(b) -> Mat3    fn local_center(b) -> float3    fn world_center(b) -> float3
fn mass_data(b) -> MassData    fn set_mass_data(b, data: MassData)    fn apply_mass_from_shapes(b)
fn linear_damping(b) -> f32    fn angular_damping(b) -> f32    fn gravity_scale(b) -> f32  // and set_...
fn is_awake(b) -> bool         fn set_awake(b, awake: bool)     // the whole island
fn enable_sleep(b, flag)       fn sleep_enabled(b) -> bool
fn sleep_threshold(b) -> f32   fn safety_factor(b) -> f32       // and set_...
fn is_enabled(b) -> bool       fn disable(b)     fn enable(b)
fn motion_locks(b) -> MotionLocks      fn set_motion_locks(b, locks: MotionLocks)
fn is_bullet(b) -> bool        fn set_bullet(b, flag: bool)
fn fast_rotation_allowed(b) -> bool    fn allow_fast_rotation(b, flag: bool)
fn contact_recycling_enabled(b) -> bool    fn enable_contact_recycling(b, flag: bool)
fn enable_hit_events(b, flag: bool)          // on each of its shapes
fn shape_count(b) -> i64     fn shapes(b) -> Shape[>..]
fn joint_count(b) -> i64     fn joints(b) -> Joint[>..]
fn contacts(b) -> Manifold[>..]            // those touching
fn aabb(b) -> AABB           fn min_extent(b) -> f32    fn max_extent(b) -> float3
fn closest_point(b, target: float3) -> float3, f32
```

针对单个刚体的查询（将刚体置于给定变换位置上）：`cast_ray(b, origin, translation, filter, max_fraction, body_transform) -> RayHit`、`cast_shape`、`overlap_shape`、`collide_mover(b, origin, mover, filter, body_transform) -> PlaneHit[>..]` 以及 `time_of_impact_mover(b, origin, mover, translation, filter, transform1, transform2) -> ToiHit`。

<a id="shapes"></a>
### 形状

```klang
struct Shape { id: u64 }
struct ShapeDef { material: SurfaceMaterial, density: f32 = 1000.0, filter: Filter,
                  is_sensor: bool = false, enable_sensor_events: bool = false,
                  enable_contact_events: bool = false, enable_hit_events: bool = false,
                  invoke_contact_creation: bool = true, update_body_mass: bool = true,
                  enable_speculative_contact: bool = true, explosion_scale: f32 = 1.0,
                  user_data: u64 = 0 }
struct SurfaceMaterial { friction: f32 = 0.6, restitution: f32 = 0.0,
                         rolling_resistance: f32 = 0.0, tangent_velocity: float3,
                         user_material_id: u64 = 0, custom_color: u32 = 0 }
struct Filter { category_bits: u64 = ALL_BITS, mask_bits: u64 = ALL_BITS, group_index: i32 = 0 }
fn create_sphere_shape(b: Body, def: ShapeDef, sphere: Sphere) -> Shape
fn create_capsule_shape(b, def, capsule: Capsule) -> Shape
fn create_box_shape(b, def, half_extents: float3) -> Shape      // and (..., frame: Transform)
fn create_hull_shape(b, def, hull: Hull) -> Shape
fn create_transformed_hull_shape(b, def, hull, transform: Transform, scale: float3) -> Shape
fn create_mesh_shape(b, def, mesh: Mesh) -> Shape      // and (..., scale, materials)
fn create_height_field_shape(b, def, h: HeightField) -> Shape  // static bodies; and (..., materials)
fn create_compound_shape(b, def, c: Compound) -> Shape         // static bodies
fn destroy(s: Shape)         // and (s, update_body_mass: bool)
fn is_valid(s) -> bool       fn shape_type(s) -> i32    fn body(s) -> Body    fn world(s) -> World
fn is_sensor(s) -> bool      fn name(s) -> u8[>..]      fn user_data(s) -> u64   // and set_...
fn density(s) -> f32         fn set_density(s, density: f32, update_body_mass: bool)
fn friction(s) -> f32        fn restitution(s) -> f32   // and set_...
fn surface_material(s) -> SurfaceMaterial      fn set_surface_material(s, material)
fn mesh_material_count(s) -> i64    fn mesh_material(s, i) -> SurfaceMaterial   // and set_...
fn shape_filter(s) -> Filter        fn set_shape_filter(s, filter: Filter, invoke_contacts: bool)
fn enable_contact_events(s, flag)   fn contact_events_enabled(s) -> bool   // and sensor_, hit_
fn sphere(s) -> Sphere    fn capsule(s) -> Capsule    fn hull(s) -> Hull   // hull(): a copy
fn mesh(s) -> Mesh        fn mesh_scale(s) -> float3  fn height_field(s) -> HeightField
fn compound(s) -> Compound
fn set_sphere(s, sphere)  fn set_capsule(s, capsule)  fn set_hull(s, hull)  fn set_mesh(s, mesh, scale)
fn ray_cast(s, origin: float3, translation: float3) -> CastOutput     // in the world
fn contacts(s) -> Manifold[>..]     fn sensor_overlaps(s) -> Shape[>..]
fn aabb(s) -> AABB        fn mass_data(s) -> MassData     fn closest_point(s, target) -> float3
fn apply_wind(s, wind: float3, drag: f32, lift: f32, max_speed: f32, wake: bool)
```

形状类型包括：`SHAPE_SPHERE`、`SHAPE_CAPSULE`、`SHAPE_HULL`、`SHAPE_MESH`、`SHAPE_HEIGHT_FIELD`、`SHAPE_COMPOUND`。两个形状发生碰撞的条件是：彼此的类别位都在对方的掩码中；但若它们共享同一个分组索引（`group_index`），则处于正分组的形状总是互相碰撞，而处于负分组的形状绝不互相碰撞。网格形状只会与非网格形状发生碰撞。默认密度为水的密度，即 1000 kg/m³。

<a id="geometry"></a>
### 几何体

凸包（Hull）、网格（Mesh）、高度场（HeightField）和烘焙复合体（Compound）均只需创建一次即可由多个形状共享。形状会持有创建它所用的几何体。销毁正在被形状使用的网格、高度场或复合体只会释放该句柄，底层数据会在最后一个使用它的形状被销毁时才释放。凸包则会被拷贝到每一个使用它的物理世界中。

```klang
struct Hull { id: u32 }      struct Mesh { id: u32 }
struct HeightField { id: u32 }      struct Compound { id: u32 }
fn create_hull(points: const float3[:]) -> Hull    // zero for points in a plane; and (..., max_vertices)
fn create_box_hull(half_extents: float3) -> Hull   // and (..., frame)
fn create_cylinder_hull(height, radius, y_offset: f32, sides: i64) -> Hull
fn create_cone_hull(height, radius1, radius2: f32, slices: i64) -> Hull
fn create_rock_hull(radius: f32) -> Hull
fn transformed(h: Hull, transform: Transform, scale: float3) -> Hull
fn create_mesh(vertices: const float3[:], indices: const i32[:]) -> Mesh
fn create_mesh(vertices, indices, material_indices: const u8[:], def: MeshDef) -> Mesh
fn create_grid_mesh(x_count, z_count: i64, cell_width: f32, material_count: i64, identify_edges: bool) -> Mesh
fn create_wave_mesh(...)    fn create_torus_mesh(...)    fn create_box_mesh(center, extent, identify_edges)
fn create_hollow_box_mesh(center, extent)    fn create_platform_mesh(center, height, top_width, bottom_width)
fn create_height_field(heights: const f32[:], material_indices: const u8[:], def: HeightFieldDef) -> HeightField
fn create_grid_height_field(rows, columns: i64, scale: float3, make_holes: bool) -> HeightField
fn create_wave_height_field(rows, columns, scale, row_frequency, column_frequency, make_holes)
fn create_compound(spheres: const CompoundSphere[:], capsules: const CompoundCapsule[:],
                   hulls: const CompoundHull[:], meshes: const CompoundMesh[:]) -> Compound
fn destroy(h: Hull)   fn is_valid(h: Hull) -> bool   fn info(h: Hull) -> HullInfo   // each kind
fn vertices(h: Hull) -> float3[>..]    fn triangles(h: Hull) -> int3[>..]    // and for Mesh
```

相同的几何体也可以在其自身坐标系中单独进行查询：针对球体、胶囊体和凸包提供了 `compute_mass(g, density)`；针对每种几何体均提供了 `compute_aabb(g, transform)`、`ray_cast(g, origin, translation, max_fraction) -> CastOutput`、`overlap(g, transform, points, radius) -> bool` 和 `shape_cast(g, points, radius, translation, max_fraction, can_encroach) -> CastOutput`（网格需先传入其缩放比例）。针对两个带半径的点云之间，还提供了 `shape_distance`、`shape_cast` 以及基于两个 `Sweep` 的 `time_of_impact`（配合 `sweep_transform(sweep, time)` 使用）。

<a id="joints"></a>
### 关节

```klang
struct Joint { id: u64 }
struct JointDef { body_a: Body, body_b: Body, frame_a: Transform, frame_b: Transform,
                  collide_connected: bool = false, force_threshold: f32, torque_threshold: f32,
                  constraint_hertz: f32 = 60.0, constraint_damping_ratio: f32 = 2.0,
                  draw_scale: f32 = 1.0, user_data: u64 = 0 }
fn joint_def(a: Body, b: Body, anchor: float3) -> JointDef     // and (a, b, frame: Transform)
fn local_frame(b: Body, world_frame: Transform) -> Transform
fn create_revolute_joint(w: World, def: RevoluteJointDef) -> RevoluteJoint
// and create_distance_joint, _filter_, _motor_, _parallel_, _prismatic_, _spherical_,
// _weld_, _wheel_: each kind's definition starts with `base: JointDef`.
```

每种关节都有各自独立的句柄类型，例如 `struct RevoluteJoint { joint: Joint }`。特定关节类型的函数接收该类型句柄；所有关节通用的函数则接收其 `joint` 字段：

```klang
fn destroy(j: Joint)    // and (j, wake_bodies: bool)
fn is_valid(j) -> bool       fn joint_type(j) -> i32     fn body_a(j) -> Body    fn body_b(j) -> Body
fn world(j) -> World         fn local_frame_a(j) -> Transform     // and _b, and set_...
fn collide_connected(j) -> bool    fn user_data(j) -> u64       // and set_...
fn wake_bodies(j)            fn is_awake(j) -> bool
fn constraint_force(j) -> float3   fn constraint_torque(j) -> float3
fn linear_separation(j) -> f32     fn angular_separation(j) -> f32
fn constraint_tuning(j) -> f32, f32     fn set_constraint_tuning(j, hertz, damping_ratio)
fn force_threshold(j) -> f32        fn torque_threshold(j) -> f32     // and set_...
```

| 类型 | 作用 | 专属接口 |
|---|---|---|
| `RevoluteJoint` | 绕坐标系 z 轴旋转的铰链 | `angle`、`enable_limit`/`set_limits`/`lower_limit`/`upper_limit`、`enable_motor`/`set_motor_speed`/`set_max_motor_torque`/`motor_torque`、`enable_spring`/`set_spring_hertz`/`set_spring_damping_ratio`/`set_target_angle` |
| `PrismaticJoint` | 沿坐标系 x 轴滑动的滑块 | `translation`、`speed`、限位、马达（`set_max_motor_force`、`motor_force`）、目标为 `set_target_translation` 的弹簧 |
| `DistanceJoint` | 保持两个锚点相距一定距离 | `rest_length`/`set_rest_length`、`current_length`、`set_length_range`/`min_length`/`max_length`、带 `set_spring_force_range` 的弹簧、马达 |
| `SphericalJoint` | 球窝关节 | `enable_cone_limit`/`set_cone_limit`/`cone_angle`、`set_twist_limits`/`twist_angle`、目标为 `set_target_rotation` 的弹簧、目标为 `set_motor_velocity` 的马达 |
| `WeldJoint` | 将两个刚体固定为一体 | `set_linear_hertz`、`set_angular_hertz` 及其阻尼比；0 赫兹表示刚性连接 |
| `WheelJoint` | 带悬挂系统的车轮 | `enable_suspension`/限位、`enable_spin_motor`/`set_spin_motor_speed`/`spin_speed`、`enable_steering`/`set_target_steering_angle`/`steering_angle` |
| `MotorJoint` | 驱动刚体 b 相对刚体 a 运动 | `set_linear_velocity`、`set_angular_velocity`、对应限制与弹簧 |
| `ParallelJoint` | 保持两个坐标系的 z 轴平行 | `set_spring_hertz`、`set_spring_damping_ratio`、`set_max_torque` |
| `FilterJoint` | 仅阻止两个刚体相互碰撞 | |

每个 setter 都有对应的 getter。关节类型常量为 `JOINT_REVOLUTE` 等；将某种关节的函数用于另一种关节属于误用。

<a id="events"></a>
### 事件

事件在对应的模拟步执行完成后读取。每个函数返回上一模拟步产生的事件数组：

```klang
fn contact_begin_events(w) -> ContactEvent[>..]      // shape_a, shape_b, contact
fn contact_end_events(w) -> ContactEvent[>..]
fn contact_hit_events(w) -> ContactHitEvent[>..]     // point, normal, approach_speed, materials
fn sensor_begin_events(w) -> SensorEvent[>..]        // sensor, visitor
fn sensor_end_events(w) -> SensorEvent[>..]
fn body_move_events(w) -> BodyMoveEvent[>..]         // transform, body, user_data, fell_asleep
fn joint_events(w) -> JointEvent[>..]                // joint, user_data: past its thresholds
struct Contact { ... }
fn is_valid(c: Contact) -> bool          fn manifolds(c: Contact) -> Manifold[>..]
```

启用 `enable_contact_events` 的形状会报告接触事件，启用 `enable_hit_events` 的形状会报告碰撞事件（接近速度超过世界的 `hit_event_threshold` 时触发），而传感器与访问者双方都启用 `enable_sensor_events` 时会报告传感器访问事件。`Manifold` 包含对应接触及其两个形状、从 a 指向 b 的法线，以及最多四个带锚点、分离距离和冲量的 `ManifoldPoint`。

<a id="queries"></a>
### 查询

```klang
struct QueryFilter { category_bits: u64 = ALL_BITS, mask_bits: u64 = ALL_BITS }
fn overlap_aabb(w, box: AABB, filter: QueryFilter) -> Shape[>..]
fn overlap_shape(w, origin: float3, points: const float3[:], radius: f32, filter) -> Shape[>..]
fn cast_ray(w, origin: float3, translation: float3, filter) -> RayHit[>..]   // nearest first
fn cast_ray_closest(w, origin, translation, filter) -> RayHit                // hit false: none
fn cast_shape(w, origin, points, radius, translation, filter) -> RayHit[>..]
```

查询形状由围绕 `origin` 的至多 `MAX_PROXY_POINTS` 个点构成的凸包再向外膨胀 `radius` 得到：1 个点构成球体，2 个点构成胶囊体，8 个点则可构成盒体的八个角点。

<a id="a-character-mover"></a>
### 角色移动器

```klang
fn cast_mover(w, origin: float3, mover: Capsule, translation: float3, filter) -> f32
fn collide_mover(w, origin, mover, filter) -> PlaneHit[>..]
fn solve_planes(target_delta: float3, planes: CollisionPlane[:]) -> float3, i64
fn clip_vector(vector: float3, planes: const CollisionPlane[:]) -> float3
```

`cast_mover` 计算胶囊体可以移动的最远比例距离。`collide_mover` 返回它接触到的碰撞平面。将这些平面作为 `CollisionPlane` 传入 `solve_planes`，即可求出最接近目标位移的允许位移。`clip_vector` 则用于剔除速度矢量中指向这些平面的分量。

<a id="recording-and-replay"></a>
### 录制与回放

```klang
struct Recording { id: u32 }      struct Player { id: u32 }
fn create_recording() -> Recording        fn load_recording(path: const u8[:]) -> Recording
fn start_recording(w: World, r: Recording)        fn stop_recording(w: World)
fn size(r) -> i64     fn bytes(r) -> u8[>..]      fn save(r, path: const u8[:]) -> bool
fn validate_replay(r, workers: i64) -> bool       // replays it; the same?
fn create_player(r, workers: i64) -> Player
fn step(p: Player) -> bool     fn sub_step(p)     fn at_pre_step(p) -> bool
fn restart(p)     fn seek(p, frame: i64)     fn frame(p) -> i64     fn frame_count(p) -> i64
fn at_end(p) -> bool     fn diverged(p) -> bool     fn diverge_frame(p) -> i64
fn world(p) -> World     fn body_count(p) -> i64    fn body(p, index: i64) -> Body
fn info(p) -> PlayerInfo     fn set_worker_count(p, count: i64)
fn destroy(r: Recording)     fn destroy(p: Player)
```

录制对象会记录物理世界在 `start_recording` 与 `stop_recording` 之间发生的所有操作。播放器将其逐步回放到其自身拥有的物理世界中，并校验结果是否完全一致。当回放创建出录制中的刚体后，这些刚体便已存在；`body(p, k)` 返回第 k 个创建的刚体。

```klang
let world = physics::create_world();
let ground = physics::create_body(world, physics::BodyDef { position: float3 { 0.0, -1.0, 0.0 } });
physics::create_box_shape(ground, physics::ShapeDef {}, float3 { 20.0, 1.0, 20.0 });
let crate = physics::create_body(world, physics::BodyDef { body_type: physics::DYNAMIC,
                                                           position: float3 { 0.0, 4.0, 0.0 } });
physics::create_box_shape(crate, physics::ShapeDef {}, float3 { 0.5, 0.5, 0.5 });
for i in 90 { physics::step(world, 1.0 / 60.0, 4); }
print(physics::position(crate).y);        // resting on the ground: about 0.5
physics::destroy(world);
```

<a id="ui"></a>
## ui

基于即时模式 GUI 库 Nuklear、并通过 `gfx` 绘制的窗口与控件：窗口、行与布局空间、分组、树形视图、列表视图、标签、按钮、复选框、单选框、可选项、滑块、旋钮、进度条、颜色选择器、属性控件、文本输入框与文本编辑器、图表、弹出层、组合框、上下文菜单、工具提示、菜单、样式与字体。与 `gfx` 一样，该模块是可选的：它需要编译器在构建时包含 `third_party/nuklear` 子模块，使用它的程序需要链接 `k --ui-link msvc|cc` 输出的内容；如果通过 gfx 渲染，还需一并链接 gfx 的库（`cl app.c @<ui> @<gfx>`）。即使编译器在构建时未包含该模块，仍可对此类程序进行类型检查并生成 C 代码；只是在进程内运行该程序时会失败。所有内容均位于命名空间 `ui` 下；`samples/29_ui_todo.k` 是一个完整示例程序，而功能包的 [UI 设计](design/ui.md) 文档描述了其原生层。

该 API 在 K 语言名称下沿用了 Nuklear 的接口：`nk_button_label` 对应 `button_label`。在即时模式下，程序在每一帧中按顺序声明有哪些窗口和控件，而这些相同的调用也会同时反馈用户的操作：`button_label` 在按钮被点击的那一帧返回 true，`checkbox_label` 翻转传入的 `bool`，`slider_float` 更新 `f32` 的值。程序自行维护自身的数据；上下文仅记录窗口的位置以及哪些内容处于打开、激活或滚动状态。

```klang
guard gfx::open("sound", 400, 300) else { abort(str("gfx: ", gfx::error())); }
let ctx = ui::create(13.0);
var volume: f32 = 0.5;
var muted = false;
while gfx::frame() {
    ui::input_from_gfx(ctx);
    if ui::begin(ctx, "Sound", ui::rect(20, 20, 220, 130), ui::WINDOW_TITLE | ui::WINDOW_MOVABLE) {
        ui::layout_row_dynamic(ctx, 24.0, 1);
        ui::slider_float(ctx, 0.0, volume, 1.0, 0.05);
        ui::checkbox_label(ctx, "Muted", muted);
        if ui::button_label(ctx, "Reset") { volume = 0.5; }
    }
    ui::end(ctx);
    gfx::begin_screen(float4 { 0.1, 0.1, 0.1, 1.0 });
    gfx::end_pass();
    ui::render(ctx);
}
ui::destroy(ctx);
```

一帧由输入（此处来自 gfx）、随后的各个窗口（每个 `begin` 后都必须配对调用 `end`，无论 `begin` 是否返回 true）以及绘制窗口构建的内容组成。在窗口内部，每个控件占据当前行的下一个位置，因此必须先调用行布局（`layout_row_*`）。ui 运行在主线程上；如果 `thread_fn` 接触到它，将会导致编译错误。

<a id="errors-1"></a>
### 错误

由于程序外部原因可能失败的调用（例如字体文件不存在或字节数据不是有效字体）会返回 false 或零句柄，并可通过 `error()` 查看原因。程序本不应发起的调用会在发生时被打印并跳过，随后程序会在下一次调用 `input_begin`、`convert`、`render`、`destroy(context)` 或 `check()` 时中止。此类错误示例包括：在窗口外调用控件、结束一个从未开始的分组、在另一个窗口内部开始新窗口、传入无效的按键值，以及使用已销毁的句柄。上下文、字体图集、字体和文本编辑器都是带有代次（generation）的句柄，因此使用已销毁的句柄会报错而非直接崩溃。

```klang
fn error() -> u8[>..]     fn check()     fn misuse_count() -> i64     fn available() -> bool
```

<a id="contexts-and-input"></a>
### 上下文与输入

```klang
struct Context { id: u32 }
fn create(font_height: f32) -> Context        // Nuklear 自带字体；13 为其原生大小
fn create(font_height: f32, scale: f32) -> Context      // 按指定缩放比例绘制（set_scale）
fn create_from_file(path: const u8[:], font_height: f32) -> Context   // 使用独立的 TrueType 字体文件
fn create_from_memory(ttf: const u8[:], font_height: f32) -> Context  // 从字体字节数据加载
fn create_context(font: Font) -> Context      // 使用已烘焙图集中的字体
fn destroy(c: Context)        fn is_valid(c: Context) -> bool
fn context_atlas(c) -> FontAtlas              fn font(c) -> Font      // 当前用于绘制的字体
fn clear(c)                   // 结束一帧；对于已绘制的帧，input_begin 会自动执行此操作
fn set_scale(c, scale: f32) -> bool           fn scale(c) -> f32      // 每个 ui 单位对应的像素数
fn set_delta_time(c, seconds: f32)            fn delta_time(c) -> f32
fn set_clipboard(c, text: const u8[:])        // KEY_PASTE 粘贴的内容
fn copied(c) -> u8[>..], bool                 // 文本输入框复制或剪切的内容（如有）
fn input_begin(c)             fn input_end(c)
fn input_motion(c, x: i64, y: i64)
fn input_button(c, button: i64, x: i64, y: i64, down: bool)     // BUTTON_LEFT, _MIDDLE, _RIGHT, ...
fn input_scroll(c, amount: float2)
fn input_key(c, key: i64, down: bool)         // KEY_*：编辑键与快捷键
fn input_text(c, text: const u8[:])           // 以及 input_char、input_glyph、input_unicode
```

整个 ui 可以通过 `set_scale` 放大或缩小绘制。程序可以按需选择缩放比例，例如根据窗口大小、显示器或设置来决定。布局保持不变：窗口、行、字体和样式都保留其在 ui 自身单位下的尺寸。缩放比例即每单位的像素数；`convert` 和 `render` 按该比例绘制，输入位置也以这些像素为单位给出。由 `create` 或 `create_from_*` 创建的上下文会按新比例重新烘焙字体，从而使文本保持清晰。（Nuklear 的内置字体模拟点阵字体，仅在整数倍缩放下效果较好；TrueType 字体在任意缩放比例下都清晰。）基于程序自有字体创建的上下文会保持该字体图集原样不变；`bake(atlas, scale)` 可使图集在指定比例下保持清晰，并在每次切换到新比例时再次调用。`set_scale` 应在帧与帧之间调用，而不是在帧内调用。

输入位于 `input_begin` 和 `input_end` 之间，在任何窗口之前，每帧一批；`input_from_gfx`（见下文）会完成所有这些操作。位置以像素为单位。同一帧内按下并释放的按键计为一次按下。Nuklear 会在之后按 `KEY_*` 值的顺序处理一帧中的按键。按钮的按下或释放也会清零该帧的鼠标移动量。控件所看到的输入也可以直接查询：

```klang
fn input_mouse(c) -> Mouse           // pos、prev、delta、scroll_delta、抓取状态
fn input_is_mouse_down(c, button) -> bool      // 以及 _pressed、_released
fn input_is_key_down(c, key) -> bool           // 以及 _pressed、_released
fn input_is_mouse_hovering_rect(c, r: Rect) -> bool     // 以及 _prev_、_still_
fn input_has_mouse_click_in_rect(c, button, r) -> bool  // 上一次点击发生的位置
fn input_is_mouse_click_in_rect(c, button, r) -> bool   // 本帧在该区域内释放
fn input_is_mouse_hovering_delay_rect(c, r, timer: f32&, delay: f32) -> bool
```

一次点击指的是按钮按下或抬起。`has_` 查询检查该按钮上一次点击发生的位置，而 `is_` 查询检查本帧内是否发生了点击。`_delay` 查询在程序跨帧保存的计时器中累计悬停时间（同样还有 `_still_delay` 和 `_still_delay_clicked`）。

与 ui 共享输入的程序（例如拥有自身按键控制的游戏）可以询问输入当前归属于哪一方：

```klang
fn wants_keyboard(c) -> bool     // 文本输入框或正在输入内容的属性控件占用了按键
fn wants_mouse(c) -> bool        // 指针位于接收输入的窗口或弹出层上方，或正从其中拖拽
```

点击文本输入框会使其获得键盘焦点。它会一直保持焦点，直到在其外部点击、调用 `edit_unfocus`，或者其所属窗口关闭或未在某帧中出现。移动鼠标不会改变焦点。当其所在窗口位于其他窗口后面、被隐藏或被自身弹出层遮挡时，该输入框仍保持焦点状态，但在窗口回到前景、重新显示或弹出层关闭之前不会收到任何按键。

当指针位于窗口或弹出层上方，或在其中按下的鼠标按钮仍保持按住状态时，鼠标输入归 ui 所有。在别处按下的按钮在释放前一直将鼠标输入保留给程序。带有 `WINDOW_NO_INPUT` 创建的窗口或弹出层（例如消息日志或悬停信息）会将鼠标输入透传给程序。它不能被移动、缩放、滚动或置顶，其控件也不会接收点击。每当鼠标位于窗口上方时，Nuklear 的 `item_is_any_active` 就为 true，因此它无法用来判断键盘输入归谁所有。

<a id="text-rendering-and-game-huds"></a>
### 文本渲染与游戏 HUD

`ui` 除了控件之外，还为 gfx 应用提供文本渲染：`ui::label` 使用行/空间布局，而 `ui::draw_text` 则在当前窗口的画布上按显式指定的矩形绘制。`ui::create(13.0)` 无需资源文件即可提供内置字体；`create_from_file` 和 `create_from_memory` 提供 TrueType 字体。字体度量与图集访问列于[字体](#fonts)。

对于 HUD，只需创建一次专用上下文，隐藏其窗口背景，并使用不带标题和边框的 `WINDOW_NO_INPUT | WINDOW_NO_SCROLLBAR`。窗口提供画布与裁剪功能，无需交互式控件。以下示例可直接放入 gfx 程序中（在打开 gfx 之后）：

```klang
let hud = ui::create(13.0);
if hud.id == 0 { abort(ui::error()); }
var style = ui::style(hud);
style.window.fixed_background = ui::style_item_hide();
ui::set_style(hud, style);
while gfx::frame() {
    ui::input_from_gfx(hud);
    if ui::begin(hud, "HUD", ui::rect(0, 0, 320, 80),
                 ui::WINDOW_NO_INPUT | ui::WINDOW_NO_SCROLLBAR) {
        ui::draw_text(hud, ui::rect(12, 12, 280, 24), "Health: 100",
                      ui::font(hud), ui::Color { 0, 0, 0, 0 },
                      ui::Color { 255, 255, 255, 255 });
    }
    ui::end(hud); // 始终与 begin 配对，即使 begin 返回 false。
    // 在此绘制游戏场景并结束其渲染通道。
    ui::render(hud);
}
ui::destroy(hud); // 在 gfx::close() 之前调用。
```

在游戏场景之后绘制 HUD。`ui::render` 在内部使用 gfx 并保留其下方的场景。若需要可调整大小的全屏叠加层，可将窗口边界更新为视口大小；对于自定义渲染器，ui 也可以导出其文本与几何绘制命令，或转换后的三角形数据。世界空间精灵、广告牌朝向和精灵动画属于游戏的精灵渲染器；普通的 HUD 文本可以使用现有的这条字体路径。

<a id="fonts"></a>
### 字体

```klang
struct FontAtlas { id: u32 }         struct Font { id: u32 }
fn create_font_atlas() -> FontAtlas              fn destroy(a: FontAtlas)
fn add_default_font(a, height: f32) -> Font      // ProggyClean
fn add_font_from_file(a, path: const u8[:], height: f32) -> Font
fn add_font_from_memory(a, ttf: const u8[:], height: f32) -> Font
    // 各自还提供 (..., config: FontConfig) 和 (..., config, ranges: const u32[:]) 重载
fn bake(a) -> bool                   // 之后字体即可使用
fn bake(a, scale: f32) -> bool       // 字形大小为 scale 倍，用于按该比例绘制的 ui；
                                     // 以其他比例再次调用时，已添加的字体会保留
fn atlas_size(a) -> int2             fn atlas_pixels(a) -> u8[>..]     // RGBA
fn set_texture(a, texture: u32)      fn texture(a) -> u32
fn fonts(a) -> Font[>..]             fn atlas(f: Font) -> FontAtlas
fn info(f) -> FontInfo               // 高度、上行高度、下行高度、字形数量、回退字形
fn text_width(f, text: const u8[:]) -> f32
fn find_glyph(f, codepoint: u32) -> FontGlyph, bool
fn glyph_ranges(which: i64) -> u32[>..]          // RANGE_DEFAULT, _CHINESE, _CYRILLIC, _KOREAN
```

图集将其字体烘焙进同一张图像中；上下文使用其中一种字体进行绘制，并使用图集中的白色纹素来绘制形状。`FontConfig` 保存这些配置，默认使用 Nuklear 的默认值：过采样、像素对齐、字距、要烘焙的字符集（`ranges`，可以是 `RANGE_*` 集合，或以数组给出的起止字符码点对）、回退字形，以及将字形合并到图集第一个字体中的 `merge_mode`。字体需在图集首次烘焙前添加。图集保留字体的数据，在不同缩放比例下再次烘焙会原地替换它们的字形和图像，因此使用其字体的上下文可以继续使用它们，并在新比例下清晰绘制。上下文使用创建它时所选字体的图集，或最近一次通过 `style_set_font` 指定的字体的图集来绘制形状；销毁上下文仍在使用的图集是错误行为。

<a id="windows-and-layout"></a>
### 窗口与布局

```klang
fn begin(c, title: const u8[:], bounds: Rect, flags: i64) -> bool   // WINDOW_* 标志
fn begin_titled(c, name, title, bounds, flags) -> bool
fn end(c)
fn window_get_bounds(c) -> Rect      // 以及 _position、_size、_content_region、_scroll 等
fn window_has_focus(c) -> bool       fn window_is_hovered(c) -> bool
fn window_is_collapsed(c, name) -> bool       // 以及 _closed、_hidden、_active
fn window_set_bounds(c, name, bounds: Rect)   // 以及 _position、_size、_focus、set_scroll
fn window_close(c, name)       fn window_collapse(c, name, state: i64)   // MINIMIZED, MAXIMIZED
fn window_show(c, name, state: i64)          // SHOWN, HIDDEN；以及 _if 形式
fn layout_row_dynamic(c, height: f32, cols: i64)            // 宽度均分
fn layout_row_static(c, height: f32, item_width: i64, cols: i64)
fn layout_row(c, format: i64, height: f32, ratios: const f32[:])    // DYNAMIC, STATIC
fn layout_row_begin(c, format, height, cols)   fn layout_row_push(c, value: f32)   fn layout_row_end(c)
fn layout_row_template_begin(c, height)   // push_dynamic、push_variable(min)、push_static(w)、_end
fn layout_space_begin(c, format, height, widget_count)   fn layout_space_push(c, bounds: Rect)
fn layout_space_end(c)       // 以及 _bounds、_to_screen、_to_local
fn layout_set_min_row_height(c, height)   fn spacer(c)   fn spacing(c, cols)
```

行定义会布局其后的控件，并不断重复直到下一次行布局调用。对于折叠或隐藏的窗口，`begin` 返回 false，但之后仍必须调用 `end`。窗口按名称查找，因此同一帧内的两个窗口不能重名。

<a id="groups-trees-and-list-views"></a>
### 分组、树形视图与列表视图

```klang
fn group_begin(c, title, flags) -> bool          fn group_end(c)
fn group_scrolled_begin(c, offset: Scroll, title, flags) -> bool
fn group_scrolled_end(c) -> Scroll               fn group_get_scroll(c, id) -> Scroll
fn tree_push(c, type: i64, title, initial: i64) -> bool    // TREE_NODE 或 _TAB，MINIMIZED..
fn tree_pop(c)
fn tree_push_id(c, type, title, initial, id: i64) -> bool     // 区分相同标题的节点
fn tree_image_push(c, type, img: Image, title, initial) -> bool
fn tree_state_push(c, type, title, state: i64&) -> bool       fn tree_state_pop(c)
fn tree_element_push(c, type, title, initial, selected: bool&) -> bool   fn tree_element_pop(c)
fn list_view_begin(c, id, flags, row_height: i64, row_count: i64) -> ListView, bool
fn list_view_end(c)          // 仅构建 view.begin 到 view.end 之间的行
```

分组是窗口内带有自身行布局的可滚动面板；只有在其 begin 返回 true 时，才需要紧随其后的 end。树节点的内容位于返回 true 的 push 与其对应的 pop 之间。

<a id="widgets"></a>
### 控件

```klang
fn label(c, text: const u8[:], align: i64)       // TEXT_LEFT, TEXT_CENTERED, TEXT_RIGHT
fn label_colored(c, text, align, color: Color)   fn label_wrap(c, text)
fn value_int(c, prefix, v: i64)                  // 以及 _bool、_uint、_float、_color_*
fn link_label(c, text, align) -> bool            // 以及 _colored、_underline、_styled 等
fn image(c, img: Image)                          fn image_color(c, img, color)
fn button_label(c, text) -> bool                 // 本帧被点击
fn button_symbol(c, symbol: i64) -> bool         // SYMBOL_*；以及 _color、_image、_*_label
fn button_label_styled(c, style: const StyleButton&, text) -> bool    // 各类按钮均有对应版本
fn button_push_behavior(c, behavior: i64) -> bool    // BUTTON_DEFAULT, BUTTON_REPEATER
fn button_pop_behavior(c) -> bool
fn checkbox_label(c, text, active: bool&) -> bool          // 以及 _align、_flags_label
fn check_label(c, text, active: bool) -> bool              // 返回新状态
fn radio_label(c, text, active: bool&) -> bool   fn option_label(c, text, active: bool) -> bool
fn selectable_label(c, text, align, value: bool&) -> bool  // 以及 _image_、_symbol_；select_*
fn slider_float(c, min: f32, value: f32&, max: f32, step: f32) -> bool    // 以及 slider_int
fn slide_float(c, min, value, max, step) -> f32           // 以及 slide_int
fn knob_float(c, min, value: f32&, max, step, zero_direction: i64, dead_zone_degrees: f32) -> bool
fn progress(c, cur: i64&, max: i64, modifiable: bool) -> bool    fn prog(c, cur, max, modifiable) -> i64
fn color_picker(c, color: Colorf, format: i64) -> Colorf    // RGB, RGBA
fn color_pick(c, color: Colorf&, format) -> bool
fn property_float(c, name, min, value: f32&, max, step, inc_per_pixel: f32) -> bool
    // 以及 _int、_double；propertyi、propertyf、propertyd 直接返回新值
fn widget(c) -> i64, Rect            // 为程序自定义控件分配位置：WIDGET_*
fn widget_bounds(c) -> Rect          // 下一个控件的位置，以及 _position、_size 等
fn widget_is_hovered(c) -> bool      fn widget_is_mouse_clicked(c, button) -> bool
fn widget_disable_begin(c)           fn widget_disable_end(c)     // 显示为变灰且不可交互
```

`_label` 函数可接收任意 K 字符串，因此 Nuklear 的 `_text` 函数也映射到同一个调用。属性控件支持拖拽、点击箭头步进或直接输入；以 `#` 开头的名称不会显示。滑块和旋钮需要非空的取值范围。

<a id="text-editing"></a>
### 文本编辑

```klang
fn edit_string(c, flags: i64, text: T&, filter: i64) -> i64     // u8[..k]、u8[..]
fn edit_string(c, flags, buffer: u8[:], len: i64&, filter) -> i64
fn edit_buffer(c, flags, edit: TextEdit, filter) -> i64         // 保留撤销历史
fn edit_focus(c, flags)        fn edit_unfocus(c)                // 作用于下一个编辑框
fn filter_accepts(filter: i64, rune: u32) -> bool
fn create_text_edit() -> TextEdit     fn destroy(e: TextEdit)
fn text(e) -> u8[>..]          fn set_text(e, text)              fn state(e) -> TextEditState
fn set_cursor(e, cursor, select_start, select_end)               fn set_mode(e, mode: i64)
fn textedit_text(e, text)      fn textedit_delete(e, at, len)    fn textedit_delete_selection(e)
fn textedit_select_all(e)      fn textedit_cut(e) -> bool        fn textedit_paste(e, text) -> bool
fn textedit_undo(e)            fn textedit_redo(e)
```

标志位为 `EDIT_*`，常用组合包括 `EDIT_SIMPLE`、`EDIT_FIELD`（单行、可选中、支持剪贴板）和 `EDIT_BOX`（多行）。返回值会报告本帧的 `EDIT_ACTIVE` 或 `_INACTIVE`、`_ACTIVATED` 和 `_DEACTIVATED`，以及在启用 `EDIT_SIG_ENTER` 时按下 Enter 的 `_COMMITTED`。输入框只接受不超过其文本容量且通过 `FILTER_*`（`DEFAULT`、`ASCII`、`FLOAT`、`DECIMAL`、`HEX`、`OCT`、`BINARY`）过滤器的字符；粘贴行为接受与键入相同的字符。文本编辑器（`TextEdit`）自身保存文本、光标、选区、模式和撤销历史；基于缓冲区的输入框在帧间不保留撤销历史。
编辑键为 `KEY_*`；`KEY_COPY` 和 `KEY_CUT` 会将选区交给 `copied`，`KEY_PASTE` 则粘贴由 `set_clipboard` 设置的文本。

<a id="popups-combo-boxes-menus-and-tooltips"></a>
### 弹出层、组合框、菜单与工具提示

```klang
fn popup_begin(c, type: i64, title, flags, rect: Rect) -> bool     // POPUP_STATIC, _DYNAMIC
fn popup_close(c)     fn popup_end(c)     // 以及 popup_get_scroll、popup_set_scroll
fn combo(c, items: const u8[:][:], selected: i64, item_height: i64, size: float2) -> i64
fn combobox(c, items, selected: i64&, item_height, size) -> bool
    // 以及 combo_separator、将所有项放在单个字符串中的 combo_string
fn combo_begin_label(c, selected: const u8[:], size) -> bool   // 以及 _color、_symbol、_image
fn combo_item_label(c, text, align) -> bool     fn combo_close(c)     fn combo_end(c)
fn contextual_begin(c, flags, size: float2, trigger_bounds: Rect) -> bool
fn contextual_item_label(c, text, align) -> bool   fn contextual_close(c)   fn contextual_end(c)
fn tooltip(c, text)       fn tooltip_offset(c, text, position: i64, offset: float2)   // TOP_LEFT..
fn tooltip_begin(c, width: f32) -> bool           fn tooltip_end(c)
fn do_tooltip(c, text, bounds: Rect)    // 当鼠标悬停在 bounds 上时；以及 _delay(.., timer: f32&)
fn menubar_begin(c)       fn menubar_end(c)       // 位于其所属窗口最前面
fn menu_begin_label(c, text, align, size: float2) -> bool    // 以及 _image、_symbol 形式
fn menu_item_label(c, text, align) -> bool        fn menu_close(c)      fn menu_end(c)
```

弹出层、组合框、上下文菜单、菜单或工具提示都是各自独立的小窗口，绘制在打开它们的窗口之上，内部包含行布局。只有当对应的 begin 返回 true 时才应调用 end，且它们之中任意一个都不能在另一个内部打开。工具提示应在目标控件之前，并在 `widget_is_hovered` 为 true 时请求显示。

<a id="charts"></a>
### 图表

```klang
fn chart_begin(c, type: i64, count: i64, min: f32, max: f32) -> bool   // CHART_LINES, _COLUMN
fn chart_begin_colored(c, type, color: Color, active: Color, count, min, max) -> bool
fn chart_add_slot(c, type, count, min, max)      // 以及 _colored：另一条折线或柱状序列
fn chart_push(c, value: f32) -> i64              // CHART_HOVERING, CHART_CLICKED
fn chart_push_slot(c, value, slot: i64) -> i64
fn chart_end(c)
fn plot(c, type, values: const f32[:])           // 完整图表，按给定值自动缩放
```

<a id="style-and-colors"></a>
### 样式与颜色

```klang
fn style(c) -> Style              fn set_style(c, s: const Style&)
fn push_style(c, s) -> bool       fn pop_style(c) -> bool
fn style_default(c)               fn style_from_table(c, table: const Color[:])   // 每个 COLOR_* 对应一项
fn default_color_table() -> Color[>..]           fn style_get_color_by_name(color: i64) -> u8[>..]
fn style_set_font(c, f: Font)     fn style_push_font(c, f) -> bool     fn style_pop_font(c) -> bool
fn style_load_cursor(c, which: i64, cursor: Cursor)     // CURSOR_*
fn style_load_all_cursors(c, cursors: const Cursor[:])  // 每个 CURSOR_* 对应一项
fn style_set_cursor(c, which: i64) -> bool     fn style_show_cursor(c)     fn style_hide_cursor(c)
fn style_item_color(color) -> StyleItem          // 以及 _image、_nine_slice、_hide
fn rgb(r, g, b: i64) -> Color     // 以及 rgba、rgba_u32、rgb_f、rgba_f、rgb_cf、rgb_hex、hsv、hsva 等
fn color_cf(c: Color) -> Colorf   // 以及 color_u32、color_hex_rgba、color_hsv_f、color_hsva_f
fn rgb_factor(c: Color, factor: f32) -> Color
```

`Style` 逐字段对应 Nuklear 的 `nk_style`（包括 text、link、buttons、toggles、selectables、sliders、knobs、progress bars、properties、edits、charts、scroll bars、tabs、combo boxes、windows），但不包含字体和光标字段。要修改样式，请先读取它，修改字段后再设置回去。`StyleItem` 根据 `kind` 可以是颜色、图像或九宫格图像。软件光标是 Nuklear 在鼠标位置绘制的图像。

<a id="images-and-rectangles"></a>
### 图像与矩形

```klang
struct Rect { x: f32, y: f32, w: f32, h: f32 }
struct Image { texture: u32, w: u16, h: u16, region: u16[4] }    // 渲染器的纹理
fn rect(x, y, w, h) -> Rect       // f32 或 i64；以及 recta、rect_pos、rect_size、get_null_rect
fn image_id(texture: u32) -> Image               fn texture_image(t: gfx::Texture) -> Image
fn subimage_id(texture, w: u16, h: u16, region: Rect) -> Image     // w x h 纹理的一部分
fn nine_slice_id(texture, l, t, r, b: u16) -> NineSlice    // 以及带尺寸的 sub9slice_id
```

<a id="drawing-and-what-was-drawn"></a>
### 绘制与绘制结果

```klang
fn stroke_line(c, x0, y0, x1, y1: f32, thickness: f32, color: Color)
    // 以及 stroke_curve、_rect、_circle、_arc、_triangle、_polyline、_polygon
fn fill_rect(c, r: Rect, rounding: f32, color)
    // 以及 fill_rect_multi_color、_circle、_arc、_triangle、_polygon
fn draw_image(c, r: Rect, img: Image, color)     fn draw_nine_slice(c, r, slice: NineSlice, color)
fn draw_text(c, r: Rect, text, font: Font, background: Color, foreground: Color)
fn push_scissor(c, r: Rect)
fn commands(c) -> Command[>..]    fn command_points(c) -> int2[>..]    fn command_text(c) -> u8[>..]
fn convert(c, config: ConvertConfig) -> i64      // CONVERT_SUCCESS 或出错原因
fn vertices(c) -> Vertex[>..]     fn indices(c) -> u32[>..]    fn draw_commands(c) -> DrawCommand[>..]
fn null_texture(c) -> u32
```

画布函数会绘制到正在构建的窗口上，使用屏幕坐标，并受限于当前的裁剪矩形（scissor）。与 `widget` 配合使用，即可绘制程序自定义的控件。一旦一帧中的所有窗口都已结束，其绘制结果会以两种形式提供给任意渲染器：

* Nuklear 命令：每个形状、图像或文本对应一个 `Command`，多边形的顶点和文本的字节数据保存在其旁边；
* 通过 `convert` 转换得到的三角形：包含位置、纹理坐标和颜色的顶点，32 位索引，以及各自带有裁剪矩形和纹理的绘制调用（形状使用空纹理 null texture）。

这两种形式在下一帧输入开始前均保持可用。

<a id="through-gfx"></a>
### 通过 gfx

```klang
fn render(c: Context)                            // 绘制到屏幕上
fn render(c, target: gfx::Texture)               // 绘制到 COLOR_TARGET 纹理上
fn render(c, target, config: ConvertConfig)
fn input_from_gfx(c: Context)                    // 在 gfx::frame() 之后、任何窗口之前调用
```

`render` 会转换当前帧并以 Alpha 混合进行绘制；它必须在渲染通道之外调用。首次使用每个字体图集时，它会将其上传到 gfx 纹理，并在销毁图集时一并释放该纹理（`released_textures()` 会列出以此方式销毁的纹理，供程序自定义渲染器使用）。它所保留的资源（管线、采样器和缓冲区）只创建一次，并在 gfx 关闭后重新打开时再次创建。`input_from_gfx` 将 gfx 上一帧的输入按顺序传递给上下文：

* 鼠标输入；
* 编辑键（Delete、Backspace、Enter、Tab、Home、End、方向键、Page Up 与 Page Down、Escape）；
* 带有 Ctrl 或 Command 的快捷键（A、C、X、V、Z、Y）；
* 键入的文本（它会开启 gfx 的文本输入）；
* 当前帧的持续时间；
* 双向剪贴板操作，即粘贴系统剪贴板中的文本，并将输入框复制的内容写入系统剪贴板。

`render` 和 `input_from_gfx` 均以屏幕像素为单位工作，因此在高像素密度显示器上，ui 占用的像素数量与其他显示器相同，看起来会偏小，直到程序设置了缩放比例为止。
