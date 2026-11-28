#!/usr/bin/env python3
"""Build the documentation site with pandoc in the KUI design system.

The site keeps the repository's layout: README.md becomes index.html, each
docs/*.md becomes docs/*.html, samples/README.md becomes samples/index.html,
and so on, so that every relative link between the files works after
links.lua turns `.md` into `.html`. Each sample program gets a highlighted
page of its own, which the links to it reach, and the files under
docs/html are copied as they are.

    python docs/site/build.py --out build/site --check

needs pandoc 2.11 or later on PATH (`--pandoc` names one elsewhere).
`--check` verifies every internal link and fragment of the built site and
fails the build on a broken one, which is how the Pages workflow runs it.
"""
import argparse
import glob
import hashlib
import html
import os
import re
import shutil
import subprocess
import sys
from html.parser import HTMLParser
from urllib.parse import unquote

SITE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(SITE))

# Markdown pages, as (source relative to the repository, output page).
PAGES = []
PAGES.append(("README.md", "index.html"))
PAGES.append(("README.zh-CN.md", "README.zh-CN.html"))
PAGES.append(("docs/index.md", "docs/index.html"))
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "*.md"))):
    name = os.path.basename(src)
    if name != "index.md":
        PAGES.append(("docs/" + name, "docs/" + name[:-3] + ".html"))
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "design", "*.md"))):
    name = os.path.basename(src)
    PAGES.append(("docs/design/" + name, "docs/design/" + name[:-3] + ".html"))
PAGES.append(("docs/site/README.md", "docs/site/index.html"))
PAGES.append(("samples/README.md", "samples/index.html"))
for src in sorted(glob.glob(os.path.join(ROOT, "bench", "*.md"))):
    name = os.path.basename(src)
    PAGES.append(("bench/" + name, "bench/" + name[:-3] + ".html"))
PAGES.append(("vscode/README.md", "vscode/index.html"))
PAGES.append(("test/k_in_k/README.md", "test/k_in_k/index.html"))

# Source files shown as highlighted pages, and files copied as they are.
SOURCES = sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                 for p in glob.glob(os.path.join(ROOT, "samples", "*.k")))
SOURCES.append("LICENSE")
COPIES = sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                for p in glob.glob(os.path.join(ROOT, "docs", "logo", "*.png")))
for pattern in ("*.png", "svg/*.svg"):
    COPIES += sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                     for p in glob.glob(os.path.join(ROOT, "docs", "diagrams", pattern)))
COPIES.extend(("LICENSE-APACHE", "NOTICE", "vscode/klang-language.vsix"))
# The raw results the benchmark reports link to.
COPIES += sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                 for p in glob.glob(os.path.join(ROOT, "bench", "*.json")))
# Finished HTML pages, and anything they load, are published untouched.
COPIES += sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                 for p in glob.glob(os.path.join(ROOT, "docs", "html", "**", "*"), recursive=True)
                 if os.path.isfile(p))

# Hashed asset arguments passed to pandoc template metadata
ASSET_ARGS = []

TOPNAV_ITEMS = [
    ("Overview", "index.html", {"index.html", "README.zh-CN.html", "docs/architecture.html", "docs/roadmap.html"}),
    ("Docs", "docs/index.html", {"docs/index.html", "docs/site/index.html"}),
    ("Tutorial", "docs/tutorial.html", {"docs/tutorial.html", "docs/stdlib.html"}),
    ("Reference", "docs/reference.html", {"docs/reference.html", "docs/k_spec.html"}),
    ("Diagrams", "docs/diagrams.html", {"docs/diagrams.html", "docs/implementation.html"}),
    ("Samples", "samples/index.html", {"samples/index.html"}),
    ("Benchmarks", "bench/summary.html", {"bench/summary.html", "bench/results.html", "bench/design.html"}),
]


def title_of(md_text, fallback):
    for line in md_text.split("\n"):
        m = re.match(r"^#\s+(.*)", line)
        if m:
            t = re.sub(r"`", "", m.group(1)).strip()
            return t
    return fallback


