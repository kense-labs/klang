<a id="first-class-functions-and-a-typescript-runtime"></a>
# 一等函数与 TypeScript 运行时

K 实现了具象闭包。本设计扩展了可调用对象模型，并定义了一个托管运行时，其长期目标是在 TypeScript 项目中替代 Node.js。兼容性里程碑以在固定版本的 Node.js 发行版上运行现有项目作为基准。

<a id="first-class-function-values"></a>
## 一等函数值

函数值具有受检的参数与返回值签名。它可以被绑定、传递给其他函数、作为返回值、存储在结构体或集合中，并在运行时被选择与调用。这适用于命名函数和闭包。现有的泛型函数实参与尾随代码块保持其特化调用及词法 `return` 行为。
[当前函数值](../reference.md#function-values)

编译器会推断 `make_adder` 的具象返回类型。该闭包拥有 `by` 的一份拷贝。闭包中的 `return` 会返回到它的调用方。

<!-- fragment -->
```klang
fn make_adder(by: i64) {
    return fn[by: i64](x: i64) -> i64 { return x + by; };
}

fn main() {
    let add_three = make_adder(3);
    print(add_three(4));
}
```

编译器像追踪其他值一样追踪闭包的环境。可复制的捕获可以通过按值所有权持有。借用数据的捕获遵循 K 的引用来源（provenance）规则，包括当闭包被返回或放入任务时的情况。仅当被借用捕获所引用的数据在闭包的所有使用期间均保持存活时，该借用捕获才能逃逸其定义作用域。将自有资源移入环境中时，需要对其清理义务进行受检转移。
[具象闭包](../reference.md#concrete-closures)

泛型代码可以针对具象闭包进行特化。对于运行时选择，`fn&` 为不同的闭包布局提供了统一的受检签名。`borrow_callable` 存储一个调用指针和一个环境指针。命名 K 函数仅需要调用指针。编译器通过常规引用根检查借用，并在不进行堆分配的情况下适配每个目标的原生调用约定。对于只读引用捕获，它使用存储记录与参数来源来检查底层存储。这涵盖通过别名替换环境的泛型调用方，包括在实参求值期间发生的替换。
[借用可调用对象](../reference.md#borrowed-callables)

自有形式使用带有显式内联字节容量的 `fn[N]`。`own_callable` 使用与代码生成相同的紧凑布局来检查具象捕获大小。目标跳板函数（trampoline）在调用时将这些字节恢复到类型化存储中，并在此处应用目标的对齐要求。拷贝会保留捕获字节。已知固定存储上的只读捕获会通过可调用对象值携带其常规借用根。
[自有可调用对象](../reference.md#owned-callables)

使用函数组合、`map`、`filter`、`fold` 以及回调注册表来验证泛型调用与运行时调用能够与 K 的带标签枚举及穷尽模式匹配良好组合。将同样的所有权规则扩展到异步闭包。任务必须拥有或有效借用其在挂起期间使用的数据，并在完成或取消时予以释放。作为后续工作，添加受检纯度，使高阶函数能够约束其回调的效应。[异步函数](async_and_decorators.md#async-functions)，
[效应检查研究](llm_frontier.md#34-effects-pure-and-frames)

<a id="runtime-architecture"></a>
## 运行时架构

Node.js 兼容运行时是一个可选的 Cordis 应用配置（profile）。其运行器从项目锁文件中解析转换器、引擎、模块加载器与宿主服务。K 编译器继续通过常规 CLI 处理 K 源码，并且不为该配置引入新的语言模式。原生 K 应用程序可以通过版本化接口共享所选定的服务。

**TypeScript 前端。** 从宿主加载的模块生成 JavaScript 和源码映射（source map），并明确报告不支持的选项。类型检查可以作为独立步骤运行。应明确记录受支持的 `tsconfig.json` 选项。围栏代码块适配器与项目运行器共享运行在 QuickJS 中的固定版本 TypeScript 编译器。项目运行器保留 ESM 导入，并降级处理诸如 `enum` 之类的语法。[TypeScript Compiler API](https://github.com/microsoft/TypeScript/wiki/Using-the-Compiler-API)，
[Node.js TypeScript 支持](https://nodejs.org/api/typescript.html)

**ECMAScript 引擎与 C 互操作。** 将一个成熟引擎嵌入在小型 C 接口之后。可选的围栏代码块适配器通过类型化值拷贝来驱动 QuickJS，并将客座异常映射回原始源码。根据一致性、启动时间、磁盘占用及工作负载基准来评估项目运行时引擎。引擎拥有 JavaScript 对象与闭包。K 代码在其接口一侧使用 K 自身的内存与生命周期规则，通过拷贝的值或带显式释放的保留句柄来交换数据。最初的 C 垫片（shim）同时驱动两侧，并对在外部调用期间会重入 K 的回调进行排队。[围栏 TypeScript 适配器](embedded_languages.md#optional-typescript-adapter)，
[QuickJS C API](https://bellard.org/quickjs/quickjs.html#QuickJS-C-API)，
[K 导出](../reference.md#export-fn)

**宿主服务。** 所选的运行时配置负责模块加载器、Node.js API 行为，以及其引擎与 Cordis I/O 服务之间的集成。文件、网络和子进程访问遵循项目被授予的能力。保持 Promise 作业、Node 的 `nextTick` 队列以及宿主事件按照兼容性测试所观察到的顺序执行。将单个 K 任务的完成适配为一个可被 JavaScript 多次观察结果的 Promise。除非 JavaScript API 明确指定了取消机制，否则将取消控制保留在 K 一侧。[Node.js process API](https://nodejs.org/api/process.html#when-to-use-queuemicrotask-vs-processnexttick)

**分发。** 通过可选的 Cordis 包交付运行器、引擎、转换器与 Node.js 适配器。首个可执行入口点可以是已安装的配置运行器。后续的启动器可以在 K 编译器之外按配置选择进行委派。共享一个有界缓存，用于存放按源码、生效标志、服务版本及锁定输入作为键的已生成 JavaScript 和源码映射。复用项目已安装的依赖树。分别追踪核心安装大小、运行时组件大小、依赖存储以及冷缓存与热缓存的使用情况。

<a id="optional-project-runner"></a>
## 可选项目运行器

与普通 K 编译器一同构建并安装 `cordis-typescript` 组件。运行时与前端均为动态选择的 Cordis 服务。

```sh
cmake -S . -B build-ts -DK_BUILD_CORDIS_TYPESCRIPT=ON
cmake --build build-ts --target cordis-typescript
cmake --install build-ts --component cordis-typescript --prefix ./cordis-tools
```

将 `cordis.json` 放置在 `cordis-tools` 旁，并使用适用于你宿主机的已安装库文件名。

```json
{
  "version": 2,
  "selections": { "typescript-project": "quickjs", "typescript-frontend": "tsc" },
  "providers": {
    "quickjs": {
      "service": "typescript-project",
      "version": 1,
      "library": "cordis-tools/lib/cordis/libk_cordis_typescript_project.so",
      "requires": { "typescript-frontend": { "minVersion": 1, "maxVersion": 1 } }
    },
    "tsc": {
      "service": "typescript-frontend",
      "version": 1,
      "library": "cordis-tools/lib/cordis/libk_cordis_typescript_frontend.so",
      "inputs": { "compiler": "cordis-tools/share/cordis/typescript/typescript.js" }
    }
  }
}
```

```sh
k cordis resolve typescript-project --manifest cordis.json > cordis.lock
./cordis-tools/bin/k-cordis-ts --manifest cordis.json --lock cordis.lock --root ./app main.ts
```

该配置会加载所选项目根目录下的相对 `.ts`、`.mts`、`.js` 与 `.mjs` 模块。当缺少 `.js` 或 `.mjs` 文件时，解析器会尝试同名的 `.ts` 或 `.mts` 文件。静态导入与动态导入共享模块标识。运行器会锁定服务与编译器输入，并在首次读取时捕获每个项目模块，使其在本次调用期间保持不变。

前端使用带 ESM 和语法诊断的 ES2022 输出。运行时会排空 Promise 作业，并报告未处理的拒绝（unhandled rejection）以及在所有作业排空后仍处于挂起状态的顶层 `await`。它通过每个模块的源码映射来映射栈行号。`console.log` 与 `console.error` 会通过运行器输出每个实参的字符串表示。此预览版涵盖本地 ESM 与 Promise 作业。Node.js 内置模块、npm 包解析、定时器事件以及 `tsconfig.json` 选项属于未来的里程碑。

<a id="implementation-and-acceptance"></a>
## 实现与验收

1. **K 闭包。** 实现具象闭包，并验证其绑定、传递、返回与存储。对试图返回借用了返回函数内部存储的闭包的做法给出诊断信息。随后测试跨越不同捕获布局的运行时选择，以及异步闭包在挂起和取消期间的生命周期。对比通过 TinyCC 与原生 C 的执行情况。
2. **TypeScript 执行原型。** 可选运行器在无 Node 安装的环境下执行包含 ESM 导入与顶层 `await` 的多文件 `.ts` 项目，并将失败位置指回对应的 TypeScript 代码行。常规 K 安装不依赖任何客座引擎。使用固定版本的 [Test262](https://github.com/tc39/test262) 子集检验引擎集成。
3. **首个 Node.js 替代目标。** 运行一个现有的 TypeScript CLI，该 CLI 读写 JSON 并导入同时包含 ESM 与 CommonJS 的固定 npm 依赖图。覆盖夹具所使用到的包 `exports` 与 `imports`、`node:fs`、`node:path`、`process`、`Buffer` 以及定时器。当相同的项目与依赖树在 K 运行时和参考 Node.js 发行版下产生一致的输出、文件、退出状态码及相关错误时，即通过该版本验收。该里程碑使用预先准备好的锁定依赖树。[Node.js 包](https://nodejs.org/api/packages.html)
4. **更广泛的项目兼容性。** 按真实项目夹具所需的顺序添加带背压（backpressure）的 HTTP 与流、子进程、worker、测试 API 以及包脚本。包含一个能够流式输出响应、启动工具子进程并取消在途工作的 TypeScript 智能体（agent）测试框架。定义所支持的 Node-API 版本，并针对这些版本测试原生插件。在兼容性矩阵中记录特定于 V8 和 Node 的插件。对比包括回调顺序与错误码在内的可观测行为。[Node-API](https://nodejs.org/api/n-api.html)
