# Kense OS 架构与原理图解中心

本页汇总了 K 语言全套 **18 张简体中文版 Kense OS 系统架构图解**——涵盖数据栈内存与生命周期、编译期边界检查消除（BCE）、结构化异步与隔离线程、性能内存基准、编译器前后端流水线、原生/WASI 目标，以及 Cordis 组件微内核。

点击任意图解卡片右下角 **放大查看** 即可全屏高清检视，或直接打开原始矢量 SVG。

## 核心语言与运行时机制

### 数据栈与值生命周期

[![数据栈与值生命周期图解：调用栈帧引用地址稳定的分段数据栈，并在作用域退出时完成 O(1) 回退释放。](../diagrams/svg/zh/data-stacks-memory.svg)](../diagrams/svg/zh/data-stacks-memory.svg)

K 不使用追踪式垃圾回收（GC），也不依赖通用堆分配器。诸如 `String`、`Array` 以及按载荷分布的变长 `enum` 都存放在**地址稳定、分段扩展的数据栈**上：内层作用域传入引用时由类型检查器校验根深度，退出作用域只需 `O(1)` 重置游标即可整体回收。详见 [K 语言架构设计](architecture.md#从资源出发) 与 [运行时实现](implementation.md#7-the-runtime)。

### 编译优化与边界检查消除（BCE）

[![编译优化与边界检查消除图解：Checked FnSpec 经过 IR 优化、BCE 范围证明和 checkfree 门禁后编译为高速 C 代码。](../diagrams/svg/zh/optimization-bce-pipeline.svg)](../diagrams/svg/zh/optimization-bce-pipeline.svg)

编译器根据控制流分支、切片范围或索引类型契约（如 `u32 in dist`、`u32 upto out`）在编译期证明索引不越界，进而**完全移除内循环检查代码**。标注 `checkfree fn` 的函数会在编译期充当硬性门禁——只要函数体内残余一条无法证明的越界检查即报错。详见 [边界检查消除实现](implementation.md#5-bounds-check-elimination) 与 [优化器](implementation.md#4-the-optimizer)。

### 结构化异步与隔离工作线程

[![结构化异步与隔离工作线程图解：协作式 async fn 运行在词法任务作用域中，原生线程仅通过类型化队列通信。](../diagrams/svg/zh/async-tasks-workers.svg)](../diagrams/svg/zh/async-tasks-workers.svg)

单线程 `async fn` 会被编译为状态机任务帧（`runtime_tasks.h`），并严格受限于词法任务作用域：父作用域退出前会确定性取消并排空所有子任务。需要多核并行时，K 启动彼此完全隔离的原生工作线程（`runtime_threads.h`），每个线程持有独立数据栈，通过类型化消息队列转移值所有权，从根源上杜绝共享可变状态与数据竞争。详见 [异步函数与装饰器设计](design/async_and_decorators.md)。

### 性能与峰值内存概览

[![K 语言性能与峰值内存概览图解：16 项大型工作负载几何平均下相对惯用 C++、专家 C++ 与安全 Rust 的速度与内存优势。](../diagrams/svg/zh/benchmark-architecture.svg)](../diagrams/svg/zh/benchmark-architecture.svg)

在 16 项大型工作负载的几何平均对比中，紧凑内联表示与 `checkfree` 无分支循环使 K 在速度上达到惯用 C++ 的 **3.51x / 3.54x**、专家优化 C++ 的 **1.24x / 1.26x**、最佳安全 Rust 的 **1.13x / 1.19x**，同时峰值内存下降 **1.25x–1.95x**。详见 [性能与内存基准总结](bench/summary.md)。

---

## 系统全貌与编译器流水线

### 核心系统架构

[![K 核心架构图：统一前端驱动原生构建、WASI 命令模块与进程内 TinyCC JIT，Cordis 根据项目锁选择宿主服务。](../diagrams/svg/zh/core-architecture.svg)](../diagrams/svg/zh/core-architecture.svg)

同一份经过语义与生命周期校验的编译器前端，同时支撑原生 `C + LTO` 构建、`wasm32-wasip1` 模块生成以及开发阶段的进程内 TinyCC 即时执行。参见 [K 语言架构设计](architecture.md)。

### 依赖边界与上下文映射

[![K 依赖上下文映射图：清晰划分 CLI、编辑器工作区、WASI 宿主与 Cordis 服务图。](../diagrams/svg/zh/context-map.svg)](../diagrams/svg/zh/context-map.svg)

K 将程序加载、包解析、语言语义、目标构建与编辑器覆盖缓冲划分为明确的限界上下文。参见 [代码中的领域边界](architecture.md#代码中的领域边界)。

### 编译器前端 · 语义分析 · C 后端

[![编译器前端图：加载导入、词法语法分析与符号解析生成带源码位置的 AST。](../diagrams/svg/zh/compiler-front-end.svg)](../diagrams/svg/zh/compiler-front-end.svg)

[![语义分析图：泛型特化、存储所有权与生命周期根检查生成特化函数体。](../diagrams/svg/zh/semantic-analysis.svg)](../diagrams/svg/zh/semantic-analysis.svg)

[![优化与 C 后端图：优化检查后表示、执行边界证明并向原生 C、WASI SDK 或 TinyCC 发射代码。](../diagrams/svg/zh/c-backend.svg)](../diagrams/svg/zh/c-backend.svg)

命令行工具 `k` 与语言服务器 `k lsp` 共用同一前端解析入口 `ParseProgram`，并保留完整源码区间以输出精准诊断。参见 [前端实现](implementation.md#2-front-end) 与 [类型检查器](implementation.md#3-the-typechecker)。

---

## 运行时、编译目标与工具链

### 目标运行时与 WebAssembly

[![目标运行时图：data stacks、task frames 与 native workers 支撑生成后的 C 代码运行。](../diagrams/svg/zh/target-runtime.svg)](../diagrams/svg/zh/target-runtime.svg)

[![WebAssembly 目标图：使用 32 位内存布局、安全增长栈检查并通过 WASI SDK 生成 .wasm 命令模块。](../diagrams/svg/zh/webassembly-target.svg)](../diagrams/svg/zh/webassembly-target.svg)

执行 `k build --target wasm32-wasip1` 可构建自带 K 运行时的 `.wasm` 命令模块，在 32 位线性内存中维持地址稳定的数据栈分片。参见 [WebAssembly 目标设计](design/wasm.md#wasi-command-target)。

### C ABI 互操作 · 工具链 · 语言服务器 · 标准库

[![C 互操作图：extern 与 export fn 明确划分原生 C ABI 句柄所有权。](../diagrams/svg/zh/c-interop.svg)](../diagrams/svg/zh/c-interop.svg)

[![工具链图：CLI 命令调度、包根目录解析与按目标区分的构建缓存。](../diagrams/svg/zh/toolchain.svg)](../diagrams/svg/zh/toolchain.svg)

[![语言服务器图：k lsp 将编辑器未保存缓冲覆盖到离线编译器分析中。](../diagrams/svg/zh/language-server.svg)](../diagrams/svg/zh/language-server.svg)

[![标准库布局图：基于 K 语言内核构建的模块化标准库。](../diagrams/svg/zh/libraries.svg)](../diagrams/svg/zh/libraries.svg)

通过 `extern` 与 `export fn` 显式区分借用句柄与拥有句柄；工具链按目标和工具链三元组隔离包缓存。参见 [C 句柄所有权](reference.md#owning-c-handles)、[命令行工具](command_line.md)、[包管理](packages.md) 与 [编辑器集成](editors.md)。

---

## Cordis 微内核与宿主服务扩展

[![Cordis 生命周期图：图解析、锁摘要校验、候选提供者校验激活与按请求世代固定。](../diagrams/svg/zh/cordis.svg)](../diagrams/svg/zh/cordis.svg)

[![宿主扩展与客座语言图：通过锁定前端与适配层支持嵌入式语言块与独立运行时。](../diagrams/svg/zh/guest-languages.svg)](../diagrams/svg/zh/guest-languages.svg)

[![托管服务提供者图：静态链接或动态加载满足版本化 ABI 契约的 I/O、诊断与设备后端。](../diagrams/svg/zh/hosted-providers.svg)](../diagrams/svg/zh/hosted-providers.svg)

宿主从受验证锁文件加载版本化服务提供者；替换新版本前先完成 ABI 校验与预激活，确保正在进行的请求和异步外设回调安全完成。参见 [Cordis 微内核设计](design/cordis_microkernel.md) 与 [嵌入式语言设计](design/embedded_languages.md)。