def short_title(out_url, fallback_title):
    custom = {
        "index.html": "Overview",
        "README.zh-CN.html": "简体中文",
        "docs/index.html": "Documentation Index",
        "docs/diagrams.html": "Architecture & Diagrams",
        "docs/architecture.html": "K Architecture",
        "docs/roadmap.html": "Roadmap",
        "docs/tutorial.html": "Tutorial",
        "docs/reference.html": "Language Reference",
        "docs/k_spec.html": "Specification",
        "docs/stdlib.html": "Standard Library",
        "docs/command_line.html": "Command Line (k)",
        "docs/packages.html": "Packages & Builds",
        "docs/editors.html": "Editors & LSP",
        "vscode/index.html": "VS Code Extension",
        "docs/implementation.html": "Compiler Implementation",
        "docs/testing.html": "Testing",
        "test/k_in_k/index.html": "Self-Hosting (k_in_k)",
        "docs/site/index.html": "Site Generator",
        "bench/summary.html": "Benchmark Summary",
        "bench/results.html": "Full Results",
        "bench/design.html": "Benchmark Design",
        "samples/index.html": "Samples Index",
        "LICENSE.html": "MIT License",
    }
    if out_url in custom:
        return custom[out_url]
    return fallback_title


def build_sidebar_groups():
    """Organize all built pages into KUI sidebar collapsible groups."""
    page_titles = {}
    built_urls = set()
    for src_rel, out_url in PAGES:
        src_path = os.path.join(ROOT, src_rel)
        if os.path.exists(src_path):
            text = open(src_path, encoding="utf-8").read()
            page_titles[out_url] = short_title(out_url, title_of(text, os.path.basename(src_rel)))
            built_urls.add(out_url)

    overview_order = [
        "index.html",
        "README.zh-CN.html",
        "docs/index.html",
        "docs/diagrams.html",
        "docs/architecture.html",
        "docs/roadmap.html",
    ]
    language_order = [
        "docs/tutorial.html",
        "docs/reference.html",
        "docs/k_spec.html",
        "docs/stdlib.html",
    ]
    toolchain_order = [
        "docs/command_line.html",
        "docs/packages.html",
        "docs/editors.html",
        "vscode/index.html",
        "docs/implementation.html",
        "docs/testing.html",
        "test/k_in_k/index.html",
        "docs/site/index.html",
    ]
    bench_order = [
        "bench/summary.html",
        "bench/results.html",
        "bench/design.html",
    ]

    groups = []

    def add_group(key, label, urls, default_open=True):
        items = [(u, page_titles[u]) for u in urls if u in built_urls]
        if key == "language" and os.path.exists(os.path.join(ROOT, "docs", "html", "K, Byte by Byte.html")):
            items.append(("docs/html/K,%20Byte%20by%20Byte.html", "K, Byte by Byte"))
        if items:
            groups.append((key, label, items, default_open))

    add_group("overview", "Overview", overview_order, True)
    add_group("language", "Language", language_order, True)
    add_group("toolchain", "Compiler & Tools", toolchain_order, True)
    add_group("benchmarks", "Benchmarks", bench_order, True)

    design_urls = sorted(u for u in built_urls if u.startswith("docs/design/"))
    add_group("design", "Design Notes", design_urls, False)

    sample_items = []
    if "samples/index.html" in built_urls:
        sample_items.append(("samples/index.html", page_titles["samples/index.html"]))
    for s in SOURCES:
        sample_items.append((s + ".html", os.path.basename(s)))
    groups.append(("samples", "Samples", sample_items, False))

    return groups


def render_topnav(root, out_url):
    links = []
    for label, target, active_set in TOPNAV_ITEMS:
        is_cur = out_url in active_set
        if label == "Samples" and out_url.startswith("samples/"):
            is_cur = True
        elif label == "Diagrams" and out_url.startswith("docs/design/"):
            is_cur = True
        elif label == "Docs" and out_url in {"docs/command_line.html", "docs/packages.html", "docs/editors.html", "docs/testing.html", "vscode/index.html", "test/k_in_k/index.html"}:
            is_cur = True
        cur_attr = ' aria-current="true"' if is_cur else ""
        links.append(f'<a href="{root}{target}"{cur_attr}>{html.escape(label)}</a>')
    return '<nav class="company-nav" aria-label="Primary sections">' + "".join(links) + "</nav>"


