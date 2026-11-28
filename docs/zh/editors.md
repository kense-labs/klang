<a id="k-in-other-editors"></a>
# 在其他编辑器中使用 K

编译器本身就是自己的语言服务器：`k lsp` 通过 stdin 和 stdout 使用 Language Server Protocol（语言服务器协议），因此任何带有 LSP 客户端的编辑器都可以使用它。VS Code 提供了一个[扩展](vscode.md)会自动为你启动该服务。对于 Neovim、Helix 和 Emacs，只需如下配置即可。

以下所有配置片段都假定编译器位于 `PATH` 中并直接启动 `k lsp`；如果不在 `PATH` 中，请将 `k` 替换为可执行文件的完整路径。语言服务器查找标准库的方式与 `k check` 相同（`K_STDLIB`，或可执行文件附近的 `stdlib/` 目录）。对于不在源码检出目录中的编译器，请在命令中加上 `--stdlib` 及对应目录：`k lsp --stdlib /path/to/klang/stdlib`。

每个打开的文件都会连同它导入的文件一起，作为一个独立的程序进行分析。如果希望将某个模块作为导入它的程序的一部分进行检查（并在该模块中显示该程序的错误），请在客户端的初始化选项中以绝对路径指定该程序的入口文件：`{ "entryFiles": ["/path/to/project/main.k"] }`。下文的配置片段展示了这些选项应放在何处。

服务器的语义标记（semantic tokens）会根据名称解析到的目标（函数、类型、字段、变体、参数、变量）进行着色。它们不涵盖关键字、字符串或注释，而 TextMate 语法仅包含在 VS Code 扩展中（已支持 `using`、`errdefer`、`async`/`await`、`?.`/`??`、解构绑定、`.Variant` 简写以及无 `main` 顶层脚本的高亮）。

<a id="neovim"></a>
## Neovim

Neovim 0.11 及以上版本，在 `init.lua` 中配置：

```lua
vim.filetype.add({ extension = { k = 'klang' } })
vim.lsp.config('klang', {
  cmd = { 'k', 'lsp' },
  filetypes = { 'klang' },
  root_markers = { '.git' },
  -- init_options = { entryFiles = { '/path/to/project/main.k' } },
})
vim.lsp.enable('klang')
```

在 Neovim 0.10 上，请改为根据文件类型启动：

```lua
vim.filetype.add({ extension = { k = 'klang' } })
vim.api.nvim_create_autocmd('FileType', {
  pattern = 'klang',
  callback = function(ev)
    vim.lsp.start({ name = 'klang', cmd = { 'k', 'lsp' }, root_dir = vim.fs.root(ev.buf, { '.git' }) })
  end,
})
```

默认情况下，Neovim 会显示诊断信息，支持悬停提示（`K`）、跳转到定义（`CTRL-]`）、查找引用（`grr`）、重命名（`grn`）、文档符号（`gO`）、签名帮助（插入模式下按 `CTRL-S`）、代码补全（`CTRL-X CTRL-O`，或通过 `vim.lsp.completion.enable` 实现输入时自动补全）以及语义高亮。内嵌提示（inlay hints）需要调用 `vim.lsp.inlay_hint.enable()`，代码折叠需要设置 `foldmethod=expr` 和 `foldexpr=v:lua.vim.lsp.foldexpr()`，代码透镜（code lenses）则需要调用 `vim.lsp.codelens.refresh()`（例如在 `BufEnter` 和 `InsertLeave` 事件中调用）。泛型函数上方的代码透镜会统计其实例化数量。若要通过 `vim.lsp.codelens.run()` 列出这些实例化，请为 Neovim 注册该命令并告知服务器客户端已支持它：

```lua
vim.lsp.commands['klang.showInstantiations'] = function(command)
  vim.ui.select(command.arguments[2], { prompt = 'Instantiations of ' .. command.arguments[1] }, function() end)
end
-- and in vim.lsp.config('klang', ...):
--   init_options = { commands = { 'klang.showInstantiations' } },
```

<a id="helix"></a>
## Helix

在 `languages.toml`（位于 Helix 的配置目录中，或项目内的 `.helix/` 目录下）中配置：

```toml
[language-server.klang]
command = "k"
args = ["lsp"]
# config = { entryFiles = ["/path/to/project/main.k"] }

[[language]]
name = "klang"
scope = "source.klang"
file-types = ["k"]
roots = [".git"]
comment-token = "//"
block-comment-tokens = { start = "/*", end = "*/" }
indent = { tab-width = 4, unit = "    " }
language-servers = ["klang"]

[[grammar]]
name = "klang"
source = { path = "/absolute/path/to/klang/tree-sitter" }
```

（`config` 即 Helix 作为初始化选项发送的内容。）

