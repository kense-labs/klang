<a id="prior-art-the-nearest-memory-models-and-what-is-new"></a>
# 现有技术：最接近的内存模型及其创新之处

<details>
<summary>目录</summary>

- [1. 比较的维度](#1-what-is-being-compared)
- [2. 最接近的匹配项（按匹配度排序）](#2-the-closest-matches-ranked)
- [3. 用于其他目的的多栈设计](#3-multiple-stacks-put-to-other-uses)
- [4. 存活期规则的近亲](#4-relatives-of-the-lifetime-rule)
- [5. 未发现先例的特性](#5-what-no-precedent-was-found-for)
- [6. 调研是如何进行的](#6-how-the-survey-was-done)

</details>

本文是一份完成于 2026 年 10 月的调研笔记，梳理了在内存管理方面与 K 最接近的编程语言、库与学术论文。在 K 中，所有动态数据都存储在一组由编译器计算得出数量的栈上，且每个栈的顶部各有一个可增长值。调研未发现将 K 的各项要素组合在一起的语言或论文，但其中的每一项要素都有现有技术可循。本文按候选方案与 K 模型的共享程度进行排名，说明各自与 K 的差异，并列出尚未发现先例的部分。十分制的评分属于主观判断，而非精确测量。

<a id="1-what-is-being-compared"></a>
## 1. 比较的维度

在候选方案可能具备或缺失的维度上，K 的模型包含以下 7 点：

1. **栈，而非堆。** 程序的动态数据驻留在 N 个数据栈上。每个栈都是一段较大的地址空间预留，在触及时按需提交物理内存，配有一个碰撞指针（bump pointer）和一个保护间隙（guard gap）。值按生命周期顺序入栈，并在作用域退出时成批出栈（[规范 §1.2](../k_spec.md#12-memory-model)、[§10.4](../k_spec.md#104-runtime-environment)）。
2. **每个栈顶部仅有一个可增长值。** 每个栈上最多同时存活一个可调整大小的值，且它总是位于栈顶，因此增长只需移动指针、无需搬移任何数据，内部引用在增长期间也始终保持有效（[§1.3](../k_spec.md#13-the-stack-invariant)）。
3. **N 是编译期常量。** 全程序编译器为每个可增长局部变量分配一个栈索引。同时存活的值获得不同的栈，依次存活的值可以共享同一个栈。这与寄存器分配类似，只是用栈代替了寄存器（[§10.3](../k_spec.md#103-stack-assignment-deterministic-recipe)）。
4. **静态、无注解的安全性。** 每个引用都有一个编译期根（root）：即限定其目标生命周期的变量。引用不能比它的根活得更久，存储操作通过作用域嵌套进行检查，函数按根进行特化而不是接收生命周期参数（[§9.1](../k_spec.md#91-the-outlives-rule)、[§9.2](../k_spec.md#92-roots-and-the-depth-check)、[§10.2](../k_spec.md#102-monomorphization)）。数组仅在编译器能够确定没有任何引用指向其内部的位置才会收缩（[§5](../k_spec.md#5-the-resizableshrink-rules)）。
5. **结果直接在目标位置构造。** 无论调用深度有多深，可变大小的结果都会直接构造在接收它的值所在的栈上，且被调用方知道目标栈的位置（[§4.3](../k_spec.md#43-the-in-place-construction-guarantee)、[§7.3](../k_spec.md#73-return-values-and-result-placement)）。
6. **嵌套数据内联存储。** 字符串数组是一个单一连续块，结构体也可以以一个可增长尾部结尾（[§3.3](../k_spec.md#33-the-array-family)、[§3.4](../k_spec.md#34-placement-rules-what-may-contain-what)）。
7. **递归保持 N 为常量。** 跨越指向递归环的调用点时，作用域内不允许有可增长局部变量存活，因此递归只消耗原生栈（[§7.8](../k_spec.md#78-recursion)）。

我们对每个候选方案都提出同样的三个问题：它的区域（region）是顶部值可原地增长且内部引用稳定的连续栈，还是由独立分配的对象组成的 arena？区域的数量是编译期常量，还是每次函数激活一个？安全性是否是静态的，如果是，需要多少注解？

<a id="2-the-closest-matches-ranked"></a>
## 2. 最接近的匹配项（按匹配度排序）

| 排名 | 候选方案 | 共同点 | 缺失点 | 评分 |
|---|---|---|---|---|
| 1 | ML Kit 区域推断（region inference） | 1、4、5，以及收缩规则的鼻祖 | 2、3、6、7 | 6 |
| 2 | GNU obstacks | 2，以及作为约定的 5 | 3、4、6、7；增长的对象可能会移动 | 6 |
| 3 | ParaSail | 精神上的 1、5、无指针安全性 | 2、3、6、7；区域实质上是局部堆 | 6 |
| 4 | Ada 与不使用分配器的 GNAT | 双栈的 1、4 的深度检查、5 | 2、3、6、7；没有任何东西可增长 | 5 |
| 5 | C 中的 arena 惯用法 | 1、2，以及作为约定的 5 | 手动管理的 3，以及 4、6、7 | 5 |
| 6 | Xhebraj 等人，ECOOP 2022 | 单栈上的 5、静态模式 | 2、3、6、7 | 5 |
| 7 | Cyclone LIFO arena | 部分的 1、5 | 2、3、6、7；需要注解 | 5 |
| 8 | OxCaml 栈分配 | 第二数据栈、静态逃逸检查 | 2、3、超过一层的 5、6、7 | 5 |
| 9 | Ruggieri 与 Murtagh，1988 | 4 中按外层作用域放置的思路 | 其余全部 | 4 |
| 10 | 自动池分配（Automatic pool allocation） | 3 中的静态区域集合 | 2、4、5、6、7 | 4 |
| 11 | Fels | 4 中按区域进行的深度检查 | 2、3、5、6、7 | 4 |
| 12 | RTSJ 作用域内存及其静态类型系统 | 1 中的 LIFO 区域、4 中的存储规则 | 2、3、5、6、7 | 3 |

<a id="21-tofte-and-talpins-region-inference-and-the-ml-kit"></a>
### 2.1 Tofte 与 Talpin 的区域推断，以及 ML Kit

这是静态方面最接近的匹配项。在区域演算中，存储“由一个区域栈组成”；`letregion` 是创建或释放区域的唯一方式，“因此区域以类似栈的方式分配与释放”（[Tofte, Birkedal, Elsman, Hallenberg, "A retrospective on region-based memory management", HOSC 2004](https://elsman.com/mlkit/pdf/retro.pdf)；[Tofte and Talpin, POPL 1994](https://dl.acm.org/doi/10.1145/174675.177855)）。区域推断是全自动的，不需要注解。区域多态函数将用于存放结果的区域作为参数接收，因此被调用方会将数据分配到调用方选择的存储中。这对应第 5 点，只是目标位置通过运行时句柄传入。区域表示推断（[Birkedal, Tofte, Vejlstrup, POPL 1996](https://www.cs.tufts.edu/~nr/cs257/archive/mads-tofte/knormal-final.pdf)）将“有限（finite）”区域（即只保存单个值的区域）放在激活记录中。[ML Kit 手册](https://elsman.com/mlkit/pdf/mlkit-4.3.0.pdf)中的存储模式分析会在编译器证明该区域在某次分配时不再包含任何存活数据的地方原地重置该区域（`atbot`），而 `resetRegions` 则是程序员可以编写的该重置操作的受检形式。这是 K 的收缩规则的鼻祖，只是它作用于整个区域。

差异在于：“无限（infinite）”区域“由固定大小区域页组成的链表构成，这些页从区域页的空闲链表中分配”，其中存放许多装箱的固定大小值，因此既不连续也无法原地增长。存活区域的数量是动态的，每次求值 `letregion` 都会产生一个区域，因此与递归深度成正比。区域参数在运行时传递，而不是通过特化消除。上述回顾论文认为无限区域会“带来碎片问题”，存储模式分析“复杂且对程序修改十分脆弱”，而 ML Kit 后来也加入了垃圾回收器，用于回收区域推断所泄漏的内存。

<a id="22-gnu-obstacks"></a>
### 2.2 GNU obstacks

Obstacks 是唯一把 K 的每栈不变式明确写进规则的早期设计。一个 obstack（[glibc 手册](https://sourceware.org/glibc/manual/latest/html_node/Obstacks.html)）是一个对象栈：“最后分配的对象必须总是第一个被释放”，而对某个对象调用 `obstack_free` 会释放它“以及自该对象之后在 obstack-ptr 中分配的所有内容”。每次只允许通过 `obstack_grow` 增量增长一个对象；当该对象处于增长状态时，obstack 不能分配其他任何对象。`obstack_finish` 会关闭当前对象，之后才允许增长下一个对象。C 代码将 obstack 传给被调用方，让被调用方在其中构造结果，这正是作为约定出现的第 5 点。§2.6 介绍的 ECOOP 2022 论文正是将其影子栈降级为这一 API。

差异在于：obstacks 是一个运行时库，有多少个 obstack、哪个对象放哪里都由程序员自行决定，没有任何检查。存储是分块的，因此当正在增长的对象超出当前块容量时会被复制（“其地址将会改变”）。因此，在对象完成之前，指向增长中对象内部的指针是不稳定的——而稳定的内部指针正是 K 引入地址空间预留所旨在提供的特性。

<a id="23-parasail"></a>
### 2.3 ParaSail

Tucker Taft 设计的这门无指针语言没有全局堆，也没有垃圾回收器。“给定作用域中声明的所有对象都关联到一个存储区域，本质上是一个局部堆。随着对象增长，它的所有新存储都从该区域分配；随着对象收缩，旧存储可以立即释放回该区域。当退出作用域时，整个区域被回收。”结果会被直接放入需要它的位置：“当调用函数时，用于分配结果对象的区域会作为隐式参数传入。该目标区域由函数结果的使用方式决定”，因此“函数返回时在调用点不会发生复制”（[Taft, "ParaSail: A Pointer-Free Pervasively-Parallel Language for Irregular Computations", Programming 2019, §3.3](https://arxiv.org/abs/1902.00525)）。这就是在运行时决定的 K 式目标放置。其安全性源于语言中不存在指针：对象都是值语义，在区域之间赋值会触发复制或移动。

差异在于：这里的区域是一个在内部复用空间的局部堆，而不是碰撞栈（bump stack）。可扩展对象通过链接新片段来增长，“幕后仍然使用指针”，因此增长并不连续，也不具备类似第 2 点的性质。区域在运行时创建，每次进入作用域创建一个，数量是动态的。每个对象在运行时还携带着所属区域的身份信息。

<a id="24-ada-and-gnat-without-allocators"></a>
### 2.4 Ada 与不使用分配器的 GNAT

在 `pragma Restrictions (No_Allocators)` 约束下的 Ada 程序，每个任务拥有两个栈。GNAT 的二级栈（[`s-secsta.ads`](https://gnu.googlesource.com/gcc.git/+/refs/heads/master/gcc/ada/libgnat/s-secsta.ads)）是“用于支持返回调用方未知大小结果（例如 `String`）之函数的运行时基础设施的一部分”。被调用方将结果压入二级栈，调用方原地使用或复制该结果，编译器插入的 `SS_Mark` 和 `SS_Release` 成对调用会在作用域边界成批释放空间。原地构造（build-in-place，[RM 7.6](https://ada-lang.io/docs/arm/AA-7/AA-7.6)）将有限（limited）结果直接构造到调用方的对象中；被调用方通过隐藏的额外形参接收目标地址。可访问性层级（accessibility levels，[RM 3.10.2](https://ada-lang.io/docs/arm/AA-3/AA-3.10)）是一套无注解的静态作用域深度规则，“绝大多数情况下在编译期由合法性规则强制执行”，当层级作为隐式参数传入时则辅以运行时检查。它正是 K 深度检查的直接前身。

差异在于：Ada 中没有任何数组可以在创建后改变边界，因此不存在增长，第 2 点无从谈起。`Bounded_Vectors`（[RM A.18.19](https://www.adaic.org/resources/add_content/standards/12rm/html/RM-A-18-19.html)）具有固定容量，类似于 K 中的 `T[..k]`。栈的数量固定为两个，而非由编译器计算得出。非有限的未约束结果先在二级栈上构造，随后再复制。可变长字符串数组则需要访问类型（access types），而访问类型依赖堆。

<a id="25-the-arena-idioms-of-modern-c"></a>
### 2.5 现代 C 的 arena 惯用法

这些惯用法建立在与 K 相同的底层机制之上。[Ryan Fleury 的《Untangling Lifetimes: The Arena Allocator》](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator)先预留“一个极大上限的连续虚拟地址空间，然后按需提交物理页”，无需重定位任何数据，并像栈一样标记和释放位置。它同样让调用方选择被调用方用来构造结果的 arena，即第 5 点。他所讨论的临时 arena（scratch-arena）冲突出现在函数的结果 arena 同时也是其临时 arena 时，导致“结果内存实际上在返回给调用方之前就被释放了”。这一冲突正是 K“两个同时存活的可增长值绝不共享同一个栈”规则的动态形式，而他得出“两个临时 arena 就足够了”的结论，实质上相当于手工算出了 N。[Chris Wellons 的《Arena allocator tips and tricks》](https://nullprogram.com/blog/2023/09/27/)按值传递临时 arena，从而使被调用方的分配在返回时自动释放。他的[动态数组](https://nullprogram.com/blog/2023/10/05/)恰好在数组末尾紧邻 arena 的下一次分配位置时原地扩展，这正是第 2 点的“可增长值位于栈顶”。[Our Machinery 的《Virtual Memory Tricks》](https://ruby0x1.github.io/machinery_blog_archive/post/virtual-memory-tricks/index.html)在预留地址空间中增长数组，从而做到“数组永不移动”。Varnish 的 [workspace](https://github.com/varnishcache/varnish-cache/blob/master/bin/varnishd/cache/cache_ws.c) 则是生产代码中的实例：每次仅将一次预留的全部剩余空间交给调用方以便向其中增长，释放操作保留已使用的前缀，而快照按 LIFO 顺序重置。

差异在于：这一切全靠约定。需要多少个 arena、哪个值放在哪个 arena 中、以及引用是否会比所属 arena 活得更久，全都由程序员负责；此外，字符串数组依然是一个指针数组。

<a id="26-what-if-we-dont-pop-the-stack"></a>
### 2.6 《What If We Don't Pop the Stack?》

[Xhebraj, Bračevac, Wei 与 Rompf, ECOOP 2022](https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2022.15) 是唯一一篇明确旨在解决与 K 相同问题的论文。由于每次返回都会弹出栈帧，“很难将栈分配的数据从被调用方向上返回给调用方——尤其是可变大小的数据”，而已知的解决办法包括装箱、在父栈帧中预留空间，或者“由可调整大小区域构成的栈（整体复杂度上升且失去性能可预测性）”。他们的方案是延迟弹栈。返回栈值的被调用方会保留其栈帧，直到某个祖先函数返回堆值为止；一套基于存储模式的类型系统防止堆对象指向栈对象。诸如过滤后的列表等结果在栈上增量构造。他们在 Scala Native 中的实现是一个“类似于 Obstacks”的影子栈。论文在第 15:16 页写道：“将各个存储层级映射到独立的物理运行时栈会是一个有趣的扩展方向”，而这正是 K 所采用的设计。

差异在于：该论文只使用一个栈，通过延迟弹栈而非像 K 那样使用目标栈来实现返回，没有将原地增长立为规则，并且需要轻量级注解，而 K 完全不需要注解。目标传递风格（destination-passing style）的相关研究是与该论文相近的另一条研究脉络。[Shaikhha, Fitzgibbon, Peyton Jones 与 Vytiniotis, FHPC 2017](https://www.microsoft.com/en-us/research/publication/using-destination-passing-style-compile-functional-language-efficient-low-level-code) 让调用方为每个数组提供输出缓冲区，通过形状推断预先确定大小，并指出这“实现了高度高效的类栈内存分配纪律”。[Minamide, POPL 1998](https://dl.acm.org/doi/10.1145/268946.268967) 与 [Bagrel 和 Spiwack, OOPSLA 2025](https://arxiv.org/abs/2503.07489) 则为空洞（hole）与目标位置赋予了类型。K 不需要事先知道大小，因为结果直接在栈顶增长。

<a id="27-cyclones-lifo-arenas"></a>
### 2.7 Cyclone 的 LIFO arena

Cyclone（[Grossman 等人，PLDI 2002](https://www.cs.tufts.edu/comp/150BUGS/cyclone-2002.pdf)）拥有块级栈区域、允许无限分配并在块退出时释放的词法作用域动态区域，以及一个堆。对区域生命周期施加的 LIFO 纪律构成了其区域子类型系统所用的“outlives（存活期更长）”关系，而被调用方则在调用方传入的区域句柄中分配内存。他们的实践经验报告（[Hicks, Morrisett, Grossman, Jim, ISMM 2004](https://homes.cs.washington.edu/~djg/papers/cyclone_ismm04.pdf)）发现，LIFO arena“在调用方知道对象生命周期、但只有被调用方能确定对象大小时非常有用”——这正是 K 的核心论点。但报告同时也发现它们“不适合”服务器和事件循环，这也正是 Cyclone 后来补充引入唯一指针、引用计数和带键动态区域（keyed dynamic regions）的原因。

差异在于：动态区域是一个页链表，每一页的大小是上一页的两倍，不存在原地增长。指针类型携带区域注解，并且注解仅在函数内部进行推断。区域句柄是运行时值，区域数量是动态的（“当抛出异常时，需要释放的区域数量在静态时是未知的”）。

<a id="28-oxcamls-stack-allocation"></a>
### 2.8 OxCaml 的栈分配

Jane Street 的 OCaml（[参考文档](https://oxcaml.org/documentation/stack-allocation/reference/)）将局部值分配在“一个遵循与 OCaml 次堆（minor heap）相同布局的独立分配栈上”。该栈与控制栈分离，每个域（domain）有一个。一个区域对应一个函数体或循环体：进入区域时记录栈指针，离开区域时重置栈指针。参考文档指出“类型检查器保证局部值不会逃逸出其所在区域”，模式推断在单个编译单元内完成。`exclave_` 会提前结束当前区域，从而使处于尾位置的结果直接在调用方的区域中构造。

差异在于：这样的栈只有一个，带垃圾回收的堆依然是默认机制。没有任何值可以增长。参考文档指出“可变字段和数组元素要求其内容是全局的（global）”，而 K 的根追踪避免了这一限制。使用 `exclave_` 在调用方区域构造结果最多只能向上延伸一层。

<a id="29-ruggieri-and-murtagh-1988"></a>
### 2.9 Ruggieri 与 Murtagh，1988

[《Lifetime analysis of dynamically allocated objects》, POPL 1988](https://dl.acm.org/doi/10.1145/73560.73585) 是最早按生命周期静态放置堆对象的研究。每个对象被放进附加在某个过程激活记录上的局部堆中，该过程的生命周期恰好包住该对象的生命周期。通过分析过程会返回哪些分配对象，由过程间分析找到这个目标过程。这种放置方式与 K 一样基于根，但局部堆是按每次激活分配的，不支持增长，也没有在放置之外给出安全性保证。

<a id="210-automatic-pool-allocation"></a>
### 2.10 自动池分配

[Lattner 与 Adve, PLDI 2005](https://llvm.org/pubs/2005-05-21-PLDI-PoolAlloc.html) 将 C 程序的堆对象划分到一组静态确定的池中。每个池在其对应非逃逸数据结构所在函数的入口处创建、在出口处销毁。这是编译器计算出一组具有作用域绑定生命周期的固定分配区域的最接近先例。其分配规则不同：它依据指针分析所得的数据结构身份来划分，而非活跃度（liveness）。这些池里存放的也是独立分配的对象，且该变换本身不提供额外安全性。

<a id="211-fels"></a>
### 2.11 Fels

Fels 是一门 2025 年的业余研究语言（[代码仓库](https://github.com/fels-lang/fels)、[设计博文](https://dev.to/chrischtel/checking-at-compile-time-that-arena-memory-cant-escape-its-block-1jie)），拥有与块绑定并在块退出时释放的命名碰撞区（bump zone）。其检查器会为每个值标记所在区域的嵌套深度，并拒绝将区域值赋给外层变量、将其返回或存储。唯一的标记是参数上的 `borrow`。Fels 拥有 K 的深度检查，但按区域而非按变量实施。区域的数量由程序员决定，结果不能离开其所在区域（与第 5 点相反），也没有原地增长规则。

<a id="212-real-time-javas-scoped-memory-and-its-static-typings"></a>
### 2.12 实时 Java 的作用域内存及其静态类型系统

RTSJ 的 [`ScopedMemory`](https://www.rtsj.org/specjavadoc/javax/realtime/ScopedMemory.html) 区域以作用域栈的方式进入和离开，并作为一个整体回收。外层、寿命更长区域中的对象不得引用内层区域中的对象。这正是 K 的存储规则，只是在每次赋值时于运行时进行检查。[Scoped Types（Zhao, Noble, Vitek, RTSS 2004）](https://janvitek.org/pubs/rtss04.pdf) 在 Java 包（package）的粒度上静态地检查这一规则。[Boyapati, Sălcianu, Beebee 与 Rinard, PLDI 2003](https://people.csail.mit.edu/rinard/techreport/MIT-LCS-TR-869.pdf) 则在类上结合所有权和区域参数进行静态检查。它们“后进先出”的区域生命周期提供了一种 outlives 关系。这些系统均不具备增长、内联嵌套或目标位置构造。

<a id="3-multiple-stacks-put-to-other-uses"></a>
## 3. 用于其他目的的多栈设计

不少系统拥有多个栈，或拥有可成批释放对象的栈，但它们都没有像 K 使用数据栈那样使用自己的栈。

* **Forth** 拥有数据栈、返回栈和浮点栈。它们存放操作数，每个单元一个格（cell）。其字典更接近 K 的存储模型：`HERE` 是下一个空闲地址，而 `ALLOT` 与 `,` 会连续扩展正在构造的区域。负数的 `ALLOT` 或 `MARKER` 按 LIFO 顺序释放内存。`HERE` 以下的地址永不移动（[Forth-2012 §3.3.3](https://forth-standard.org/standard/usage)、[`MARKER`](https://forth-standard.org/standard/core/MARKER)）。但整个字典只有一个区域，且没有任何检查。
* **Prolog 的 WAM** 拥有用于构造结构的全局栈、存放环境与选择点的局部栈、轨迹栈（trail）以及下推列表（push-down list）。选择点会保存全局栈顶，使得失败回溯时一次性回收“在失败尝试期间构造的所有结构与变量”（[Aït-Kaci, "Warren's Abstract Machine: A Tutorial Reconstruction"](https://wambook.sourceforge.io/)）。内存是在回溯时而非作用域退出时回收的。词项是不可变的，且确定性程序仍需要垃圾回收器。
* **PostScript 的 `save` 与 `restore`** 为虚拟内存拍摄快照并将其重置回快照状态。快照可以嵌套，若存在对更新对象的引用，则会触发 `invalidrestore` 错误（[PostScript 语言参考手册](https://www.adobe.com/jp/print/postscript/pdfs/PLRM.pdf)）。这是“引用不能比它的根活得更久”的动态版本，作用于单一层级栈上的固定大小对象。
* **Turbo Pascal 的 `Mark` 与 `Release`** 把早期的堆当作栈来使用：`New` 向上推进堆顶，而 `Release` 将其截断回标记处，一次性释放其上的所有内容。它既不支持增长，也没有检查。
* **Icon 的字符串区域** 是所有字符串共享的单一连续碰撞区域；当左操作数恰好结束于区域的空闲指针处时，字符串拼接会原地追加，因为它“是最后分配的字符串，无需重新分配”（[`ocat.r`](https://github.com/gtownsend/icon/blob/master/src/runtime/ocat.r)）。这是针对单一数据类型的第 2 点，但运行在压缩式垃圾回收器之下。
* **Odin、Zig、Jai 与 C3** 提供了由程序员自主选择的碰撞 arena 和临时分配器。Odin 的 [`virtual.Arena`](https://pkg.odin-lang.org/core/mem/virtual/) 会预留地址空间并分步提交；它和 Zig 的 [`ArenaAllocator`](https://github.com/ziglang/zig/blob/master/lib/std/heap/arena_allocator.zig) 都只会在某次分配恰好是最近一次分配时才原地调整其大小；C3 的 [`@pool`](https://c3-lang.org/language-common/memory/) 会在作用域退出时释放该作用域的临时对象。所有这些都是动态的，且没有任何检查。
* **PL/I 的 `AREA` 与 `OFFSET`** 类型分别是一个可复制的存储池以及相对于该池的引用，因此整块区域移动后其中的链接依然有效（[IBM PL/I 语言参考](https://www.ibm.com/docs/SSY2V3_5.3.0/lr/ofda.html)）。这只是 K 的池相对引用（[§3.9](../k_spec.md#39-relative-references)）的先例；这些区域本身并不是栈。

<a id="4-relatives-of-the-lifetime-rule"></a>
## 4. 存活期规则的近亲

K 的存活期规则是：引用不能比拥有其目标的变量活得更久，并通过作用域嵌套在无注解的情况下完成检查。这一规则还有一些并不关心数据物理存放位置的近亲。

* Algol 68 要求赋值左侧的作用域不能比右侧更宽。这是针对栈分配（`loc`）对象的首个此类静态检查，对于静态检查无法判定的情况则由运行时检查兜底（[Berry, 1970](https://cs.uwaterloo.ca/~dberry/FTP_SITE/reprints.journals.conferences/Berry1970ImpleModelsA68.pdf)）。Ada 的可访问性层级（§2.4）正源自这一规则。
* 二等值（second-class values，[Osvald, Essertel, Wu, González-Alayón, Rompf, OOPSLA 2016](https://www.cs.purdue.edu/homes/rompf/papers/osvald-oopsla16.pdf)）可以向下传递，但绝不能被存储或返回，从而借助一等/二等标记提供“栈界定生命周期（stack-bounded lifetimes）”。可达性类型（reachability types，[Bao, Wei, Bračevac, Jiang, He, Rompf, OOPSLA 2021](https://www.cs.purdue.edu/homes/rompf/papers/bao-oopsla21.pdf)）在每个类型中携带一个值可能到达的变量集合，这是 K 的根在类型层面的体现。Scala 中的捕获类型（capturing types，[Boruch-Gruszecki 等人，TOPLAS 2023](https://plg.uwaterloo.ca/~olhotak/pubs/toplas23.pdf)）对能力（capabilities）做了同样的事情。
* [Chapel 的生命周期检查器](https://chapel-lang.org/docs/technotes/lifetimeChecking.html) 根据词法作用域拒绝会比所有者活得更久的借用，并提供可选的 `lifetime` 子句；不过类实例仍然分配在堆上。
* [Hylo](https://docs.hylo-lang.org/language-tour/bindings) 通过参数传递约定和投影（projection）将引用保持为二等公民，由抽象解释器进行检查，“无需生命周期注解”；但可增长集合仍然由堆支持。
* [Brandon, Driscoll, Dai, Ragan-Kelley, Milano 与 Aiken, OOPSLA 2026](https://doi.org/10.1145/3798221) 在纯函数式语言中无需注解即可推断借用与生命周期，在推断失败处插入引用计数；堆依然存在。
* D 语言的 [DIP1000](https://github.com/dlang/DIPs/blob/master/DIPs/other/DIP1000.md) `scope` 将引用限制在其词法作用域内，可对参数进行推断并在 `@safe` 代码中检查，作用域位于原生栈上。
* 用按根特化替代生命周期参数的做法仅出现在学术文献之外，见于 [Fernando Borretti 关于内存安全类型系统的文章](https://borretti.me/article/type-systems-memory-safety) 以及 Sean Baxter 的 [P3444R0《Memory Safety without Lifetime Parameters》](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3444r0.html)。

逃逸分析（escape analysis）文献从另一面揭示了同样的空白。Java 编译器对对象的栈分配在遇到可变长数组时便止步了，因为栈帧大小是固定的（[Kotzmann 与 Mössenböck, VEE 2005](https://www.usenix.org/legacy/events/vee05/full_papers/p111-kotzmann.pdf)；[OpenJ9 的乐观栈分配，2024](https://dl.acm.org/doi/10.1145/3656389)）。为每个可增长值赋予自己的栈恰恰消除了这一限制。

<a id="5-what-no-precedent-was-found-for"></a>
## 5. 未发现先例的特性

* 按活跃度（liveness）将可增长值分配到编译期确定数量的栈上，如同寄存器分配一样，使依次存活的值共享同一个栈。
* 将“每个栈上恰好一个可调整大小的值、始终位于栈顶、内部引用在增长期间保持稳定”确立为语言不变式，而非库的使用约定。Obstacks 规定了该规则但会重定位对象，而 arena 惯用法虽能保持地址稳定，却将遵守规则的责任留给了程序员。
* 作为公开发表的技术，用按引用根特化来替代生命周期参数。
* 旨在使栈数量保持为常量的递归限制。
* 在此类栈上将可变大小数据内联嵌套存储，例如将字符串数组放在同一个连续块中。

本次调研未发现任何其他工作将这些特性与跨越任意调用深度的目标位置构造以及收缩规则结合在一起。

<a id="6-how-the-survey-was-done"></a>
## 6. 调研是如何进行的

本次调研于 2026 年 10 月通过五组并行的网络检索完成，每组覆盖一类候选对象：

* 动态 arena 与类 obstack 设计；
* 静态区域系统；
* Ada、OCaml、ParaSail 与实时 Java；
* 广泛排查生产语言、业余语言与研究型语言（包括 Austral、Verona、Cogent、Wuffs、Ivory、occam、Oberon、Mesa、CLU、Scopes、Myrddin、Hare、Inko、Carp、Mojo、Koka、Lobster、Nim、V、Bosque、Pony、Jakt、Roc、Gleam、Sing#、Deca、Vault、BitC、ATS、Mezzo、SPARK、June、Mercury、Ante、Bone Lisp、Hike、Dyon、Vale、Zest、Hume、Virgil、Cone 等）；
* 以多栈分配、可变大小数据的栈分配、无注解生命周期推断、目标传递以及基于栈帧的堆为关键词检索学术文献。

文中引自 ParaSail、ML Kit 回顾论文以及 ECOOP 2022 论文的引文均已对照原文核对。[Hacker News 上关于 K 的讨论](https://news.ycombinator.com/item?id=49748954) 未提供超出上述范围的候选方案。上述排查中未提及的其他语言，要么运行在带垃圾回收或手动管理的堆上，要么完全不进行动态分配，要么是完全静态且不支持递归的语言，因此至多只共享 §1 中的一点。