def render_sidebar(root, out_url, groups):
    parts = []
    chevron = '<svg class="sb-group-chevron" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M6 4l4 4-4 4"/></svg>'
    for key, label, items, default_open in groups:
        contains_active = any(u == out_url for u, _ in items)
        open_attr = " open" if (default_open or contains_active) else ""
        rows = []
        for u, title in items:
            cur = ' aria-current="page"' if u == out_url else ""
            rows.append(f'<a class="sb-row" href="{root}{u}"{cur}>{html.escape(title)}</a>')
        parts.append(
            f'<details class="sb-group" data-group="{key}"{open_attr}>'
            f'<summary class="sb-group-head">'
            f'<span class="sb-group-label">{chevron}<span>{html.escape(label)}</span></span>'
            f'<span class="sb-group-count">{len(items)}</span>'
            f'</summary>'
            f'<div class="sb-rows">{"".join(rows)}</div>'
            f'</details>'
        )
    return "".join(parts)


def render_hero(root, is_zh):
    if is_zh:
        eyebrow = "K 编程语言 · Kense OS 架构"
        title = "无 GC 内存安全系统语言，性能领先极致 Rust"
        lead = "K 将可变大小数据存放在地址稳定的数据栈中，在作用域退出时以 O(1) 时间回收，并通过编译期边界检查消除（BCE）与 C/LTO 后端生成高速原生与 WASI 可执行文件。"
        cta_tut = ("阅读教程", f"{root}docs/tutorial.html")
        cta_diag = ("查看 Kense OS 图解", f"{root}docs/diagrams.html")
        tab_labels = [
            ("core", "核心架构", f"{root}docs/diagrams/svg/core-architecture.svg",
             "单一检查前端驱动原生 C 编译、WASI WebAssembly 与进程内 TinyCC JIT。", "K 核心架构图"),
            ("stacks", "数据栈与生命周期", f"{root}docs/diagrams/svg/data-stacks-memory.svg",
             "地址稳定数据栈与作用域退出回退机制：零 GC 停顿、零逐对象释放。", "数据栈与生命周期图解"),
            ("bce", "优化与 BCE", f"{root}docs/diagrams/svg/optimization-bce-pipeline.svg",
             "compile-time 范围证明与 checkfree 契约消除热循环数组越界检查。", "优化与边界检查消除图解"),
            ("async", "异步与隔离线程", f"{root}docs/diagrams/svg/async-tasks-workers.svg",
             "词法任务作用域管理协作式 async fn，原生工作线程通过类型化队列通信。", "结构化异步与工作线程图解"),
            ("bench", "性能与内存基准", f"{root}docs/diagrams/svg/benchmark-architecture.svg",
             "16 项大型基准几何平均：速度最高 1.19x 安全 Rust、内存占用降低 1.25x。", "性能与内存基准图解"),
        ]
        install_unix_lbl = "macOS / Linux"
        install_win_lbl = "Windows (PowerShell)"
        metrics = [
            ("1.19x", "vs 最佳安全 Rust (Clang)"),
            ("3.54x", "vs 惯用 C++ (Clang)"),
            ("1.25x", "峰值内存优于 Rust"),
            ("0", "运行时 GC 停顿"),
        ]
        cards = [
            ("语言教程", "Tutorial", f"{root}docs/tutorial.html", "通过实操示例掌握 K 的数据栈、数组族、引用生命周期与工作线程。"),
            ("Kense OS 图解中心", "Diagrams", f"{root}docs/diagrams.html", "浏览数据栈、BCE、结构化并发、WASM 与 Cordis 微内核架构全览图。"),
            ("语言参考与规范", "Reference", f"{root}docs/reference.html", "完整语言特性手册与形式化规则，附可编译代码例程。"),
            ("性能基准报告", "Benchmarks", f"{root}bench/summary.html", "查看 16 项基准测试相对 C++ 与安全 Rust 的完整耗时与内存数据。"),
        ]
        open_svg_label = "打开 SVG"
    else:
        eyebrow = "K Programming Language · Kense OS"
        title = "Memory-safe systems speed without a garbage collector."
        lead = "K stores growable values on address-stable data stacks with O(1) scope-exit rewind, eliminates inner-loop bounds checks at compile time, and compiles through a C/LTO backend to native and WASI binaries."
        cta_tut = ("Start Tutorial", f"{root}docs/tutorial.html")
        cta_diag = ("Explore Diagrams", f"{root}docs/diagrams.html")
        tab_labels = [
            ("core", "Core Architecture", f"{root}docs/diagrams/svg/core-architecture.svg",
             "Unified semantic checker powering native C/LTO, WASI WebAssembly, and TinyCC JIT.", "K core architecture diagram"),
            ("stacks", "Data Stacks", f"{root}docs/diagrams/svg/data-stacks-memory.svg",
             "Address-stable growable storage with checked reference roots and O(1) scope rewind.", "Data stacks and value lifetimes diagram"),
            ("bce", "BCE & Optimizer", f"{root}docs/diagrams/svg/optimization-bce-pipeline.svg",
             "Compile-time range proofs and checkfree contracts compile away hot-loop bounds checks.", "Optimization and bounds-check elimination diagram"),
            ("async", "Async & Workers", f"{root}docs/diagrams/svg/async-tasks-workers.svg",
             "Lexical task scopes for cooperative async paired with zero-shared-state native workers.", "Structured async and isolated workers diagram"),
            ("bench", "Benchmarks", f"{root}docs/diagrams/svg/benchmark-architecture.svg",
             "16 large workloads: 1.13x–1.19x faster than safe Rust with 1.25x lower peak memory.", "Performance and memory benchmark profile"),
        ]
        install_unix_lbl = "macOS / Linux"
        install_win_lbl = "Windows"
        metrics = [
            ("1.19x", "vs Best Safe Rust (Clang)"),
            ("3.54x", "vs Idiomatic C++ (Clang)"),
            ("1.25x", "Lower peak RAM vs Rust"),
            ("0", "Runtime GC pauses"),
        ]
        cards = [
            ("Language Tutorial", "Start here", f"{root}docs/tutorial.html", "Learn K's data stacks, array family, checked references, enums, and isolated threads."),
            ("Kense OS Diagrams", "18 SVGs", f"{root}docs/diagrams.html", "Explore interactive technical diagrams of the compiler pipeline, runtime, WASI, and Cordis."),
            ("Language Reference", "Manual", f"{root}docs/reference.html", "Topic-by-topic specification of types, lifetimes, checkfree contracts, and C interop."),
            ("Benchmark Suite", "16 workloads", f"{root}bench/summary.html", "Detailed timing and peak-memory methodology vs idiomatic C++, expert C++, and safe Rust."),
        ]
        open_svg_label = "Open SVG"

    unix_cmd = "curl -fsSL https://raw.githubusercontent.com/kense-labs/klang/main/scripts/install.sh | bash"
    win_cmd = "irm https://raw.githubusercontent.com/kense-labs/klang/main/scripts/install.ps1 | iex"

    tabs_html = []
    for idx, (tid, tlabel, tsrc, tcap, talt) in enumerate(tab_labels):
        sel = "true" if idx == 0 else "false"
        tabs_html.append(
            f'<button type="button" class="stage-tab" role="tab" aria-selected="{sel}" '
            f'data-tab="{tid}" data-src="{tsrc}" data-caption="{html.escape(tcap)}" data-alt="{html.escape(talt)}">'
            f'{html.escape(tlabel)}</button>'
        )

    metrics_html = "".join(
        f'<div class="kui-metric-card"><div class="kui-metric-val">{html.escape(v)}</div>'
        f'<div class="kui-metric-label">{html.escape(lbl)}</div></div>'
        for v, lbl in metrics
    )
    cards_html = "".join(
        f'<a class="kui-card" href="{href}">'
        f'<div class="kui-card-top"><span class="kui-card-title">{html.escape(ctitle)}</span>'
        f'<span class="kui-badge">{html.escape(cbadge)}</span></div>'
        f'<span class="kui-card-desc">{html.escape(cdesc)}</span></a>'
        for ctitle, cbadge, href, cdesc in cards
    )

    first_src = tab_labels[0][2]
    first_cap = tab_labels[0][3]
    first_alt = tab_labels[0][4]

    return (
        f'<section class="kui-hero">'
        f'<div class="hero-eyebrow"><span class="hero-dot"></span><span>{html.escape(eyebrow)}</span></div>'
        f'<h1 class="kui-hero-title">{html.escape(title)}</h1>'
        f'<p class="kui-hero-lead">{html.escape(lead)}</p>'
        f'<div class="hero-actions">'
        f'<a class="btn-primary" href="{cta_tut[1]}">{html.escape(cta_tut[0])}</a>'
        f'<a class="btn-secondary" href="{cta_diag[1]}">{html.escape(cta_diag[0])}</a>'
        f'</div>'
        f'<div class="install-box">'
        f'<div class="install-box-head">'
        f'<div class="install-os-tabs" role="tablist">'
        f'<button type="button" class="install-os-tab" aria-selected="true" data-cmd="{html.escape(unix_cmd)}">{html.escape(install_unix_lbl)}</button>'
        f'<button type="button" class="install-os-tab" aria-selected="false" data-cmd="{html.escape(win_cmd)}">{html.escape(install_win_lbl)}</button>'
        f'</div>'
        f'<button type="button" class="kui-copy-btn" id="k-install-copy">Copy</button>'
        f'</div>'
        f'<div class="install-box-body"><code class="install-cmd" id="k-install-cmd">{html.escape(unix_cmd)}</code></div>'
        f'</div>'
        f'<div class="kui-stage">'
        f'<div class="view-bar">'
        f'<div class="stage-tabs" role="tablist">{"".join(tabs_html)}</div>'
        f'<div class="stage-bar-end">'
        f'<a class="stage-chip" id="k-stage-open-svg" href="{first_src}" target="_blank" rel="noopener">{html.escape(open_svg_label)}</a>'
        f'<button type="button" class="stage-expand" id="k-stage-expand" aria-label="Expand diagram" title="Expand diagram">'
        f'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M10 2h4v4M6 14H2v-4M14 2l-4.5 4.5M2 14l4.5-4.5"/></svg>'
        f'</button>'
        f'</div>'
        f'</div>'
        f'<div class="stage-canvas">'
        f'<img id="k-stage-img" class="stage-canvas-img" src="{first_src}" alt="{html.escape(first_alt)}">'
        f'<div class="stage-caption"><span id="k-stage-caption-text">{html.escape(first_cap)}</span></div>'
        f'</div>'
        f'</div>'
        f'<div class="kui-metrics">{metrics_html}</div>'
        f'<div class="kui-cards">{cards_html}</div>'
        f'</section>'
    )


