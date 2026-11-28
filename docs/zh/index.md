# K 编程语言文档中心

K 是一门兼顾内存安全与极致系统性能的编程语言：无需堆分配器、垃圾回收器或繁琐的生命周期标注（参见[中文主页](../../README.zh-CN.md)）。本站点全部核心手册、编译器文档、基准报告与设计笔记均已提供完整简体中文版本。

## 语言学习（建议阅读顺序）

| 文档 | 内容说明 | 适合场景 |
|---|---|---|
| [中文项目主页](../../README.zh-CN.md) | 语言设计理念、系统架构全览与快速起步。 | 初次了解 K 语言、安装编译器并运行首个程序。 |
| [快速上手与核心要点](quickstart.md) | 10 分钟速览：安装、常用命令与 K 的 5 大核心差异。 | 希望快速建立整体心智模型。 |
| [完整教程（Tutorial）](tutorial.md) | 通过大量可编译示例系统讲解 K 的数据栈、数组族、引用、`enum`、线程与 `return … from`。 | 系统学习 K 语言（约需一个下午）。 |
| [语言参考手册（Reference）](reference.md) | 逐主题全面覆盖语法、类型、存储与 FFI 规则（含 `using`、`errdefer`、`?.`/`??`、`.Variant`、`..base`、解构、具名实参、`async`/`await` 及无 `main` 脚本），每节均附可编译例程与规范索引。 | 掌握基础后系统查阅特性与写法。 |
| [形式化语言规范（Specification）](k_spec.md) | 编译器实现级精准规则：尺寸分类、构造/收缩规则、底层表示与编译模型。 | 排查复杂编译报错或实现编译器时精确查阅。 |

## 标准库、工具链与实战示例

* [标准库指南（Standard Library）](stdlib.md)：覆盖 `std`、`dictionary`、`vec`、`math`、`os`、`ffi`、`binary`、`base64`、`csv`、`json`、`regex`、`exception` 以及图形/音频/物理/UI 等全部 16 个标准模块。
* [命令行工具 `k`](command_line.md)：`k run`、`k build`、`k check`、`k fmt`、`k pkg`、`k lsp` 以及代码发射选项详解。
* [包管理与多目标构建](packages.md)：Git 仓库或本地目录依赖、`.build` 原生 C 集成、多目标产物缓存与包发布规范。
* [编辑器与 LSP 集成](editors.md) · [VS Code 扩展指南](vscode.md)
* [31 个精选实战示例](samples.md)：从语言导览到 JSON 解析器、多线程曼德勃罗集、旋转立方体与小游戏。
* [C++ 与 Rust 开发者迁移指南](cpp_rust_guide.md)：常见系统编程模式在 K 中的对应写法。
* [K, Byte by Byte（图解内存字节布局）](../html/K,%20Byte%20by%20Byte%20(zh).html)：单页互动图文，逐字节剖析数组族、内联布局与速度来源。

## 架构图解与编译器内部实现

* [Kense OS 架构与原理图解中心](diagrams.md)：全部 **18 张中文 Kense OS 架构图解**（数据栈、BCE、并发、基准、前后端、WASI、Cordis）。
* [K 语言架构设计](architecture.md)：限界上下文、资源所有权、Cordis 组件微内核与宿主目标装配。
* [编译器实现笔记](implementation.md)：逐阶段解析前端加载、类型检查根分析、优化器、边界检查消除（BCE）、代码生成与运行时。
* [发展路线图](roadmap.md) · [测试套件指南](testing.md) · [自举测试 `k_in_k`](k_in_k.md) · [文档站生成器说明](site.md)

## 性能与内存基准报告

* [基准总结报告（Summary）](bench/summary.md)：16 项大型工作负载对比惯用 C++、专家 C++ 与安全 Rust 的速度与峰值内存结论。
* [完整基准数值表（Results）](bench/results.md)：三档尺寸与 JIT 全量测速/内存数据明细。
* [基准设计原则](bench/design.md) · [诊断实验记录](bench/notes.md) · [迁移与采用评估](bench/adoption.md) · [优化计划](bench/plan.md)
* 存储池专题：[Pool 工作负载](bench/pool_workloads.md) · [Slice Pool](bench/slice_pool.md) · [Slot Pool](bench/slot_pool.md)

<a id="design-notes"></a>
## 设计笔记（34 篇）

记录 K 语言核心特性的权衡依据与前沿研究提案：

* 索引与边界证明：[索引类型与 `checkfree` 函数](design/index_types.md) · [编译器分析清单](design/analysis_inventory.md)
* 存储、所有权与布局：[独立所有权动态数据](design/owning_dynamic_data.md) · [作用域退出清理](design/scope_exit_cleanup.md) · [嵌套容器就地增长](design/nested_growth.md) · [根结构体](design/root_structs.md) · [结果就地构造](design/result_construction.md) · [列式存储（SoA）](design/soa.md) · [字符串常量与只读保护](design/string_constants.md) · [无析构函数所有权](design/ownership.md) · [相对引用](design/relative_references.md) · [序列化](design/serialization.md)
* 元编程与语言特性：[`const for`](design/const_for.md) · [命名空间](design/namespaces.md) · [调用点源码位置](design/call_location.md) · [标准库设计](design/stdlib_design.md) · [先驱工作对比](design/prior_art.md)
* 并发、异步与运行时：[异步函数与装饰器](design/async_and_decorators.md) · [协程](design/coroutines.md) · [子线程与并行循环](design/child_threads.md) · [一等函数与 TypeScript 运行时](design/functions_and_typescript_runtime.md)
* 系统集成、目标与 Cordis：[Cordis 微内核](design/cordis_microkernel.md) · [嵌入式语言块](design/embedded_languages.md) · [动态 `extern`](design/dynamic_externs.md) · [进程内 TinyCC 后端](design/jit_backend.md) · [WebAssembly 目标](design/wasm.md) · [构建与包设计](design/packages.md) · [语言服务器架构](design/language_server.md) · [源码级调试](design/debugging.md) · [面向 LLM 的可验证契约](design/llm_frontier.md)
* 多媒体标准库设计：[图形 `gfx`](design/gfx.md) · [音频 `audio`](design/audio.md) · [物理 `physics`](design/physics.md) · [界面 `ui`](design/ui.md)
