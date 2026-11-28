<a id="the-physics-module-physics"></a>
# 物理模块（`physics`）

<!-- Adapted for K. -->

本文档介绍物理（physics）包的实现、测试覆盖范围以及剩余工作。关于 API 参考，请参阅 K 的 `docs/stdlib.md`；关于模块源码，请参阅 `physics.k`。该包曾存放在 K 仓库中，直到后来独立成为一个单独的包（参见该处的 `docs/design/packages.md`）。

<a id="what-it-is"></a>
## 它是什么

`import physics;` 为 K 程序提供了 Box3D（Erin Catto 开发的 3D 刚体引擎）：世界（world）、刚体（body）、球体、胶囊体、凸包、三角网格、高度场（height field）和烘焙复合体（baked compound）、九种关节、接触事件、传感器事件、碰撞（hit）事件、移动事件与关节事件、射线与形状投射（cast）、重叠查询、角色移动器（character mover），以及录制与回放。它的构建方式与 `gfx`（`docs/design/gfx.md`）相同，只是将 SDL3 替换为 Box3D：

    K program --extern fn--> k_phys_* (k_physics, native C) --> Box3D (static)

位于其间的封装层 `k_physics`（`src/`）是一次性编译而成的原生 C 静态库：`k` 会为 JIT 运行链接该库，并通过 `tcc_add_symbol` 将其函数交给 TinyCC（`AddPhysicsSymbols`，`src/jit.h`），而由生成的 C 代码构建的程序则链接同一个静态归档库。无论是生成的 C 代码还是 TinyCC，都不会看到任何 Box3D 头文件。

其 API 保持与 Box3D 一致，仅换用 K 命名——`b3Body_GetPosition` 对应 `physics::position(body)`——并且其定义结构体采用字段默认值取自 Box3D 自身默认值的写法，因此 `physics::BodyDef { body_type: physics::DYNAMIC,
position: p }` 即构成一份完整定义。该封装层涵盖了 `box3d.h` 的几乎全部内容以及 `collision.h` 中的几何体部分：共计约 500 个函数。

<a id="how-it-is-built"></a>
## 它是如何构建的

* **Box3D** 来自经过 SHA-256 校验的固定版本源码归档。该固定修订版本来自 2026 年 9 月。2026 年 6 月发布的 v0.1.0 版本在部分地方拥有不同的 API（此后重命名了回放播放器函数和质心 getter），且缺少自那之后所做的修复；下一个发布版本应当替换掉此固定版本。`CMakeLists.txt` 使用 `add_subdirectory` 引入它，从而略去其示例、测试和文档（这些仅为顶层构建的选项），并以单精度、静态库以及在所有平台上均使用 release C 运行时的方式构建：Box3D 在 debug 构建中会请求使用 debug 运行时，而这无法链接进使用 `/MT` 的程序中。Box3D 使用 C17 编写，除 C 运行时、libm 和线程库外无其他依赖；其 CMake 需要 3.22 及以上版本，更旧的 CMake 会走禁用（opt-out）路径。
* **作为包（package）**（`docs/design/packages.md`）：本仓库包含模块 `physics.k`、指名 CMake 目标 `k_physics` 与 `main_thread_only` 的旁车（sidecar）文件 `physics.build`、`src/` 下的封装层以及将其与 Box3D 一同构建的 CMakeLists。`import physics;` 会通过受祝福的表（`src/blessed.txt`）找到它。使用 `-DK_BUILTIN_LIBS=all` 配置的编译器会将其编译进编译器自身（`cmake/package.cmake`）：其函数通过 CMake 根据模块的 `extern fn` 生成的符号表进入 JIT 运行；而探测可执行文件的链接闭包（在编译器构建完成后从 CMake 的 file API 应答中读取，`cmake/linkclosure.cmake`）则成为响应文件 `build/pkg/physics/<config>/link-{cc,msvc}.rsp`，`k build` 使用该文件来链接程序（`k --physics-link msvc|cc` 会打印其路径）。未内置该包的编译器会在程序首次使用它时，通过 `src/native.h` 的包装工程将其构建到 `K_HOME` 中，并加载该次构建为 JIT 运行生成的共享库。

从 `K_BUILTIN_LIBS` 中省略 physics 会将其排除在编译器之外。物理程序依然可以通过类型检查并生成 C 代码，但随后运行或构建此类程序需要 CMake 和 C 编译器。首次构建时会解析并获取 Box3D。

