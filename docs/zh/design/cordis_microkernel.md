<a id="cordis-microkernel-for-the-compiler-and-runtimes"></a>
# 编译器与运行时的 Cordis 微内核

K 正在将 Cordis 风格的组合构建为核心基础设施。编译器、原生 K 运行时以及可选的 TypeScript 运行时使用同一套模型来加载组件、解析服务并持有注册项。组件可以在宿主运行期间添加或移除。该设计借鉴了 Cordis 的作用域上下文、服务依赖以及可逆注册机制。[Cordis](https://github.com/cordiverse/cordis)、[Cordis 入门指南](https://deepseek-harness.github.io/deepseek-harness/reference/cordis-primer)

<a id="native-diagnostics-in-the-language-server"></a>
## 语言服务器中的原生诊断

`k lsp --cordis-manifest file.json` 为语言服务器启用一个原生诊断提供者。通过 `diagnostics` 设置初始提供者，默认值为 `builtin`。库路径可为相对于清单文件的路径或绝对路径。每个提供者版本使用不同的产物路径。

```json
{
  "version": 1,
  "diagnostics": "lint-v1",
  "providers": {
    "lint-v1": "providers/lint-v1.so",
    "lint-v2": "providers/lint-v2.so"
  }
}
```

当语言服务器携带清单启动时，能力 `experimental.klangCordisDiagnostics` 的值为 `1`。向 `klang/cordis/selectDiagnostics` 发送携带 `{"provider":"lint-v2"}` 的请求以替换提供者，或选择 `builtin` 以切回内置诊断。响应结果报告当前处于激活状态的提供者 ID。启用失败会返回错误并保留原提供者。切换成功后会重新分析已打开的程序。客户端按提供者 ID 进行选择。启动清单提供库路径。请使用宿主信任的清单和库启动服务器。原生代码以服务器的权限运行。

请针对源码树中的 `src/cordis/diagnostics.h` 或发行版中的 `include/cordis/diagnostics.h` 构建提供者。使用该头文件中的描述符和回调函数导出 `k_cordis_diagnostics_v1`。`start` 分配实例状态并在成功时返回零。即使启动失败，`stop` 也会释放一次状态。在 `inspect` 调用期间，编译器将源码文本和诊断数据借用给它。接收端（sink）会在返回前复制补充诊断信息。服务器将这些诊断信息连同编译器诊断一并发布，其来源标注为 `cordis/<provider-id>`。工作线程串行执行调用。每次分析在完成发布之前都会一直保留其启动时所使用的提供者。[生命周期测试](../testing.md#running-the-suite)

<a id="resolving-a-provider-graph"></a>
## 解析提供者图

在加载代码之前，使用 `k cordis resolve <service> --manifest cordis.json` 解析受信任的清单并报告所选图。使用 `--provider <id>` 可为根服务解析不同的选择。语言服务器也可以通过 `k lsp --cordis-manifest cordis.json --cordis-graph` 解析诊断服务。版本 1 清单按 ID 选择诊断提供者并默认使用 `builtin`。版本 2 则声明每个提供者所提供的服务及其依赖要求。

```json
{
  "version": 2,
  "selections": { "diagnostics": "lint", "symbols": "index" },
  "grants": ["workspace.read"],
  "providers": {
    "lint": {
      "service": "diagnostics",
      "version": 1,
      "library": "providers/lint.so",
      "capabilities": ["workspace.read"],
      "requires": { "symbols": { "minVersion": 1, "maxVersion": 2 } },
      "inputs": { "rules": "lint-rules.json" }
    },
    "index": {
      "service": "symbols",
      "version": 2,
      "library": "providers/index.so"
    }
  }
}
```

服务版本为正 32 位整数，依赖边界为闭区间。服务版本与原生 C ABI 版本分别独立校验。当多个提供者满足同一要求时，在 `selections` 中指定其中一个。解析图对每个可达服务使用一个提供者。`builtin` 指代已链接的诊断提供者。宿主会将其注册，并以正在运行的可执行文件作为其产物。每个所需的权限能力都必须出现在清单的 `grants` 数组中。原生提供者仍以宿主的权限运行。

库路径和输入路径可为相对于清单的路径或绝对路径。在对应选择处于使用状态期间，请保持这些路径上的文件不可变。输出包含 `identity` 和一个带有按序排列节点与边的版本化 `graph`。每个节点记录服务版本、提供者 ID、产物 SHA-256，以及显式跟踪输入的 SHA-256 值。图标识对 `graph` 字段的规范化 JSON 进行哈希计算。清单格式排版、绝对路径位置以及未被选中的提供者均不计入该标识。

解析错误会输出一个包含 `code`、`message`、`chain` 和 `candidates` 的 `error` 对象，并以状态码 1 退出。运行中的服务器通过 `klang/cordis/graph` 暴露其当前激活图，并宣告版本为 1 的 `experimental.klangCordisGraph` 能力。失败的切换会保留旧图，并将解析详情放入 JSON-RPC 错误的 `data` 字段中。语言服务器目前激活单个诊断服务。对于如上所示的多服务图，可使用仅解析命令进行检查。

<a id="locked-checks-and-builds"></a>
## 锁定检查与构建

将图文档保存为 UTF-8 锁文件，并与清单一起提供。对于编译器宿主能够激活的图，同一锁文件同样适用于 `k check` 和 `k build`。

```sh
k lsp --cordis-manifest cordis.json --cordis-graph > cordis.lock.tmp &&
    mv cordis.lock.tmp cordis.lock
k check --offline --cordis-manifest cordis.json --cordis-lock cordis.lock app.k
k build --offline --cordis-manifest cordis.json --cordis-lock cordis.lock app.k
```

两个标志均为必填。编译器在激活提供者之前，会根据清单和所选文件核对锁文件中的图摘要。有意识地做出变更后请重新生成锁文件，并审查所选提供者、授权能力与摘要。已保存锁文件中的空白字符可以改变，而其对象字段顺序需与生成的文档保持一致。`--offline` 会应用编译器常规的仅本地包策略。

单次调用会固定所选提供者，并向其传递与语言服务器相同的源码和编译器诊断。在 `--diagnostics json` 中，诊断发现会以其提供者 ID 作为 `source`。提供者出错会导致检查或构建失败。在提供者返回之后、写入生成文件之前，编译器会重新校验所选动态库及受跟踪输入。锁与 ABI 错误会在 JSON 诊断数组中包含一个带有 `code`、`message` 与依赖 `chain` 的 `cordis` 对象。

成功完成锁定构建后，会在原生构建产物中将其图文档嵌入为名为 `k_cordis_build_graph` 的以空字符结尾的 UTF-8 C 数组。在受支持的 macOS 系统 Clang 路径上，该图标识参与可执行文件缓存键的计算。仅修改未被选中的提供者不会改变缓存键。生成的应用程序 C 代码包含该图，而共享运行时对象与原生包缓存仍保持其原有缓存键。编译器激活目前覆盖诊断服务；包含依赖服务的图激活属于后续里程碑。

<a id="shared-native-service-lifecycle"></a>
## 共享原生服务生命周期

`src/cordis/service.h` 包含由 LSP 诊断适配器与独立原生宿主共享的激活和退休逻辑。每个服务提供其 ABI 版本、入口符号与必需方法。宿主可为同一服务注册已链接入口或库路径。宿主会在发布候选版本前先进行预备并启动它。请求会持有对所选世代的引用。最后一个引用会在卸载库之前调用 `stop`，包括在替换期间仍被持有的旧世代。宿主会串行化选择与请求获取操作。

独立测试夹具使用一个与诊断服务具有不同 C ABI 的值服务。它演练了已链接到动态、动态到已链接的替换、回滚、缺失入口点以及在替换期间仍被持有的旧请求。另一独立二进制文件在不编译也不链接动态加载器的情况下运行相同的已链接提供者。[生命周期测试](../testing.md#running-the-suite)

<a id="cancellable-tcp-receive"></a>
## 可取消的 TCP 接收

当 `receive` 成功时，可选的 TCP 提供者会接管一个已连接的阻塞式 TCP 套接字的所有权。单个操作在工作线程上读取单个字节并发布固定大小的结果。取消操作通过关闭套接字来中断接收。驱动会等待其完成后再 join 工作线程并关闭套接字。宿主可使用 `src/cordis/foreign_task.h`，在释放操作的调用完成前持续固定所选的提供者世代。

使用 `-DK_BUILD_TCP_READ_PROVIDER=ON` 进行配置，并构建 `k_cordis_tcp_read` 目标以获得共享库。其入口点为 `k_cordis_tcp_read_v1`。发行包在 `include/cordis/tcp_read.h` 中包含相应的 C 契约。该可选模块单独构建。

回环测试将该操作移交给 K，在接收仍处于挂起状态时将动态提供者替换为已链接提供者，并检查在取消操作排空之前旧提供者与套接字是否保持存活。[原生操作任务](async_and_decorators.md#native-operation-tasks) · [回环测试](../testing.md#running-the-suite)

<a id="bounded-tcp-streams"></a>
## 有界 TCP 流

同一可选模块还导出了 `k_cordis_tcp_stream_v1`。其 C 契约位于 `src/cordis/tcp_stream.h`，并在发行版中作为 `include/cordis/tcp_stream.h` 提供。宿主可将其注册为已链接或动态 Cordis 服务，并通过 `open` 接管一个已连接的阻塞式套接字。宿主可选择 1 到 1,024 个数据块的容量，每个数据块最多容纳 4 KiB。内存分配与套接字所有权转移均在打开时发生，且所有权转移在成功时生效。

每条流拥有一个读取端（reader）和一个生产者任务（producer task）。生产者在向其工作线程发出套接字读取前会先预留一个环形缓冲区槽位。在工作线程完成前，该预留会占用容量配额；而部分读取的 `take` 也会保留其槽位，直到消费者取走剩余字节。当环形缓冲区满时，生产者不再提交新的读取。单个工作线程负责该流的重复读取。工作线程在完成时唤醒任务驱动。释放已满环形缓冲区中的一个槽位也会唤醒生产者，使其能够预留下一个槽位。

K 宿主可以将生产者描述符装入一个自有 C 句柄并返回，再将其传给 `native_task<ReadResult>`。将该生产者纳入 `task_scope` 中。消费者使用自己的字节切片调用 `take`。该调用在需要更多工作时返回 `PENDING`，或返回 `DATA` 及已复制的字节数。消费者在收到 `PENDING` 后可以调用 `scope.wait()`。消费者会在收到 `EOF` 或套接字 `ERROR` 之前按序收到已缓冲的字节。[托管就绪通知](async_and_decorators.md#hosted-readiness)

取消操作会停止新的提交并丢弃已排队的字节。它会撤销未认领的工作或中断正在进行的接收，并在 join 工作线程和关闭套接字之前完成排空。仍被保留的读取端在清理完成后会观察到 `CANCELLED`。在接收期间释放读取端也会请求取消，而生产者任务仍会驱动其直到完成。宿主适配器分别为读取端和生产者持有独立的世代引用，因此替换提供者仅影响新建的流，而现有流会保留其原始回调直到双方所有者全部结束。[回环测试](../testing.md#running-the-suite) · [托管 I/O 里程碑](../roadmap.md#next-mergeable-changes)

<a id="locked-application-activation"></a>
## 锁定应用激活

托管 K 模块 `cordis` 会根据版本 2 清单及经过验证的锁文件激活一个 `tcp.stream` 提供者及其可选的 `tcp.policy` 依赖。应用宿主与 TCP 提供者相互独立。使用 K 自带的该服务契约实现时，请同时构建两者。

```sh
cmake -S . -B build -DK_BUILD_CORDIS_HOST=ON -DK_BUILD_TCP_READ_PROVIDER=ON
cmake --build build --target k_cordis_application k_cordis_tcp_read
k cordis resolve tcp.stream --manifest cordis.json > tcp.lock.tmp &&
    mv tcp.lock.tmp tcp.lock
k cordis resolve tcp.stream --manifest cordis.json --provider next > next.lock.tmp &&
    mv next.lock.tmp next.lock
```

将应用宿主库部署在程序资源目录下的 `lib/` 中。它在 macOS 上的名称为 `libk_cordis_application.dylib`，在 Linux 上为 `libk_cordis_application.so`，在 Windows 上为 `k_cordis_application.dll`。将提供者库置于清单指定的路径。其入口点与 ABI 与其他原生 TCP 流宿主一致。[动态库路径](dynamic_externs.md) · [TCP 流 ABI](#bounded-tcp-streams)

清单可以选择一个提供者，同时注册另一个用于替换的提供者。请使用各平台实际的提供者产物文件名。

```json
{
  "version": 2,
  "selections": { "tcp.stream": "default" },
  "providers": {
    "default": { "service": "tcp.stream", "version": 1, "library": "providers/tcp-v1.so" },
    "next": { "service": "tcp.stream", "version": 2, "library": "providers/tcp-v2.so" }
  }
}
```

```klang
import cordis;

fn main() {
    let host = cordis::open("cordis.json", "tcp.lock");
    defer cordis::close(host);
    assert(cordis::valid(host) == 1);
    var id: u8[64] = [0; 64];
    let size = cordis::active_provider(host, id[..]);
    assert(size <= id.len);
    print(id[..size]);
}
```

应用程序在单线程上驱动其宿主。它可以调用 `cordis::select(host, "next", "next.lock")` 在启动目录内进行切换。宿主在加载代码之前会校验请求的锁文件，按先依赖后消费者的顺序启动服务，并在启动后重新校验所选输入。只有激活成功时才会将整张图发布生效。若失败，已预备的服务按逆序停止，当前图保持激活状态。选择操作在发生替换时返回 `1`，与当前图相同时返回 `0`，失败时返回 `-1`。失败的 open、adopt 或 pump 会返回一个自有的无效句柄。请使用 `valid` 检查，使用对应的关闭或丢弃函数释放它，并读取 `error_json` 获取原因。`error_json`、`active_graph`、`active_provider` 和 `stream_provider` 返回所需字节长度，并在缓冲区足以容纳时复制完整结果。

`adopt_tcp` 从 C 绑定接管一个自有套接字，并且无论成功与否都会消耗它。成功创建的读取端及其泵（pump）各自持有对所选世代的固定引用。请将有效的泵传递给 `native_task<cordis::ReadResult>`，或在调度前使用 `discard_task`。新建读取端使用新选定的提供者。旧读取端和泵在切换后仍可继续运行。它们的固定引用使已退休的流提供者及其策略保持存活。当最后一个固定引用被释放时，流提供者会先于其策略停止。请确保部署路径上的提供者二进制文件保持不可变。

如需附加策略，请在流提供者中添加 `"requires": {"tcp.policy": {"minVersion": 1, "maxVersion": 1}}`，并为所需服务版本注册一个提供者。其 C 描述符 `k_cordis_tcp_policy_v1` 会为新读取端返回最大队列槽位数。宿主会在调用 TCP 提供者前检查该限制，若超出限制则关闭已移交的套接字。错误码为 `stream_capacity_denied`，其调用链 `chain` 会标识两个提供者。

通用解析器可以检查其他服务契约的图。应用宿主支持 `tcp.stream`、`tcp.policy` 与 `inference` 的激活。原生提供者以应用程序的权限运行。核心发行版在 `include/cordis/` 下随附 C 声明头文件。应用宿主库和提供者库独立部署。[应用集成测试](../testing.md#running-the-suite)

<a id="locked-cpu-inference"></a>
## 锁定 CPU 推理

可选的 ONNX Runtime 提供者通过该引擎的 C API 实现 `inference` 服务。它接收一个自包含的 ONNX 图，要求具有一个固定形状、连续布局的 float32 输入和输出，并使用 CPU 执行提供者。任何头文件兼容的运行时均可作为本地库提供。清单锁文件记录提供者产物、模型和运行时库的摘要。[ONNX Runtime C API](https://onnxruntime.ai/docs/get-started/with-c.html)

将 SDK 头文件放在所选根目录的 `include/` 目录下。集成测试会在同一根目录的 `lib/` 或 `bin/` 下查找引擎库。参考测试使用 ONNX Runtime 1.24.4。[Microsoft ONNX Runtime 包](https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime/1.24.4)

```sh
cmake -S . -B build -DK_BUILD_CORDIS_HOST=ON -DK_BUILD_LSP_TESTS=ON \
    -DK_ONNXRUNTIME_ROOT=/path/to/onnxruntime
cmake --build build
ctest --test-dir build -R '^cordis_onnx_inference$' --output-on-failure
k cordis resolve inference --manifest providers.json
```

`providers.json` 中的提供者条目指定平台所构建的 `k_cordis_onnx` 模块，并提供以下受跟踪输入。运行时库必须导出 `OrtGetApiBase`。

```json
{
  "version": 2,
  "providers": {
    "cpu": {
      "service": "inference",
      "version": 1,
      "library": "providers/libk_cordis_onnx.so",
      "inputs": { "model": "models/classifier.onnx", "runtime": "engine/libonnxruntime.so" }
    }
  }
}
```

将解析后的文档保存为锁文件，并与清单一同传给 `cordis::open_inference`。此外，TCP 图也可以将 `inference` 声明为所选 `tcp.stream` 提供者的依赖项。激活过程会在发布图之前校验文件摘要、按路径加载声明的运行时，并从已校验的模型字节初始化会话。

`cordis::inference_layout` 返回输入与输出形状。`cordis::infer` 将输入复制到一个自有请求中。使用 `cordis::inference_task` 获取其任务，并通过 `native_task<cordis::InferenceResult>` 驱动执行。状态为零表示该自有请求已有结果。使用 `cordis::inference_output` 复制结果，并使用 `cordis::close_inference` 释放请求。取消操作会通过 C API 请求终止，并在释放其会话前排空工作线程。新请求使用当前提供者，而旧请求和任务仍保留其原始模型世代。

模型文件大小上限为 64 MiB。每个发布的张量最多支持 8 个轴和 1,048,576 个元素。每个世代拥有一个请求槽位，在请求及其任务均被释放前一直被占用。第二个请求会报告 `inference_busy`。

`cordis::inference_workspace` 采样当前处于激活状态的 CPU 分配区（arena），包括保留字节数、已使用字节数以及该会话的峰值已使用字节数。它还会报告存活的已退休世代，并汇总那些带有遥测数据的世代的保留字节数。当某个退休世代的最后一个请求、任务或依赖服务释放该世代时，其退休条目失效。最后一次激活采样会在成功发布前夕汇总即将退出的、已预备的以及此前已退休的内存池。被拒绝的替换会保留该采样结果和当前激活图。

推理 ABI 将内存池遥测设为可选。测量标志指示哪些读数可用，包括在发布时是否已测量全部内存池。基于 ONNX Runtime 1.23 或更高版本构建的 CPU 提供者使用各自会话的分配器统计信息。进程 RSS 还包括宿主、运行时、模型解析缓冲区、线程栈以及 CPU 内存池之外的内存。在制定进程内存预算时，请单独测量 RSS。[ONNX Runtime CPU 分配器](https://onnxruntime.ai/docs/get-started/with-c.html)

<a id="loopback-inference-gateway"></a>
## 回环推理网关

`examples/inference_gateway.k` 为每个 TCP 连接处理一次请求。K 负责请求校验、推理任务作用域、提供者切换以及响应成帧。可选的应用宿主负责管理套接字和已接受连接的有界 FIFO 队列。它一次处理一个活跃请求，并绑定 IPv4 回环地址。可选的 ONNX Runtime 提供者仍可通过锁定的 `inference` 服务进行选择。

对于 `k run`，请将应用宿主共享库放在 K 源码旁的 `lib/` 下；对于 `k build`，则放在可执行文件旁的 `lib/` 下。使用上述推理清单生成其启动锁文件。

Linux 云发行归档包内含 `k-inference-gateway`、应用宿主、CPU 提供者及其引擎。在你的应用清单中，将提供者的 `library` 指向已安装的 `lib/libk_cordis_onnx.so`，并将其 `runtime` 输入指向已安装的 `lib/libonnxruntime.so`。将 `model` 指向你的模型文件，使用已安装的 `k` 解析启动锁文件，然后携带清单、锁文件及下列数值选项运行 `k-inference-gateway`。[云发行版构建](../testing.md#rebuilding-the-linux-cloud-release)

```sh
k cordis resolve inference --manifest providers.json > initial.lock
k run --offline gateway.k -- providers.json initial.lock 0 8 2000 16384 256 0
```

这些数值选项依次设置端口、待处理连接数上限、请求超时截止时间（毫秒）、张量元素数上限、响应帧元素数上限以及退出前出队处理的连接数。端口设为 0 会选择临时端口并打印在 `ready` 行中。连接数设为 0 则一直运行直至停止。张量上限同时适用于两个模型张量。可选的末尾参数 `MARKER PROVIDER LOCK` 会在请求边界或推理空闲停靠时触发一次激活替换尝试。已在运行的请求保持其原有世代。激活失败则会保留当前世代。

附加 `--workspace` 可在启动后、替换尝试后以及关闭前向标准输出打印 JSON 快照。每条快照以 `workspace` 为前缀，后跟所属阶段及一个 JSON 对象。通信线路协议独立于这些诊断信息。

<a id="linux-deployment-memory-budget"></a>
### Linux 部署内存预算

在执行之前，请将网关置于专用的 Linux cgroup v2 中。将 `memory.max` 设为部署预算，并将 `memory.swap.max` 设为零。附加 `--memory-budget-mib MIB` 可使网关在加载模型或绑定监听器之前验证这些限制。它会接受等于或低于所请求字节数的有效限制，并检查进程实际所在 cgroup 的可读祖先节点。缺失或不足的限制边界会导致启动失败。部署监管器在网关整个生命周期内负责维护这些限制和组归属。

对于 Docker 部署，请为 `--memory` 和 `--memory-swap` 指定相同的值。将网关及其回环客户端运行在同一网络命名空间中。在替换期间，cgroup 内存记账包含运行时以及两个模型世代。如果硬限制下的内存回收失败，Linux 可能会终止该进程。被 Cordis 拒绝的替换在进程存活期间仍会保留当前世代。[Linux cgroup v2 内存控制器](https://docs.kernel.org/admin-guide/cgroup-v2.html#memory) · [Docker 内存约束](https://docs.docker.com/engine/containers/resource_constraints/#limit-a-containers-access-to-memory)

<a id="supervised-oom-rollback"></a>
### 监管式 OOM 回滚

Linux 云发行版同时提供了 `k-inference-supervisor`。请预先配置一个空的工作 cgroup，为其设置专属的 `memory.max`、`memory.swap.max=0` 和 `memory.oom.group=1`。在该组之外运行监管器，并将进程迁移权限委派给它。工作进程会在执行网关前进入该组。请在上级 cgroup 中为监管器保留足够内存。

```sh
k-inference-supervisor --worker-cgroup /sys/fs/cgroup/cordis/worker \
  --memory-budget-mib 96 \
  --reload promote candidate candidate.lock --rollback-file candidate.rolled-back \
  -- providers.json stable.lock 8000 8 2000 16384 256 0
```

监管器需要固定且非零的端口以支持恢复。在其生命周期内，请保持稳定清单、锁文件及其输入可用且不变。当发生 `SIGKILL` 且工作组的 `oom_kill` 计数上升时，监管器会使用稳定锁文件重试一次。如果启用了灰度发布，它会在重试前写入并同步（sync）回滚文件。后续启动时若检测到该文件存在，即使重载标记仍然存在，也会抑制同一次发布。每次发布请使用不同的回滚文件。在替换后的工作进程报告 `ready` 之后，客户端可重新连接到同一端口。第二次 OOM 会耗尽重试机会。

[Cgroup 委派](https://docs.kernel.org/admin-guide/cgroup-v2.html#delegation)

每个请求以 ASCII 字节 `KIG1` 开头，随后是一个小端序 32 位元素计数以及同等数量的 IEEE 754 小端序 `f32` 值。客户端发送单个请求并保持其写半部打开，直到收到终止帧为止。服务器发送的帧包含 1 字节标签和小端序 32 位载荷字节计数。标签 1 携带张量分块，而终止帧携带零字节载荷。

| 标签 | 含义 |
| --- | --- |
| 1 | 张量数据 |
| 2 | 完成 |
| 3 | 无效请求 |
| 4 | 超过截止时间 |
| 5 | 准入队列已满 |
| 6 | 推理失败 |

模型在开始成帧之前会先生成完整张量。截止时间采用单调时钟，从接受套接字连接时开始计时。它涵盖排队期间、I/O 期间以及等待推理完成期间。超时或断开连接的请求会取消其未完成的推理，并在复用活跃槽位前完成排空。提供者启动以及外部取消的排空耗时可能会超出截止时间。超时和过载通知属于尽力而为（best-effort）。客户端仅在收到标签 2 终止帧之后才消费该张量。元素数量上限约束应用程序自有的张量缓冲区。模型权重、后端工作区以及操作系统套接字缓冲区各自独立占用进程与系统内存。

`ctest --test-dir build -R '^cordis_inference_gateway$' --output-on-failure` 使用上述 ONNX SDK 构建来对两种执行后端演练该线路协议。

<a id="user-selected-components"></a>
## 用户自选组件

微内核是 K 的首要架构原则。宿主掌控语言语义、安全检查与组件生命周期。项目按需选择编译器工具链、I/O、外部执行以及设备服务所需的提供者。版本化清单描述需求。锁文件记录所选组件版本与内容摘要。诊断信息会在不隐式安装提供者的前提下指出未满足的需求。同一套服务契约既支持无法动态加载时的静态组合，也支持长生命周期宿主中的动态替换。一次构建会暴露解析出的组件图以及其宿主授予的权限能力。[K 架构](../architecture.md)

<a id="contexts-and-services"></a>
## 上下文与服务

内核拥有上下文树、服务注册表、依赖图、类型化事件分发以及组件生命周期。每个组件实例都会获得一个子上下文，用于存放其配置、服务、任务和注册项。工作区或应用可以创建自己的作用域，并为同一服务使用不同的提供者。消费者使用版本化的服务接口。它们按服务 ID 与版本范围声明必需依赖与可选依赖。子作用域可以显式覆盖父作用域所选的提供者。当同一作用域中有多个符合条件的提供者且项目未做出选择时，解析器会报告歧义。可选依赖可以缺失。组件会等待其必需提供者全部就绪。必需提供者的变更会重启受影响的依赖方。如果必需提供者消失，这些依赖方会退回等待状态。遇到版本不兼容或依赖循环时，诊断信息会给出涉及的组件链。

事件声明定义载荷、结果与分发顺序。作用域拥有其全部监听器以及它们捕获的任何闭包。卸载作用域会移除其服务和监听器、取消其拥有的任务，并释放已注册的资源。类型化闭包实现回调，`async fn` 实现启动与关闭，而装饰器可以声明服务和事件元数据。在首个原型中使用显式注册，并验证装饰器注册能否产生相同的元数据与生命周期。[一等函数](functions_and_typescript_runtime.md#first-class-function-values)、[异步函数与装饰器](async_and_decorators.md)

<a id="extension-points"></a>
## 扩展点

| 宿主 | 初始扩展点 | 进一步扩展点 |
| --- | --- | --- |
| `k check`、`k build` 与 `k lsp` | 对已检查编译快照进行诊断与只读分析 | 代码检查器（linter）、代码操作、声明驱动的代码生成，以及具有版本化输入契约的后端 |
| 原生 K 运行时 | 应用服务、事件处理器与配置提供者 | I/O 适配器、追踪导出器与应用级中间件 |
| 可选 TypeScript 运行时配置档 | TypeScript 转换、ECMAScript 引擎、模块解析与兼容性服务 | Node.js 适配器以及经版本化兼容性测试验证的宿主集成 |
| 可选 Cordis 构建驱动 | 将扩展源码转换为普通 K 代码、客座产物及源码映射的源码变换 | 编辑器叠加层以及基于同一锁文件的离线重构建 |
| 外部语言宿主 | 精选的 Python 与 TypeScript 执行适配器 | 通过 C ABI、隔离进程或 Wasm 组件实现相同类型化边界的更多语言 |

暴露在这些扩展点上的内置提供者与外部组件使用相同的接口。编译器掌控语言文法、类型系统、引用检查以及 IR 不变量。生成的 K 源码经过常规检查器校验。构建驱动的源码变换会在编译器读取这些模块之前执行。客座语法、引擎和 Node.js 兼容性保留在项目自选的组件中。编译器在整次编译或检查期间持有固定的一组组件。长生命周期的 LSP 宿主可以在两次请求之间变更该集合。生成产物的扩展需声明其输入。缓存条目包含启用的扩展版本、内容哈希、生效配置以及受跟踪输入哈希。能够读取未受跟踪输入的扩展会禁用其所影响阶段的缓存复用。

<a id="dynamic-loading-and-replacement"></a>
## 动态加载与替换

清单声明组件 ID、宿主与 ABI 兼容性、入口点、提供与需要的服务以及配置模式。宿主根据显式配置选择启用的组件。初期从使用版本化 C ABI 的已编译 K 或 C 共享库起步，在边界处采用不透明句柄、显式所有权和固定布局数据。现有的 K 共享库构建和 `export fn` 是编写适配器的起点。引导加载器可以位于 C 或 C++ 中，无需依赖 JavaScript 引擎。一旦可选的 TypeScript 运行时激活，TypeScript 组件后续便可通过桥接使用同一注册表。[K 共享库构建](../command_line.md#3-building-an-executable)、[TypeScript 运行时架构](functions_and_typescript_runtime.md#runtime-architecture)、[嵌入式语言适配器](embedded_languages.md)

进行替换时，先在预备上下文中准备新组件及其受影响的依赖方。在独立的产物路径下加载新的原生二进制文件，以便平台加载器在切换期间同时保持两个版本存活。启动成功后，在宿主安全点将它们的注册项一并发布。若失败，则处置预备作用域并让当前激活配置继续提供服务。切换瞬间，新调用看到新提供者。停止向已退休作用域路由新工作，排空或取消正在进行的工作，并在卸载其提供者之前先拆除依赖方。只有当原生库的最后一个未决句柄、回调和任务全部消失后，才释放该库。需要独占访问资源的提供者必须声明其在切换期间何时可以获取该资源。外部写入和其他不可逆副作用仍受服务自身契约的约束。

服务作用域控制查找与生命周期。进程内原生代码共享宿主权限。运行不受信任组件的宿主需要独立的执行边界与显式的服务协议。核心注册表随 `k` 一同分发并在各宿主间共享，无需依赖可选的 TypeScript 运行时，也无需为每个项目单独下载副本。

<a id="implementation-and-acceptance"></a>
## 实现与验收

1. 从清单向 `k lsp` 加载一个原生诊断组件。检查其观察到的已检查程序是否与 `k check` 一致。在不重启服务器的情况下在请求之间替换该组件，并验证两个版本能够独立报告且已退休的监听器被释放。
2. 在包含两个带作用域提供者和类型化消费者的 K 应用中使用共享生命周期。测试缺失依赖、带回滚的单次失败替换、活跃异步回调期间的移除，以及所有注册项和句柄的释放。验证正在进行中的调用绝不会进入已卸载的代码。
3. 通过可选运行时加载一个 TypeScript 组件，并让其消费一个由 K 提供的服务。在异步调用挂起期间替换该组件。验证旧调用依据服务契约正常结算，而新调用使用新实现。在机器可读的诊断信息中暴露当前激活的组件和服务图，使这些状态能够被智能体测试框架检查。
