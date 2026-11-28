<a id="pools-in-application-benchmarks"></a>
# 应用基准测试中的池（Pool）

`reusable` 和 `reusable[]` 仍然处于弃用状态，但仍可使用。现有的标量移植版本使用 `slot_pool`；切片移植版本使用 `slice_pool`，但 LangArena 的 `Etc::CacheSimulation` 除外，它仍然使用 `reusable[]`。此处测量的候选版本包含了这最后一处迁移，因此全套基准测试的结果回答了将所有 LangArena 切片用户（包括准备阶段）切换过来的开销。

<a id="current-comparison"></a>
## 当前对比

于 2026-10-25 在 Windows x64、Ryzen 9 9950X3D、Clang 22.1.3 上测得。双方在 K O2 优化级别下使用同一分支的编译器。内置（builtin）实现源码来自 `94cc64da`；标量候选版本为当前的移植实现，切片候选版本则是对这些基线源码进行机械迁移的结果。算法、内存布局、输入以及分配顺序均保持不变。仅单独标注的 `alloc_copy` 试验将先分配后复制（allocate-then-copy）替换为直接初始化。

双方各自拥有三个重命名的可执行文件副本，每个副本预热 1 次并进行 3 次正式测量运行，且交替执行不同后端的顺序。比率为 library / builtin，取全部 9 个样本的中位数。单线程用例固定使用 CPU 28；整套基准测试使用 CPU 0–15，以便多线程用例保留 16 个逻辑 CPU。LangArena 和标量构建均将函数按 4096 字节对齐。使用 clang-cl 时，测试框架通过 `/clang:` 传递该选项。LangArena 使用其原生的 O2 编译标志；kostya2 保留其 O3/native/LTO 标志及 TCP 计时协议。

| 工作负载 | 观测指标 | 内置（Builtin） | 库（Library） | 比率 |
|---|---|---:|---:|---:|
| lru_indices | 进程周期数（百万） | 1989.4 | 1959.0 | 0.985 |
| lru_nonopt | 进程周期数（百万） | 1925.6 | 1925.2 | 1.000 |
| plb_lru_1 | 进程周期数（百万） | 496.6 | 496.9 | 1.001 |
| plb_lru_2 | 进程周期数（百万） | 204.9 | 206.1 | 1.006 |
| merkletrees | 进程周期数（百万） | 522.6 | 545.7 | 1.044 |
| Etc::CacheSimulation | 计时窗口秒数 | 0.9120 | 1.1620 | 1.274 |
| Graph::BFS | 计时窗口秒数 | 0.8380 | 0.8660 | 1.033 |
| Graph::BFS | 准备阶段秒数 | 0.7365 | 0.8560 | 1.162 |
| Graph::BFS | 整进程秒数 | 1.7620 | 1.9126 | 1.085 |
| kostya2/primes | 计时窗口秒数 | 0.0322 | 0.0346 | 1.073 |
| LangArena，全部 50 个用例 | 计时窗口总和（秒） | 22.707 | 22.928 | 1.010 |
| LangArena，全部 50 个用例 | 整进程秒数 | 29.938 | 30.567 | 1.021 |
| LangArena，全部 50 个用例 | 整进程周期数（十亿） | 169.215 | 171.654 | 1.014 |
| CacheSimulation，alloc_copy 试验 | 计时窗口秒数 | 0.936 | 1.153 | 1.232 |

全套运行会在每次执行时校验全部 50 个上游结果。整进程时间包含构造函数、准备阶段、预热、计时工作以及清理阶段。其 **+2.1% 挂钟时间 / +1.4% 进程周期**取代了早期基于组件的估算值。仅统计报告的计时窗口时，增幅约为 1.0%；准备阶段耗时从 2.623 秒增至 3.048 秒。这些数值是中位数之比，既非加权预测，也不代表声明每一项基准测试都达到了 1% 的阈值。

标量比率采用 Windows 进程周期数，因为在短时 LRU 运行中后台调度导致了较大的挂钟时间异常值；两个指标的所有样本均被保留。在此次受控运行中，`lru_nonopt` 为 1.000×。Merkle 仍然慢约 4.4%。为了保证正确性，捕获环境的回调会保留其递归栈帧；无捕获的回调仍允许进行叶调用消除（leaf-call elimination）。当禁用这一安全优化时，旧的释放写法与 `free_ref` 都表现出同样更大的差距。Merkle 剩余的性能差距并非由该封装层所致。

CacheSimulation 确实存在性能回退：机械迁移版本在其计时窗口和进程周期数上均慢了 +27.4%。`alloc_copy` 将实测差距缩小到约 23%，但仍无法忽略不计。因此其发布的源码继续保留在内置实现上；关于将差距减半但尚未完全消除的运行时 span 树，请参见下文。Graph 的分配器在准备阶段（+16.2%）运行，位于其搜索计时器之外；搜索阶段耗时的区间存在重叠，而整进程开销为 +8.5%。Primes 在其较短的计时窗口中慢 +7.3%（整进程为 +4.1%）。已迁移的现有移植版本予以保留；这些测量结果并不声称在所有场景下都已全面持平。