<a id="the-compilers-side"></a>
## 编译器侧

编译器的集成工作很小：physics 没有任何可与 `embed_shader` 编译着色器相类比的编译期处理流程。

* **对封装层的调用**是 physics 包的 extern fn：代码生成阶段会记录程序通过 extern 调用触及的包（`NativePackagesUsed`），而 JIT 运行则从已编译内置的表或包的共享库中获取这些函数。
* **`thread_fn` 永远无法触及封装层**，这与 gfx 相同：封装层以进程为单位维护其错误文本、误用计数以及几何体表，且 Box3D 的世界创建并非线程安全。Box3D 会在步进（step）内部自行进行多线程处理，使用由 `WorldDef.workers` 请求的工作线程数量。

<a id="the-layer-src"></a>
## 封装层（`src/`）

`physics_api.h` 集中列出了每一个函数（`K_PHYS_API`）和每一个常量（`K_PHYS_CONSTANTS`）；`test/api_check.py` 会检查 `physics.k` 是否恰好声明了完全相同的函数、相同的 C 形态、字段顺序一致的结构体以及相同的常量值，与对 gfx 所做的检查相同。

* **结构体使用 K 的内存布局。** Box3D 的定义结构体带有指针、名称和回调函数，且采用自然对齐；封装层的结构体则是紧凑打包（packed）的，只保存跨越边界的数据，并按字段逐一转换到 Box3D 的默认值（`b3Default*Def`）之上，从而使其内部 cookie 及未暴露的字段保留 Box3D 的默认值。`default_*_def()` 会返回 Box3D 的默认值，测试则会将它们与 K 的字段默认值逐一比对。指针会变为句柄或独立参数：名称通过 `set_name` 设置，用户数据则是 `u64`。
* **只有与 TinyCC 和原生 C 调用约定均兼容的结构体才会按值传递。** 定义、变换和结果结构体均按值传递和返回，这在 K 一侧十分简洁。TinyCC 将不超过 16 字节的结构体作为一个整体进行分类，而 System V ABI 则按每 8 字节（eightbyte）分别分类，因此一半是浮点数、另一半是整数的小结构体会被放进错误的寄存器；`test/api_check.py` 会拒绝此类按值跨越边界的结构体，也会拒绝包含未对齐字段的结构体。所有大于 16 字节的结构体在任何 ABI 上都通过内存传递。
* **句柄（Handle）** 就是 Box3D 自身的 ID，完整存储：`World` 为 `u32`，`Body`、`Shape` 和 `Joint` 为 `u64`（`b3Store*Id`），`Contact` 为 Box3D 的三个字（word）。每个句柄都带有代数（generation），且每个入口点都会在 Box3D 接触它们之前用 `b3*_IsValid` 检查其 ID，因此使用已销毁的对象属于误用（misuse），而不会导致崩溃——无论是在 Box3D 的 release 构建还是 debug 构建中皆如此。每种关节各自拥有独立的句柄类型（`RevoluteJoint { joint: Joint }`），这使得在移动关节（prismatic joint）上调用旋转关节（revolute）函数成为编译错误；封装层仍然会检查关节种类，以防句柄是手动构造的。
* **由封装层管理的几何体**——凸包、网格、高度场、复合体、录制记录、回放播放器——存放在各自带代数的槽位表中，方式与 gfx 的资源相同。Box3D 会将凸包复制到使用它的每个世界中，但网格、高度场和复合体形状会继续指向创建它们时所用的数据：销毁这类几何体会立即释放其句柄，而底层数据则会在不再有任何形状使用它时释放（通过 `phys_sweep`，在任何会销毁形状的操作之后执行）。正在被某个世界写入的录制记录会在销毁前先停止。
* **回调函数变为数组。** 事件与查询结果会被复制到程序提供的切片中，调用返回实际数量；K 封装函数会调用两次，第二次传入足以容纳全部结果的空间。射线与形状投射会收集全部命中结果并按最近优先排序，距离相同时按形状排序，以保证在任何地方都得到相同的顺序。Box3D 中原本通过回调实现的摩擦力和恢复系数混合，在封装层中改为六种规则之一（`MIX_*`），每种规则对应封装层中的一个 C 函数。
* **错误处理**遵循 gfx 的做法：程序控制之外的失败会返回 false 或零句柄，并将原因记录在 `error()` 中；误用调用则会被跳过、在发生时打印出来并计数，随后 K 侧会在下一次 `step()`、`destroy(world)` 或 `check()` 时携该原因中止。封装层会检查那些 Box3D 原本只会做断言的条件：句柄有效性、关节种类、是否属于同一世界中的刚体、仅限静态（static-only）的形状、索引与数量范围、网格索引是否落在顶点范围内。Box3D 自身的断言在 debug 构建中处于激活状态，它们同样会被路由到这一误用处理路径，而不是中断进入调试器；其日志输出到 stderr，绝不会混入程序的 stdout。

