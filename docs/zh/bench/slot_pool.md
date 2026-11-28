<a id="slot_pool-versus-reusable"></a>
# slot_pool 对比 reusable

`reusable` 和 `reusable[]` 仍处于弃用状态但依然可用。这些对比将内置实现作为基线保留；移除它们属于一项独立变更。应用层测量结果与迁移决策见 [pool_workloads.md](pool_workloads.md)。

该库沿用了内置实现的 LIFO 空闲索引策略。两个版本均会分配已初始化的槽位、保持稳定地址并检查索引。该工作负载使用两个局部所有者，分别用它们调用相同的 `exercise` 辅助函数，并检查分配顺序与最终内容。

<a id="current-measurements"></a>
## 当前测量结果

于 2026-10-28 在 Windows、Ryzen 9 9950X3D、CPU 16–31 上测得。表中比值为“库实现 / 内置实现”；数值越小越好。Clang 以 O2 优化级别编译生成的 C 代码。每个单元格为两次预热后七次交替采样的进程周期数中位数之比，且校验和完全一致。六组用例的均值对每个工作负载/字节数配对赋予相同权重。这些以分配为主的循环在各操作之间不包含任何应用层工作。

| 工作负载 | 字节数 | O1 Clang | O1 TinyCC | O1 JIT 总计 | O2 Clang | O2 TinyCC | O2 JIT 总计 |
|---|---:|---:|---:|---:|---:|---:|---:|
| append | 8 | 1.029 | 0.968 | 1.029 | 0.981 | 0.973 | 1.033 |
| append | 128 | 0.990 | 0.996 | 1.012 | 0.964 | 0.997 | 1.004 |
| lifo | 8 | 0.861 | 0.939 | 1.001 | 0.872 | 0.926 | 1.009 |
| lifo | 128 | 0.904 | 0.986 | 1.095 | 0.879 | 0.983 | 1.066 |
| scattered | 8 | 0.913 | 0.952 | 0.990 | 0.888 | 0.962 | 0.988 |
| scattered | 128 | 0.911 | 1.011 | 0.936 | 0.873 | 1.012 | 0.920 |
| 几何均值 | | 0.933 | 0.975 | 1.009 | 0.908 | 0.975 | 1.002 |

独立运行的 TinyCC 仅测量执行阶段。**JIT 总计**包含在指定优化级别下的 K 编译、TinyCC 编译与执行；它并非纯分配操作测量。O2 现为默认优化级别。对比双方均包含进程启动、初始化与最终校验和计算。挂钟时间也一并保留了记录，但相比进程周期数更容易受后台活动影响。

<a id="reproduce"></a>
## 复现方法

```text
python bench/slot_pool.py --exe build-root/Release/k.exe --levels 1 2 --calls natural --widths 1 16 --compilers clang tcc jit --reps 7 --warmups 2 --cpus 16-31 --out build-root/review-final-slot
```

测试框架会在 `--out` 目录下保存 K/C 快照、日志、校验和、编译器与库指纹、产物大小以及所有采样数据。已签入仓库的[当前结果](../../../bench/slot_pool_results.json)包含这一矩阵。`--levels 0` 同样受支持。`--calls natural` 会保持生成的 C 代码原样不变；`boundary` 会为原生 `exercise` 函数添加 `K_NOINLINE`，而 `inline` 则请求内联。JIT 始终使用编译器未修改的输出。TinyCC 会在 K 构建产物旁或 `PATH` 中查找。MSVC 可通过 `--compilers msvc` 使用；本文计时采用 Clang、独立 TinyCC 与 JIT。

<a id="interpretation-and-remaining-work"></a>
## 结果解读与后续工作

`alloc_ref` 直接返回追加操作的结果。索引构造器与引用释放函数以 `alloc_index_with` 和 `free_ref` 形式提供；使用它们的应用层结果见 [pool_workloads.md](pool_workloads.md)。该库保留了其索引检查，且未添加占用状态追踪或私有字段假设。

原生 Clang 与独立 TinyCC 均已达到通用微基准测试目标。由于 TinyCC 会将每个 C 临时变量保存在内存中，并通过字符串搬移指令（`movsq`）来复制结构体赋值，因此在 2026-10-28 之前，该库在 TinyCC 下的 8 字节复用耗时曾是内置实现的 1.14–1.20 倍（几何均值 1.08–1.10）。弥合这一差距的编译器改动均属通用优化：由前置长度检查可确保安全的 pop 操作会省去判空检查（边界检查消除）；pop、push 以及元素读写不再保留不必要的临时变量；小型结构体变量以单个整数形式存储；而 `i < 0 || i >= items.len` 则编译为一次无符号比较。对从公共空闲链表弹出的槽位进行的索引检查依然保留。包含每个程序编译时间在内的 JIT 总计耗时在平均值上已处于同等水平。