Helix 支持显示诊断信息、悬停提示（`space k`）、跳转到定义（`gd`）、查找引用（`gr`）、重命名（`space r`）、文档及工作区符号（`space s`、`space S`）、签名帮助、输入时自动补全，以及通过在 `config.toml` 的 `[editor.lsp]` 下设置 `display-inlay-hints = true` 启用内嵌提示。Helix 不支持语义标记、代码折叠或代码透镜。

语法高亮需要 Helix 25.07 或更高版本：与 Helix 25.01 不同，这些查询规则依赖于最后一个匹配的模式生效。请从 K 源码检出目录构建语法并安装 Helix 查询文件（请根据实际情况替换路径）：

```sh
hx --grammar build
mkdir -p ~/.config/helix/runtime/queries/klang
cp /absolute/path/to/klang/tree-sitter/queries/helix/*.scm \
    ~/.config/helix/runtime/queries/klang/
hx --health klang
```

生成的解析器已提交至版本库，因此构建时只需 C 编译器，无需 Node.js。这些查询还提供自动缩进以及函数/类/参数/注释文本对象。基于语法的符号（`tags.scm`）和彩虹括号（`rainbows.scm`）需要从 master 分支构建的 Helix；25.07.1 发行版不会读取这两个查询文件。若要在 master 版本上使用基于语法的符号，请一并安装共享的 tags 查询文件：

```sh
cp /absolute/path/to/klang/tree-sitter/queries/tags.scm \
    ~/.config/helix/runtime/queries/klang/
```

安装或更新语法和查询文件后请重启 Helix；拉取较新的 K 源码更新时，请同步更新这两者。

<a id="emacs"></a>
## Emacs

使用 Eglot（自 Emacs 29 起内置），在 `init.el` 中配置：

```elisp
(define-derived-mode klang-mode prog-mode "K"
  (setq-local comment-start "// "))
(add-to-list 'auto-mode-alist '("\\.k\\'" . klang-mode))
(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs '(klang-mode "k" "lsp")))
(add-hook 'klang-mode-hook #'eglot-ensure)
```

如需指定入口文件，服务器程序配置可在命令后附带选项：`'(klang-mode "k" "lsp" :initializationOptions (:entryFiles ["/path/to/project/main.k"]))`。

Eglot 通过 Flymake 显示诊断信息，在 ElDoc 中显示悬停提示和签名帮助，支持跳转到定义（`M-.`）、查找引用（`M-?`）、重命名（`eglot-rename`）、在 Imenu 中显示文档符号、工作区符号（`xref-find-apropos`）、代码补全（`completion-at-point`）以及内嵌提示；来自 GNU ELPA 的最新版 Eglot 还支持使用语义标记进行高亮（`eglot-semantic-tokens-mode`）。Eglot 不支持代码折叠或代码透镜。

<a id="formatting"></a>
## 格式化

`k format` 会按标准排版格式重写文件：包括缩进、词元（token）之间的空格、空行和尾随逗号，同时保留原有的换行位置。与 gofmt 类似，连续一组声明（连续的 `const`、结构体的字段）中的类型和 `=` 会按列对齐。它接受文件和目录作为参数（目录代表其下的 `.k` 文件）；如果不传参数或传入 `-`，则将标准输入格式化后输出到标准输出。无法解析的文件将保持原样不变并打印错误，同时以状态码 2 退出；`k format --check` 不会写入任何内容，而是列出尚未格式化的文件并以状态码 1 退出，适用于 CI 步骤或提交钩子（commit hook）。

语言服务器同样支持格式化（`textDocument/formatting`），它针对编辑器中的文本而非磁盘上的文件进行操作，且仅修改发生变化的行，从而保留其他行上的光标和标记。无法解析的文档不会收到任何编辑；其诊断信息会说明原因。

在 Neovim 中，可通过 `vim.lsp.buf.format()` 进行格式化；若要在保存时自动格式化，可在上述配置中添加：

```lua
vim.api.nvim_create_autocmd('BufWritePre', {
  pattern = '*.k',
  callback = function() vim.lsp.buf.format() end,
})
```

在 Helix 中，可通过 `:format` 进行格式化，在上述 `[[language]]` 条目中设置 `auto-format = true` 即可在保存时自动格式化。

在 Emacs 中，可通过 `eglot-format-buffer` 进行格式化；若要在保存时自动格式化，可添加：

```elisp
(add-hook 'klang-mode-hook
          (lambda () (add-hook 'before-save-hook #'eglot-format-buffer nil t)))
```

如果不使用语言服务器，编辑器也可以改用针对缓冲区运行 `k format -`：例如在 Neovim 中配合 [conform.nvim](https://github.com/stevearc/conform.nvim) 使用：

```lua
require('conform').setup({
  formatters = { klang = { command = 'k', args = { 'format', '-' }, stdin = true } },
  formatters_by_ft = { klang = { 'klang' } },
  format_on_save = { timeout_ms = 2000 },
})
```

或者在 Helix 的 `[[language]]` 条目中配置 `formatter = { command = "k", args = ["format", "-"] }`。
