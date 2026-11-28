<a id="the-graphics-module-gfx"></a>
# 图形模块（`gfx`）

<!-- Adapted for K. -->

本文档介绍可选图形模块的实现、测试覆盖范围以及剩余工作。关于 API 参考，请参阅 K 的 `docs/stdlib.md`；关于模块源码，请参阅 `gfx.k`。

对于**文本渲染、字体、标签和游戏 HUD**，请使用现有的 [`ui` 模块](../stdlib.md#text-rendering-and-game-huds)：`ui::label` 通过布局放置文本，`ui::draw_text` 将其绘制到窗口的画布上，而 `ui::render` 将其绘制在 gfx 场景之上。普通的游戏文本不需要直接基于 gfx 构建新的字体图集或文本渲染器。自定义精灵合批（sprite batching）和使用自有位图美术资源的游戏则是另外的关注点。

<a id="what-it-is"></a>
## 它是什么

`import gfx;` 为 K 程序提供了窗口、输入、GPU 缓冲区与纹理、图形与计算管线、渲染与计算通道（pass）以及结果回读功能。其底层是 SDL3 的 GPU API（SDL_GPU），通过 Direct3D 12、Vulkan 或 Metal 进行绘制。着色器使用 GLSL 编写，在编译程序时由内置函数 `embed_shader` 编译进程序中：既可以在着色器阶段名称之后以 `"""` 字符串形式内联编写（`embed_shader("frag", """ ... """)`），也可以放在外部文件中（`embed_shader("file.frag")`）。

    K program --extern fn--> k_gfx_* (k_gfx, native C) --> SDL3 (static)

位于 K 与 SDL 之间的封装层 `k_gfx`（`src/gfx/`）是一个原生编译的静态库，而不是拼接到生成的 C 代码中的运行时头文件：

* 生成的 C 代码与 TinyCC 均完全不会看到任何 SDL 头文件。JIT 运行只需通过 `tcc_add_symbol` 为其定义该层自身的函数即可：`src/gfx/gfx_api.h` 中的 64 个函数，而不是 SDL 的 1,270 个函数。
* 需要平台头文件的代码（例如调用 `D3DCompile`）保留在原生代码中。
* JIT 与 AOT 执行的是同一个已编译封装层：JIT 运行使用链接进 `k` 的那一份副本，AOT 程序则链接同一份静态归档库。

<a id="how-it-is-built"></a>
## 它是如何构建的

* **作为包（package）**（参见 K 的 `docs/design/packages.md`）：本仓库包含 `gfx.k`、`audio.k` 和 `ui.k` 三个模块，指名 CMake 目标 `k_gfx_package` 与 `main_thread_only` 的旁车（sidecar）文件 `gfx.build`，位于 `src/` 下的三个实现层，以及将它们与 SDL3 和 Nuklear 一同构建的 CMakeLists。这三者组成同一个包，因为它们共享 SDL 且 ui 通过 gfx 进行绘制；`import gfx;`、`import audio;` 和 `import ui;` 会通过受祝福的模块表（`src/blessed.txt`）找到它。使用 `-DK_BUILTIN_LIBS=all` 配置的编译器会将其编译进编译器自身（`cmake/package.cmake`）：各层的函数通过 CMake 根据模块的 `extern fn` 生成的符号表进入 JIT 运行；而探测可执行文件的链接闭包（在编译器构建完成后从 CMake 的 file API 应答中读取，`cmake/linkclosure.cmake`）则成为响应文件 `build/pkg/gfx/<config>/link-{cc,msvc}.rsp`，`k build` 使用该文件来链接程序（`k --gfx-link msvc|cc` 会打印其路径）。未内置该包的编译器会在程序首次使用它时，通过 `src/native.h` 的包装工程将其构建到 `K_HOME` 中，并加载该次构建为 JIT 运行生成的共享库。
* **SDL3** 来自经过 SHA-256 校验的固定版本源码归档。该包的 CMakeLists 将其构建为静态库，禁用其 2D 渲染器、OpenGL 和摄像头支持，因为 SDL_GPU 均不需要这些功能。`SDL_DEPS_SHARED` 保持开启，因此在 Linux 上 SDL 仅链接 C 标准库，并在运行时动态加载 X11、Wayland、音频和 udev。
* **Windows 上的静态 C 运行时**（在所有配置中均为 `CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded`）：编译器、TinyCC、SDL 与封装层共享普通 `cl game.c` 所链接的同一运行时，因此程序可以链接同一套静态归档库。`k.exe` 直接导入 KERNEL32 和 ADVAPI32；SDL 额外引入的 9 个系统 DLL 采用延迟加载（delay-loaded），因此不打开窗口的编译器运行不会加载它们。编译时间未受影响。
* **`cute_spirv`**（`third_party/cute_spirv`，复制自 cute_framework，参见其 README）将 GLSL 编译为 SPIR-V，并将其转译为 HLSL 和 MSL。无论是否启用 SDL，它都作为 `k_shaderc`（`src/shaderc.c`）被编入每一个编译器中，因为着色器编译属于图形功能中编译器一侧的工作。它输出的二进制块（blob）是编译器（`src/gfx_blob.h`）与包的封装层之间的契约；封装层自带该头文件的一份副本（`src/gfx/gfx_blob.h`）并校验 blob 的版本。

**无图形支持构建**：当 `K_BUILTIN_LIBS` 不包含 gfx，或 Linux 机器缺少 X11 和 Wayland 开发文件时（在这些机器上，SDL 自身的 configure 会静默构建出一个无法打开窗口的 SDL，而本包的 CMakeLists 会提示需要安装哪些包并跳过构建），该包不会被编译进编译器，编译器的 configure 阶段也会对此给出说明。无论哪种情况，gfx 程序都能照常通过类型检查并生成相同的 C 代码，着色器也仍会编译；但随后运行或构建此类程序则需要 CMake 和 C 编译器。首次构建时会解析并获取 SDL 和 Nuklear。

<a id="the-compilers-side"></a>
## 编译器侧

* **`embed_shader`** 是一个内置函数，其实参为字符串字面量，或以此类字面量初始化的 `let` / `const` 全局变量——检查器会将其替换为对应的字面量。`embed_shader("frag", source, ...)`（`"vert"`、`"frag"` 或 `"comp"`）编译写在程序中的 GLSL，各部分按行拼接，其中的 `#include` 相对于包含该调用的文件路径解析；`embed_shader("x.vert")`（`.vert`、`.frag` 或 `.comp`）编译着色器文件，文件本身相对于该源文件解析，其中的 `#include` 则相对于着色器文件解析。检查器对每个不同的着色器仅编译一次（`TypeCheck::EmbedShader`、`src/gfx.h`、`src/shaderc.c`），将生成的 blob 保存在 `Ast::shaders` 中，并在调用节点上保留指向其 blob 的指针供代码生成使用。除非着色器编译器指出具体的源码行号，否则编译失败会报告在调用处。对于写在程序中的 GLSL，该行号会直接映射到 `"""` 字符串（`StrLit::multiline`）中对应的那一行；若源码来自全局变量，错误信息还会指明编译它的调用处。着色器文件中的错误会包含着色器自身的 `file:line`。该调用的返回值是一个指向静态数据的 `const u8[:]`（与字符串字面量类似），代码生成阶段会将其发射为字节初始化列表。
* **blob**（`src/gfx_blob.h` 及封装层持有的同份副本，由 `shaderc.c` 与封装层共享）同时打包保存 SPIR-V、MSL 和 HLSL，从而使生成的 C 代码可在任何平台构建，此外还包含 SDL_GPU 所需的反射信息：采样器、存储纹理及存储缓冲区（只读与读写）的数量、每个 uniform 块的 std140 大小、按 location 排列的顶点输入，以及计算着色器的局部工作组大小（local size）。
* **SDL_GPU 的绑定契约在编译期进行检查。** 每种资源都必须放置在 SDL_GPU 为其种类和着色器阶段分配的描述符集（set）中，按采样器、存储纹理、存储缓冲区的顺序从 0 开始编号。位置错误的资源在任何后端都无法工作，或仅在部分后端生效；在这里会直接作为编译错误报出，并指明具体违反的规则（`test/gfx/gfx_err_shader_binding.k`）。
* **对封装层的调用**是 gfx 包的 extern fn：代码生成阶段会记录程序通过 extern 调用触及的包（`NativePackagesUsed`），而 JIT 运行会在重定位程序之前从已编译内置的表或包的共享库中获取这些函数。
* **`thread_fn` 永远无法触及封装层**：SDL_GPU 与窗口系统仅限主线程使用，线程程序调用 gfx 会产生编译错误。

<a id="the-layer-srcgfx"></a>
## 封装层（`src/gfx/`）

封装层为 K API 处理 SDL_GPU 的资源管理与校验规则。`gfx_api.h` 集中列出了每一个函数（`K_GFX_API`）和每一个常量（`K_GFX_CONSTANTS`）；`test/api_check.py` 会检查 `gfx.k` 是否恰好声明了完全相同的函数、相同的 C 形态、字段顺序一致的结构体以及相同的常量值。

* **句柄（Handle）** 是一个单独的 `u32`：包含槽位索引（slot index）与代数（generation），因此使用已释放的句柄会报错，而不会崩溃。
* **错误分为两类。** 程序控制之外的失败（没有 GPU、文件缺失）会返回 false 或零句柄，并将原因记录在 `gfx::error()` 中。程序本不应发起的调用属于*误用（misuse）*：该调用会被跳过、在发生时打印出来并计数，随后 K 侧会在下一次 `frame()`、`check()`、回读或 `close()` 时携原因中止。SDL_GPU 自身的校验断言也被路由到同一处理路径，而非弹出 SDL 对话框。绘制（draw）与派发（dispatch）调用会检查已绑定管线的着色器所读取的每一个采样器、存储资源、uniform 块和顶点缓冲区是否都已提供，且推送（push）的 uniform 块大小必须等于着色器中的 std140 大小。
* **屏幕（screen）** 是一张 RGBA8 纹理，而非交换链（swapchain）：每帧都绘制到该纹理中，再由 `frame()` 将其位块传输（blit）到窗口。如此一来，管线永远不会依赖交换链格式，屏幕截图与屏幕回读就是普通的纹理下载操作，最小化窗口（此时没有交换链图像）也只需跳过 blit 即可。无头设备（`open_headless`，或在 `K_GFX_HEADLESS=1` 下调用 `open`）同样拥有该屏幕纹理，只是没有窗口。屏幕的深度纹理在首次调用 `screen_depth()` 时创建（`begin_screen` 会调用它），并在之后随屏幕一同重建；使用 `begin_pass(screen(), Texture { 0 }, clear)` 进行绘制的 2D 程序则不会创建深度纹理。清除屏幕或其深度纹理不会对其执行轮转（cycling）：上一帧几乎总是在继续使用它们，因此轮转会导致每种纹理各自保留两到三份窗口大小的副本。
* **按需绘制（Drawing on demand）** 对应 `WAIT_WHEN_IDLE` 打开标志。不带该标志时，每次 `frame()` 都会呈递（present）。带上该标志后，仅当自上一帧以来有操作使用了 GPU（如通道、拷贝或上传），或窗口需要重新显示图像（曝光、显示、恢复或回到前台）时，`frame()` 才会呈递。在一帧未呈递任何内容之后，下一次 `frame()` 会等待事件到来，最长等待一秒以便程序的定时器仍能运行（调用 `quit()` 之后除外）；`delta_time()` 不计入这段等待时间，因此空闲期间运行的定时器应读取 `time()`。窗口尺寸变化会生成一张新的黑色屏幕纹理，因此程序应在 `screen_size()` 变化时重新绘制。当窗口无法提供图像时（例如窗口最小化，或 Android 正在重建其表面），呈递操作会最多重试 8 帧，之后在每次等待后重试直至成功。在 Android 上，该标志会关闭 SDL 的轮询哨兵（poll sentinel），否则每次等待都会立即结束。
* **设备丢失（lost device）会结束程序循环。** 一旦 Vulkan 报告 `VK_ERROR_DEVICE_LOST`，随后的每一次等待和提交都会立刻失败，导致每帧都绘制的程序陷入空转自旋。gfx 会打印首次此类失败，并从此使 `frame()` 返回 false，效果等同于窗口已关闭。Direct3D 12 的设备移除（device removal）与 Metal 的设备丢失目前尚未识别，因为 SDL 对它们的报错表述不同。
* **执行解析（resolve）的通道是其多重采样目标的最后一个通道。** 执行解析的通道会丢弃其多重采样颜色与深度目标的采样数据而不进行存储（store），使分块渲染（tiling）GPU 无需将其写回内存。后续若有通道对这些目标执行加载（load）而非清除（clear），则属于误用。不执行解析的通道会存储它们，因此一帧可以跨越多个通道绘制并在最后一个通道中解析。多重采样目标在清除时不会执行轮转。
* **为其他渲染器提供窗口**对应 `WINDOW_ONLY` 打开标志：gfx 会打开窗口并维护其输入、事件、光标、剪贴板、时间及 `WAIT_WHEN_IDLE` 的等待逻辑，但不创建 GPU 设备，任何需要 GPU 设备的调用都会构成误用。`native_window()` 是渲染器绘制所用的原生句柄：在 Apple 系统上是 `CAMetalLayer`，在 Windows 上是 `HWND`，在 Android 上是 `ANativeWindow`，且每次应用回到前台时都会分配一个新的句柄（`native_window()` 会重新读取它；处于后台时为 0），因此渲染器此时需重建其表面（surface）；在 Linux 上则是 `wl_surface` 或 X11 窗口，同时 `native_display()` 返回其 `wl_display` 或 `Display*`，`native_kind()` 返回 `NATIVE_WAYLAND` 或 `NATIVE_X11`；无头模式下均为 0。其他视频驱动或平台会使 `open` 失败。`screen_size()` 以像素为单位跟随窗口大小。配合 `WAIT_WHEN_IDLE` 时，渲染器在每帧通过 `presented(shown)` 报告是否显示了内容，未显示任何内容的帧会使下一次 `frame()` 进入等待；在窗口曝光、显示、恢复或调整大小后的几帧内，`needs_present()` 会保持为 true，直到渲染器报告有一帧已显示。未设置 `WINDOW_ONLY` 时调用 `presented()` 属于误用。程序可借此标志在运行时自行选择渲染器。
* **一切皆以像素为单位。** 窗口始终向系统请求显示器的完整像素密度，因此在高密度显示器上，其屏幕的像素数会大于显示器坐标系下的尺寸（而 SDL 的鼠标坐标正是基于后者给出）：鼠标位置与移动增量会先乘以窗口像素尺寸再除以坐标系尺寸（使用 double 计算），从而使整像素坐标计算结果仍为整数。注入的鼠标输入本身就已经是以像素为单位的。
* **缩放比例归程序所有。** `pixel_density()` 和 `display_scale()` 直接报告 SDL 给出的窗口参数，gfx 和 ui 均不会自动对其采取行动：人眼观看屏幕的距离差异过大，系统报告的缩放比例并不足以决定 UI 应有多大，许多游戏转而按屏幕高度来缩放界面。确实希望采用系统缩放尺寸的程序可将 `display_scale()` 传给 `ui::set_scale`。
* **第一人称输入**使用 SDL 的每窗口相对鼠标模式（relative mouse mode）。开启它会隐藏光标并提供连续的像素增量。焦点变化既可查询，也会作为有序事件发出。失去焦点会清除按住的输入与移动增量、释放相对模式并显示光标；重新获得焦点时绝不会擅自重新捕获鼠标，除非程序主动调用。无头实现保持相同的逻辑状态，并通过注入焦点事件实现确定性测试，而不会改变桌面光标。
* **系统光标**通过 `set_cursor(CURSOR_POINTER)`、`set_cursor(CURSOR_TEXT)` 或其他与 [SDL_SystemCursor](https://wiki.libsdl.org/SDL3/SDL_SystemCursor) 匹配的 `CURSOR_*` 常量来选择。固定版本的 SDL 支持从 `CURSOR_DEFAULT` 到 `CURSOR_W_RESIZE` 的 20 种光标形状。调用失败时返回 false，详情记录在 `error()` 中。该调用保持光标可见性与相对模式状态不变。无头模式下的调用会校验形状参数而不触及桌面光标。`close()` 会恢复默认光标并释放已缓存的光标形状。
* **游戏手柄（Gamepad）** 按连接顺序占用 `0` 到 `MAX_GAMEPADS - 1` 的槽位。SDL 的手柄子系统随视频子系统一同打开，没有手柄子系统的机器只会表现为没有手柄，而不会导致 `open` 失败。按键状态通过 SDL 事件以按住状态加上最近一次 `frame()` 的边沿变化（edge）来维护，与键盘按键一致；断开连接的手柄会释放其按住的按键。摇杆轴与震动（rumble）直接透传给 SDL，不设死区（deadzone）。SDL 会每隔一毫秒读取一次已打开的手柄，这会持续唤醒 `WAIT_WHEN_IDLE` 的等待，因此开启该标志时 gfx 改为在每次 `frame()` 读取手柄，并且在有手柄打开且窗口拥有焦点时，空闲等待缩短为 16 ms 而非一秒，从而使摇杆轻微移动能像持续绘制时那样及时响应。
* **全屏（Fullscreen）** 采用无边框窗口模式，分辨率与桌面一致，且 SDL 异步执行切换：`fullscreen()` 的状态可能会比 `set_fullscreen` 滞后一两帧，而屏幕纹理会像对待任何尺寸变化一样跟随窗口更新。
* **管线针对其渲染目标创建。** 管线描述结构体本身不指明目标格式。封装层会在管线首次与每种颜色格式、深度格式和采样数的组合搭配使用时创建对应管线。
* **顶点布局来自着色器**：其输入按 location 顺序依次紧密排布，与 K 结构体排布字段的方式一致；在 `instance_location` 之前为来自缓冲区 0 的逐顶点数据，从 `instance_location` 起为来自缓冲区 1 的逐实例数据。`PipelineDesc.formats` 可覆盖某个 location 的格式，例如将字节表示作为颜色使用。
* **拷贝按程序顺序执行。** 新资源的初始内容通过独立命令缓冲区上传；其他任何上传、回读或 mipmap 生成均记录在当前帧的命令缓冲区中，因此发生在各通道之间。重写整个缓冲区时，封装层允许 SDL 对其进行轮转（cycle），而不等待仍在使用旧数据的绘制完成。回读操作会立即提交并等待完成。
* **小块上传共享同一个缓冲区。** 在 Direct3D 12 上，每次创建与释放传输缓冲区至少耗费零点几毫秒，因此不超过 4 MB 的上传会依次打包进一个与设备生命周期相同的 4 MB 缓冲区中。每次上传取下一段空闲区域，映射时不启用轮转，因为尚未分配出去的区域不可能被任何在途（pending）操作读取。当下一个上传放不下时，以轮转方式重新映射该缓冲区，并从偏移 0 重新开始填充。纹理上传按 512 字节边界对齐（Direct3D 12 的放置对齐要求，否则 SDL 会再次复制数据），缓冲区上传按 16 字节对齐。更大的上传则会获得各自独立的缓冲区。
* **压缩纹理**（`BC1`、`BC3`、`BC7`、`ASTC_4x4` 到 `ASTC_8x8`）仅支持采样，且每次必须整层 mip 上传（即区域参数中 `w` 为 0），按完整块紧密排布：Vulkan 要求行长必须是块大小的整数倍，而某一级 mip 的宽度未必如此。压缩纹理不能被回读，也不能对其调用 `generate_mips`。`format_supported` 用于向设备查询是否支持对某种格式进行采样：BC 属于桌面 GPU，ASTC 属于移动或 Apple GPU。
* **着色器**：设备能用 SPIR-V 时优先采用 SPIR-V，否则采用 DXBC，再否则采用 MSL。DXBC 是在加载时通过按名称加载的 `d3dcompiler_47.dll`（随 Windows 10 和 11 自带）将 blob 中的 HLSL 编译得到的。每个 blob 只会创建一次对应的 SDL 着色器对象。
* **调试模式**（SDL 自身的校验，以及系统已安装的平台校验层）在封装层的 debug 构建、启用 `DEBUG` 打开标志或设置 `K_GFX_DEBUG=1` 时开启。

<a id="the-k-module-gfxk"></a>
## K 模块（`gfx.k`）

命名空间 `gfx` 包含常量、句柄与描述结构体、`extern` 声明以及薄封装函数。描述结构体带有默认值，并以按值传递的方式传入封装函数，再由封装函数按引用向下传递，因此 `gfx::pipeline(vs, fs, gfx::PipelineDesc { depth_test: true })` 可以直接工作。`uniforms(stage, slot, value)` 可推送任意扁平结构体；回读辅助函数会从要追加写入的数组中推断元素类型（`read_buffer(b, floats, n)`），`read_pixels(t)` 则返回纹理的字节数据。列主序 `mat4` 辅助函数（`perspective`、`ortho`、`look_at`、`rotate`、`mul`）生成适用于 SDL_GPU 裁剪空间的矩阵。K 的 `docs/stdlib.md` 为权威参考文档。

<a id="testing"></a>
## 测试

`test/gfx/gfx_mouse_capture.k` 检查鼠标捕获/可见性状态转换、焦点丢失与重新获取、释放时的边沿变化与事件、忽略后台输入以及无头模式下的关闭/重新打开。在具备显示器和 GPU 的机器上，`python packages/gfx/test/window/run_input_test.py --exe <k>` 作为补充测试：通过一个隐藏的真实窗口验证 SDL 模式/可见性设置器、无关窗口焦点过滤、双击清理、关闭后光标恢复，以及在 `WAIT_WHEN_IDLE` 下 `frame()` 在未绘制的帧之后会等待直到事件到来、而在绘制过的帧之后不会等待；借助 SDL 的虚拟手柄，验证在 `frame()` 前松开的按键仍会计入、有手柄打开时等待时间较短、轻微摇杆移动与按键在下一帧即可显现；以及验证 `WINDOW_ONLY` 能提供原生窗口、窗口尺寸、输入和等待行为。物理鼠标限制范围（confinement）和 Alt-Tab 切换仍需在各平台上进行交互式检查。

* **`test/gfx/`** 位于常规测试套件中：这些无头程序将内容渲染到纹理并回读结果（包括几何体、深度、实例化、基于存储缓冲区的顶点拉取、混合、MSAA 解析；2D、3D、立方体贴图和数组纹理、局部区域、mipmap、浮点纹素、渲染到立方体贴图面）、计算着色器（存储缓冲区、存储纹理、原子操作、在计算着色器中采样纹理）、帧控制与注入输入、带键名的有序输入事件与无头剪贴板，以及按预期中止的运行时误用测试。它们仅在像素边界上绘制，因此输出在每个后端上都是精确一致的。只要编译器带有封装层，这些程序就会以 AOT 和 JIT 方式构建并运行；没有 GPU 设备的机器会报告将其跳过（程序打印 `gfx: no GPU device`）。它们的着色器直接内联写在程序中。除此之外，测试中还有检查同一着色器无论来自文件、内联编写在程序中还是分段提供都会编译为相同 blob 的测试、各类拒绝测试（无法编译的着色器，其报错必须精确落在程序中出错的那一行，包括由全局变量持有的片段；绑定在错误描述符集中的资源；缺失的文件；未知扩展名或阶段名；未指定阶段就提供源码；将局部变量作为实参；在 `thread_fn` 中调用 gfx）、针对为此保留的着色器文件测试隐藏标志 `--compile-shader` 的探测测试，以及 API 一致性检查。整个测试套件都会设置 `K_GFX_HEADLESS=1`，因此不会打开任何窗口。
* **`samples/27_gfx_cube.k`** 在示例运行器中以无头模式运行 30 帧。
* **`test/window/`** 不在常规测试套件中，因为它需要显示器：其中包含一个在单一场景中使用了大量 API 特性的展示程序（阴影贴图、多重采样 HDR 通道、由计算着色器驱动的实例与粒子、立方体贴图天空盒、2D/3D 和数组纹理、sRGB PNG 往返读写、线框模式、线段绘制、混合、视口与裁剪矩形、色调映射），通过回读自检并保存截图。`run_window_test.py` 会以当前机器支持的每一种方式运行该程序，并跨运行方式比对截图。

<a id="platforms-tested"></a>
### 已测试平台

| | Windows 11 (RTX 3080) | Linux (Ubuntu 24.04，WSL2 下，WSLg) | macOS 26 (M1 Pro) |
|---|---|---|---|
| 构建、禁用构建（opt-out） | 是 | 是 | 是 |
| 测试套件（含 `test/gfx/`） | 是：MSVC, TinyCC | 是：gcc, TinyCC；不带 SDL 的 sanitizer 配置 | 是：clang, TinyCC |
| 展示程序（Showcase） | JIT, cl, clang-cl, clang；Direct3D 12 与 Vulkan | JIT, gcc, clang；Vulkan | JIT, clang；Metal |

在 Windows 上，Direct3D 12 与 Vulkan 生成的展示程序截图在每个通道上平均相差 255 中的 0.65。`open_headless`（以及在 `K_GFX_HEADLESS=1` 下运行的程序）会使用 SDL 的 offscreen 驱动初始化视频子系统，这无需显示器即可满足 Vulkan 后端的要求：因此 `test/gfx/` 中的程序无需修改即可在完全没有显示器的 Linux 上运行（即 CI 运行器的环境）。它不会先尝试连接显示服务器：在 WSL 中，若设置了 `DISPLAY` 且 WSLg 的 X 服务器无响应，SDL 的 X11 驱动在回退之前会耗时 86 秒才失败，且每个程序都会如此。在 Apple 系统上，无头模式使用默认驱动（Cocoa），没有显示器也能立即启动：SDL_GPU 的 Metal 后端需要一个能够创建 Metal 视图的视频驱动，而 offscreen 驱动无法提供。在 macOS 上，TinyCC 与 clang 生成的展示程序截图完全一致。

<a id="decisions-on-the-plans-open-questions"></a>
## 对方案中开放性问题的决策

* 内置函数为 `embed_shader`，每个着色器阶段对应一个着色器：既可在阶段名称后在程序中编写 GLSL，也可提供文件，由其扩展名指明阶段。
* 在 `thread_fn` 中调用 gfx 会产生编译错误。
* uniform 结构体按着色器中的块大小检查，而不检查成员偏移量。
* AOT 程序保持为控制台应用程序。
* macOS 上静态链接 SDL 对编译器启动时间的开销，以及 `k` 是否应当拆分出独立的 `k-gfx`，仍有待在 Mac 上实测。

<a id="follow-up-work"></a>
## 后续工作

* **macOS**：运行测试套件与展示程序；测量上述启动开销。
* **一路上发现的编译器 bug**，每一项均已在模块或测试中采用变通方案：将新生成的可调整大小（resizable）结果直接传给内联函数的切片参数会在 `-O1`/`-O2` 下生成无效的 C 代码（需先将其绑定到局部变量）；此外结构体字段的默认值不能引用全局常量（因此 gfx 的描述结构体将其默认值直接拼写为数字）。
* **API**：间接绘制（indirect draws）、模板测试（stencil）、混合常量（blend constants）、调试标签（debug labels）、用于回读深度纹理的 `Region`（SDL_GPU 无法直接下载深度纹理）、检查 uniform 块成员偏移量而不仅仅是总大小，以及在现有追加到输出数组的形式之外，新增直接返回指定元素类型的新数组的回读形式（`read_buffer<f32>(b, n)`）。
* **构建在当前层之上的 K 语言 2D 与 3D 立即模式（immediate-mode）层**（类似于 Lobster 的做法），以及着色器热重载。