def render_doc_head(out_url, src_rel, groups):
    group_label = "Docs"
    for _, glabel, items, _ in groups:
        if any(u == out_url for u, _ in items):
            group_label = glabel
            break
    pill = f'<span class="doc-source-pill">{html.escape(src_rel)}</span>' if src_rel else ""
    return f'<div class="doc-crumb"><span>{html.escape(group_label)}</span><span>/</span>{pill}</div>'


def render_pager(root, out_url, reading_order):
    idx = None
    for i, (u, _) in enumerate(reading_order):
        if u == out_url:
            idx = i
            break
    if idx is None:
        return ""
    prev_html = ""
    next_html = ""
    if idx > 0:
        pu, ptitle = reading_order[idx - 1]
        prev_html = f'<a class="pager-link prev" href="{root}{pu}"><span class="pager-dir">← Previous</span><span class="pager-title">{html.escape(ptitle)}</span></a>'
    if idx + 1 < len(reading_order):
        nu, ntitle = reading_order[idx + 1]
        next_html = f'<a class="pager-link next" href="{root}{nu}"><span class="pager-dir">Next →</span><span class="pager-title">{html.escape(ntitle)}</span></a>'
    if not prev_html and not next_html:
        return ""
    return f'<nav class="doc-pager" aria-label="Pagination">{prev_html}{next_html}</nav>'


