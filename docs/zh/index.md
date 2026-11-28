# K 编程语言文档中心

K 是一门兼顾内存安全与极致系统性能的编程语言：无需堆分配器、垃圾回收器或繁琐的生命周期标注（参见[中文主页](../../README.zh-CN.md)）。下面按学习与查阅顺序组织了核心中文指南，以及完整英文技术规范、标准库和编译器实现索引。

## 核心中文文档

| 文档 | 内容说明 | 适合场景 |
|---|---|---|
| [中文项目主页](../../README.zh-CN.md) | 语言设计理念、系统架构全览与从源码/安装包起步。 | 初次了解 K 语言、快速安装并运行第一个程序。 |
| [快速上手与核心特性](quickstart.md) | 数据栈内存模型、数组族、受检引用、`checkfree`、变长枚举、异步与隔离线程。 | 从 C++ / Rust / Go 切换过来，快速掌握 K 的独特语义与工程实践。 |
| [Kense OS 架构与原理图解](diagrams.md) | 18 张系统级矢量架构图：数据栈、BCE、并发模型、编译器流水线与 Cordis 微内核。 | 从系统结构角度直观理解 K 的内存布局与编译产物。 |
| [K 语言架构设计](architecture.md) | 领域边界、资源所有权、Cordis 微内核生命周期与跨目标组合方式。 | 深入了解编译器边界、宿主集成以及可扩展服务设计。 |
| [性能与内存基准总结](benchmarks.md) | 2026 年 10 月 16 项大型基准测速报告，对比 C++ 与安全 Rust。 | 评估 K 的实际加速比、峰值内存节省与编译器后端取舍。 |

## 语言深度手册（英文原版）

目前以下完整手册与源码级设计笔记保持与编译器同步更新的英文原版（可在顶部导航随时切换界面语言）：

* [Tutorial（完整教程）](../tutorial.md)：通过大量可编译示例系统讲解 K 的核心概念。
* [Language Reference（语言参考手册）](../reference.md)：逐特性查询语法、类型、存储规则、Ffi 与 C 互操作。
* [Language Specification（形式化语言规范）](../k_spec.md)：尺寸分类、构造规则、收缩规则、表示布局与编译模型。
* [Standard Library（标准库指南）](../stdlib.md)：覆盖 `std`、`dictionary`、`vec`、`math`、`os`、`ffi`、`json`、`regex` 及图形音频 UI 等 16 个内置模块。
* [K, Byte by Byte（图解内存字节布局）](../html/K,%20Byte%20by%20Byte.html)：单页互动图文详解 K 数组、内联树与数据栈字节布局。

## 工具链、编译器与示例

* [命令行工具 `k`](../command_line.md)：`k run`、`k build`、`k check`、`k fmt`、`k pkg` 与 `k lsp` 用法详解。
* [包管理与多目标构建](../packages.md)：Git 依赖解析、本地路径依赖、`.build` 原生 C 集成与离线缓存。
* [编辑器与 LSP 集成](../editors.md) · [VS Code 扩展](../../vscode/README.md)
* [编译器实现笔记](../implementation.md)：前端解析、类型/根检查器、优化器、BCE 证明器与 C 代码生成逐阶段解析。
* [测试套件指南](../testing.md) · [自举前端测试 `k_in_k`](../../test/k_in_k/README.md)
* [31 个精选示例程序](../../samples/README.md)
* [基准测试详细数据表](../../bench/results.md) · [基准套件设计原则](../../bench/design.md)
