<a id="embedded-language-blocks"></a>
# 嵌入式语言代码块

可选的 Cordis 源码扩展用于运行带围栏定界符的客座语言代码块。项目可在 Cordis 图中选择 `using` 语法扩展与客座运行时。源码扩展负责处理扩展语法并生成普通 K 代码。每个适配器各自负责其客座前端与执行引擎。K 编译器则使用既有规则校验生成的调用。
[Cordis 微内核](cordis_microkernel.md#extension-points)

<a id="source-boundary"></a>
## 源码边界

Cordis 构建驱动会在向源码扩展传入扩展源码之前，先解析并锁定该源码扩展。源码扩展负责解析 `using` 头部，并保留客座文本及其源码偏移量。最初可采用显式围栏语法，其定界符可按需更改。当所选适配器能够借助其客座词法分析器确定闭合边界时，源码扩展也可以公开 `using "python" { ... }` 和 `using "typescript" { ... }` 形式。Python 字符串、TypeScript 模板字符串以及正则表达式字面量中都可能包含花括号。对于规模更大的客座模块，应使用普通的外部源文件，并保持相同的接口与执行语义。

扩展会向驱动返回生成的 K 模块、客座产物以及源码映射（source map）。驱动将这些模块送入常规编译器，并将编译器产生的诊断信息映射回扩展源码。未来的编辑器集成可以使用同一个锁定的转换器来为其覆盖层（overlay）提供支持。默认编译器接收的是普通 K 代码，且核心语法不会为该扩展保留任何关键字。

Python 与 TypeScript 适配器实现了相同的围栏语法。以下示例分别使用各自的项目锁文件来选择对应的适配器。

<!-- fragment -->
```klang
let result, error = using "python" (left: i64 = x, right: i64 = y) -> i64 {|
answer = left + right
|};

let same_result, error = using "typescript" (left: i64 = x, right: i64 = y) -> i64 {|
export const answer: bigint = left + right;
|};
```

Python 适配器从执行作用域中读取 `answer`，而 TypeScript 适配器则读取模块导出的 `answer`。`-> i64` 表示预期的成功返回类型。扩展会将该表达式降级为一个同样可能返回客座错误的调用。代码块通过显式参数与导入接收 K 值和服务。值在客座运行时中作为数据进行绑定，客座程序文本本身保持固定不变。

<a id="optional-build-driver"></a>
## 可选构建驱动

将构建驱动及其 C ABI 头文件作为独立组件进行构建和安装，并使用现有 K 安装中的编译器。

```sh
cmake -S . -B build-source -DK_BUILD_CORDIS_SOURCE=ON
cmake --build build-source --target k-cordis-source
cmake --install build-source --component cordis-source --prefix ./cordis-tools
```

在版本 2 的 Cordis 清单中选择名为 `source-transform` 的原生服务。其库文件从 `include/cordis/source_transform.h` 导出 `k_cordis_source_transform_v1`。在调用驱动之前，请先锁定该服务及其声明的输入。每次调用都会捕获项目输入。

```sh
k cordis resolve source-transform --manifest project/cordis.json > project/cordis.lock
cordis-tools/bin/k-cordis-source check --manifest project/cordis.json \
  --lock project/cordis.lock --root project --input helper.src program.ext
cordis-tools/bin/k-cordis-source build --manifest project/cordis.json \
  --lock project/cordis.lock --root project --input helper.src -o app-bundle program.ext
```

v1 宿主激活单个受信任的原生转换器。它按声明顺序将入口文件、所有 `--input` 文件以及锁定的提供者输入作为字节切片传入。转换器输出一个生成的入口、若干普通 `.k` 模块、可选的不透明产物以及互不重叠的字节跨度（byte span）。路径应使用仅包含 ASCII 字母、数字、点号、连字符与下划线的相对路径。诊断位置采用从 1 开始的 UTF-8 字节列号。当提供了字节跨度时，驱动会重新映射编译器 JSON 诊断数组中的位置；未映射的生成位置则使用 `source/` 后接其模块名表示。可选的 `last_error` ABI 后缀会直接按原始请求的源码坐标报告转换诊断信息。

编译器使用 `--offline` 参数运行。默认情况下，它会使用临时缓存，并从生成的模块和已安装的库中解析导入。若要在产物包外部使用现有的离线包与产物存储，请设置 `K_HOME`。通过 `--compiler PATH` 指定编译器，或直接使用 `PATH` 中的 `k`。原生构建还可以透传 `--cc`。构建成功后，会发布一个新的产物包目录，其中包含 `program`（Windows 上为 `program.exe`）、生成的 `source/` 文件、可选的 `assets/` 以及 `source-build.json`。构建回执会记录 Cordis 图、源码映射以及内容摘要。若发生输入漂移或检查失败，目标目录将不会发布。每次构建请选择一个新的输出目录。

<a id="typed-execution-boundary"></a>
## 类型化执行边界

- 先从受检标量、UTF-8 字符串和字节缓冲区值开始；待所有权规则可被强制执行后，再添加记录（record）和命名资源句柄。默认采用复制传递。零拷贝缓冲区需要双方运行时均能强制执行的借用或保留契约。超出 `i64` 范围的 Python 整数将转换失败；TypeScript 则通过 `bigint` 映射精确的 `i64` 值。
- 返回一个类型化结果，其中要么包含转换后的值，要么包含带有语言名称、源码范围和客座栈信息的外部错误。明确记录实例与状态的生命周期，使重复求值具有确定行为。异步代码块返回一个任务，其取消与清理遵循适配器的执行契约。运行中的代码块会持续持有其启动时所在的适配器世代，直到执行完成与清理结束。
- Cordis 驱动会验证所声明的适配器是否存在且兼容。标准 K 检查器会校验生成的 K 侧签名。选定的适配器可以在不执行代码块的情况下验证客座源码。构建锁文件会记录源码扩展、适配器与引擎标识、生效配置以及声明的输入。缓存条目同样涵盖生成的源码、产物以及构建目标。在首个实现中，驱动可以直接调用现有的 K CLI。
- 准入策略取决于所选目标平台。裸机（freestanding）目标可运行具备受支持 ABI 和资源的预编译组件；托管目标还可以加载外部进程、解释器或 Wasm 组件。应锁定适配器并随构建记录运行模式，以便行为可复现。

<a id="adapter-implementations"></a>
## 适配器实现

TypeScript 适配器嵌入了 QuickJS，用于转译并求值每个代码块。运行时配置可以复用这部分工作，并通过宿主服务提供模块解析与 Node.js API。对于其他语言，适配器可以将调用编组（marshal）至独立进程，或者将代码编译为带有声明接口的 Wasm 组件。
[CPython 嵌入指南](https://docs.python.org/3/extending/embedding.html) ·
[TypeScript 运行时](functions_and_typescript_runtime.md#runtime-architecture) ·
[Wasm Component Model 接口](https://component-model.bytecodealliance.org/design/wit.html)

进程内引擎会共享宿主进程权限。需要硬超时控制或租户隔离的工作负载应当在受限进程或 Wasm 沙箱中运行。宿主仅提供明确授予的文件、网络、内存或设备能力。隔离式适配器可以在其执行边界处被中止；进程内适配器则必须配合取消机制，并保留客座代码仍在使用的所有资源。
[WASI 能力模型](https://wasi.dev/security)

<a id="optional-cpython-adapter"></a>
## 可选 CPython 适配器

基于本地 CPython 3.12 或更高版本及其嵌入头文件与库进行构建。CMake 会与可选源码驱动一同安装两个原生组件。构建产物包会复制锁定的桥接库，并在运行时需要兼容的 CPython 安装与标准库。

```sh
cmake -S . -B build-python -DK_BUILD_CORDIS_SOURCE=ON -DK_BUILD_CORDIS_PYTHON=ON
cmake --build build-python --target k-cordis-source k_cordis_python_transform k_cordis_python_runtime
cmake --install build-python --component cordis-source --prefix ./cordis-tools
cmake --install build-python --component cordis-python --prefix ./cordis-tools
```

将已安装的转换库作为所选的 `source-transform` 提供者，并将其 `inputs.runtime` 设置为对应的桥接库。以下 macOS 示例将 `cordis.json` 放置在与 `cordis-tools` 相同的目录中；请根据实际宿主机使用对应的已安装文件名。

```json
{
  "version": 2,
  "selections": { "source-transform": "python" },
  "providers": {
    "python": {
      "service": "source-transform",
      "version": 1,
      "library": "cordis-tools/lib/cordis/libk_cordis_python_transform.so",
      "inputs": { "runtime": "cordis-tools/lib/cordis/libk_cordis_python_runtime.so" }
    }
  }
}
```

如上文所示，使用 `k cordis resolve` 解析 `source-transform` 并使用 `k-cordis-source` 进行构建。起始围栏必须位于行尾；闭合的 `|}` 位于行首（其前可带可选缩进）。每个代码块会保留其 Python 缩进并嵌入到生成的 K 代码中。产物包还会将原始 Python 代码作为可读产物保留下来。K 绑定表达式保持其常规类型与求值顺序。如果表达式在圆括号、方括号或花括号之外包含逗号，请为其加上括号。

`i64`、`text` 和 `bytes` 类型分别映射为 Python 的 `int`、`str` 和 `bytes`。K 侧的文本和字节结果为带所有权的字节数组。UTF-8 文本转换采用严格模式，且 Python 整数必须能容纳于 `i64` 中。代码块必须为 `answer` 赋值。返回值会解构为结果值与一个带所有权的错误字符串。成功时错误字符串为空；失败时，`i64` 结果为零，文本和字节结果为空数组。Python 异常会返回其回溯信息与原始源码位置；类型错误和范围错误则会返回说明性消息。每次调用均使用全新的 globals 字典，并共享进程内的解释器与模块缓存。

在 `check` 或 `build` 期间，适配器会编译 Python 源码以校验语法，并按原始文件的 UTF-8 字节列号报告错误。客座语句会在构建出的 K 程序求值该代码块时执行。该适配器在程序进程内同步执行，并使用构建适配器时所选定的 CPython 安装。进程隔离与取消支持可由未来的 Cordis 运行时适配器提供。
[CPython 初始化配置](https://docs.python.org/3.12/c-api/init_config.html)

<a id="optional-typescript-adapter"></a>
## 可选 TypeScript 适配器

显式启用该适配器以下载已锁定的 QuickJS 和 TypeScript 归档文件。CMake 会校验其 SHA-256 摘要，并将编译器脚本与许可证随可选组件一同安装。QuickJS 在转换阶段运行编译器脚本，并在产物包中运行生成的 JavaScript。
[QuickJS C API](https://bellard.org/quickjs/quickjs.html#QuickJS-C-API) ·
[TypeScript Compiler API](https://github.com/microsoft/TypeScript/wiki/Using-the-Compiler-API)

```sh
cmake -S . -B build-ts -DK_BUILD_CORDIS_SOURCE=ON -DK_BUILD_CORDIS_TYPESCRIPT=ON
cmake --build build-ts --target k-cordis-source cordis-typescript
cmake --install build-ts --component cordis-source --prefix ./cordis-tools
cmake --install build-ts --component cordis-typescript --prefix ./cordis-tools
```

在项目的 Cordis 清单中选择此转换器。将清单放在 `cordis-tools` 同级目录下，并根据宿主机使用对应的已安装库文件名。使用与可选构建驱动相同的命令进行解析和构建。

```json
{
  "version": 2,
  "selections": { "source-transform": "typescript" },
  "providers": {
    "typescript": {
      "service": "source-transform",
      "version": 1,
      "library": "cordis-tools/lib/cordis/libk_cordis_typescript_transform.so",
      "inputs": {
        "runtime": "cordis-tools/lib/cordis/libk_cordis_typescript_runtime.so",
        "compiler": "cordis-tools/share/cordis/typescript/typescript.js"
      }
    }
  }
}
```

`i64`、`text` 和 `bytes` 分别转换为 `bigint`、UTF-8 字符串和 `Uint8Array`。每个代码块都在全新的 QuickJS 运行时中同步求值，并将结果导出为 `answer`。结果会被复制为带所有权的 K 值。缺失结果、类型错误、整数溢出或客座异常都会按照与 Python 适配器相同的二元组约定返回错误。

在 `check` 和 `build` 期间，`transpileModule` 会校验语法并降级处理 `enum` 等结构。K 检查器则负责校验生成的 K 侧绑定类型。完整的 TypeScript 语义检查属于独立的项目检查环节。转换器会在 `assets/typescript` 中保留 `.ts`、生成的 `.js` 与 `.js.map`，以及锁定的运行时桥接库和 QuickJS 许可证。语法错误使用 K 的 UTF-8 字节列号；运行时异常则利用 TypeScript 源码映射将 JavaScript 调用栈行号映射回原始 K 文件。
[TypeScript Compiler API](https://github.com/microsoft/TypeScript/wiki/Using-the-Compiler-API) ·
[源码映射格式](https://tc39.es/ecma426/)

模块解析、Node.js 兼容层与隔离式取消支持属于后续里程碑计划。在同一个源码图中同时使用两种客座语言的项目需要组合转换器（composing transform）。

<a id="verification"></a>
## 验证

`cordis_source_transform` 测试通过一个会输出模块、二进制产物与源码映射的夹具来验证版本化契约。`cordis_python_guest` 测试构建真实的 Python 代码块，并检查值拷贝、UTF-8 语法诊断、`SystemExit`、整数溢出以及运行时锁失效机制。`cordis_typescript_guest` 测试则验证真实的 TypeScript 降级转换、BigInt 边界、二进制视图、UTF-8 诊断、调用栈映射以及锁定的编译器输入，并在空可执行文件搜索路径下检查并启动重定位后的产物包。