<a id="cachesimulation-on-the-runtime-span-tree"></a>
## 运行时 span 树上的 CacheSimulation

在 `slice_pool` 将其树结构移入运行时（`k_slice_pool_*`，即 `reusable[]` 所使用的 C 实现，采用会检查所读取的 `nodes` 与 `tree` 字段的副本）之后，于 2026-10-28 在同一台机器上按相同测试协议测得（3 个副本，每个副本 1 次预热与 3 次测量运行，CPU 28，Clang 22.1.3 配合 LangArena 编译标志）。双方均由同一编译器构建；库实现侧为机械迁移版本。

| 工作负载 | 观测指标 | 内置（Builtin） | 库（Library） | 比率 |
|---|---|---:|---:|---:|
| Etc::CacheSimulation | 计时窗口秒数 | 0.898 | 0.989 | 1.101 |
| Etc::CacheSimulation | 整进程秒数 | 1.094 | 1.207 | 1.103 |
| Etc::CacheSimulation | 进程周期数（百万） | 4661 | 5142 | 1.103 |

差距已从 +27.4% 降至 +10.1%，且源码仍保留在内置实现上。相关样本位于下方原始结果文件的 `runtime_span_tree` 字段下：

```text
python bench/pool_workloads.py --exe build/Release/k.exe --arena C:/W/LangArena --kostya C:/W/kostya-benchmarks --out build/pool-apps-spans --construct-timer --only Etc::CacheSimulation --copies 3 --reps 3 --warmups 1 --cpus 28
```

<a id="where-the-gap-is"></a>
### 差距来源分析

于 2026-10-28 对该次运行生成的 C 代码进行性能剖析。双方执行的树操作完全相同：在 C 代码中加入计数器后，双方均记录到 9,082,800 次放置（placement）与 9,077,910 次释放（free），树下降与扫描步数相同，释放的索引也完全一致。通过对双方进行采样剖析（使用 Clang 22 配合上述标志外加 DWARF 行表，每 250 µs 采样一次主线程地址）并对库实现侧生成的 C 代码进行计时修改实验，定位出差距所在：

| 库实现侧的 C 代码 | 相对于内置实现的比率 |
|---|---:|
| 原始生成结果 | 1.101 |
| 强制内联 `new_string` | 1.027 |
| 移除树副本的检查 | 1.102 |
| 两者同时应用 | 1.034 |

各 3 个副本、每个副本 3 次交替运行在 CPU 28 上的计时窗口秒数中位数。

绝大部分差距来自单次内联决策。Clang 将内置实现侧的 `new_string` 内联进了 `put`，其内联代价为 185（内联阈值为 225）；但未能内联库实现侧的 `new_string`，后者的内联代价为 370。内联进其中的 `alloc_slice` 带来了 7 个 abort 调用（2 次长度检查、span 检查、2 次切片检查、扩容检查以及 `copy_from` 的长度断言），而内置实现的分配仅带来 2 个。Clang 对每次调用计入约 24 的代价：即便将其替换为 trap 指令，这 7 处检查的代价仍为 205。将 abort 函数标记为 cold 没有任何改变，而每个函数共享一个 abort 调用时，代价依然高达 280。在未内联的情况下，这 5 个调用各自需要传递 8 个参数（在 Windows ABI 下其中 4 个需通过栈传递），并保存 8 个寄存器。在 WSL 中使用 Clang 20 构建时，`new_string` 同样未被内联，但函数调用代价更低，库实现侧虽然多执行了 9.2% 的指令，周期数却仅多出 4.5%。

树副本的检查在这里没有产生可测量的开销。在 `new_string` 被内联后，剩余 2.7% 差距中约有一半来自树副本的放置与释放操作，其余部分则分散在 `put` 的内联代码中。

库实现的 K 代码没有低成本的方法能把内联代价降到阈值以下：去掉第二次长度检查只能省下 10 点代价，却会失去追加元素的批量清零能力；而改用按索引逐个对复用元素清零、而非通过切片清零，则会使代价升至 465。真正能消除这部分差距的改进在编译器侧：能将 span 检查传递至 `defaults` 切片的边界检查消除、更低代价的 abort 调用点，或者允许 C 编译器内联因包含已内联 `inline fn` 而体积变大的小函数。此外，库实现侧还生成了两个 C 代码完全相同的 `new_string` 特化版本，这仅增加了代码体积。

<a id="compiler-and-allocator-costs"></a>
## 编译器与分配器开销

