<a id="slice_pool-versus-reusable"></a>
# slice_pool 与 reusable[] 对比

`reusable` 和 `reusable[]` 仍处于弃用状态但目前仍然可用。这些对比将内建实现作为基线；移除它们将作为单独的变更处理。应用程序级测量数据与迁移决策见 [pool_workloads.md](pool_workloads.md)。

该库将其空闲跨度（free spans）保存在内建实现自身的增强 B+ 树中：即运行时的跨度树（span tree），通过针对其 `nodes` 和 `tree` 字段的三个 extern 访问。它所持有的树函数副本与 `reusable[]` 的版本相比，仅在两处存在差异：将树头（header）与节点（nodes）拆分开来，以及对其从节点读取的数据进行检查（因为程序可以写入这些节点）。差分测试不仅检查内容，也检查放置位置。

<a id="current-measurements"></a>
## 当前测量结果

测量于 2026-10-28 在 Windows、Ryzen 9 9950X3D（CPU 16–31）上进行。比值为库实现 / 内建实现；数值越小越好。Clang 以 O2 编译生成的 C 代码。每个单元格为两次预热后七次交错采样的进程周期数中位数之比，且校验和完全一致。六组用例的均值对每对工作负载/字节大小赋予相同权重。这些以内存分配为主的循环在各操作之间没有应用程序计算工作。

| 工作负载 | 字节数 | O1 Clang | O1 TinyCC | O1 JIT 总计 | O2 Clang | O2 TinyCC | O2 JIT 总计 |
|---|---:|---:|---:|---:|---:|---:|---:|
| append | 8 | 0.934 | 1.033 | 1.100 | 0.960 | 1.017 | 1.069 |
| append | 128 | 1.007 | 1.027 | 1.017 | 1.009 | 1.036 | 1.009 |
| fragmented | 8 | 0.994 | 1.084 | 1.205 | 0.995 | 1.087 | 1.196 |
| fragmented | 128 | 0.999 | 1.080 | 1.140 | 1.006 | 1.077 | 1.140 |
| resize | 8 | 1.011 | 1.143 | 1.229 | 1.014 | 1.116 | 1.194 |
| resize | 128 | 0.995 | 1.157 | 1.253 | 1.003 | 1.112 | 1.140 |
| 几何平均 | | 0.990 | 1.086 | 1.154 | 0.998 | 1.073 | 1.123 |

同一轮测试还将此前版本的库（其树实现为带检查的 K 代码）作为第三组对比对象进行了测量。新库消耗的周期数占旧库周期数的比例如下：

| 工作负载 | 字节数 | O1 Clang | O1 TinyCC | O1 JIT 总计 | O2 Clang | O2 TinyCC | O2 JIT 总计 |
|---|---:|---:|---:|---:|---:|---:|---:|
| append | 8 | 1.016 | 1.007 | 0.879 | 1.019 | 1.005 | 0.849 |
| append | 128 | 1.002 | 1.010 | 0.967 | 1.004 | 1.011 | 0.975 |
| fragmented | 8 | 0.892 | 0.730 | 0.457 | 0.901 | 0.731 | 0.453 |
| fragmented | 128 | 0.925 | 0.754 | 0.507 | 0.923 | 0.751 | 0.506 |
| resize | 8 | 0.918 | 0.729 | 0.470 | 0.872 | 0.708 | 0.460 |
| resize | 128 | 0.882 | 0.793 | 0.615 | 0.910 | 0.762 | 0.541 |
| 几何平均 | | 0.938 | 0.829 | 0.620 | 0.937 | 0.819 | 0.601 |

独立运行的 TinyCC 仅测量执行阶段。**JIT 总计**包含在指定优化级别下的 K 编译、TinyCC 编译以及执行过程；它并非仅针对分配操作的测量。两侧的测量均包含进程启动、初始化和最终校验和计算。挂钟时间（wall times）也予以保留，但其对后台活动的敏感度高于进程周期数。

<a id="reproduce"></a>
## 复现

```text
python bench/slice_pool.py --exe build/Release/k.exe --levels 1 2 --calls natural --widths 1 16 --compilers clang tcc jit --reps 7 --warmups 2 --affinity 0xffff0000 --previous-library <old>/slice_pool.k --out build/slice-pool-bench-final
```

`<old>` 中包含来自 `06a6300f` 的 `stdlib/slice_pool.k` 和 `stdlib/slice_pool_internal.k`。测试套件会在 `--out` 下保存 K/C 快照、日志、校验和、编译器及库指纹、体积大小以及全部采样数据。已检入仓库的[当前结果](../../../bench/slice_pool_results.json)包含这一测试矩阵。同时还支持 `--levels 0`。`--calls natural` 保持生成的 C 代码不变；`boundary` 会为原生 `exercise` 函数添加 `K_NOINLINE`，而 `inline` 则请求内联。JIT 始终使用编译器未修改的输出。TinyCC 会从 K 构建产物旁或 PATH 中自动发现。MSVC 可通过 `--compilers msvc` 使用；此处的计时使用了 Clang、独立 TinyCC 和 JIT。

<a id="interpretation-and-remaining-work"></a>
## 解读与剩余工作

将树放在 C 中实现后，Clang 达到了与基线持平的水平。在 JIT 下，树以原生代码运行，与 `reusable[]` 相同——这正是旧库损失最大的地方（2.0–2.7×）；而在独立 TinyCC 下，双方的树均为 TinyCC 编译的代码。在 TinyCC 和 JIT 下仍存在差异的部分，是树调用周围的 K 代码，TinyCC 会按原样对其进行编译。在 O2 优化级别下，resize 循环在两者下每步都比内建实现多消耗约 60 个周期。其中，树副本执行的检查在 TinyCC 下约占运行总量的 1.5%。内联 `realloc_slice` 和 `finish` 时作为参数绑定产生的三次 16 字节切片拷贝，在总计 5800 万个周期中占了约 1200 万个周期。对 `k_rref` 参数制作快照、在调用前后回写缓存的顶部值（cached tops），以及库自身的边界与跨度检查，各自约占 1%。通过包装函数调用树并未带来可测量的差异。JIT 总计时间还额外承担了编译内联库代码的开销（在此测试中每个程序约为 700 万至 1500 万个周期），这构成了 append 各行差距的主要原因。不内联 `alloc_slice` 会拖慢 append，且对复用没有任何帮助。

`reusable[]` 保留了无检查的树函数。如果共享带检查的副本，实测显示其在 TinyCC 和 JIT 下的 fragmented 和 resize 负载中会慢 3–5%。而使用其自有副本时，其测量速度与 master 分支相当：在相同的 12 组工作负载/优化级别组合上，几何平均比值分别为 Clang 1.001、TinyCC 0.991 和 JIT 1.017。