以下特性未暴露，因为它们各自需要回调到 K 代码中，或者属于 Box3D 自身的工具功能：自定义接触过滤器与预求解（pre-solve）回调、调试绘制、独立的动态树（dynamic tree）、底层碰撞对函数，以及回放播放器的查询检查功能。

<a id="the-k-module-physicsk"></a>
## K 模块（`physics.k`）

命名空间 `physics` 包含：常量、句柄、带有 Box3D 默认值的定义结构体、`extern` 声明、薄封装函数（将事件、查询和列表作为新数组返回；`name()`；将两个结果作为两个返回值），以及四元数与变换辅助函数（`quat_axis_angle`、`mul`、`rotate`、`transform_point`、`inv_mul`、`local_frame`、`joint_def`）。名称避开了模块内部带命名空间声明会遮蔽的全局名称（使用 `rest_length` 而非 `length`，使用 `shape_filter` 而非 `filter`）以及 K 关键字（`body_type`、`shape_type`）。

<a id="testing"></a>
## 测试

* **`test/physics/`** 位于常规测试套件中：在编译器内置了该封装层时，以 AOT（-O0 和 -O2）以及通过 TinyCC 运行；未内置时则仅进行类型检查并生成 C 代码：
  `physics_world`（库本身、默认值、设置项、休眠、统计信息、一次爆炸效果，以及同一堆叠体在使用一个和四个工作线程步进后输出完全一致）、`physics_bodies`、`physics_shapes`（每一种形状与几何体、过滤器的实际效果、通过 Box3D 字节计数测得的网格延迟释放）、`physics_joints`（全部九种关节，各自完成其应有功能）、`physics_queries`（世界查询、角色移动器、单独使用的几何体、旋转辅助函数）、`physics_events` 和 `physics_recording`（将录制保存、加载、校验并回放至完全相同的最终变换）。这些测试调用了封装层全部 502 个函数中的 502 个。`physics_misuse` 检查已销毁的刚体会在下一次步进时携原因中止程序，而 `physics_err_thread` 检查在 `thread_fn` 中触及 physics 会产生编译错误。
* **`samples/28_physics_boxes.k`** 配合 gfx 模块向竞技场中倾泻数千个箱子，并在示例运行器中以无头模式在 JIT 与 AOT 下运行 120 帧、共 3,000 个箱子。

Box3D 在不同平台和工作线程数量之间具有确定性，且封装层在传递过程中不引入任何自身的浮点运算，因此测试会打印保留至百分位或千分位的物理结果。

<a id="platforms-tested"></a>
### 已测试平台

| | Windows 11 | Linux (WSL2 下的 Ubuntu 24.04) | macOS |
|---|---|---|---|
| 构建 | MSVC | clang++ 编译编译器，gcc 编译 C 代码 | 未运行 |
| 测试套件（含 `test/physics/`） | MSVC, TinyCC | gcc, clang, TinyCC | 未运行 |
| 示例 | JIT, cl | JIT, gcc | 未运行 |

在 Windows 上，使用 clang 和 clang-cl 构建的物理测试与示例也与预期输出一致：Box3D 在每一种工具链上、以及在使用一个或四个工作线程时，输出都精确到最后一位打印数字。与 gfx 一样，尚未在 macOS 上运行；上述 ABI 规则针对 System V x86-64 与 Windows x64 推演得出，在 arm64 上尚未验证。

<a id="follow-up-work"></a>
## 后续工作

* 将固定提交（pinned commit）替换为 Box3D 的下一个发布版本。
* 通过封装层提供调试绘制，向 K 传递用于 gfx 绘制的线段与形状，并为任意世界提供 K 侧的立即模式（immediate-mode）渲染器。
* 自定义接触过滤器与预求解回调，这需要一种由 C 调用 K 函数的机制。
* 将动态树作为游戏数据的空间索引。
* 支持在 `thread_fn` 中使用 physics（每个线程一个世界），前提是封装层的状态改为线程局部且 Box3D 的世界创建完成串行化。
