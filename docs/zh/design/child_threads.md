<a id="child-threads-fork-join-over-shared-memory-inside-one-program-and-how-it-meets-par-for"></a>
# 子线程：单个程序内基于共享内存的 fork-join，及其与 `par for` 的结合

<details>
<summary>目录</summary>

- [0. 摘要](#0-summary)
- [1. 问题](#1-the-problem)
  - [1.1 无共享模型在计算内核上的开销](#11-what-share-nothing-costs-on-a-kernel)
  - [1.2 “低开销”必须意味着什么](#12-what-cheap-has-to-mean)
- [2. K 语言已经确立的设计](#2-what-k-already-decides)
  - [2.1 休眠的父线程冻结了所有生命周期](#21-a-dormant-parent-freezes-every-lifetime)
  - [2.2 可写性是自动推导的，因此“按 `const` 捕获”只需一条规则](#22-writability-is-inferred-so-captured-as-const-is-one-rule)
  - [2.3 循环体本就会被提纲化，自由变量本就按指针传递](#23-bodies-are-already-outlined-and-free-variables-already-pass-by-pointer)
  - [2.4 栈模型允许的两种循环体形态](#24-two-body-shapes-the-stack-model-allows)
  - [2.5 程序实例，以及池中线程必须接管的内容](#25-program-instances-and-what-a-pool-thread-must-adopt)
  - [2.6 不支持线程的后端直接运行循环](#26-backends-without-threads-run-the-loop)
  - [2.7 检查器如今对副作用掌握多少](#27-what-the-checker-knows-about-effects-today)
- [3. 其他系统的做法](#3-what-others-built)
  - [3.1 线程级与通道级并列对比](#31-thread-level-and-lane-level-side-by-side)
  - [3.2 安全规则：谁可以写什么](#32-the-safety-rule-who-may-write-what)
  - [3.3 线程池](#33-pools)
  - [3.4 归约](#34-reductions)
  - [3.5 应采纳的设计](#35-what-to-take)
- [4. 设计方案](#4-the-design)
  - [4.1 一种语法结构，两个层级](#41-one-construct-two-levels)
  - [4.2 循环采用哪一层级](#42-which-level-a-loop-gets)
  - [4.3 循环体可以访问什么](#43-what-a-body-may-touch)
  - [4.4 不相交写入：结构化优先，证明其次](#44-disjoint-writes-structural-first-proven-second)
  - [4.5 无栈循环体与有栈循环体](#45-stackless-and-stackful-bodies)
  - [4.6 结果、归约与确定性](#46-results-reductions-determinism)
  - [4.7 中止（Abort）](#47-aborts)
  - [4.8 语言规范中的表述](#48-in-the-spec)
  - [4.9 运行时](#49-the-runtime)
  - [4.10 编译器需要做的工作](#410-what-the-compiler-needs)
- [5. 查询引擎各阶段的完整代码](#5-the-query-engines-phases-written-out)
- [6. 备选方案对比](#6-options-side-by-side)
- [7. 建议与实施顺序](#7-recommendation-and-order)
- [8. 开放问题](#8-open-questions)
- [参考文献](#references)

</details>

研究笔记，2026 年 11 月。所提议的语法展示了 K 程序如何跨核心切分大数组上的工作，同时*无需复制该数组*：各线程就地读取父线程的数据，并各自写入输出中互不相交的部分，且线程的启动与汇合开销足够低，低到可以在查询引擎或图像处理流水线的每一趟中都使用一次。第二个问题是，这种线程如何与 [llm_frontier.md §4.3](llm_frontier.md#43-par-for-shared-memory-iteration-proven-race-free) 中提议的 SIMD 级 `par for` 结合：在同一种语言中，外层按线程池线程切分，内层按 SIMD 通道切分。第 1 节给出问题与实测数据，第 2 节说明语言本身已经确定的前提，第 3 节回顾现有方案，第 4 节阐述本设计，第 6 节列出替代方案，第 7 节给出构建顺序。

<a id="0-summary"></a>
## 0. 摘要

* **一项声明，两个关键字，两个层级。** 标有 `simd for`、`par for` 或 `par simd for` 的循环只作出一项声明——“这些迭代相互独立”，由同一套规则检查；关键字则指明它采用哪种降级（lowering）方式。`simd` 将迭代放在调用线程的 *SIMD 通道（lane）* 上执行，从不派生线程；`par` 将迭代范围切成连续分块（chunk）交给同一程序的*子线程（child thread）*，而每个分块内部仍是普通循环；`par simd` 两者兼顾，先按线程分块，再在每个分块内按通道向量化，相当于 OpenMP 的 `parallel for simd`。在 `par for` 循环体内部编写 `simd for` 是兼顾两者的另一种方式：即 ISPC 的 `launch` 加 `foreach`、Mojo 的 `parallelize` 加 `vectorize`、Halide 的 `.parallel(y).vectorize(x)`（[3.1](#31-thread-level-and-lane-level-side-by-side)）。每个层级都可以单独使用，且两者本身都不具有独立的语义：程序计算的结果与顺序循环完全一致（[4.1](#41-one-construct-two-levels)、[4.2](#42-which-level-a-loop-gets)）。
* **父线程处于休眠状态，因此共享是安全的。** 在循环运行期间，父线程不执行任何代码，它所拥有的任何对象都不会增长、收缩或离开作用域，它持有的每一个引用都保持有效：这正是 Rust `std::thread::scope` 的论证逻辑，凡是允许子线程借用父线程栈上数据的语言都采用同样的理由（[2.1](#21-a-dormant-parent-freezes-every-lifetime)、[3.2](#32-the-safety-rule-who-may-write-what)）。唯一剩下的问题是哪些写入可能发生冲突，答案是一份简短的清单：循环体捕获的所有外部位置都是只读的，唯独通过 `for &x` 交予的元素、通过分块内建函数交予的分块，以及证明器证明没有其他迭代会触及的位置除外（[4.3](#43-what-a-body-may-touch)、[4.4](#44-disjoint-writes-structural-first-proven-second)）。结构化不相交不需要新的静态分析，且覆盖了该议题中的所有阶段；llm_frontier §4.3 的双迭代证明则作为第二步加入。
* **两种循环体形态。** 仅包含固定大小局部变量的循环体直接在池线程的原生栈上运行，无需预留任何区域，也可在 SIMD 通道上运行。包含可增长局部变量的循环体则被编译为一个拥有独立栈块的*子程序（child program）*，池线程会在多次 fork 之间保留该栈块，并在任务之间将其回卷；它只能在线程上运行，而父线程自身执行的那一个分块则使用父线程高水位线之上的栈空间（[4.5](#45-stackless-and-stackful-bodies)）。
* **低开销，已有实测。** 在 32 线程的开发机上，目前每次 `thread_spawn` 加 `thread_wait` 的开销约为每个工作线程 40 µs，加上每个数据栈区域 7-10 µs，几乎全部耗在操作系统上；而由休眠线程组成的线程池，fork 并 join 8 个子线程只需 9-16 µs，32 个需要 43-57 µs；若线程池在休眠前短暂自旋，则只需 0.6-2.8 µs。该议题实测达到单核耗时 2.5 倍的数据拷入拷出开销则完全消失（[1.1](#11-what-share-nothing-costs-on-a-kernel)、[4.9](#49-the-runtime)）。
* **相对于 llm_frontier §4.3 的变化。** 在线程层级允许中止（abort）（程序会像在主线程中一样中止；当多个迭代都可能中止时，报告哪一个并未指定），通过子程序机制允许使用可增长局部变量，将结构化不相交与基于证明的不相交并列引入，并且运行时线程池由两个层级共用（[4.7](#47-aborts)）。
* **实施顺序。** 首先实现结构化分块和无栈子线程，这无需改动证明器，只需一条很小的检查器规则；其次实现子程序；接着实现双迭代证明；随后先将 `simd for` 作为向量化器断言实现，之后再扩展为 SPMD（[7](#7-recommendation-and-order)）。

<a id="1-the-problem"></a>
## 1. 问题

<a id="11-what-share-nothing-costs-on-a-kernel"></a>
### 1.1 无共享模型在计算内核上的开销

K 的线程互不共享：每个工作线程都是一个独立的程序，拥有自己的数据栈及其所使用全局变量的独立副本，并通过扁平值的类型化队列接收数据（[spec §11.2](../k_spec.md#112-concurrency)）。这正是该语言无需锁且不存在数据竞争的原因，对于服务器和流水线而言这是恰当的模型。但对于处理单个大数组的计算内核来说，这种模型并不合适，因为数据已经就位，而工作内容是一个各次迭代相互独立的循环。以下是该议题在 8 核 `-O2` 下的实测数据：

| 工作负载 | 1 核 | 8 个工作线程通过队列 | 8 个池化工作线程就地处理 |
|---|---|---|---|
| `samples/22_image_filters.k`，3840x2160 分辨率，20 趟模糊与 Sobel | 78 ms | 38 ms | 10 ms |
| 128 MB `i64` 变换，带 8 MB 查找表，20 趟 | 151 ms | 418 ms | 50 ms |

在第二个工作负载上，队列版比单核还慢，原因在于主线程必须独自一人把 128 MB 数据切分并复制进 `qput` 消息，再把结果从 `qget` 消息中复制出来，重复二十次，同时 8 MB 查找表也要作为副本发给每一个工作线程。第三列是该议题作者按本文提案思路实现的原型：工作线程直接读取父线程数组的 `const T[:]` 视图，并将结果写入输出中互不相交的 `T[:]` 片段，同时跨趟保留各自的数据栈。

派生线程是第二项开销，在设计线程池前有必要掌握其量级。以下是在 32 线程开发机（Windows，clang）上使用 `bench/fork_cost/spawn_cost.k` 和 `bench/fork_cost/pool_cost.c` 测得的数据：

| 子线程数 | `thread_spawn` + `thread_wait`，空工作线程 | 同左，带两个数据栈的工作线程 | `CreateThread` + join，C 语言 | 线程池，休眠状态，fork + join | 线程池，自旋状态，fork + join |
|---|---|---|---|---|---|
| 8 | 327 µs | 451 µs | 338-346 µs | 8.5-15.5 µs | 0.6-0.7 µs |
| 16 | 637 µs | 865 µs | 624-651 µs | 21-23 µs | 1.2-1.4 µs |
| 31 或 32 | 1238 µs | 1637 µs | 1214-1256 µs | 43-57 µs | 2.3-2.8 µs，偶发一次 39 µs |

由此可见，当前的派生开销本质上就是操作系统的线程创建开销——每个工作线程约 40 µs，再加上工作线程的每个数据栈保留 2 GB 地址空间、提交一页内存并释放所花的 7-10 µs；K 自身的打包与解包开销相比之下微不足道。通过 `WaitOnAddress`（Linux 上是 futex）唤醒常驻线程池的成本要低 25-40 倍；而像所有生产级运行时那样先让工作线程自旋一段有界时间再休眠（[3.3](#33-pools)），则能再降到 200-500 倍之低。32 个子线程时偶发的那一次慢速自旋，源自超额订阅（oversubscription）风险：当每个逻辑 CPU 都在忙于自旋时，一旦调度器抢占了某个工作线程，就会拖延 join 的完成，这正是 libgomp 在线程数超过 CPU 数时只自旋 1000 次的原因。

对照该议题的数据：128 MB 变换在池化版本下每趟耗时 2.5 ms，如果每趟都派生 8 个带栈工作线程（0.45 ms），派生开销会占到 18%；而从休眠线程池 fork（10 µs）只占 0.4%。查询引擎在数兆字节分区上的各阶段比这还要短，此时派生线程的耗时就会超过计算本身。

<a id="12-what-cheap-has-to-mean"></a>
### 1.2 “低开销”必须意味着什么

四项开销，按重要程度排序：

1. **输入零复制。** 子线程直接原地读取父线程的数组。这就是整整 418 ms 的来源。
2. **输出零复制。** 每个子线程直接把自己的那部分输出写到父线程将要读取的位置。对于确需每个子线程各自输出可增长结果的程序，这是唯一可以容忍复制的地方，而且这也只是结果的一次复制，而不是把所有数据都复制两遍（[4.6](#46-results-reductions-determinism)）。
3. **每趟不创建操作系统线程。** 使用延迟启动、贯穿整个程序生命周期的线程池。
4. **每趟不分配数据栈区域。** 不拥有可增长局部变量的子线程完全不需要数据栈区域；拥有可增长局部变量的子线程则在各趟之间保留自己的栈块并将其回卷（[4.5](#45-stackless-and-stackful-bodies)）。

还有一项该议题未提及、但无共享设计每次派生都会付出的开销：工作线程的全局变量会在 `thread_spawn` 时被复制（[implementation.md §6.8](../implementation.md#68-threads-and-queues)）。子线程则直接原地读取父线程的全局变量，一份也不复制。

<a id="2-what-k-already-decides"></a>
## 2. K 语言已经确立的设计

<a id="21-a-dormant-parent-freezes-every-lifetime"></a>
### 2.1 休眠的父线程冻结了所有生命周期

子线程读取父线程内存之所以安全，建立在一个事实之上：在子线程运行期间，父线程不执行任何代码。循环是父线程中的一条语句；在所有迭代结束之前，父线程一直停留在其中。因此在此期间，父线程拥有的任何对象都不会被赋值、增长、收缩或释放，循环体所捕获的每一个引用与切片在循环结束时都和开始时一样有效。K 还额外提供了绝大多数语言所没有的两点保障：

* **地址稳定。** 增长“永远不会移动内存；指向数据栈的引用在其下方数据的整个生命周期内都是稳定的”（[spec §1.2](../k_spec.md#12-memory-model)）。子线程持有的父线程可增长数组的切片，其内部就是不会变化的原始地址，无论父线程数组是什么形态（况且依前所述，它也根本无法增长）。
* **活出作用域规则（outlives rule）已经过检查。** 循环体是父函数的一个代码块，因此其内部创建的引用会在其内部消亡，遵循与任何普通代码块相同的规则（[spec §9.1](../k_spec.md#91-the-outlives-rule)、[§9.2](../k_spec.md#92-roots-and-the-depth-check)）。子线程创建的任何内容都不可能逃逸回父线程，唯一能传递出去的途径是写入所捕获的位置，这由 [4.3](#43-what-a-body-may-touch) 约束。

这正是 Rust 的 `std::thread::scope` 所作的论证：“作用域线程可以借用非 `'static` 数据，因为该作用域保证所有线程都会在作用域结束时 join”（[rust-scope]）。Rust 曾付出惨痛代价才明白，这一保证必须由语言结构自身来承担，而不能依赖可被 drop 的守卫对象（2015 年的“leakpocalypse”事件，[rust-24292]、[rfc-1066]）；后来所有语言的设计——Swift 的 task group、Java 的结构化任务作用域（[jep-453]）、Kotlin 的 `coroutineScope`（[kotlin-scope]）、Trio 的 nursery（“nursery 块在内部所有任务退出之前不会退出”，[njs]）、X10 的 `finish`、Chapel 的 `forall`——出于同样的原因都将父线程阻塞在一个词法作用域中（[3.2](#32-the-safety-rule-who-may-write-what)）。K 没有析构函数，也没有途径泄漏作用域，因此循环语句本身就是这个作用域，不需要额外机制。

<a id="22-writability-is-inferred-so-captured-as-const-is-one-rule"></a>
### 2.2 可写性是自动推导的，因此“按 `const` 捕获”只需一条规则

一个引用或切片是否允许通过它进行写入，“在每次实例化时以与根（root）相同的方式进行推导，无需任何标注”（[spec §9.5](../k_spec.md#95-writability)），并且语言本身就已经具备充当可写数据只读视图的绑定：按值绑定的 `for` 变量、`match` 绑定、`const` 值的字段。子线程所需要的规则——“父线程的所有变量在循环体内部都是只读的，唯独循环交予本次迭代的对象除外”——正是在循环边界施加这种只读视图，而且相应的诊断信息也已经存在（“cannot write through a read-only slice”）。[analysis_inventory.md](analysis_inventory.md#3-inventory) 中的检查器分析第 14 项只有 60 行代码，评级为 A-。

<a id="23-bodies-are-already-outlined-and-free-variables-already-pass-by-pointer"></a>
### 2.3 循环体本就会被提纲化，自由变量本就按指针传递

子线程循环体必须转换为一个接受范围与环境的 C 函数，而现有机制中已有三项为此铺好了路：

* 嵌套函数或函数值本就已经接收“嵌套函数或函数值的自由变量（固定大小的按指针，可调整大小的按头部指针加栈，池按 `k_pref`）”（[implementation.md §6.4](../implementation.md#64-the-calling-convention-c3)）。被提纲化（outlined）的循环体就是一个嵌套函数，其自由变量被打包进单个结构体，循环分块边界也放在一起。
* 线程 thunk（`EnsureThreadThunk`、`EmitThreadThunk`）已经能将工作线程的入口函数提纲化，接收一个数据包；子线程 thunk 与之相同，只是包里放的是指针而非值拷贝镜像。
* `simd` 版本已经能为同一个函数体生成多份代码并在前面加上分发（[implementation.md §6.13](../implementation.md#613-simd-functions)）。子线程运行的那个分块循环，恰好就是这类版本所要向量化的循环。

Halide 也以同样的方式降级其 `parallel` 维度：“循环体被降级为一个单独的函数……最终我们生成的代码会将循环的迭代放入任务队列，由运行时线程池消费”（[halide-pldi13]）。

<a id="24-two-body-shapes-the-stack-model-allows"></a>
### 2.4 栈模型允许的两种循环体形态

递归规则——“环中的任何函数在其某个非固定大小局部变量处于作用域内时，都不得调用进入该环”（[spec §7.8](../k_spec.md#78-recursion)）——就是协程规则（[coroutines.md §2.2](coroutines.md#22-the-recursion-rule-is-the-coroutine-rule)），也是让一个循环体在另一条线程的原生栈上运行、而自身不携带任何数据栈的规则：没有非固定大小的局部变量，也就没有需要放进一个该线程并不拥有的栈里的东西。llm_frontier §4.3 将其列为规则 2。代码生成已经按特化版本来判定这一点：不在栈上构造任何东西的函数体“不需要 `k_sp`”，而若在没有它的情况下分配内存，`EmitSpec` 会拒绝编译（[implementation.md §6.4](../implementation.md#64-the-calling-convention-c3)）。这就是**无栈（stackless）**形态。

另一种形态正是线程机制本来就在构建的东西：线程程序会“作为一个拥有自己栈分配（自己的 N′）的独立程序”来编译，且“每个线程都携带一个指向自己那组 N′ 个栈块的指针”（[spec §11.2](../k_spec.md#112-concurrency)）。带有可增长局部变量的循环体可以用同样的方式编译为一个**子程序（child program）**，由池线程预留一次它的栈块并持续保留，它所调用的函数也会像今天为工作线程那样按程序重新特化（“任何静态栈使用都会按程序再次将其特化，就像模板实例化一样”）。它与普通工作线程的唯一区别在于其引用的指向：指向父线程内部，这由 [2.1](#21-a-dormant-parent-freezes-every-lifetime) 保证安全。

<a id="25-program-instances-and-what-a-pool-thread-must-adopt"></a>
### 2.5 程序实例，以及池中线程必须接管的内容

所有非静态的全局变量都存放在每个程序实例唯一的一份 `k_globals_t` 中；在带工作线程的程序里，通过线程局部的 `k_gl` 访问它，而线程程序的栈则通过线程局部的 `k_stks` 访问（[implementation.md §6.3](../implementation.md#63-globals-and-program-instances)、[§7](../implementation.md#7-the-runtime)）。池线程在执行程序实例 X 的子任务时，只需在任务期间将自己的 `k_gl` 设为 X 的全局变量结构体，对于无栈子任务还将 `k_stks` 设为 X 的栈块（子任务绝不会在上面分配，但读取所捕获的可调整大小全局变量的头部要经过 `k_gl`，而所捕获的可调整大小局部变量的头部与栈则会无论如何都作为自由变量传入）。每个任务只需两次线程局部存储写入；关于程序实例的其他一切都无需改动。子程序的 `k_stks` 则是它自己保留的那个栈块。只读静态数据（`static const` 全局变量、字符串字面量）本来就是共享的。

<a id="26-backends-without-threads-run-the-loop"></a>
### 2.6 不支持线程的后端直接运行循环

“带线程的程序需要真正的 C 编译器（`k build`），因为进程内的 TinyCC 后端无法放置线程局部存储”（[reference §22](../reference.md#22-threads)）。无论是 `par` 还是 `simd` 都没有自身独立的语义，因此在 TinyCC 和 wasm 下，带有这两种标记的循环都直接作为普通循环，在调用线程上逐次迭代执行；而在任意构建中，`K_THREADS=1` 也会对所有 `par` 做同样处理；程序的输出完全相同。同时这也是调试模式：如果带标记的循环在 `K_THREADS=1` 或 `k run` 下给出不同的答案，那就说明它打破了编译器本应捕获的规则——这永远是编译器 bug，而不是程序 bug。`k run` 在每个程序上都能继续工作；`k build` 则负责并行化。

<a id="27-what-the-checker-knows-about-effects-today"></a>
### 2.7 检查器如今对副作用掌握多少

循环体对外部世界造成的影响目前在三个地方被追踪，却没有任何一处做过汇总：检查器的 `FnSpec` 摘要按特化记录了收缩、增长、存储和返回根（[analysis_inventory.md §2](analysis_inventory.md#2-what-is-tracked-where)）；代码生成的 `CollectSpecs` 在调用图上求不动点，计算某个特化能触及的自由变量与持有栈的全局变量；优化器则按变量记录了写入与取址。缺少的正是 llm_frontier 的 `pure` 所提议的那种“每类副作用一位”的摘要（[§3.4](llm_frontier.md#34-effects-pure-and-frames)）：写入全局变量、调用队列或线程内建函数、打印输出、调用非 `pure` 的 extern 函数。子线程循环体所需要的正是涵盖自身及所有被调用者的这份摘要，这也是两个特性唯一共享的那一块分析基础设施。

<a id="3-what-others-built"></a>
## 3. 其他系统的做法

<a id="31-thread-level-and-lane-level-side-by-side"></a>
### 3.1 线程级与通道级并列对比

| 系统 | 线程级 | 通道级 | 谁来选择切分方式 | 嵌套 |
|---|---|---|---|---|
| ISPC ([ispc], [ispc-paper]) | `launch[n] f()` 运行 `task` 函数的 `n` 个任务，使用 `taskIndex`/`taskCount`；`sync` 或函数返回将其汇合，“与 Cilk 相同”；任务系统由用户用 C++ 提供（`tasksys.cpp` 中包含 pthreads、TBB、OpenMP、GCD、ConcRT 后端，[ispc-tasksys]） | `foreach (i = a ... b)` 将域映射到 gang 的各个通道；`foreach_tiled`、`foreach_active`、`foreach_unique` 用于一致性与逐通道串行化；嵌套 `foreach` 非法 | 均由程序员决定；指南建议启动“比处理器数量多得多的任务……但又不能多到让调度开销……占主导” | 词法嵌套：`mandelbrot_tasks.ispc` 按 `taskIndex` 计算行范围并在其中运行 `foreach`（[ispc-mandel]）；范围会做填充“以避免伪共享” |
| Mojo ([mojo-matmul]) | `parallelize[calc_row](C.rows, num_workers)` 在运行时线程池上运行（`parallelism_level()`，[mojo-runtime]） | 在行闭包内调用 `vectorize[dot, nelts, size = C.cols]()`，`nelts` 取自 `simdwidthof`（[mojo-vectorize]） | 两种宽度均由程序员决定；`num_workers` 通常为核心数 | 词法嵌套，一个闭包套在另一个闭包内 |
| Halide ([halide], [halide-blur]) | 在拆分后的维度上调用 `.parallel(y)`；运行时的 `halide_do_par_for` 使用延迟启动的线程池，调用线程也会参与执行任务（[halide-pool]） | 在另一个拆分后的维度上调用 `.vectorize(x, 8)` | 程序员或自动调优器，在一条调度链中指定：`blur_y.split(y, y, yi, 32).parallel(y).vectorize(x, 16)` | 同一循环嵌套；“任意维度都可以被调度为串行、并行或向量化”（[halide-pldi13]） |
| OpenMP ([omp-parallel], [omp-for-simd], [omp-pfs]) | `#pragma omp parallel for`，带 `schedule(static[,chunk])`、`dynamic`、`guided`；遇阻线程即为团队中的线程 0 并参与执行迭代 | `#pragma omp simd`，带 `safelen`、`simdlen`、`aligned`、`reduction` | 程序员；组合指令 `parallel for simd` 会“先将逻辑迭代……分发到各个线程”，然后“再把每个结果迭代分块转换为 SIMD 循环” | 单条循环用一条指令，或外层 `parallel for`、内层 `simd` |
| C++17 ([cpp-policy], [cpp-draft]) | `std::execution::par` | `unseq`（C++20）；`par_unseq` 兼顾两者，并禁止“非向量化安全”的操作（如对互斥锁加锁），否则属于未定义行为 | 程序员通过策略标签指定；库选择线程池（libstdc++ 需要 TBB，[gcc9]；MSVC 使用 Windows 线程池，[msvc-exec]） | 无 |
| TBB ([tbb-pfor], [tbb-part]) | `parallel_for(blocked_range, body, partitioner)`，支持 `auto`、`simple`、`static` 与 `affinity` 分区器；工作窃取；调用线程参与执行 | 依赖 C 编译器对循环体的自动向量化器 | 程序员决定，遵循粒度经验法则：“grainsize 次迭代……至少应耗费 100,000 个时钟周期”（[tbb-grain]） | 通过工作窃取支持 `parallel_for` 嵌套 `parallel_for` |
| Cilk ([opencilk-spec], [cilk5]) | `cilk_for`，基于 `cilk_spawn`/`cilk_scope` 的分治，以及 `#pragma cilk grainsize`；一次 spawn 的开销在“一次 C 函数调用开销的 2 到 6 倍之间” | Cilk Plus 的 `#pragma simd` 与数组记法，已在 GCC 8 和 ICC 19.1 中移除（[gcc8], [icc191]） | 程序员 | 词法嵌套 |
| Rust rayon ([rayon-join], [rayon-chunks]) | `par_iter_mut`、`par_chunks_mut`、`join`、`scope`；延迟启动全局线程池，每个逻辑 CPU 一个线程；`join` 在调用线程上执行其中一个闭包，开销约 15 ns（[spice]） | 自动向量化器 | 程序员 | 词法嵌套 |
| Chapel ([chapel-forall], [chapel-foreach]) | `forall`：leader 迭代器跨 `dataParTasksPerLocale` 个任务运行 `coforall`，默认每核一个任务 | `foreach`：“循环迭代与顺序无关。这使编译器能够进行向量化”；`forall` 内每个任务的内层循环即是一个 `foreach`（[chapel-both]） | `forall` 这一个结构同时提供两者；单独的 `foreach` 只提供通道；`coforall` 为每次迭代创建一个任务 | 在 `forall` 内自动完成 |
| Futhark ([futhark-perf], [futhark-msc], [futhark-ispc]) | `map`：多核后端将外层并行操作跨线程分块，任何嵌套并行都会编译出两个版本并在运行时择一执行 | ISPC 后端把每个分块内的顺序循环变成 `foreach` | 两者均由编译器决定 | 自动；由编译器扁平化 |
| Julia ([julia-threads], [julia-simd]) | `Threads.@threads`，支持 `:static`（每线程一个任务）或 `:dynamic` 调度；一次派生开销“通常小于 10 微秒” | `@simd` 放在最内层循环上，承诺迭代“相互独立且可重排序” | 两者均由程序员决定 | 词法嵌套 |

归纳起来共有三种形式：由程序员分别放置两个结构（ISPC、Mojo、Julia、Cilk、rayon），用一条调度说明同时命名两个维度（Halide、OpenMP 的组合编译指令），以及只写一个结构而由编译器或迭代器来决定（Chapel、Futhark）。对于一维计算内核，三者生成的最终代码是一样的：每个线程处理连续分块，分块内部按通道并行。它们的区别在于由谁来显式写出分块。两级叠加带来的实测收益是两者收益的乘积：ISPC 自己的测试集在 4 核 AVX 机器上，仅靠通道能获得 4-8 倍加速，加上任务级后达到 15-33 倍（[ispc-perf]）；Futhark 的 ISPC 后端在可向量化的内核上，能在线程分块之外再带来 1.7-9.5 倍的加速，而在无法向量化的 Barnes-Hut 上则损失 20-40%（[futhark-ispc]、[futhark-0-21-13]）。

<a id="32-the-safety-rule-who-may-write-what"></a>
### 3.2 安全规则：谁可以写什么

所有允许子线程看到父线程栈的系统，都会将父线程阻塞在某个词法结构中（[2.1](#21-a-dormant-parent-freezes-every-lifetime)）。它们随后如何处理写入，则分成了三条路线：

* **能力类型（Capability types）。** Rust 的 `Send`/`Sync`（[rust-sync]）、Swift 的 `Sendable`（[se-0302]）、Pony 的 `iso`/`val`（[pony-caps]）、Verona 的区域（[verona]）：对不可变数据的共享读取是自由的，而写入则要求拥有整个值的独占所有权。这些类型系统都无法表达“迭代 `i` 只写 `out[i]`”。Rust 通过标准库解决这一点——`split_at_mut` 和 `chunks_mut` 的“各个分块都是可变切片，且互不重叠”（[rust-chunks]），它们内部用 `unsafe` 实现一次即可；rayon 的 `for_each` 接受 `Fn` 闭包，因此根本无法通过捕获的 `&mut [T]` 写入，必须使用 `par_chunks_mut` 或 `par_iter_mut`（[rayon-foreach]、[rayon-faq]）。Swift 曾考虑允许子任务修改所捕获变量的互不相交部分，但最终放弃：“Swift 的 `@Sendable` 闭包检查必须保持保守，除非我们赋予它特殊知识”（[se-0304]）；在 Swift 6 中，从子任务对同一数组进行不相交写入会报错，变通办法是使用不安全的缓冲区指针（[swift-forum]）。
* **证明（Proof）。** Deterministic Parallel Java 通过区域与副作用标注来证明：“`i` 的值在每次循环迭代中都不同，因此所有更新都落在互不相交的区域中”（[dpj-tutorial]、[dpj]）。ParaSail 在调用点证明切片互不重叠（“因为编译器可以证明 `Left > Right`，所以两个切片之间没有重叠”），并禁止其中一方可更新的两个参数之间存在别名（[parasail-qs]、[parasail-embedded]）。编译器文献中对一般仿射情况有 GCD 测试、Banerjee 测试和 Omega 测试（[gcd]、[banerjee]、[omega]）；K 的边界检查证明器属于同一家族的分析域，而这里面对的问题还要更简单。
* **断言（Assertion）。** OpenMP、Chapel 的 `forall`（“程序员断言该循环的迭代可以安全地并行执行”，[chapel-forall-guide]）、`#pragma GCC ivdep`、clang 的 `vectorize(assume_safety)`、Intel 的 `ivdep`：全靠程序员承诺，一旦承诺有误便是未定义行为（[gcc-ivdep]、[llvm-parallel]）。

K 在自己的证明器里免费拥有第二种机制，在可写性推导里拥有第一种机制。本提案同时采用前两种，绝不采用第三种。

<a id="33-pools"></a>
### 3.3 线程池

调研过的每一个运行时都会延迟启动线程池，将线程数设为在线核心数减一，并让发起 fork 的线程也承担一份工作：ISPC 的 pthreads 后端启动“比核心数少一个的线程，因为这里的主线程也会抢任务来运行”（[ispc-tasksys]）；libgomp 的团队会将“当前（主）线程加入 `threads[]`”，并由遇阻线程自己调用提纲化后的区域函数（[libgomp-parallel]、[libgomp-team]）；OpenMP 规范直接将遇阻线程定为团队中的线程 0（[omp-parallel]）；Halide 的 `halide_do_par_for` 让调用线程不断运行任务直到自己的那份完成（[halide-pool]）；rayon 的 `join` 在调用线程上执行其中一个闭包（[rayon-join]）；TBB 的 `execute` 会让“调用线程加入 task_arena”（[tbb-arena]）。

在两次 fork 之间，工作线程会先自旋再休眠：libgomp 默认自旋 300,000 次，超额订阅时自旋 1,000 次（[gomp-spin]、[omp-wait]）；LLVM 的 libomp 会自旋 `KMP_BLOCKTIME`（200 ms）（[kmp-blocktime]）；Halide 的条件变量“在阻塞前先自旋”，这“有助于降低并行 for 循环结束时的开销”（[halide-pool]）。不自旋的代价就是唤醒开销：在 Ryzen 5950X 8 线程上重跑 EPCC 微基准测试，两个运行时执行一次 `parallel for` 均只需 0.5-1 µs；而在默认不自旋的 Apple M1 上，libomp 在 4 核与 8 核上分别为 6.7 µs 与 25.8 µs，而强制开启自旋后则降为 0.36 µs 与 1.63 µs（[epcc-rerun]）。[1.1](#11-what-share-nothing-costs-on-a-kernel) 中的实测数据与此完全吻合：唤醒休眠线程的开销为每个子线程 1-2 µs，自旋交接的开销则只有其十分之一。

工作窃取（Chase 和 Lev 的双端队列，[chase-lev]）被 TBB、Cilk、rayon 和 Julia 用来均衡不均匀的迭代以及支持嵌套并行。Halide 则没有使用：“源码并未实现经典的工作窃取”，只用了一个全局互斥锁和一个任务栈（[halide-pool]）；Futhark 的多核后端在顶层静态划分工作，只在嵌套或不规则并行时才进行窃取（[futhark-msc]）。对于静态内核而言，更简单的线程池同样快，而且它还能保持分块到线程的映射固定不变，这正是 [4.6](#46-results-reductions-determinism) 所需要的。

<a id="34-reductions"></a>
### 3.4 归约

并行浮点求和在任何系统里都是顺序相关的：OpenMP 声明“各值合并的顺序未作指定”，并且“不保证不同次运行之间会得到逐比特完全相同的结果”（[omp-reduction]）；rayon 的 `sum` 对浮点数而言“并非完全确定性”（[rayon-sum]）；Julia 的 `reduce` 具有依赖实现的结合律顺序（[julia-reduce]）。常见的补救方式是在固定线程数下采用固定的合并顺序（LLVM 的 `KMP_DETERMINISTIC_REDUCTION`，[kmp-blocktime]），以及 Cilk 的归约器——保持串行程序“从左到右的顺序”，同时不指定“运算的分组方式（结合律）”（[opencilk-spec]、[opencilk-reducers]）。整数归约无论按何种顺序都是精确的；这正是 K 划下的界线（[4.6](#46-results-reductions-determinism)）。

<a id="35-what-to-take"></a>
### 3.5 应采纳的设计

* 父线程阻塞在该结构中；该结构就是循环。应当采用 Rust 基于闭包的 `scope` 以及 Halide、OpenMP 的提纲化循环形态，而不是 Rust 最初的守卫对象或 ISPC 的 `launch`/`sync` 配对——后者 K 日后如有需要可以另外作为语句形式添加（[6](#6-options-side-by-side)）。
* 先实现构造上的不相交（如同 rayon 的 `chunks_mut` 和 ParaSail 的切片拆分那样），再实现证明上的不相交。结构化形式零分析成本且覆盖分块工作；证明形式则覆盖模板（stencil）计算和跨步写入。
* 对单层循环提供组合形式（线程切分分块，分块内按通道并行）——所有系统在一维内核上都收敛于此——对双层循环采用词法嵌套。由程序员在并行所在的循环上标明层级（`simd`、`par` 或两者）；编译器不去猜测程序员未曾声明的循环嵌套。
* 延迟启动线程池、核心数减一、调用者执行分块 0、先自旋后休眠，v1 版本不做工作窃取。
* 只支持整数归约；浮点数通过逐迭代元素数组处理。

<a id="4-the-design"></a>
## 4. 设计方案

<a id="41-one-construct-two-levels"></a>
### 4.1 一种语法结构，两个层级

该结构是一个 `for` 循环，程序员通过在其前面加上一两个词来声明它的迭代相互独立，并指明这些迭代可以在何处运行：

```klang
simd for i in n { ... }             // 通道：当前线程，迭代在 SIMD 通道上运行
par for i in n { ... }              // 线程：池上执行连续分块，每个分块内是普通循环
par simd for i in n { ... }         // 两者：线程上执行分块，每个分块内使用通道
```

每种写法都支持 `for` 现有的全部范围形式：

```klang
par for i in a..b { ... }           // 范围
par for x in arr { ... }            // 元素拷贝：循环体读取 arr
par for &x in arr { ... }           // 元素引用：迭代 i 拥有 arr[i]
par for &x, i in arr { ... }        // 带索引
```

它的语义就是普通 `for` 循环的语义。这两个词都是“许可”而非“指令”：一旦编译器检查确认那样执行不可被观测到，就可以按关键字所说的方式运行各次迭代，也始终可以改为顺序执行。循环上的 `simd` 就是将函数上的 `simd`（[spec §7.12](../k_spec.md#712-simd-fn-versions-per-instruction-set)）缩小到单个循环，遵守同一原则：没有自身独立的语义，任何降级方式的计算结果都与基线一致。循环体必须满足的规则对三种写法完全相同，见 [4.3](#43-what-a-body-may-touch) 至 [4.7](#47-aborts)。就像普通 `for` 循环体一样，该循环体也是外围函数中的一个代码块：它可以访问外围函数的局部变量、参数以及程序的全局变量，可以声明自己的局部变量，可以调用任意允许的函数，并在本次迭代结束时结束。

该结构*不*包含的内容：线程数、分块大小、调度策略、通道宽度或归约子句。线程数由线程池决定（[4.9](#49-the-runtime)），分块是静态且连续的，通道宽度由 C 编译器或外围的 `simd fn` 版本决定，而归约则是由编译器自动识别的一种代码形态（[4.6](#46-results-reductions-determinism)）。OpenMP 的 `schedule(dynamic)` 和 TBB 的分区器用来解决迭代负载不均问题；K 的解决方案见 [4.9](#49-the-runtime)，无需引入语法。

<a id="42-which-level-a-loop-gets"></a>
### 4.2 循环采用哪一层级

完全由关键字决定；不会根据嵌套关系或循环体内容做隐式推断。共有四种写法：

**仅通道：`simd for`。** 循环在调用线程上运行，其迭代可以展开到 SIMD 通道上。在 v1 中，这意味着生成该循环时附带 C 编译器的“迭代相互独立”断言（`#pragma clang loop vectorize(assume_safety)`、`#pragma GCC ivdep`），并在它所写入的数组上加上 `restrict`，使向量化器不必再去证明检查器已经证明的事实；之后它将采用 llm_frontier §4.4 中的 SPMD 降级方式，按通道宽度编译循环体并为其中的 `if` 生成掩码。该形式不启动线程、不发生 fork，进入循环的开销与普通 `for` 相同。它适用于处理像素的内层循环、任何已经运行在子线程内部的循环，以及程序员希望无需线程即可向量化的循环：

```klang
fn scale(xs: f32[:], s: f32) {
    simd for &x in xs { x = x * s; }        // 单线程；通道并行；迭代 i 拥有 xs[i]
}
```

**仅线程：`par for`。** 迭代范围被切成连续分块，每个池线程加上调用线程各分一块，每个线程将自己的分块作为普通循环运行，C 编译器可能会也可能不会像对待普通循环那样对其向量化；由于没有写 `simd`，不会给出独立性断言。这是处理行外层循环或处理 `chunk` 索引的写法，也是唯一允许在循环体中持有可增长局部变量的形式（[4.5](#45-stackless-and-stackful-bodies)）：

```klang
fn count_distinct(rows: const i64[:], bits: i64, distinct: i64[:]) {
    par for k in P {                        // 多线程；每个线程处理一个行分块
        var table: Slot[>..] = [];          // 可增长变量：子程序形态，无通道形式
        ...                                 // 完整循环体见第 5 节
        distinct[k] = n;
    }
}
```

**单循环兼具两者：`par simd for`。** 在线程上分块，同时每个线程的分块循环本身是一个 `simd for`。这正是 OpenMP 的 `parallel for simd`（[omp-pfs]），也是 [3.1](#31-thread-level-and-lane-level-side-by-side) 中所有系统针对一维计算内核生成的形式；它是处理大数组扁平循环的标准写法：

```klang
fn saxpy(ys: f32[:], xs: const f32[:], a: f32) {
    assert(xs.len == ys.len);
    par simd for &y, i in ys { y = a * xs[i] + y; }
}
```

**嵌套兼具两者：外层 `par for`，内层 `simd for`。** 外层按线程分块行，内层按通道处理像素；[5](#5-the-query-engines-phases-written-out) 中的模糊滤镜就是这样写的。这对应 ISPC 的 `launch` 内嵌 `foreach`、Mojo 的 `parallelize` 内嵌 `vectorize`、Halide 的 `.parallel(y).vectorize(x)`。外层循环的分块是分给每个线程的行带，内层 `simd for` 则在各个子任务内部、在该子任务的线程上运行。

嵌套会做什么，又不会做什么：

* 当一个线程已经处于子任务内部时，如果遇到 `par for`——无论是直接写在外层循环体里，还是通过函数调用到达——它都**不会**再次 fork：它会直接顺序运行，就像 OpenMP 默认对嵌套区域所做的那样。因此，包含 `par for` 的函数可以从任何地方调用；若从子任务内部调用，它的代价就只是一个普通循环。运行时通过一个线程局部标志来判断这一点，父线程自身在执行分块 0 时也会设置该标志（[4.9](#49-the-runtime)）。
* 若 `simd for` 的循环体能到达一个 `par for`，则该 `simd for` 不会被向量化：fork 是一次通道降级无法内联的调用，因此循环按原样执行，fork 在每次迭代内部发生。
* 写在 `simd for` 循环体内部的 `simd for` 在每个通道内就是普通循环。ISPC 将嵌套 `foreach` 视为错误（[ispc]）；K 拒绝它没有任何好处。
* 用普通 `for` 包住一个 `par for` 会在每次外层迭代时都 fork 一次。对于程序员未曾声明的循环，编译器不会擅自把 fork 外提；如果 fork 成了开销来源，应当改为标记外层循环。

读者光看循环的第一个词就能明白它的降级方式，编译器绝不会擅自提升未标明的循环。llm_frontier §4.3 曾把这种形态笼统称作 `par for`，而在本设计笔记中它对应 `par simd for`，将线程与通道分开命名，使两者可以按需单独请求；而 llm_frontier §4.4 的方案 2（SPMD 降级）则成为 `simd for` 的第二种降级方式。除此之外该提案中的内容都不变，唯有线程池采用本笔记指定的实现，准入规则采用下文所述规则。

<a id="43-what-a-body-may-touch"></a>
### 4.3 循环体可以访问什么

在三种写法中任意一种的循环体内部，设 `i` 为循环变量：

* **R1. 捕获的位置均为只读。** 外围函数的每一个局部变量以及每一个全局变量在循环体内部都是 `const`，就像按值绑定的 `for` 变量一样：不得赋值、不得 `push`、不得 `resize`、不得收缩，也不得通过从中读出的引用或切片进行写入。相应的报错信息直接复用可写性规则的诊断。
* **R2. 本次迭代所拥有的位置才可写。** 获得位置所有权的途径有三种：
  * `for &x` 形式绑定的元素（`x` 即 `arr[i]`，且没有其他迭代绑定它）；
  * 以 `i` 为索引、通过分块内建函数从被捕获数组或切片中取出的分块（[4.4](#44-disjoint-writes-structural-first-proven-second)）；
  * 由包含 `i` 的表达式索引的位置，且证明器能够证明没有其他迭代会读取或写入它（[4.4](#44-disjoint-writes-structural-first-proven-second)）；`out[i]` 是最平凡的情况。
* **R3. 除写入外不得有其他副作用。** 循环体及其所调用的一切函数都不得调用 `qput`、`qget`、`qpoll`、`thread_spawn`、`thread_wait`、`print`、`write_stderr`、`exit`，也不得调用非 `pure` 的 `extern fn`。一次迭代对外部世界唯一的影响就是写入 R2 所允许的位置；否则调度顺序就会变得可被观测。
* **R4. 不得跨越循环边界跳转退出。** 不得 `return`，不得 `return … from` 跳出循环体之外的函数，不得 `break` 或 `continue` 到外围循环。针对所标记循环自身的 `continue` 会结束本次迭代；循环体内部的 `defer` 在本次迭代结束时执行。
* **R5. 循环体自身的局部变量归其自身所有。** 循环体内声明的局部变量按每次迭代独立存在，位于线程原生栈上，或者对于非固定大小的变量，位于子程序的栈上（[4.5](#45-stackless-and-stackful-bodies)）。

R1 与 R2 本质上是同一条规则：一次写入若要被接受，其目标位置必须归本次迭代所有。R3 就是 [2.7](#27-what-the-checker-knows-about-effects-today) 中的副作用摘要。R4 正是 `return from` 已经按特化追踪的机制（`FnSpec::needs`），只需新增一种绝不允许的目标即可。阅读 `par for` 代码的人光凭关键字就知道循环体不会触碰任何其他迭代能看到的东西，这正满足了该议题第 1 至第 3 点要求编译器进行检查的初衷。

初次上手最常触发的错误是违反 R1，因此诊断信息应当明确提示改法：“`total += x` in a `par for` writes a variable of the enclosing function; make `total` a reduction (an integer accumulated by one operation, [4.6](#46-results-reductions-determinism)) or write a per-iteration element `partials[i]`”。

<a id="44-disjoint-writes-structural-first-proven-second"></a>
### 4.4 不相交写入：结构化优先，证明其次

“没有其他迭代会触及此位置”有两个来源。

**结构化（Structural）。** 在任意数组族值或切片上提供三个内建函数，其不相交性由检查器直接作为关于该内建函数的事实来认知，而不是靠临时证明：

| 内建函数 | 含义 | 不相交性 |
|---|---|---|
| `a.chunk(k, n) -> T[:]` | 将 `a` 分为 `n` 个连续片段中的第 `k` 个，各片段长度相差至多为 1，共同覆盖 `a` | 不同的 `k` 给出互不相交的片段 |
| `a.chunk_at(bounds, k) -> T[:]` | `a[bounds[k]..bounds[k + 1]]`，其中 `bounds: const i64[:]` 非递减且位于 `a` 的范围内 | 不同的 `k` 给出互不相交的片段；边界数组在 fork 时一次性检查完毕（单调且在范围内），就像索引类型在构造时检查一次那样 |
| `for &x in a` | 元素 `a[i]` | 不同的 `i` 给出不同的元素 |

在 `par for k in n` 内部，`out.chunk(k, n)` 是归本次迭代所有的可写 `out` 切片，而 `src.chunk(k, n)` 则是具有相同形状的 `src` 只读视图（R1）。任意数量的数组都可以用同样的方式分块——扫描阶段正是这样读取一列、写入另一列，探测阶段也正是这样读取键、读取共享表并写入匹配结果。等分形式相当于 rayon 的 `par_chunks_mut`（[rayon-chunks]）；带边界的形式则是分区哈希表或 CSR 行结构所需要的，它在 fork 处的那一次检查是以运行时数组作为划分依据的代价。

在 `par for` 之外，同样的内建函数就是普通的切片操作。它们本身也很有用（`for k in n { let part = a.chunk(k, n); ... }`），完全可以率先加入标准库，走在并发语法结构之前。

**证明（Proven）。** 即 llm_frontier §4.3 规则 3 和 [analysis_inventory.md §5.6](analysis_inventory.md#6-loops-two-iteration-check-instead-of-name-scans-small-effort-independent) 中的双迭代检查：对于 `i1 < i2`，在边界检查证明器的域（`Lin` 基址与偏移）中判定，迭代 `i1` 的每一个写入位置都与迭代 `i2` 的每一个读取或写入位置互不相交。它能放行 `out[i]`、`b in 0..4` 时的 `out[i * 4 + b]`、`x in 1..W - 1` 时的 `dst[y * W + x]`，以及在循环体内切出的行切片 `dst[y * W..(y + 1) * W]`，因为 `y1 + 1 <= y2` 可推出 `(y1 + 1) * W <= y2 * W`。通过两个同类型被捕获引用访问的数组需要 `distinct` 根（[llm_frontier §4.1](llm_frontier.md#41-distinct-roots-as-restrict)），否则检查失败；写入两个不同数组则无需任何标注。上述结构化内建函数所处理的场景恰恰是该证明器最难处理的（除以 `n`、运行时 `bounds` 数组），这正是将它们设为既定事实而非待证定理的原因。

Rust 的经验表明绝大多数并行代码使用的是结构化形式，而 Swift 的经验表明只提供能力类型的语言会把不相交数组写入逼向不安全代码（[3.2](#32-the-safety-rule-who-may-write-what)）。DPJ 证明了证明形式既健全又富有表达力（[dpj-tutorial]）。K 可以两者兼得，并且按这个顺序先后实现。

<a id="45-stackless-and-stackful-bodies"></a>
### 4.5 无栈循环体与有栈循环体

**无栈（Stackless）。** 如果一个循环体及其所调用的所有函数中只包含固定大小的局部变量，它就不需要数据栈：这正是递归规则所刻画的形态（[2.4](#24-two-body-shapes-the-stack-model-allows)）。它在池线程的原生栈上运行，不预留任何栈区域，并且可以是 `simd for`。该议题列表里的所有计算内核（扫描、筛选进分块并记录计数、分区进表分块、探测、合并各工作线程状态）、`samples/22_image_filters.k` 中的模糊与 Sobel 滤镜、Mandelbrot 行计算以及粒子更新，全都是这一形态。

**有栈（Stackful）。** 带有非固定大小局部变量（如临时 `u8[>..]`、局部哈希表、`str()` 结果）的循环体会被编译为一个**子程序（child program）**：拥有自己独立的栈分配（共 N″ 个栈），就像 `thread_fn` 的 N′ 那样（[spec §11.2](../k_spec.md#112-concurrency)），但存在以下差异：

* 它的引用与切片指向父线程，不复制全局变量也不复制参数：它像无栈循环体一样通过指针接收父线程环境。
* 每个池线程在第一次执行子程序时，会预留一块包含 N″ 个栈的区域——其中 N″ 取整个程序中所有子程序所需的最大值——并在程序的整个生命周期内保留它。在两次任务之间，该栈块会回卷到基址，从而让已提交的内存页保持有效（[spec §1.2](../k_spec.md#12-memory-model)），因此第二趟执行不会触发缺页异常；从父线程调用 `trim_memory()` 时也会请求线程池一并裁剪其栈块。这就是该议题的第 4 点要求。
* 父线程自己运行的那一个分块不需要单独的栈块：从父线程的视角看它就是一次函数调用，其非固定大小的局部变量放在父线程自身高水位线之上的栈空间里，与普通被调用函数的做法一致。
* 它只能在线程上运行：只能写 `par for`，绝不能写 `simd for` 或 `par simd for`。非固定大小局部变量没有通道形态。
* `hardware_threads()` 原本就会保证“报告的工作线程数不超过实现的数据栈地址空间在主程序之外所能容纳的上限”（[spec §11.2](../k_spec.md#112-concurrency)）；用于子程序的线程池规模也按同样的方式受限，只是以 N″ 代替 N′。纯无栈的线程池没有这一上限。默认地址空间预算为 32 TB，每个栈块为 N″ × 2 GB，因此这一上限在数千个以上，对于核心数规模的线程池永远不会构成约束。

程序员只需要记住一条规则：“带有可增长局部变量的循环体只能用 `par`，不能用 `simd`”。编译器看到的则是它早已拥有的两种函数体特化形态（主程序中的函数、线程程序中的函数），加上一种新的形态（子程序中的函数，拥有自己的栈编号，但使用父程序的全局变量）。

<a id="46-results-reductions-determinism"></a>
### 4.6 结果、归约与确定性

如何将结果从一次迭代中传出，按推荐优先级排列如下：

1. **单个元素。** `found[k] = n`、`partials[k] = acc`、`states[k] = state`：将每次迭代产出的固定大小结果写入父线程数组，通过 R2 中最平凡的情形获得所有权。这是传回各工作线程计数、各工作线程聚合状态以及各分块浮点部分和的方式，父线程随后按自己写明的顺序将它们合并，因此浮点求和之所以是确定性的，是因为合并顺序由程序自己选定。
2. **部分填充的分块。** 筛选操作将输入分块中保留下来的元素写入同样形状的输出分块，并汇报实际个数（示例见 [5](#5-the-query-engines-phases-written-out)）；父线程随后做紧缩（compact），或者下一阶段直接通过 `chunk_at` 把这些计数当作边界。内存占用与输入同规模，输出就地写入，全程无复制。这是 GPU 和 ISPC 中做紧缩的标准惯用法。
3. **整数归约。** `total += f(i)`、`count++`、`bits |= m`、`best = max(best, v)`：外围函数的局部整数变量，在循环体中仅通过某一种可结合运算进行累积，且不在循环体中以其他方式被读取。编译器为每个分块维护一个累加器，并在 join 时按分块顺序合并。整数运算在任意结合顺序下都是精确的，因此结果与顺序执行完全相同。浮点数**不**允许作为归约目标：它们走第（1）条路径，由程序指定合并顺序。这就是 `simd` 的精确性规则（[spec §7.12](../k_spec.md#712-simd-fn-versions-per-instruction-set)）在线程上的应用，它比 [3.4](#34-reductions) 中的所有系统都更严格，因为 K 承诺的是调度过程完全不可观测，而不仅仅是答案“接近”。
4. **子程序的可增长结果。** 如果一个循环体构造了 `T[>..]` 且希望保留它，它目前无处安放：父线程的数组不能增长（R1），而子线程的栈在迭代结束时会被回卷。v1 的解法是采用第（2）条：按输入估算输出大小，写入分块并汇报计数。未来可以增加 `par for k in n into out { ... }` 形式，在 join 时按迭代顺序将每次迭代的结果镜像追加到 `out` 中——这在父线程上只有每个结果一次的复制，相当于去掉队列、也去掉输入复制的 `qget` 复制；该想法列在 [8](#8-open-questions) 中，暂不作为本次提案内容。

确定性直接源于上述规则：迭代除了写入仅归自己所有的位置之外没有任何副作用（R1 至 R3），归约是精确的，且分块数量从不改变结果。因此同一个程序无论是用 `K_THREADS=1`、在 TinyCC 下，还是在另一台核心数不同的机器上运行，输出都将逐字节完全一致。

<a id="47-aborts"></a>
### 4.7 中止（Abort）

llm_frontier §4.3 曾要求循环体必须是 `abortfree` 的，以免某个迭代在依赖调度的时刻终止整个程序。对于线程层级，本笔记放宽了这一要求，因为该议题希望运行的代码里包含证明器未必总能消除的边界检查，而放宽这一限制只付出了一项代价：

* 子任务中的中止（失败的边界检查、`assert`、`abort(msg)`、除以零）会像在主线程上一样终止程序。顺序循环本来是否会中止，取决于程序本身而非调度方式：在任何调度下所有迭代都会运行，且排在中止迭代之前的迭代除了写入之外没有其他可观测副作用，而死掉的进程不会展示这些写入。唯一*确实*依赖调度的，是当两个或更多迭代都会中止时**报告哪一个**，规范对此明确说明：报错消息是其中之一，不指定具体哪一个。
* 如果程序实例本身是一个受控工作线程（contained worker，[reference §22](../reference.md#22-threads)），中止只能结束该工作线程而不能杀掉整个进程。池线程会记录下这次中止并提前结束当前分块；父线程则在 join 时将其作为自己的中止抛出，之后现有的受控中止路径原封不动地执行。其他子线程会跑完各自有界的分块。
* 池线程原生栈上的栈溢出、`exit(code)` 以及运行时故障会直接结束进程，与工作线程中的行为相同。

对于 `simd for`，更严格的规则仍在必要之处保留：SPMD 降级要求循环体没有保留下来的检查，否则只能降级为标量循环。而在 v1 中，通道降级只是 C 编译器向量化器加上断言，因此保留下来的检查仅仅意味着循环不会向量化，与现状一致。

<a id="48-in-the-spec"></a>
### 4.8 语言规范中的表述

在 §11.2 旁边新增一节，规定如下：

* 标有 `simd`、`par` 或 `par simd` 的循环与 `for` 计算相同的结果；实现可以按任意交错顺序、在同一程序实例的任意数量线程上（`par`）、在 SIMD 通道上（`simd`）或顺序地执行其迭代；程序的输出、中止与退出状态均不依赖于具体采用了哪种方式。
* 准入条件：R1 至 R5、分块内建函数及其既定事实、针对证明情况所接受的核心索引形态（如同 §7.14 列出 `checkfree` 所接受的形态那样）、归约形态，以及中止规则。
* 嵌套：在子任务内部运行的 `par for` 不会 fork，而是顺序执行。子任务内部的 `simd for` 是在该子任务的线程上运行的通道循环。
* 在 `thread_fn` 工作线程内部规则相同；线程池是进程全局共享的（[4.9](#49-the-runtime)）。
* 重申无数据竞争承诺：程序不存在数据竞争，原因在于线程之间不共享数据（§11.2），且被标记循环的各次迭代仅写入经编译器证明只归其自身所有的位置。两者皆经检查，均不靠口头信任。

关于队列、工作线程和全局变量的一切保持不变。

<a id="49-the-runtime"></a>
### 4.9 运行时

在 `runtime_threads.h` 旁边编写几百行 C 代码：

* **线程池。** 包含 `hardware_threads() - 1` 个原生线程，在进程第一次 fork 时延迟创建，永不销毁；两次 fork 之间在有界自旋后于单个字上休眠（Windows 上使用 `WaitOnAddress`，[waitonaddress]；Linux 上使用 futex，[futex]；macOS 上使用 `os_sync_wait_on_address` 或条件变量）。原生栈大小与主线程一致：Windows 上新创建线程默认继承可执行文件的栈预留大小（`k build` 将其设为 64 MB）；POSIX 需要调用 `pthread_attr_setstacksize`，因为 glibc 默认 pthread 栈为 8 MB 而 macOS 只有 512 KB，而子循环体可能调用任意函数。
* **一次 fork。** 父线程写入任务描述（提纲化函数、环境指针、范围、分块数），递增代次（generation）字，唤醒所有池线程，自己运行分块 0，然后等待完成计数器归零。子线程接管父线程的 `k_gl` 与栈块（[2.5](#25-program-instances-and-what-a-pool-thread-must-adopt)），运行自己的分块，递增完成计数器，并在自己是最后一个完成者时唤醒父线程。fork 是一次 release 操作，join 是一次 acquire 操作；程序其余部分不需要任何原子操作。完整协议见 `bench/fork_cost/pool_cost.c`，[1.1](#11-what-share-nothing-costs-on-a-kernel) 中的数据正是由它测得。
* **分块。** 静态且连续：总计 `P` 个分块中的第 `c` 块就是 `range.chunk(c, P)`。对于迭代耗时不均的场景（如 Mandelbrot 行），提供第二种模式，通过原子计数器认领固定大小的块；此时块到线程的映射会变化，但结果并不依赖于此（[4.6](#46-results-reductions-determinism)），因此该模式纯粹是运行时选项（`K_PAR_BLOCK=n`），绝不进入语法。v1 不做工作窃取。
* **繁忙的线程池。** 一次 fork 只认领当时空闲的池线程，其余分块由发起者自己的线程执行。池线程绝不会再次 fork，父线程在执行分块 0 时也不会（[4.2](#42-which-level-a-loop-gets)），因此没有任何线程会在任务内部等待线程池，也就不存在死锁问题需要推理。多个 `thread_fn` 工作线程如果同时发起 fork，它们共享同一个线程池，最坏情况下只是核心被超额订阅，如同 OpenMP 嵌套并行区域的表现。
* **子程序的栈块。** 每个池线程一个，在首次使用时预留，包含 N″ 个区域，每次任务后回卷，接到请求时裁剪（[4.5](#45-stackless-and-stackful-bodies)）。
* **伪共享。** 两个分块在同一个缓存行边界相遇时，只会带来边界处的几次缓存行弹跳，别无他害；ISPC 会填充范围“以避免伪共享”（[ispc-mandel]）。字节数组的 `chunk` 可以将边界向 64 字节对齐；这属于调优手段，而非语言规则。
* **调节旋钮。** `K_THREADS=n`（设为 1 时所有 `par for` 顺序执行）、`K_SPIN=n`（休眠前的自旋次数），两者都在首次 fork 时读取一次。不提供编程 API：想要限制线程数的程序本质上是在请求部署配置，这与 `OMP_NUM_THREADS` 的定位一致。
* **不提供的特性。** 每子线程私有状态、程序可见的线程 ID、循环体内部的屏障、原子操作，以及 llm_frontier §4.5 中的 GPU 风格工作组特性。这些都没有对应的顺序语义。

<a id="410-what-the-compiler-needs"></a>
### 4.10 编译器需要做的工作

| 位置 | 内容 | 已有基础 | 新增工作 |
|---|---|---|---|
| 解析器 | `for` 的各种形式中支持 `simd for`、`par for`、`par simd for` | `for`、`simd` | 一个关键字，一个新语法位置 |
| 检查器 | R1：循环体内部捕获位置设为 `const` | 可写性推导、按值 `for` 绑定 | 在循环边界对每个捕获变量重新施加只读视图，约 100 行 |
| 检查器 | R2 结构化：`chunk`、`chunk_at`、`for &x`；它们归迭代所有的事实 | 切片、`for &x` | 三个带“按索引不相交”标记的内建函数，以及写入时的所有权检查，约 200 行 |
| 检查器 | R2 证明：双迭代不相交性 | 证明器域、`HandedApart`、`DistinctTypes` | 判定逻辑与诊断信息，约 1,000 行（llm_frontier 的估算） |
| 检查器 | R3：跨越被调用者的副作用摘要 | 可达全局变量（代码生成）、写入记录（优化器） | 每个特化一份摘要，与 `pure` 共用，约 300 行 |
| 检查器 | R4、R5、归约、中止、嵌套 | 用于 `return from` 的 `needs`、递归环规则 | 约 200 行 |
| 代码生成 | 将循环体提纲化并附带环境结构体；分块循环；fork 调用 | 嵌套函数自由变量、线程 thunk、`simd` 版本 | 约 400 行 |
| 代码生成 | 子程序：第三种程序类型 | 线程程序的按程序特化与栈编号 | 约 300 行 |
| 代码生成 | `simd for`：由 `distinct` 根推导 `restrict`，以及循环上的独立性断言 | §4.1 的 `restrict`、`simd` 版本 | 约 50 行；SPMD 延后，按 llm_frontier §4.4 实现 |
| 运行时 | 线程池、fork、栈块、旋钮 | `runtime_threads.h` 的线程与区域 | 约 300 行 C 代码，覆盖三个平台 |
| 规范、参考、测试 | 新增章节、诊断信息、每条规则的 `test/errors_tc` 用例、`--par-test` 报告 | | |

最先交付的第一组内容——结构化 R2、R1、R3 至 R5、提纲化以及线程池——大约共 1,500 行编译器与运行时代码，且不需要任何新的静态分析。它已经足以支撑 [5](#5-the-query-engines-phases-written-out) 中的每一个阶段。

<a id="5-the-query-engines-phases-written-out"></a>
## 5. 查询引擎各阶段的完整代码

该议题中的工作负载包括：就地扫描并筛选各列，将结果写入每个工作线程各自的输出；对构建行进行分区并构建哈希表的 P 个互不相交分区；由所有工作线程以共享只读切片的形式并发探测该表，并将匹配结果写入互不相交的输出片段；合并每个工作线程的聚合状态。下面以提议的语法写出每个阶段（去掉 `par` 和 `simd`、并将 `chunk` 作为函数实现的等价顺序版本，在今天的编译器上即可编译运行）。

**扫描与筛选（Scan and filter）**，无栈、结构化。每次迭代读取其对应的列分块，将满足条件的元素写入输出中的同一分块，并汇报数量：

```klang
let P = 8;

fn filter(vals: const i64[:], keep: i64[:], counts: i64[:], threshold: i64) {
    assert(keep.len == vals.len && counts.len == P);
    par for k in P {
        let src = vals.chunk(k, P);             // 只读视图：R1
        let dst = keep.chunk(k, P);             // 本次迭代持有的 keep 片段：R2
        var n = 0;
        for v in src { if v > threshold { dst[n] = v; n++; } }
        counts[k] = n;                          // 每次迭代一个元素：R2
    }
}
```

除非证明器将 `n` 与 `src.len` 关联起来（`n` 至多等于 `v` 的索引），否则 `dst[n]` 会保留其边界检查——这是保留在子线程内部的检查：[4.7](#47-aborts) 允许这样做，而常规的 `checkfree` 优化会消除它。

**探测（Probe）**，无栈、结构化，带有一个共享表。该表由所有迭代同时读取，没有任何迭代写入它：

```klang
struct Slot { key: i64, val: i64 }

fn probe(keys: const i64[:], table: const Slot[:], out: i64[:], found: i64[:]) {
    assert(out.len == keys.len && found.len == P && table.len > 0);
    par for k in P {
        let ks = keys.chunk(k, P);
        let os = out.chunk(k, P);
        var n = 0;
        for key in ks {
            var h = hash(key) as i64 % table.len;
            while table[h].key != 0 && table[h].key != key { h = (h + 1) % table.len; }
            if table[h].key == key { os[n] = table[h].val; n++; }
        }
        found[k] = n;
    }
}
```

**分区构建（Partitioned build）**，无栈、结构化且使用运行时边界。同一张表的各个分区是由偏移量数组指定的互不相交片段；每次迭代填充属于自己的那一段：

```klang
fn build(rows: const i64[:], table: Slot[:], bounds: const i64[:]) {
    par for k in bounds.len - 1 {
        let mine = table.chunk_at(bounds, k);   // bounds 在 fork 时一次性检查单调性与范围
        let cap = mine.len;
        for r in rows {
            if partition_of(r, bounds.len - 1) != k { continue; }
            var h = (hash(r) as i64) % cap;
            while mine[h].key != 0 { h = (h + 1) % cap; }
            mine[h] = Slot { key: r, val: r };
        }
    }
}
```

**有栈构建（A stackful build）**，即子程序：每次迭代拥有一个局部表，适用于各个分区的大小无法预先得知的 group-by 场景。由于循环体声明了一个可增长变量，它在各自拥有一个栈块的线程上运行，并为每次迭代汇报一个固定大小的摘要：

```klang
fn count_distinct(rows: const i64[:], bits: i64, distinct: i64[:]) {
    par for k in P {
        let mine = rows.chunk(k, P);
        var table: Slot[>..] = [];              // 非固定大小的局部变量：这是一个子程序
        table.resize(1 << bits, Slot { key: 0, val: 0 });
        for r in mine {
            var h = (hash(r) as i64) & ((1 << bits) - 1);
            while table[h].key != 0 && table[h].key != r { h = (h + 1) & ((1 << bits) - 1); }
            table[h] = Slot { key: r, val: r };
        }
        var n = 0;
        for s in table { if s.key != 0 { n++; } }
        distinct[k] = n;
    }
}
```

**合并（Merge）**，包含一次归约和按迭代输出的部分和。计数是整数归约；浮点求和则通过部分和数组返回，从而使求和顺序与原程序保持一致：

```klang
fn totals(a: const f64[:], b: const f64[:], partial: f64[:]) -> i64 {
    assert(a.len == b.len && partial.len == P);
    var nonzero = 0;                            // 整数归约：在任何顺序下结果都精确
    par for k in P {
        let xs = a.chunk(k, P);
        let ys = b.chunk(k, P);
        var acc = 0.0;
        for i in xs.len { acc += xs[i] * ys[i]; if xs[i] != 0.0 { nonzero++; } }
        partial[k] = acc;                       // 浮点部分和按分块返回
    }
    return nonzero;                             // sum(partial) 由调用方按自身顺序完成
}
```

**两个层级（Two levels）**，即 `samples/22_image_filters.k` 中的模糊滤镜：行分配到线程，像素分配到通道。`dst` 的行切片通过基于证明的规则获得所有权（不同的 `y` 对应的行互不相交），内层的 `simd for` 是通道循环，此外没有其他写入：

```klang
fn blur(src: const u8[:], dst: u8[:]) {
    assert(src.len == W * H && dst.len == W * H);
    par for y in 1..H - 1 {                     // 线程：处理行的分块
        let a = src[(y - 1) * W..y * W];
        let b = src[y * W..(y + 1) * W];
        let c = src[(y + 1) * W..(y + 2) * W];
        let d = dst[y * W..(y + 1) * W];        // 已证明：dst 的各行按 y 互不相交
        assert(a.len == W && b.len == W && c.len == W && d.len == W);
        simd for x in 1..W - 1 {                // 通道，运行在子任务的线程上
            let s = a[x - 1] as u16 + a[x] + a[x + 1]
                  + b[x - 1] + b[x] + b[x + 1]
                  + c[x - 1] + c[x] + c[x + 1];
            d[x] = (s / 9) as! u8;
        }
    }
}
```

如果没有基于证明的规则，同一个计算内核可以用结构化方式写成：通过 `dst.chunk(k, P)` 划分行带，内部嵌套一个按行的顺序循环——这正是该议题原型所使用的写法；其中的内层 `simd for` 保持不变。

<a id="6-options-side-by-side"></a>
## 6. 备选方案对比

| 备选方案 | 具体内容 | 收益 | 代价 | 结论 |
|---|---|---|---|---|
| A. 结构化所有权的 `par for`、无栈循环体、静态线程池 | 不含证明器的 [4.1](#41-one-construct-two-levels) 至 [4.4](#44-disjoint-writes-structural-first-proven-second)、[4.5](#45-stackless-and-stackful-bodies) 的前半部分、[4.9](#49-the-runtime) | 覆盖该议题的所有阶段、零拷贝、10 µs 级 fork；在分区负载上跑满核心数 | 约 1,500 行代码；三个内建函数；一个关键字 | **v1** |
| B. 子程序（有栈循环体） | [4.5](#45-stackless-and-stackful-bodies) 的后半部分 | 每次迭代可使用可增长临时变量；满足该议题第 4 点 | 代码生成中新增第三种程序形态；每个池线程一个栈块；地址空间预算上限 | **v2**，当方案 A“按输入估算输出大小”的惯用法不再够用时引入 |
| C. 基于证明的不相交性 | 双迭代判定 | 无需重构代码即可支持模板（stencil）计算和跨步写入；满足审阅者提出的“因已证明而并行” | 约 1,000 行证明器与诊断代码；来自 llm_frontier §4.1 的 `distinct` | **v3**，在依照 llm_frontier §7 实现 `requires`/`abortfree` 之后 |
| D. `simd for`，以及两者结合的 `par simd for` | 当前先做独立性断言，日后做 SPMD | 立即弥补 MSVC 在模糊滤镜上的差距；日后获得 ISPC 级别的加速 | 断言：约 50 行；SPMD：依照 llm_frontier §4.4 维护掩码 | 断言进 v1，SPMD 待实测后决定 |
| E. 向池化的 `thread_fn` 工作线程传入借用的 `const` 视图，并在同一作用域由 `thread_wait` 汇合（该议题的字面形式） | 沿用现有的工作线程，只要每个线程 ID 都在作用域结束前被等待，就允许引用参数 | 无需新语法结构 | ID 是会流经数组和循环的运行时值；检查器无法看见汇合点；整个作用域必须变成一个语言结构，到那一步其实就是语法更差的方案 A | 否决；Rust 的 RFC 3151 作出了相同选择，“依赖闭包而非守卫对象”（[rfc-3151]） |
| F. 语句形式 `par { f(a); g(b); }`，每条语句作为一个子任务（ISPC 的 `launch`/`sync`、Cilk 的 `spawn`/`sync`、ParaSail 的 `\|\|`） | 异构子任务 | 拥有不同阶段的流水线 | 与方案 A 相同的一套规则（每条语句视为一个满足 R1-R5 的循环体）；第二套语法 | 延后，待确有程序需要时作为方案 A 规则之上的语法糖 |
| G. 工作窃取任务 | TBB、Cilk、rayon | 自动均衡嵌套并行与不规则并行 | 每个线程一个双端队列、非确定性的分块到线程映射、对静态计算内核毫无收益 | 不进 v1；[4.9](#49-the-runtime) 中的原子计数器分块模式已能覆盖不均匀行负载 |
| H. 视图的零拷贝 `qput`（一种“借用”消息类型） | 携带 `const T[:]` 及未回收视图运行时计数的队列消息 | 保留无共享模型的外观 | 在没有运行时生命周期的语言里引入运行时生命周期（引用计数）；计数归零前发送方不得收缩数据，这无法用现有任何规则表达 | 否决 |

<a id="7-recommendation-and-order"></a>
## 7. 建议与实施顺序

1. **分块内建函数**（`chunk`、`chunk_at`）：放入标准库并编写测试，作为普通切片操作。今天即可派上用场；它们是后续所有功能的词汇基础。
2. **副作用摘要**：即 [2.7](#27-what-the-checker-knows-about-effects-today) 的内容，与 llm_frontier 的 `pure` 共用：对调用图做一次遍历，记录全局写入、队列与线程内建函数、输出以及非 `pure` 的 extern 函数。可通过 `klang claims` 或某个命令行标志单独检验。
3. **`par for`，方案 A**：关键字、配合结构化 R2 的 R1 至 R5 规则、提纲化、线程池、`K_THREADS`、面向 TinyCC 与 wasm 的顺序回退、语言规范章节。在 `test/errors_tc` 中为每条规则准备测试夹具，并对每个 `par for` 程序在 `K_THREADS=1` 与核心数下进行差分运行，两者必须打印完全相同的字节。测量该议题的两项工作负载（在 `bench/` 中重建）、`samples/22_image_filters.k`、改写为不使用队列的 `samples/23_mandelbrot_threads.k`，以及 `blur`、`particles` 和 `scene` 等基准项。
4. **`simd for`**，带独立性断言（方案 D 的前半部分）及来自不同根的 `restrict`，在 MSVC 的 `blur` 基准项上实测；`par simd for` 则只是对同一个循环同时应用这两种降级。
5. **子程序**（方案 B），当有程序需要每次迭代独立的可增长变量时引入：第三种程序类型、保留的栈块、容量上限。
6. **基于证明的不相交性**（方案 C），在 `requires` 和 `abortfree` 就绪之后引入，使其诊断信息能够指明缺失的前提条件；先在基准测试语料库上跑一次 `--par-test` 报告以观察哪些循环能够通过，正如 llm_frontier §7 第 6 步所提议的那样。
7. **SPMD 通道与 GPU**，按 llm_frontier §4.4 与 §4.5 的规划推进，作为检查器届时所接纳的循环体形态的后端。

走到第 3 步时，该议题中的工作负载即可跨核心以内存带宽速率运行；第 1 步和第 2 步体量小且彼此独立，随时可以落地。

<a id="8-open-questions"></a>
## 8. 开放问题

* **未显式请求时也在 `par for` 上添加断言。** 若一个 `par for` 循环体本就可以作为 `simd for` 通过检查，则即使没有写 `simd`，也可以为其分块循环加上独立性断言；所作的声明是一致的。目前暂不开启，以便让循环的降级方式直接由其关键字决定（[4.2](#42-which-level-a-loop-gets)）；等第一次实测遇到自动向量化器在 `par for` 分块循环上放弃向量化时再作决定。
* **循环体中的 `print`。** R3 排除了它，因为各行的顺序将取决于调度（各行之间不会交错，因此单行内容本身不会乱）。如果在文档中注明“顺序未指定”并放开限制，会方便调试，但代价是失去“没有自身独立语义”的承诺；替代方案是设置 `K_THREADS=1`，并在循环体之外用 `print` 输出结果。
* **子程序的可增长结果**：[4.6](#46-results-reductions-determinism) 中的 `into out` 形式，在 join 时对每个结果做一次拷贝，还是干脆不提供。
* **按程序实例建池**，而非按进程建池，以应对多个 `thread_fn` 工作线程同时 fork 的情形。按进程建池会超额订阅；按程序实例建池则会使线程数成倍增加。OpenMP 默认禁用嵌套并行；K 的工作线程并非嵌套而是兄弟程序，这是更棘手的情况，应当在一个同时也会 fork 的每核一线程服务器上实测后再决定。
* **TinyCC 下的线程池。** 运行时对象其实可以持有线程池以及 JIT 无法放置的线程局部状态，从而让 `k run` 也能并行。顺序执行既正确又更简单；JIT 本就不是为了追求极限速度。
* **浮点归约**：在固定分块数上使用固定归约树（如 llm_frontier §8 所问），在同一台机器上具有确定性，但结果会不同于顺序求和，也会在不同核心数的机器之间有所不同。本文的回答是不做；使用部分和数组只需多写一行代码。
* **`chunk` 边界与缓存行**：对于字节数组是否对齐到 64 字节，还是留给程序自行决定。
* **语句形式**（方案 F），以及在真正有程序需要它之前是否值得引入第二套语法。

<a id="references"></a>
## 参考文献

上文提及的语言规范与实现章节：
[§1.2 内存模型](../k_spec.md#12-memory-model)、
[§1.3 栈不变量](../k_spec.md#13-the-stack-invariant)、
[§6.5 `for`](../k_spec.md#65-for)、
[§7.6 静态函数值](../k_spec.md#76-static-function-values)、
[§7.8 递归](../k_spec.md#78-recursion)、
[§7.12 `simd fn`](../k_spec.md#712-simd-fn-versions-per-instruction-set)、
[§9.1 存活期规则](../k_spec.md#91-the-outlives-rule)、
[§9.2 根](../k_spec.md#92-roots-and-the-depth-check)、
[§9.5 可写性](../k_spec.md#95-writability)、
[§10.3 栈分配](../k_spec.md#103-stack-assignment-deterministic-recipe)、
[§10.4 运行时环境](../k_spec.md#104-runtime-environment)、
[§11.2 并发](../k_spec.md#112-concurrency)；
[implementation.md §6.3](../implementation.md#63-globals-and-program-instances)、
[§6.4](../implementation.md#64-the-calling-convention-c3)、
[§6.8](../implementation.md#68-threads-and-queues)、
[§6.13](../implementation.md#613-simd-functions)、
[§7](../implementation.md#7-the-runtime)；
[reference §22 线程](../reference.md#22-threads)；
[llm_frontier.md §3.4](llm_frontier.md#34-effects-pure-and-frames)、
[§4.1](llm_frontier.md#41-distinct-roots-as-restrict)、
[§4.3](llm_frontier.md#43-par-for-shared-memory-iteration-proven-race-free)、
[§4.4](llm_frontier.md#44-explicit-simd)；
[coroutines.md §2.2](coroutines.md#22-the-recursion-rule-is-the-coroutine-rule)；
[analysis_inventory.md §2](analysis_inventory.md#2-what-is-tracked-where)、
[§5.6](analysis_inventory.md#6-loops-two-iteration-check-instead-of-name-scans-small-effort-independent)。
实测文件：`bench/fork_cost/spawn_cost.k`、
`bench/fork_cost/pool_cost.c`。

[ispc]: https://ispc.github.io/ispc.html "ISPC 用户指南：任务模型、foreach、uniform 与 varying"
[ispc-mandel]: https://raw.githubusercontent.com/ispc/ispc/main/examples/cpu/mandelbrot_tasks/mandelbrot_tasks.ispc "ISPC：使用 launch 与 foreach 实现的 Mandelbrot"
[ispc-tasksys]: https://raw.githubusercontent.com/ispc/ispc/main/examples/common/tasksys.cpp "ISPC：示例任务系统"
[ispc-paper]: https://www.pharr.org/matt/assets/ispc.pdf "Pharr and Mark: ispc，一种面向高性能 CPU 编程的 SPMD 编译器（InPar 2012）"
[ispc-perf]: https://ispc.github.io/perf.html "ISPC 性能：单核与四核"
[mojo-matmul]: https://github.com/modular/modular/blob/83284b5b3cfc163bcccd44f1fbca15bcb52d7ec8/examples/mojo/matmul.mojo "Mojo：matmul 示例，按行并行并在内部使用 vectorize（2025 年 5 月移除前的最后一版）"
[mojo-vectorize]: https://mojolang.org/docs/std/algorithm/backend/vectorize/vectorize/ "Mojo: vectorize"
[mojo-runtime]: https://mojolang.org/docs/std/runtime/parallelism_level/ "Mojo: parallelism_level，运行时的工作线程数"
[halide]: https://halide-lang.org/ "Halide：算法与调度"
[halide-blur]: https://raw.githubusercontent.com/halide/Halide/main/apps/blur/halide_blur_generator.cpp "Halide：blur 生成器的 CPU 调度"
[halide-pldi13]: https://people.csail.mit.edu/jrk/halide-pldi13.pdf "Ragan-Kelley 等人：Halide（PLDI 2013），将并行循环降级为闭包与任务队列"
[halide-pool]: https://raw.githubusercontent.com/halide/Halide/main/src/runtime/thread_pool_common.h "Halide 运行时：线程池"
[omp-parallel]: https://www.openmp.org/spec-html/5.1/openmpse14.html "OpenMP 5.1 §2.6：parallel 结构；遇阻线程即为线程 0"
[omp-for-simd]: https://www.openmp.org/spec-html/5.1/openmpsu49.html "OpenMP 5.1 §2.11.5: simd 与 for simd"
[omp-pfs]: https://www.openmp.org/spec-html/5.1/openmpsu77.html "OpenMP 5.1 §2.16.5: parallel for simd"
[omp-reduction]: https://www.openmp.org/spec-html/5.0/openmpsu107.html "OpenMP 5.0 §2.19.5：reduction 子句，顺序未作指定"
[omp-wait]: https://gcc.gnu.org/onlinedocs/libgomp/OMP_005fWAIT_005fPOLICY.html "libgomp: OMP_WAIT_POLICY"
[gomp-spin]: https://gcc.gnu.org/onlinedocs/libgomp/GOMP_005fSPINCOUNT.html "libgomp: GOMP_SPINCOUNT"
[libgomp-parallel]: https://gcc.gnu.org/onlinedocs/libgomp/Implementing-PARALLEL-construct.html "libgomp：PARALLEL 结构的实现方式"
[libgomp-team]: https://raw.githubusercontent.com/gcc-mirror/gcc/master/libgomp/team.c "libgomp team.c：线程复用与团队中的主线程"
[kmp-blocktime]: https://openmp.llvm.org/design/Runtimes.html "LLVM OpenMP 运行时：KMP_BLOCKTIME、KMP_DETERMINISTIC_REDUCTION"
[epcc-rerun]: https://github.com/vchuravy/ThreadTeams.jl/issues/46 "EPCC syncbench 重测：Ryzen 5950X 与 Apple M1 上的 parallel for 开销"
[cpp-policy]: https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t "C++ 执行策略：seq、par、unseq、par_unseq"
[cpp-draft]: https://eel.is/c++draft/algorithms.parallel "C++ 标准：非向量化安全函数"
[gcc9]: https://gcc.gnu.org/gcc-9/changes.html "GCC 9：并行算法需要 TBB"
[msvc-exec]: https://learn.microsoft.com/en-us/cpp/standard-library/execution?view=msvc-170 "MSVC：Windows 线程池上的并行算法"
[tbb-pfor]: https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/algorithms/functions/parallel_for_func "oneTBB: parallel_for"
[tbb-part]: https://uxlfoundation.github.io/oneTBB/main/tbb_userguide/Partitioner_Summary.html "oneTBB：分区器"
[tbb-grain]: https://www.intel.com/content/www/us/en/docs/onetbb/developer-guide-api-reference/2023-1/controlling-chunking.html "oneTBB：控制分块，100,000 时钟周期规则"
[tbb-arena]: https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_arena/task_arena_cls "oneTBB: task_arena"
[opencilk-spec]: https://cilk.mit.edu/docs/OpenCilkLanguageExtensionSpecification.htm "OpenCilk 语言扩展规范：cilk_for、grainsize、归约器"
[opencilk-reducers]: https://www.opencilk.org/doc/reference/reducers/ "OpenCilk：归约器"
[cilk5]: https://www.fftw.org/~athena/abstracts/abstract17.html "Frigo, Leiserson, Randall：Cilk-5 多线程语言的实现（PLDI 1998）"
[gcc8]: https://gcc.gnu.org/gcc-8/changes.html "GCC 8：移除 Cilk Plus"
[icc191]: https://www.intel.com/content/www/us/en/developer/articles/release-notes/c-compiler-legacy/19-1-macos-for-psxe2020.html "Intel C++ 19.1：移除 Cilk Plus"
[rust-scope]: https://doc.rust-lang.org/std/thread/fn.scope.html "Rust: std::thread::scope"
[rust-sync]: https://doc.rust-lang.org/std/marker/trait.Sync.html "Rust: Sync"
[rust-chunks]: https://doc.rust-lang.org/std/primitive.slice.html#method.chunks_mut "Rust: chunks_mut"
[rust-24292]: https://github.com/rust-lang/rust/issues/24292 "Rust issue 24292：JoinGuard 是不健全的（leakpocalypse 事件）"
[rfc-1066]: https://rust-lang.github.io/rfcs/1066-safe-mem-forget.html "Rust RFC 1066：安全的 mem::forget"
[rfc-3151]: https://rust-lang.github.io/rfcs/3151-scoped-threads.html "Rust RFC 3151：作用域线程"
[rayon-join]: https://docs.rs/rayon/latest/rayon/fn.join.html "rayon: join"
[rayon-chunks]: https://docs.rs/rayon/latest/rayon/slice/trait.ParallelSliceMut.html#method.par_chunks_mut "rayon: par_chunks_mut"
[rayon-foreach]: https://docs.rs/rayon/latest/rayon/iter/trait.ParallelIterator.html#method.for_each "rayon: for_each 接受 Fn + Sync + Send"
[rayon-faq]: https://github.com/rayon-rs/rayon/blob/main/FAQ.md "rayon FAQ：两个闭包不能同时可变借用同一个切片"
[rayon-sum]: https://docs.rs/rayon/latest/rayon/iter/trait.ParallelIterator.html#method.sum "rayon: sum 对浮点数不具备确定性"
[spice]: https://github.com/judofyr/spice "Spice：实测 rayon::join 开销"
[se-0302]: https://github.com/apple/swift-evolution/blob/main/proposals/0302-concurrent-value-and-concurrent-closures.md "Swift SE-0302: Sendable"
[se-0304]: https://github.com/apple/swift-evolution/blob/main/proposals/0304-structured-concurrency.md "Swift SE-0304：结构化并发；拒绝了对捕获变量的不相交写入"
[swift-forum]: https://forums.swift.org/t/concurrent-work-on-array/79276 "Swift 论坛：Swift 6 下对数组的并发操作"
[jep-453]: https://openjdk.org/jeps/453 "JEP 453：结构化并发"
[kotlin-scope]: https://kotlinlang.org/docs/coroutines-basics.html "Kotlin：父协程会等待其子协程"
[njs]: https://vorpus.org/blog/notes-on-structured-concurrency-or-go-statement-considered-harmful/ "Smith：结构化并发笔记（2018）"
[chapel-forall]: https://chapel-lang.org/docs/language/spec/data-parallelism.html "Chapel: forall 与 dataParTasksPerLocale"
[chapel-forall-guide]: https://chapel-lang.org/docs/users-guide/datapar/forall.html "Chapel 用户指南：forall 是程序员的断言"
[chapel-foreach]: https://chapel-lang.org/docs/technotes/foreach.html "Chapel: foreach 循环"
[chapel-both]: https://chapel.discourse.group/t/vectorized-and-parallell-loops-can-i-have-both/13082 "Chapel 论坛：forall 内每个任务的循环是一个 foreach"
[futhark-perf]: https://futhark.readthedocs.io/en/latest/performance.html "Futhark：各后端下的嵌套并行"
[futhark-msc]: https://futhark-lang.org/student-projects/duc-msc-thesis.pdf "Tran：Futhark 的多核后端（2020）"
[futhark-ispc]: https://www.futhark-lang.org/student-projects/ispc-bsc-thesis.pdf "Malling 等人：Futhark 的 ISPC 后端（2022）"
[futhark-0-21-13]: https://futhark-lang.org/blog/2022-07-07-futhark-0.21.13-released.html "Futhark 0.21.13：ISPC 后端性能数据"
[julia-threads]: https://docs.julialang.org/en/v1/base/multi-threading/ "Julia: Threads.@threads、:static 与 :dynamic"
[julia-simd]: https://docs.julialang.org/en/v1/manual/performance-tips/ "Julia: @simd"
[julia-reduce]: https://docs.julialang.org/en/v1/base/collections/#Base.reduce-Tuple{Any,%20Any} "Julia: reduce 的结合律顺序取决于实现"
[pony-caps]: https://tutorial.ponylang.io/reference-capabilities/reference-capabilities.html "Pony：引用能力"
[verona]: https://arxiv.org/abs/2309.02983 "Arvidsson 等人：面向灵活内存管理的引用能力（Verona，OOPSLA 2023）"
[dpj]: https://dl.acm.org/doi/10.1145/1640089.1640097 "Bocchino 等人：面向确定性并行 Java 的类型与副作用系统（OOPSLA 2009）"
[dpj-tutorial]: https://dpj.cs.illinois.edu/DPJ/Download_files/DPJTutorial.html "DPJ 教程：不相交数组更新"
[parasail-qs]: https://parasail-programming-language.blogspot.com/2010/12/parallel-quicksort-in-parasail-and.html "Taft：ParaSail 中的并行快速排序，证明切片互不相交"
[parasail-embedded]: https://www.embedded.com/?p=4375616 "Taft：ParaSail，在多核编程中少即是多"
[gcd]: https://en.wikipedia.org/wiki/GCD_test "GCD 依赖测试"
[banerjee]: https://en.wikipedia.org/wiki/Banerjee_test "Banerjee 依赖测试"
[omega]: https://dl.acm.org/doi/10.1145/125826.125848 "Pugh：Omega 测试（Supercomputing 1991）"
[gcc-ivdep]: https://gcc.gnu.org/onlinedocs/gcc/Loop-Specific-Pragmas.html "GCC: #pragma GCC ivdep"
[llvm-parallel]: https://llvm.org/docs/LangRef.html#llvm-loop-parallel-accesses "LLVM: llvm.loop.parallel_accesses"
[chase-lev]: https://www.dre.vanderbilt.edu/~schmidt/PDF/work-stealing-dequeue.pdf "Chase and Lev：动态环形工作窃取双端队列（SPAA 2005）"
[futex]: https://man7.org/linux/man-pages/man2/futex.2.html "futex(2)"
[waitonaddress]: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress "Windows: WaitOnAddress"