以下数据于 2026-10-25 在库实现的树结构仍为 K 代码时测得。切片分配器采用与 `reusable[]` 相同的增强型 B+ 树：按地址顺序首次适应（first fit）、相邻 span 合并、尾部复用、节点回收以及根节点坍缩。来自 `1ab1d26d` 的树维护改动同时存在于双方。差分测试对比了分配位置与内容，包括 60,000 次操作的模型。测试过程中未禁用任何边界、包含关系、溢出或初始化检查。

当时的树辅助函数独立于元素类型接收 `nodes` 和 `tree`。LangArena 为 `place`、`fix` 和 `delete` 各自只生成了一份函数体，而不是为每种元素类型各生成一份。其生成的 C 代码从 2,611,464 字节增长到 2,694,704 字节（+3.2%），可执行文件从 1,272,320 字节增长到 1,293,312 字节（+1.7%）。在 1 次预热后进行的 5 样本交替纯编译重测显示，进程周期数从 10.12 亿增至 10.62 亿（+4.9%），耗时从 0.242 秒增至 0.254 秒。两份生成的 C 文件与运行时测量所用的构建完全一致；最初噪声较大的编译样本也保留在 JSON 文件中。

批量零值默认值、统一固定数组初始化以及重叠安全的 `move_from` 在语义允许的场合消除了逐元素循环。`alloc_copy` 还避免了在随后立即会被源数据覆盖的位置上进行默认初始化。[槽位池测试矩阵](slot_pool.md)与[切片池测试矩阵](slice_pool.md)涵盖了在 K O1/O2 下使用原生 Clang、独立 TinyCC 以及编译加运行 JIT 的计时数据。最终编译器与标准库除源码路径与诊断行号外，完整复现了全部 48 个微基准测试的 C 文件；其原始测量指纹仍保留在结果中。原生槽位分配的性能基本持平。切片分配当时在 Clang O2 下平均慢约 6%，在 TinyCC O2 下慢 34%，且在碎片化分配与调整大小（resize）场景下差距更大；迁移至运行时 span 树后，其在 Clang 下已持平，在 TinyCC 下慢 7%（见[切片池测试矩阵](slice_pool.md)）。

<a id="reproduce"></a>
## 复现方法

下方命令中的上游检出路径用于提供 LangArena 配置及 kostya2 的测量辅助脚本；K 源码则来自本仓库。配置/源码哈希及精确的编译器标志请参见 JSON 结果文件。

```text
python bench/slot_pool_users.py --exe build-root/Release/k.exe --baseline 94cc64da --out build-root/review-final-slot-users --copies 3 --reps 3 --warmups 1 --cpus 28
python bench/pool_workloads.py --exe build-root/Release/k.exe --arena C:/W/LangArena --kostya C:/W/kostya-benchmarks --out build-root/review-final-apps-measured --construct-timer --only Etc::CacheSimulation Graph::BFS kostya2/primes --copies 3 --reps 3 --warmups 1 --cpus 28
python bench/pool_workloads.py --exe build-root/Release/k.exe --arena C:/W/LangArena --kostya C:/W/kostya-benchmarks --out build-root/review-final-apps-measured --reuse-builds --results whole.json --only LangArena::All --copies 3 --reps 3 --warmups 1 --cpus 0-15
```

使用 `--whole-suite` 可在选定用例集之后追加完整的 LangArena 运行，或在单独的输出目录中配合 `--alloc-copy` 测试直接复制候选版本。`--compile-reps` 默认采用 5 个交替样本。`--reuse-builds` 会刻意沿用已保存的二进制文件及其元数据，即使编译器或源文件此后已发生变更。[当前原始结果](../../../bench/pool_workloads_results.json)包含应用测试、全套测试、标量测试、纯编译及直接复制的完整测量数据。

<a id="remaining-work"></a>
## 后续工作

- 根引用 ABI：在跨函数调用时直接透传组件头部/栈信息，同时为已存储、已捕获及可选引用保留描述符身份。同时传递描述符与拆分字段的原型未能对 TinyCC 带来实质性改善，因此已被弃用；但这并不排除未来采用更完备的逃逸敏感（escape-sensitive）设计。
- 进一步减少受检的树元数据访问、暂存区（scratch）初始化以及 K 生成的临时变量/辅助函数调用，尤其是在 TinyCC 下。暂存区目前仍保持初始化；大范围的强制内联在试验中并未带来改善。
- 针对可证明归属错误池的切片释放提供编译期诊断。运行时仍保留包含关系检查；`free_ref` 保留编译期来源检查。
- 根所有者的工作线程自动转移。一份经过测试的扁平快照方案见[工作线程示例](../../../samples/32_pool_worker_snapshots.k)；[迁移指南](../stdlib.md#migrating-from-reusable)说明了当前存在的不足。

上述 Windows 测量结果不能替代 Linux/macOS 或 sanitizer CI 测试，也不能替代公开测试套件的跨语言对比结果。内置实现的移除以及最终提交历史的整理属于独立的评审与合入决策。
