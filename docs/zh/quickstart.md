# 快速上手与 K 语言核心特性

本指南面向希望快速安装 K 编译器，并在 10 分钟内掌握 K 语言与 C++、Rust、Go 关键差异的开发者。如需逐行完整教材或规范细节，可随时参阅英文原版 [Tutorial](../tutorial.md)、[Language Reference](../reference.md) 与 [Specification](../k_spec.md)。

---

## 1. 一键安装工具链

### macOS / Linux

```bash
curl -fsSL https://raw.githubusercontent.com/kense-labs/klang/main/scripts/install.sh | bash
```

### Windows (PowerShell)

```powershell
irm https://raw.githubusercontent.com/kense-labs/klang/main/scripts/install.ps1 | iex
```

验证安装：

```bash
k --version
```

---

## 2. 运行、编译与发布命令

命令行工具 `k` 将即时运行、优化编译、格式化、包管理与语言服务器整合在统一入口中：

| 命令 | 用途 |
|---|---|
| `k hello.k` / `k run hello.k` | 通过 C/TinyCC 路径快速编译并直接运行程序 |
| `k build -O3 --lto --cpu=native app.k` | 开启最高级别优化、链接时优化与本机指令集构建发布产物 |
| `k build --target wasm32-wasip1 app.k` | 编译为 WASI Preview 1 WebAssembly (`.wasm`) 模块 |
| `k check app.k` | 执行完整类型、生命周期与 `checkfree` 静态校验（不生成可执行文件） |
| `k fmt app.k` | 按官方规范格式化 `.k` 源码 |
| `k pkg list` | 查看与解析当前项目依赖与锁定图 |

---

## 3. K 语言最核心的 5 项设计差异

### 3.1 数据栈（Data Stacks），而非垃圾回收或手写释放

在 K 中，固定大小标量与结构放在普通调用栈帧中；长度可增长或尺寸可变的数据（`String`、动态数组 `[T]`、变长 `enum`）存放在与词法作用域绑定的**地址稳定数据栈**中：

- 无需扫描全局堆的 GC 停顿；
- 无需像 Rust 那样在所有接口签名上手写生命周期参数 `'a`；
- 在数组增长时（依托分段数据栈），已有元素的引用依然保持地址稳定（详情见[图解数据栈](diagrams.md)）。

### 3.2 数组家族与显式存储控制

K 把容器按照所有权与可变性清晰划分为多个轻量形态：

- `[T]`：可增长、独占拥有的动态数组（存放在当前作用域数据栈）；
- `[]T`：不拥有存储的连续借用切片视图；
- `[N]T`：固定长度栈上数组；
- 通过 `return ... from` 直接把结果构建在调用方目标槽位上，避免构建期大对象多次搬运。

### 3.3 类型化索引与 `checkfree` 编译期无越界检查

性能敏感的内循环（如图遍历、图像卷积、字节解析）不应反复在运行期检查下标越界。K 允许你把索引约束提升到类型系统：

- `u32 in graph.dist` / `u32 upto table.len` 将合法范围写进变量类型中，在数据入口一次性验证；
- 为热路径函数标上 `checkfree fn`，编译器的边界检查消除器（BCE）会验证函数体内**所有数组下标都已在编译期证明安全**；哪怕漏掉一处证明，编译也会直接拒绝通过。

### 3.4 按载荷布局的变长 `enum`

传统语言中的 Tagged Union 往往不得不按最大变体对齐，导致大量小节点或内联字符串浪费数倍内存。K 的可变模式 `enum` 允许每个实例只占用自己载荷所需的紧凑空间——这也是 [基准报告](benchmarks.md) 中 `records` 与 `interp` 内存大幅低于 Rust 的关键原因。

### 3.5 结构化 `async` 与零共享工作线程

- `async fn` / `await`：编译为显式状态机任务帧，并在词法任务作用域结束时确保全部子任务取消与排空（原生与 WASI 均可用）；
- **原生工作线程**：每个线程拥有彼此隔离的私有数据栈，仅允许通过**类型化通道**转移所有权值，语言层彻底消除跨线程数据竞争。

---

## 下一步阅读

- 浏览全部 [18 张 Kense OS 架构图解](diagrams.md)
- 查看 [31 个精选实战示例](../../samples/README.md)
- 查阅 [完整英文教程](../tutorial.md) 与 [性能基准报告](benchmarks.md)