def render_lang_switch(root, out_url):
    if out_url == "README.zh-CN.html":
        return f'<a class="cta-chip" href="{root}index.html" title="Switch to English">EN</a>'
    return f'<a class="cta-chip" href="{root}README.zh-CN.html" title="切换到简体中文">中文</a>'


def apply_kui_chrome(out_path, root, out_url, src_rel, groups, reading_order):
    raw = open(out_path, encoding="utf-8").read()
    is_home = out_url in ("index.html", "README.zh-CN.html")
    raw = raw.replace("<!--KUI_TOPNAV-->", render_topnav(root, out_url))
    raw = raw.replace("<!--KUI_LANG_SWITCH-->", render_lang_switch(root, out_url))
    raw = raw.replace("<!--KUI_SIDEBAR-->", render_sidebar(root, out_url, groups))
    raw = raw.replace("<!--KUI_HERO-->", render_hero(root, out_url == "README.zh-CN.html") if is_home else "")
    raw = raw.replace("<!--KUI_DOC_HEAD-->", "" if is_home else render_doc_head(out_url, src_rel, groups))
    raw = raw.replace("<!--KUI_BODY_CLASS-->", " home-readme-body" if is_home else "")
    raw = raw.replace("<!--KUI_PAGER-->", "" if is_home else render_pager(root, out_url, reading_order))
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(raw)


