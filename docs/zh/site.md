<a id="the-documentation-site"></a>
# 文档站点

这些文档是可在 GitHub 上直接阅读的 Markdown 文件，文件之间通过相对链接关联。本目录使用 pandoc 将这些文件构建为适用于 GitHub Pages 的静态站点。它会保留仓库的目录布局，从而确保链接持续有效：`README.md` 会生成 `index.html`，`docs/tutorial.md` 会生成 `docs/tutorial.html`，`samples/README.md` 会生成 `samples/index.html`。每个示例程序还会拥有各自带有语法高亮的页面。

```bash
python docs/site/build.py --out build/site --check
```

该命令要求 `PATH` 中包含 pandoc 2.11 或更高版本。生成的页面会写入 `build/site`；可在浏览器中打开 `build/site/index.html`。`--check` 会校验构建后站点中的每一个内部链接和片段锚点（包括文档间的 `spec §` 链接），一旦发现失效链接即会报错退出。

| 文件 | 职责 |
|---|---|
| `build.py` | 列出页面、对每个页面运行 pandoc、注入 KUI 导航与首屏（hero）框架、复制静态资源和 `docs/html`，并检查内部链接 |
| `template.html` | KUI 风格的页面外壳：吸顶分段式顶部栏、可折叠左侧边栏、主内容框架、命令面板模态框、图表灯箱（lightbox） |
| `style.css` | KUI 设计系统令牌（浅色/深色主题、强调色预设）及组件样式 |
| `site.js` | 主题/强调色切换、代码块复制栏、交互式首屏展示区、图表灯箱及移动端抽屉菜单 |
| `search.js` | 驱动 `⌘K` / `/` 命令面板的零依赖模糊搜索 |
| `links.lua` | 一个 pandoc 过滤器：将 `.md` 链接转换为 `.html`，使 `.k` 链接指向示例页面，并移除各文件为适配 GitHub 而附带的 `<details>` 目录，改用站点导航 |
| `klang.xml` | 供 pandoc 使用的 K 语言语法高亮定义，采用 KDE 语法定义格式 |

Pandoc 以 GitHub Flavored Markdown 格式读取这些文件，因此生成的标题锚点与 GitHub 一致（如 `#33-the-array-family`），教程和参考文档中的跨文档链接在两边都能正确解析。

工作流 `.github/workflows/pages.yml` 会在每次推送到 `main` 分支时构建该站点，并在启用了 Pages 的仓库上进行部署。请在 Settings、Pages、Build and deployment 下将 Pages 来源设置为 **GitHub Actions** 以启用发布。

如需添加页面，请在仓库中对应的位置编写 Markdown 文件；若其所在目录尚未列入，请将其添加到 `build.py` 的 `PAGES` 中（`docs/*.md`、`docs/design/*.md` 和 `bench/*.md` 下的所有文件都会被自动收录）。已经是成品 HTML 的页面应放入 `docs/html`，构建过程会原样复制其中的文件，并在 `docs/index.md` 中添加指向它的链接。站点自身的索引页为 [`docs/index.md`](index.md)。
