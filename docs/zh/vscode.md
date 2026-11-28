<a id="k-for-visual-studio-code"></a>
# K 的 Visual Studio Code 扩展

[K 语言](https://github.com/kense-labs/klang)的编辑器扩展。当所使用的编译器支持 `k lsp` 时，编译器本身即作为语言服务器运行：扩展会启动该服务器，服务器会在你输入代码时实时分析程序。对于较旧版本的编译器，扩展则改用 `k check` 检查已保存的文件。
其他编辑器也可使用同一个服务器：[其他编辑器中的 K](editors.md) 提供了 Neovim、Helix 和 Emacs 的配置说明。

<a id="features"></a>
## 功能特性

- 使用语言服务器时：
  - 在输入代码时实时显示整个程序（由 `klang.entryFile` 指定的入口文件，或当前打开的文件）所有文件上的错误与警告；
  - 悬停提示，展示声明、类型、实例化以及文档注释；
  - 跳转到定义（包括跨导入文件跳转以及在 `import` 语句行上跳转）；
  - 在程序的所有文件中查找所有引用和重命名符号；
  - 当前文件与工作区的大纲（Outline）和“转到符号”（Go to Symbol）；
  - 对解析为函数、类型、参数、字段或枚举变体的名称进行语义高亮；
  - 在输入调用参数时提供签名帮助；
  - 为 `let` 与 `var` 的类型以及调用处的参数名称提供内联提示（Inlay Hints）；
  - 折叠代码块与连续注释；
  - 按语法结构扩展选区；
  - 代码镜头（CodeLens）：在每个泛型函数上方显示其实例化次数（点击可列出具体位置），在 `main` 上方显示 **Run**（点击即可通过 JIT 运行该文件）。

  **K: Restart Language Server** 命令可重启语言服务器；将 `klang.useLanguageServer` 设为关闭即可禁用它。

- `.k` 文件的语法高亮，包括嵌套块注释、转义字符串与字符、跨行原始 `"""` 字符串、十进制/十六进制浮点数、命名空间以及内建标识符。
- 在编辑器和 Markdown 预览中，为 Markdown 里的 `klang` 围栏代码块（``` 或 `~~~`）提供语法高亮。
- 括号匹配、自动闭合、自动缩进、注释切换、代码折叠以及代码片段（`main`、`fn`、`struct`、`enum`、`for`、`guard`、`match` 等）。
- 不使用语言服务器时：在打开或保存文件时执行编译器检查，提供错误与警告下划线波浪线以及“问题（Problems）”面板条目。使用 **F8 / Shift+F8** 可在问题之间跳转。支持 `k check --diagnostics json` 的编译器会报告每条错误与警告的精确源码范围并据此绘制下划线，且实例化调用链会在发起调用的位置作为“问题”中的关联位置显示。对于较旧的编译器，扩展会改用解析其文本输出：词法错误使用编译器输出的插入符（caret）位置，类型错误高亮整行源码，实例化调用链则链接到调用所在行。
  只要有 K 文件处于打开状态，状态栏就会显示最近一次检查结果：**K** 加对勾图标、**K: N errors**（点击打开“问题”面板），或在检查无源码位置失败时显示 **K: Check Failed**（未找到编译器、超时或无位置信息的报错）。检查失败时还会向受检文件的“问题”面板添加一条无文本下划线的警告，点击状态栏条目则会打开“K”输出面板以查看完整的编译器输出。使用语言服务器时，该状态栏条目统计的是语言服务器报告的错误数量。其旁的 **Run** 按钮会通过 JIT 运行当前文件。
- 命令面板中提供 **K: Check Program**、**K: Run with JIT**、**K: Build Executable**（`k build`：在源码旁生成可执行文件，并链接程序所用的包）、**K: Generate C** 以及 **K: Show Compiler Output** 命令。编辑器的运行播放按钮与状态栏中的 **Run** 按钮会在任务终端中运行程序，支持标准输入（stdin）和可点击跳转的错误信息。
- 编辑 K 文件时，可使用 **Ctrl+Shift+B**、**F5** 和 **Ctrl+F5** 通过 JIT 运行程序（在 macOS 上使用 **Cmd+Shift+B** 进行构建）。VS Code 的运行配置选择器中也提供了 **K: Run with JIT**。
- 不使用语言服务器时，词法扫描器会为大纲、面包屑导航和“转到符号”（**Ctrl+Shift+O**）提供顶层声明信息。导入路径也支持点击跳转，并按照编译器相同的规则解析：相对于根目录、相对于当前导入文件（`import .foo;`）或位于标准库中。
- 提供 Check、Run、Build 和 Generate C 任务，以及 `$klang` 问题匹配器（Problem Matcher），可用于处理绝对路径或相对于工作区文件夹路径的自定义任务。

语言服务器还支持作用域内名称与 `.` 后成员的自动补全、查找引用与调用方、重命名、显示内联提示、折叠以及代码格式化：**Format Document**（Shift+Alt+F）会按 `k format` 的规则排版文件，设置 `"editor.formatOnSave": true` 则会在每次保存时自动格式化。目前语言服务器尚不提供调试功能（设计规划见 `docs/design/language_server.md`）。没有任何实例化到达的函数体没有类型信息：悬停提示会说明这一点，该处的成员补全则会列出所有字段和函数。

<a id="install-the-prebuilt-extension"></a>
## 安装预构建扩展

代码仓库中包含预构建包 [klang-language.vsix](../../vscode/klang-language.vsix)。下载该文件并在 VS Code 中选择 **Extensions: Install from VSIX...**，或在本地检出仓库后运行：

```sh
code --install-extension vscode/klang-language.vsix
```

安装预构建扩展不需要 Python、Node.js 或 npm。检查代码和运行程序仍需要 K 编译器（见下文）。

<a id="build-or-develop"></a>
## 构建或开发

如需重新构建 VSIX 安装包，请安装 Python 3 以及带有 npm 的 Node.js（推荐 Node.js 22+），然后在仓库根目录运行：

```sh
python vscode/build_vsix.py
```

该脚本可在任意工作目录下运行。它会自动安装锁定的 npm 依赖、运行扩展测试、使用 esbuild 将扩展及语言客户端库打包到 `dist/extension.js`，并生成 `vscode/klang-language.vsix`。传入 `--skip-install` 可复用现有的 `node_modules`。该 VSIX 文件受 Git 版本追踪。更新扩展时请一并提交重新构建的 VSIX 文件，以便他人无需本地构建即可直接安装。扩展版本号仍取自 `package.json`。

在 VS Code 中打开此 `vscode/` 目录并按下 **F5**，即可启动一个默认打开上级 K 代码仓库的扩展开发宿主（Extension Development Host）窗口；启动前会先执行打包（`npm run bundle`，需先运行过一次 `npm install`）。
打包仅在本地进行，该扩展未发布到 VS Code Marketplace。
本扩展基于 Apache License 2.0 分发。构建过程会将许可证文件复制到当前目录以供 `vsce` 打包。[Apache 2.0 许可证](../../LICENSE-APACHE)

<a id="compiler-setup"></a>
## 编译器配置

请单独构建 K 编译器。默认情况下，扩展会在工作区文件夹中依次查找 `k`、`build/Release/k`、`build/Debug/k` 和 `build/k`（在 Windows 上带有 `.exe` 后缀），之后再使用 `PATH` 中的 `k`。如需指定其他位置，请将 `klang.compilerPath` 设置为可执行文件路径，且不要包含 shell 引号或命令行参数。

检查器允许文件不包含 `fn main()`，因此编辑被导入的文件时不会报错缺少 `main`。对于包含多个导入文件的项目，请设置入口文件，使检查和运行作用于整个程序：

```json
{
  "klang.compilerPath": "${workspaceFolder}/build/Release/k.exe",
  "klang.entryFile": "samples/01_tour.k",
  "klang.checkOnSave": true,
  "klang.checkTimeout": 15000,
  "klang.runArguments": []
}
```

配置中的路径相对于所在工作区文件夹；若未打开文件夹，则相对于源文件所在目录。在多根工作区中，可按文件夹分别配置这些设置。`klang.stdlibPath` 用于传递 `--stdlib`；留空则使用 K 的默认查找路径，包括 `K_STDLIB` 环境变量及可执行文件附近的目录。

语言服务器在每个窗口仅启动一次。它会使用第一个工作区文件夹（若无工作区文件夹，则使用当前打开文件）所配置的编译器和标准库，并将每个工作区文件夹的 `klang.entryFile` 作为独立程序进行分析。未被任何入口文件导入的 K 文件则会像 `k check` 那样单独分析。扩展会在激活时以及 K 设置、工作区文件夹或工作区信任状态发生变更时向编译器探测是否支持语言服务器（`k lsp --probe`）；若不支持，则回退到下文所述的检查方式。语言服务器的输出显示在“K”输出通道中。语言服务器不会自动拉取包：未拉取的包会在其导入处报出错误，可通过 **K: Check Program**（启用语言服务器时该命令会在终端运行 `k check`）或执行 `k fetch` 来解决。

不使用语言服务器时，检查通过 `k check --diagnostics json` 执行，绝不会运行程序或生成 C 代码。若编译器拒绝该参数，扩展之后将一直回退到普通的 `k check` 直到编译器重新构建，并改为解析文本输出；无论哪种方式，“K”输出通道都会显示编译器的文本输出。检查针对已保存的文件运行，当同一文件夹中有任何已打开的 K 文件存在未保存更改时会暂停等待，并保留上一次的检查结果（标记为过期/stale）直到下一次检查完成。更改 K 设置、调整工作区文件夹，或关闭使用某入口文件的所有文件时，会清除对应的检查结果。
显式执行命令前会先保存同一文件夹中已打开的 K 文件。编译器目前会在遇到第一个错误时停止，且只对实例化的函数进行类型检查；扩展同样保留了这些限制。配置入口文件对于库项目尤其有用。

**Run with JIT** 使用 `--jit` 参数，需要启用了 TinyCC 的 K 编译器构建版本。
**Generate C** 会在入口文件旁生成同名的 `.c` 文件，并覆盖已存在的同名文件。在不受信任的工作区中会禁用编译器执行；此时语法高亮、代码片段和大纲功能仍可正常使用。

<a id="tasks"></a>
## 任务

JIT Run 是本扩展的默认构建任务。编辑 K 文件时按 **Ctrl+Shift+B** 即可直接运行；Check、Build Executable 和 Generate C 则保留为 **Tasks: Run Task** 与命令面板中的显式命令。编辑器播放按钮与状态栏中的 **Run** 按钮调用的是同一个 JIT 命令。保存时检查（Check-on-save）仍然只检查程序而不运行。

当 K 编辑器获得焦点时，K 的快捷键优先生效。若希望改回你原有的 VS Code 构建/调试快捷键，请设置 `"klang.useRunKeybindings": false`。现有 `tasks.json` 中的分组（group）与默认（default）配置会被保留。由其他扩展（例如 CMake Tools）提供的按钮仍控制各自的构建系统；运行 K 请使用 **Run** 按钮。

**Tasks: Run Task** 会为当前 K 文件（或已配置的入口文件，即使当前未激活 K 编辑器）提供相关任务。
如需持久化任务配置，可将以下内容添加到项目的 `.vscode/tasks.json` 中：

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "type": "klang",
      "label": "Run K with JIT",
      "action": "run",
      "file": "main.k",
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": "$klang"
    },
    {
      "type": "klang",
      "label": "Generate C",
      "action": "generateC",
      "file": "main.k",
      "output": "main.c"
    }
  ]
}
```

`file` 默认指向 `klang.entryFile` 或当前活动的 K 文档；支持 `${file}` 和 `${workspaceFolder}` 变量。`output` 默认为将入口文件扩展名替换为 `.c`。任务直接向进程传递参数，包括带有空格的路径。如需在运行自定义任务前自动保存已编辑文件，请使用 VS Code 自带的任务保存行为设置。

<a id="run-configurations"></a>
## 运行配置

在“运行和调试（Run and Debug）”配置选择器中选择 **K: Run with JIT**，即可使用 VS Code 原生的运行控制面板。这会使用 `klang.entryFile` 或当前 K 文件启动同一个终端任务。也可以通过可选的 `.vscode/launch.json` 配置来指定要运行的特定程序：

```json
{
  "version": "0.2.0",
  "configurations": [{
    "type": "klang",
    "request": "launch",
    "name": "K: Run with JIT",
    "program": "${workspaceFolder}/main.k",
    "noDebug": true
  }]
}
```

运行配置启动程序时不支持断点或单步调试。要停止正在运行的程序，可使用 **Tasks: Terminate Task** 或点击对应终端的垃圾桶图标。程序命令行参数仍取自 `klang.runArguments`。

<a id="verification"></a>
## 验证

`npm test` 覆盖 TextMate/Oniguruma 语法、声明与导入扫描，以及 JSON 和文本格式诊断信息的解析（包括 Windows 路径和 UTF-8 字节列号）。它还测试从 `--diagnostics json` 回退到文本格式、任务参数处理、任务取消以及过期结果的处理逻辑。语言服务器测试会覆盖探测（probing）与初始化配置，随后通过真实协议验证文本更改后的诊断信息更新及悬停（hover）响应。

当上级代码仓库中存在 K 编译器构建产物时，还会运行编译器集成测试。可通过设置 `K_TEST_COMPILER` 来测试其他可执行文件。若找不到编译器，相关测试将标记为跳过（skip）。同样地，当编译器不支持 `--diagnostics json` 参数时 JSON 测试会跳过，当编译器不支持 `lsp` 子命令时语言服务器测试会跳过。

对于编辑器集成测试套件，请先在上级代码仓库中创建一个空目录 `build/klang-extension-fixture`，然后在当前目录的“运行和调试（Run and Debug）”菜单中选择 **Test K Extension**。该测试套件仅在该临时目录中写入测试夹具和配置。在关闭语言服务器的情况下，它会测试扩展激活、真实编译器诊断、关联错误位置、导入、大纲、任务、编辑/保存生命周期，以及通过命令、构建任务和运行配置发起的真实 JIT 执行。随后，如果编译器支持语言服务器，它还会检查语言服务器返回的悬停响应及其代码镜头（CodeLens）：泛型函数的实例化次数，以及 **Run** 运行其所在文件而非入口文件的行为。运行前请先使用 TinyCC 构建编译器。

本扩展的实现使用了 VS Code 的 [TextMate 语法支持](https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide)、[任务提供程序 API](https://code.visualstudio.com/api/extension-guides/task-provider) 以及 [工作区信任 API](https://code.visualstudio.com/api/extension-guides/workspace-trust)。