def pandoc(args, pandoc_exe, stdin_text=None, src=None, out=None):
    cmd = [pandoc_exe, "--from", "gfm", "--to", "html5", "--standalone",
           "--template", os.path.join(SITE, "template.html"),
           "--lua-filter", os.path.join(SITE, "links.lua"),
           "--syntax-definition", os.path.join(SITE, "klang.xml"),
           "--highlight-style", "tango"] + ASSET_ARGS + args
    if src:
        cmd.append(src)
    if out:
        cmd += ["-o", out]
    p = subprocess.run(cmd, input=stdin_text, capture_output=True, text=True, encoding="utf-8")
    if p.returncode != 0:
        sys.stderr.write(p.stderr)
        raise SystemExit("pandoc failed on %s" % (src or out))
    for line in p.stderr.split("\n"):
        if line.strip() and "highlight-style" not in line:
            sys.stderr.write(line + "\n")
    return p.stdout


def build_page(src_rel, out_rel, out_dir, pandoc_exe, groups, reading_order):
    src = os.path.join(ROOT, src_rel)
    out = os.path.join(out_dir, out_rel)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    text = open(src, encoding="utf-8").read()
    depth = out_rel.count("/")
    root = "../" * depth
    args = ["--toc", "--toc-depth", "3",
            "--metadata", "pagetitle=" + title_of(text, os.path.basename(src_rel)),
            "--variable", "root=" + root,
            "--variable", "source=" + src_rel]
    if src_rel == "README.zh-CN.md":
        args += ["--metadata", "lang=zh-CN"]
    pandoc(args, pandoc_exe, src=src, out=out)
    apply_kui_chrome(out, root, out_rel, src_rel, groups, reading_order)


def build_source_page(src_rel, out_dir, pandoc_exe, groups, reading_order):
    """A program or text file as one highlighted code block on a page."""
    src = os.path.join(ROOT, src_rel)
    out_rel = src_rel + ".html"
    out = os.path.join(out_dir, out_rel)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    text = open(src, encoding="utf-8").read()
    lang = "klang" if src_rel.endswith(".k") else ""
    name = os.path.basename(src_rel)
    md = "# %s\n\n```%s\n%s\n```\n" % (name, lang, text.rstrip("\n"))
    depth = src_rel.count("/")
    root = "../" * depth
    args = ["--metadata", "pagetitle=" + name,
            "--variable", "root=" + root,
            "--variable", "source=" + src_rel]
    html_text = pandoc(args, pandoc_exe, stdin_text=md)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(html_text)
    apply_kui_chrome(out, root, out_rel, src_rel, groups, reading_order)


class LinkScanner(HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs = []
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        d = dict(attrs)
        if "id" in d:
            self.ids.add(d["id"])
        if tag == "a" and "name" in d:
            self.ids.add(d["name"])
        if tag == "a" and "href" in d:
            self.hrefs.append(d["href"])
        if tag in ("img", "link", "script") and (d.get("src") or d.get("href")):
            self.hrefs.append(d.get("src") or d.get("href"))


def check(out_dir):
    pages = {}
    for dirpath, _, files in os.walk(out_dir):
        for f in files:
            if f.endswith(".html"):
                path = os.path.join(dirpath, f)
                s = LinkScanner()
                s.feed(open(path, encoding="utf-8").read())
                pages[os.path.normpath(path)] = s
    bad = 0
    for path, s in pages.items():
        for href in s.hrefs:
            if re.match(r"^[a-z][a-z0-9+.-]*:", href):
                continue
            target, _, frag = href.partition("#")
            target = target.partition("?")[0]
            frag = html.unescape(frag)
            if target == "":
                if frag and frag not in s.ids:
                    print("%s: missing fragment #%s" % (os.path.relpath(path, out_dir), frag))
                    bad += 1
                continue
            tpath = os.path.normpath(os.path.join(os.path.dirname(path), unquote(html.unescape(target))))
            if not os.path.exists(tpath):
                print("%s: missing target %s" % (os.path.relpath(path, out_dir), href))
                bad += 1
                continue
            if frag and tpath in pages and frag not in pages[tpath].ids:
                print("%s: missing fragment %s" % (os.path.relpath(path, out_dir), href))
                bad += 1
    print("%d pages, %d broken links" % (len(pages), bad))
    return bad == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "site"), help="output directory (default build/site)")
    ap.add_argument("--pandoc", default="pandoc", help="the pandoc executable")
    ap.add_argument("--check", action="store_true", help="verify every internal link of the built site")
    a = ap.parse_args()
    out_dir = os.path.abspath(a.out)
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    for asset in ("style.css", "search.js", "site.js"):
        shutil.copy(os.path.join(SITE, asset), os.path.join(out_dir, asset))

    sys.path.insert(0, SITE)
    from generate_search import build_index

    def file_hash(fname):
        with open(os.path.join(SITE, fname), "rb") as f:
            return hashlib.sha256(f.read()).hexdigest()[:10]

    ASSET_ARGS.extend([
        "--metadata", "searchfile=" + build_index(out_dir),
        "--metadata", "searchver=" + file_hash("search.js"),
        "--metadata", "stylever=" + file_hash("style.css"),
        "--metadata", "sitever=" + file_hash("site.js"),
    ])

    groups = build_sidebar_groups()
    reading_order = []
    for key, _, items, _ in groups:
        for u, title in items:
            if not u.startswith("docs/html/"):
                reading_order.append((u, title))

    for src_rel, out_rel in PAGES:
        if os.path.exists(os.path.join(ROOT, src_rel)):
            build_page(src_rel, out_rel, out_dir, a.pandoc, groups, reading_order)
    for src_rel in SOURCES:
        build_source_page(src_rel, out_dir, a.pandoc, groups, reading_order)
    for rel in COPIES:
        dst = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(os.path.join(ROOT, rel), dst)
    # GitHub Pages runs Jekyll over the artifact unless told not to.
    open(os.path.join(out_dir, ".nojekyll"), "w").close()
    print("built %d pages and %d source pages into %s" % (len(PAGES), len(SOURCES), out_dir))
    if a.check and not check(out_dir):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
