#!/usr/bin/env python3
"""Build the fully bilingual (EN / 简体中文) K documentation site in KUI style.

The site keeps the repository's layout for English docs (`README.md` ->
`index.html`, `docs/*.md` -> `docs/*.html`, `bench/*.md` -> `bench/*.html`)
and publishes full 1:1 Simplified Chinese mirrors under `README.zh-CN.html` and
`docs/zh/**/*.html` plus `docs/diagrams/svg/zh/*.svg`.

    python docs/site/build.py --out build/site --check
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
from urllib.parse import quote, unquote

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
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "zh", "*.md"))):
    name = os.path.basename(src)
    PAGES.append(("docs/zh/" + name, "docs/zh/" + name[:-3] + ".html"))
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "zh", "design", "*.md"))):
    name = os.path.basename(src)
    PAGES.append(("docs/zh/design/" + name, "docs/zh/design/" + name[:-3] + ".html"))
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "zh", "bench", "*.md"))):
    name = os.path.basename(src)
    PAGES.append(("docs/zh/bench/" + name, "docs/zh/bench/" + name[:-3] + ".html"))
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
for pattern in ("*.png", "svg/*.svg", "svg/zh/*.svg"):
    COPIES += sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                     for p in glob.glob(os.path.join(ROOT, "docs", "diagrams", pattern)))
COPIES.extend(("LICENSE-APACHE", "NOTICE", "vscode/klang-language.vsix"))
COPIES += sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                 for p in glob.glob(os.path.join(ROOT, "bench", "*.json")))
COPIES += sorted(os.path.relpath(p, ROOT).replace(os.sep, "/")
                 for p in glob.glob(os.path.join(ROOT, "docs", "html", "**", "*"), recursive=True)
                 if os.path.isfile(p))

ASSET_ARGS = []

# Full 1:1 EN <-> ZH document pairs across the entire site
EN_TO_ZH = {
    "index.html": "README.zh-CN.html",
    "docs/index.html": "docs/zh/index.html",
    "docs/K for C++ and Rust Programmers Translating Common Patterns.html": "docs/zh/cpp_rust_guide.html",
    "samples/index.html": "docs/zh/samples.html",
    "vscode/index.html": "docs/zh/vscode.html",
    "test/k_in_k/index.html": "docs/zh/k_in_k.html",
    "docs/site/index.html": "docs/zh/site.html",
}
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "*.md"))):
    stem = os.path.basename(src)[:-3]
    if stem not in ("index", "K for C++ and Rust Programmers Translating Common Patterns"):
        if os.path.exists(os.path.join(ROOT, "docs", "zh", stem + ".md")):
            EN_TO_ZH[f"docs/{stem}.html"] = f"docs/zh/{stem}.html"
for src in sorted(glob.glob(os.path.join(ROOT, "docs", "design", "*.md"))):
    stem = os.path.basename(src)[:-3]
    if os.path.exists(os.path.join(ROOT, "docs", "zh", "design", stem + ".md")):
        EN_TO_ZH[f"docs/design/{stem}.html"] = f"docs/zh/design/{stem}.html"
for src in sorted(glob.glob(os.path.join(ROOT, "bench", "*.md"))):
    stem = os.path.basename(src)[:-3]
    if os.path.exists(os.path.join(ROOT, "docs", "zh", "bench", stem + ".md")):
        EN_TO_ZH[f"bench/{stem}.html"] = f"docs/zh/bench/{stem}.html"

ZH_TO_EN = {v: k for k, v in EN_TO_ZH.items()}
ZH_TO_EN["docs/zh/quickstart.html"] = "docs/tutorial.html"

TOPNAV_ITEMS = [
    ("Overview", "概览", "index.html", "README.zh-CN.html",
     {"index.html", "README.zh-CN.html", "docs/architecture.html", "docs/zh/architecture.html", "docs/roadmap.html", "docs/zh/roadmap.html"}),
    ("Docs", "文档", "docs/index.html", "docs/zh/index.html",
     {"docs/index.html", "docs/zh/index.html", "docs/site/index.html", "docs/zh/site.html",
      "docs/command_line.html", "docs/zh/command_line.html", "docs/packages.html", "docs/zh/packages.html",
      "docs/editors.html", "docs/zh/editors.html", "docs/testing.html", "docs/zh/testing.html",
      "vscode/index.html", "docs/zh/vscode.html", "test/k_in_k/index.html", "docs/zh/k_in_k.html"}),
    ("Tutorial", "教程", "docs/tutorial.html", "docs/zh/tutorial.html",
     {"docs/tutorial.html", "docs/zh/tutorial.html", "docs/zh/quickstart.html",
      "docs/stdlib.html", "docs/zh/stdlib.html",
      "docs/K for C++ and Rust Programmers Translating Common Patterns.html", "docs/zh/cpp_rust_guide.html"}),
    ("Reference", "语言参考", "docs/reference.html", "docs/zh/reference.html",
     {"docs/reference.html", "docs/zh/reference.html", "docs/k_spec.html", "docs/zh/k_spec.html"}),
    ("Diagrams", "架构图解", "docs/diagrams.html", "docs/zh/diagrams.html",
     {"docs/diagrams.html", "docs/zh/diagrams.html", "docs/implementation.html", "docs/zh/implementation.html"}),
    ("Samples", "示例", "samples/index.html", "docs/zh/samples.html",
     {"samples/index.html", "docs/zh/samples.html"}),
    ("Benchmarks", "性能基准", "bench/summary.html", "docs/zh/bench/summary.html",
     {"bench/summary.html", "docs/zh/bench/summary.html", "bench/results.html", "docs/zh/bench/results.html",
      "bench/design.html", "docs/zh/bench/design.html"}),
]


def is_zh_page(out_url):
    return out_url == "README.zh-CN.html" or out_url.startswith("docs/zh/")


def title_of(md_text, fallback):
    for line in md_text.split("\n"):
        m = re.match(r"^#\s+(.*)", line)
        if m:
            t = re.sub(r"`", "", m.group(1)).strip()
            return t
    return fallback


def url_quote_rel(rel_path):
    return "/".join(quote(seg, safe="-._~()") for seg in rel_path.split("/"))


def label_pair(en_url, page_titles):
    """Return (en_label, zh_label, en_only_flag) using real headings."""
    zh_url = EN_TO_ZH.get(en_url)
    custom = {
        "index.html": ("Overview", "项目概览"),
        "docs/index.html": ("Documentation Index", "文档总览"),
        "docs/diagrams.html": ("Architecture & Diagrams", "Kense OS 图解中心"),
        "docs/architecture.html": ("K Architecture", "K 语言架构设计"),
        "docs/roadmap.html": ("Roadmap", "发展路线图"),
        "docs/tutorial.html": ("Tutorial", "完整教程 (Tutorial)"),
        "docs/reference.html": ("Language Reference", "语言参考手册"),
        "docs/k_spec.html": ("Specification", "形式化语言规范"),
        "docs/stdlib.html": ("Standard Library", "标准库指南"),
        "docs/K for C++ and Rust Programmers Translating Common Patterns.html": ("C++ & Rust Guide", "C++ 与 Rust 迁移指南"),
        "docs/command_line.html": ("Command Line (k)", "命令行工具 (k)"),
        "docs/packages.html": ("Packages & Builds", "包管理与多目标构建"),
        "docs/editors.html": ("Editors & LSP", "编辑器与 LSP"),
        "vscode/index.html": ("VS Code Extension", "VS Code 扩展"),
        "docs/implementation.html": ("Compiler Implementation", "编译器实现笔记"),
        "docs/testing.html": ("Testing", "测试套件指南"),
        "test/k_in_k/index.html": ("Self-Hosting (k_in_k)", "自举前端测试 (k_in_k)"),
        "docs/site/index.html": ("Site Generator", "文档站生成器"),
        "bench/summary.html": ("Benchmark Summary", "基准总结报告"),
        "bench/results.html": ("Full Results", "完整基准数值"),
        "bench/design.html": ("Benchmark Design", "基准设计原则"),
        "bench/notes.md": ("Diagnostic Notes", "诊断实验记录"),
        "bench/notes.html": ("Diagnostic Notes", "诊断实验记录"),
        "bench/adoption.html": ("Adoption Analysis", "迁移与采用评估"),
        "bench/plan.html": ("Optimization Plan", "性能优化计划"),
        "bench/pool_workloads.html": ("Pool Workloads", "存储池基准负载"),
        "bench/slice_pool.html": ("Slice Pool", "Slice Pool 设计"),
        "bench/slot_pool.html": ("Slot Pool", "Slot Pool 设计"),
        "samples/index.html": ("Samples Index", "31 个实战示例"),
        "LICENSE.html": ("MIT License", "MIT 开源协议"),
    }
    if en_url in custom:
        return custom[en_url][0], custom[en_url][1], False
    en_t = page_titles.get(en_url, os.path.basename(en_url))
    zh_t = page_titles.get(zh_url, en_t) if zh_url else en_t
    return en_t, zh_t, (zh_url is None)


def build_sidebar_groups():
    """Organize unified EN/ZH sidebar groups mapped cleanly to 1:1 paired URLs."""
    page_titles = {}
    built_urls = set()
    for src_rel, out_url in PAGES:
        src_path = os.path.join(ROOT, src_rel)
        if os.path.exists(src_path):
            text = open(src_path, encoding="utf-8").read()
            page_titles[out_url] = title_of(text, os.path.basename(src_rel))
            built_urls.add(out_url)

    overview_order = [
        "index.html",
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
        "docs/K for C++ and Rust Programmers Translating Common Patterns.html",
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
        "bench/notes.html",
        "bench/adoption.html",
        "bench/plan.html",
        "bench/pool_workloads.html",
        "bench/slice_pool.html",
        "bench/slot_pool.html",
    ]

    groups = []

    def add_group(key, en_label, zh_label, urls, default_open=True):
        items = []
        for en_u in urls:
            if en_u not in built_urls:
                continue
            zh_u = EN_TO_ZH.get(en_u, en_u)
            en_t, zh_t, en_only = label_pair(en_u, page_titles)
            items.append((url_quote_rel(en_u), url_quote_rel(zh_u), en_t, zh_t, en_only))
        if key == "language":
            if os.path.exists(os.path.join(ROOT, "docs", "zh", "quickstart.md")):
                items.insert(0, ("docs/tutorial.html", "docs/zh/quickstart.html", "Quick Start", "10 分钟快速上手", False))
            if os.path.exists(os.path.join(ROOT, "docs", "html", "K, Byte by Byte.html")):
                en_html = "docs/html/K,%20Byte%20by%20Byte.html"
                zh_html = "docs/html/K,%20Byte%20by%20Byte%20(zh).html" if os.path.exists(os.path.join(ROOT, "docs", "html", "K, Byte by Byte (zh).html")) else en_html
                items.append((en_html, zh_html, "K, Byte by Byte", "图解内存字节布局", False))
        if items:
            groups.append((key, en_label, zh_label, items, default_open))

    add_group("overview", "Overview", "概览与架构", overview_order, True)
    add_group("language", "Language", "语言指南", language_order, True)
    add_group("toolchain", "Compiler & Tools", "工具链与编译器", toolchain_order, True)
    add_group("benchmarks", "Benchmarks", "性能基准", bench_order, True)

    design_items = []
    for en_u in sorted(u for u in built_urls if u.startswith("docs/design/")):
        zh_u = EN_TO_ZH.get(en_u, en_u)
        en_t = page_titles.get(en_u, os.path.basename(en_u))
        zh_t = page_titles.get(zh_u, en_t)
        design_items.append((en_u, zh_u, en_t, zh_t, zh_u not in built_urls))
    if design_items:
        groups.append(("design", "Design Notes", "设计笔记", design_items, False))

    sample_items = []
    if "samples/index.html" in built_urls:
        zh_s = EN_TO_ZH.get("samples/index.html", "samples/index.html")
        sample_items.append(("samples/index.html", zh_s, "Samples Index", "31 个实战示例", False))
    for s in SOURCES:
        u = s + ".html"
        t = "MIT License" if s == "LICENSE" else os.path.basename(s)
        sample_items.append((u, u, t, "MIT 开源协议" if s == "LICENSE" else t, False))
    groups.append(("samples", "Samples", "示例源码", sample_items, False))

    return groups


def render_topnav(root, out_url):
    zh_mode = is_zh_page(out_url)
    links = []
    for en_lbl, zh_lbl, en_tgt, zh_tgt, active_set in TOPNAV_ITEMS:
        is_cur = out_url in active_set
        if en_lbl == "Samples" and out_url.startswith("samples/"):
            is_cur = True
        elif en_lbl == "Diagrams" and (out_url.startswith("docs/design/") or out_url.startswith("docs/zh/design/")):
            is_cur = True
        elif en_lbl == "Benchmarks" and (out_url.startswith("bench/") or out_url.startswith("docs/zh/bench/")):
            is_cur = True
        cur_attr = ' aria-current="true"' if is_cur else ""
        href = zh_tgt if zh_mode else en_tgt
        txt = zh_lbl if zh_mode else en_lbl
        links.append(
            f'<a href="{root}{href}" data-en-href="{root}{en_tgt}" data-zh-href="{root}{zh_tgt}" '
            f'data-en="{html.escape(en_lbl)}" data-zh="{html.escape(zh_lbl)}"{cur_attr}>{html.escape(txt)}</a>'
        )
    return '<nav class="company-nav" aria-label="Primary sections">' + "".join(links) + "</nav>"


def render_sidebar(root, out_url, groups):
    zh_mode = is_zh_page(out_url)
    quoted_out = url_quote_rel(out_url)
    quoted_equiv = url_quote_rel(ZH_TO_EN.get(out_url, out_url))
    parts = []
    chevron = '<svg class="sb-group-chevron" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M6 4l4 4-4 4"/></svg>'
    for key, en_g, zh_g, items, default_open in groups:
        contains_active = any(en_u == quoted_out or zh_u == quoted_out for en_u, zh_u, _, _, _ in items)
        open_attr = " open" if (default_open or contains_active) else ""
        rows = []
        for en_u, zh_u, en_t, zh_t, en_only in items:
            is_active = (quoted_out == en_u or quoted_out == zh_u or (quoted_equiv == en_u and out_url != "docs/zh/quickstart.html"))
            cur = ' aria-current="page"' if is_active else ""
            href = zh_u if zh_mode else en_u
            txt = zh_t if zh_mode else en_t
            badge = '<span class="sb-en-badge">EN</span>' if en_only else ""
            rows.append(
                f'<a class="sb-row" href="{root}{href}" '
                f'data-en-href="{root}{en_u}" data-zh-href="{root}{zh_u}"{cur}>'
                f'<span class="sb-row-label" data-en="{html.escape(en_t)}" data-zh="{html.escape(zh_t)}">{html.escape(txt)}</span>'
                f'{badge}</a>'
            )
        g_txt = zh_g if zh_mode else en_g
        parts.append(
            f'<details class="sb-group" data-group="{key}"{open_attr}>'
            f'<summary class="sb-group-head">'
            f'<span class="sb-group-label">{chevron}<span data-en="{html.escape(en_g)}" data-zh="{html.escape(zh_g)}">{html.escape(g_txt)}</span></span>'
            f'<span class="sb-group-count">{len(items)}</span>'
            f'</summary>'
            f'<div class="sb-rows">{"".join(rows)}</div>'
            f'</details>'
        )
    return "".join(parts)


def render_lang_dropdown(root, out_url):
    zh_mode = is_zh_page(out_url)
    has_pair = (out_url in EN_TO_ZH) or (out_url in ZH_TO_EN)
    en_href = root + url_quote_rel(ZH_TO_EN[out_url]) if out_url in ZH_TO_EN else (root + url_quote_rel(out_url) if not zh_mode else root + "index.html")
    zh_href = root + url_quote_rel(EN_TO_ZH[out_url]) if out_url in EN_TO_ZH else (root + url_quote_rel(out_url) if zh_mode else root + "docs/zh/index.html")
    cur_label = "简体中文" if zh_mode else "English"
    en_sel = "false" if zh_mode else "true"
    zh_sel = "true" if zh_mode else "false"
    check_svg = '<svg class="lang-check" width="13" height="13" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M3.5 8.5l3 3 6-6.5"/></svg>'
    return (
        f'<div class="lang-wrap">'
        f'<button type="button" class="cta-chip" id="k-lang-btn" aria-haspopup="true" aria-expanded="false" '
        f'title="Switch language / 切换语言">'
        f'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"><circle cx="8" cy="8" r="6"/><path d="M2.5 8h11M8 2a9.5 9.5 0 0 1 0 12M8 2a9.5 9.5 0 0 0 0 12"/></svg>'
        f'<span class="lang-btn-label" data-en="English" data-zh="简体中文">{cur_label}</span>'
        f'<svg width="11" height="11" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6l4 4 4-4"/></svg>'
        f'</button>'
        f'<div class="lang-popover" id="k-lang-popover" role="menu" aria-label="Language">'
        f'<button type="button" class="lang-option" role="menuitemradio" data-lang-val="en" data-target-href="{en_href}" data-paired="{"true" if has_pair else "false"}" aria-checked="{en_sel}"><span>English</span>{check_svg}</button>'
        f'<button type="button" class="lang-option" role="menuitemradio" data-lang-val="zh" data-target-href="{zh_href}" data-paired="{"true" if has_pair else "false"}" aria-checked="{zh_sel}"><span>简体中文</span>{check_svg}</button>'
        f'</div>'
        f'</div>'
    )


def render_hero(root, is_zh):
    if is_zh:
        eyebrow = "K 编程语言 · Kense OS 架构"
        title = "无 GC 内存安全系统语言，性能领先极致 Rust"
        lead = "K 将可变大小数据存放在地址稳定的数据栈中，在作用域退出时以 O(1) 时间回收，并通过编译期边界检查消除（BCE）与 C/LTO 后端生成高速原生与 WASI 可执行文件。"
        cta_tut = ("快速上手", f"{root}docs/zh/quickstart.html")
        cta_diag = ("Kense OS 图解中心", f"{root}docs/zh/diagrams.html")
        tab_labels = [
            ("core", "系统核心架构", f"{root}docs/diagrams/svg/zh/core-architecture.svg",
             "统一前端驱动原生 C/LTO 构建、WASI 命令模块与进程内 TinyCC JIT。", "K 核心架构图"),
            ("stacks", "数据栈与生命周期", f"{root}docs/diagrams/svg/zh/data-stacks-memory.svg",
             "地址稳定数据栈与作用域退出回退机制：零 GC 停顿、零逐对象释放。", "数据栈与生命周期图解"),
            ("bce", "优化与 BCE", f"{root}docs/diagrams/svg/zh/optimization-bce-pipeline.svg",
             "编译期范围证明与 checkfree 门禁彻底消除热循环数组越界检查。", "优化与边界检查消除图解"),
            ("async", "异步与隔离线程", f"{root}docs/diagrams/svg/zh/async-tasks-workers.svg",
             "词法任务作用域约束协作式 async fn，原生线程仅通过类型化队列通信。", "结构化异步与隔离线程图解"),
            ("bench", "性能与内存基准", f"{root}docs/diagrams/svg/zh/benchmark-architecture.svg",
             "16 项大型基准几何平均：速度最高 1.19x 安全 Rust、内存占用降低 1.25x。", "性能与内存基准图解"),
        ]
        install_unix_lbl = "macOS / Linux"
        install_win_lbl = "Windows"
        copy_lbl = "复制"
        expand_lbl = "放大图解"
        open_svg_lbl = "打开 SVG"
        metrics = [
            ("1.19x", "对比最佳安全 Rust (Clang)"),
            ("3.54x", "对比惯用 C++ (Clang)"),
            ("1.25x", "峰值内存较 Rust 更省"),
            ("0", "运行时 GC 停顿"),
        ]
        cards = [
            ("完整中文教程", "Tutorial", f"{root}docs/zh/tutorial.html", "系统学习 K 的数据栈、数组族、受检引用、变长 enum 与隔离工作线程。"),
            ("Kense OS 图解中心", "18 张中文图解", f"{root}docs/zh/diagrams.html", "全套 18 张中文 Kense OS 架构矢量图解，支持全屏放大与 SVG 检视。"),
            ("语言参考与规范", "完整中文版", f"{root}docs/zh/reference.html", "逐主题查阅完整语言参考手册与形式化语言规范，含可编译代码例程。"),
            ("性能与内存基准报告", "16 项基准", f"{root}docs/zh/bench/summary.html", "查看 2026 年 10 月完整测速数据与峰值内存对比（对比 C++ 与安全 Rust）。"),
        ]
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
        copy_lbl = "Copy"
        expand_lbl = "Expand diagram"
        open_svg_lbl = "Open SVG"
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
        f'<button type="button" class="kui-copy-btn" id="k-install-copy" data-en="Copy" data-zh="复制">{html.escape(copy_lbl)}</button>'
        f'</div>'
        f'<div class="install-box-body"><code class="install-cmd" id="k-install-cmd">{html.escape(unix_cmd)}</code></div>'
        f'</div>'
        f'<div class="kui-stage">'
        f'<div class="view-bar">'
        f'<div class="stage-tabs" role="tablist">{"".join(tabs_html)}</div>'
        f'<div class="stage-bar-end">'
        f'<a class="stage-chip" id="k-stage-open-svg" href="{first_src}" target="_blank" rel="noopener">{html.escape(open_svg_lbl)}</a>'
        f'<button type="button" class="stage-expand" id="k-stage-expand" aria-label="{html.escape(expand_lbl)}" title="{html.escape(expand_lbl)}">'
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


def render_doc_head(root, out_url, src_rel, groups):
    zh_mode = is_zh_page(out_url)
    quoted_out = url_quote_rel(out_url)
    quoted_equiv = url_quote_rel(ZH_TO_EN.get(out_url, out_url))
    en_group, zh_group = "Docs", "文档"
    for _, eg, zg, items, _ in groups:
        if any(eu == quoted_equiv or zu == quoted_out for eu, zu, _, _, _ in items):
            en_group, zh_group = eg, zg
            break
    pill = f'<span class="doc-source-pill">{html.escape(src_rel)}</span>' if src_rel else ""
    return (
        f'<div class="doc-crumb">'
        f'<span data-en="{html.escape(en_group)}" data-zh="{html.escape(zh_group)}">{html.escape(zh_group if zh_mode else en_group)}</span>'
        f'<span>/</span>{pill}</div>'
    )


def render_pager(root, out_url, groups):
    zh_mode = is_zh_page(out_url)
    quoted_out = url_quote_rel(out_url)
    en_order = []
    zh_order = []
    for _, _, _, items, _ in groups:
        for en_u, zh_u, en_t, zh_t, _ in items:
            if not en_u.startswith("docs/html/"):
                en_order.append((en_u, en_t, zh_t))
        for en_u, zh_u, en_t, zh_t, _ in items:
            if not zh_u.startswith("docs/html/"):
                if not any(x[0] == zh_u for x in zh_order):
                    zh_order.append((zh_u, en_t, zh_t))

    order = zh_order if zh_mode else en_order
    idx = None
    for i, (u, _, _) in enumerate(order):
        if u == quoted_out:
            idx = i
            break
    if idx is None:
        return ""

    prev_html = ""
    next_html = ""
    if idx > 0:
        pu, pen, pzh = order[idx - 1]
        pdir = "← 上一页" if zh_mode else "← Previous"
        ptxt = pzh if zh_mode else pen
        prev_html = (
            f'<a class="pager-link prev" href="{root}{pu}">'
            f'<span class="pager-dir" data-en="← Previous" data-zh="← 上一页">{pdir}</span>'
            f'<span class="pager-title" data-en="{html.escape(pen)}" data-zh="{html.escape(pzh)}">{html.escape(ptxt)}</span></a>'
        )
    if idx + 1 < len(order):
        nu, nen, nzh = order[idx + 1]
        ndir = "下一页 →" if zh_mode else "Next →"
        ntxt = nzh if zh_mode else nen
        next_html = (
            f'<a class="pager-link next" href="{root}{nu}">'
            f'<span class="pager-dir" data-en="Next →" data-zh="下一页 →">{ndir}</span>'
            f'<span class="pager-title" data-en="{html.escape(nen)}" data-zh="{html.escape(nzh)}">{html.escape(ntxt)}</span></a>'
        )
    if not prev_html and not next_html:
        return ""
    return f'<nav class="doc-pager" aria-label="Pagination">{prev_html}{next_html}</nav>'


def localize_zh_home_body(raw):
    """Switch README.zh-CN.html inline diagram assets and doc links to Chinese versions."""
    raw = re.sub(r'src="docs/diagrams/([a-z0-9-]+)\.png"', r'src="docs/diagrams/svg/zh/\1.svg"', raw)
    raw = re.sub(r'href="docs/diagrams/svg/([a-z0-9-]+)\.svg"', r'href="docs/diagrams/svg/zh/\1.svg"', raw)

    def map_home_link(m):
        href = m.group(1)
        target, sep, frag = href.partition("#")
        if target in EN_TO_ZH:
            return f'href="{url_quote_rel(EN_TO_ZH[target])}{sep}{frag}"'
        return m.group(0)

    return re.sub(r'href="(docs/[^"#]+\.html(?:#[^"]+)?|samples/index\.html(?:#[^"]+)?|bench/[^"#]+\.html(?:#[^"]+)?|vscode/index\.html(?:#[^"]+)?)"', map_home_link, raw)


def apply_kui_chrome(out_path, root, out_url, src_rel, groups):
    raw = open(out_path, encoding="utf-8").read()
    is_home = out_url in ("index.html", "README.zh-CN.html")
    zh_mode = is_zh_page(out_url)
    raw = raw.replace("<!--KUI_TOPNAV-->", render_topnav(root, out_url))
    raw = raw.replace("<!--KUI_LANG_DROPDOWN-->", render_lang_dropdown(root, out_url))
    raw = raw.replace("<!--KUI_SIDEBAR-->", render_sidebar(root, out_url, groups))
    raw = raw.replace("<!--KUI_HERO-->", render_hero(root, zh_mode) if is_home else "")
    raw = raw.replace("<!--KUI_DOC_HEAD-->", "" if is_home else render_doc_head(root, out_url, src_rel, groups))
    raw = raw.replace("<!--KUI_BODY_CLASS-->", " home-readme-body" if is_home else "")
    raw = raw.replace("<!--KUI_PAGER-->", "" if is_home else render_pager(root, out_url, groups))
    if out_url == "README.zh-CN.html":
        raw = localize_zh_home_body(raw)
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


def locale_vars(root, out_rel):
    zh_mode = is_zh_page(out_rel)
    doclang = "zh-CN" if zh_mode else "en"
    pagelang = "zh" if zh_mode else "en"
    vars_list = [
        "--variable", "doclang=" + doclang,
        "--variable", "pagelang=" + pagelang,
    ]
    if out_rel in EN_TO_ZH:
        vars_list += ["--variable", "alt-zh=" + root + url_quote_rel(EN_TO_ZH[out_rel])]
        vars_list += ["--variable", "alt-en=" + root + url_quote_rel(out_rel)]
    elif out_rel in ZH_TO_EN:
        vars_list += ["--variable", "alt-en=" + root + url_quote_rel(ZH_TO_EN[out_rel])]
        vars_list += ["--variable", "alt-zh=" + root + url_quote_rel(out_rel)]
    return vars_list


def build_page(src_rel, out_rel, out_dir, pandoc_exe, groups):
    src = os.path.join(ROOT, src_rel)
    out = os.path.join(out_dir, out_rel)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    text = open(src, encoding="utf-8").read()
    depth = out_rel.count("/")
    root = "../" * depth
    args = ["--toc", "--toc-depth", "3",
            "--metadata", "pagetitle=" + title_of(text, os.path.basename(src_rel)),
            "--variable", "root=" + root,
            "--variable", "source=" + src_rel] + locale_vars(root, out_rel)
    pandoc(args, pandoc_exe, src=src, out=out)
    apply_kui_chrome(out, root, out_rel, src_rel, groups)


def build_source_page(src_rel, out_dir, pandoc_exe, groups):
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
            "--variable", "source=" + src_rel] + locale_vars(root, out_rel)
    html_text = pandoc(args, pandoc_exe, stdin_text=md)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(html_text)
    apply_kui_chrome(out, root, out_rel, src_rel, groups)


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

    for src_rel, out_rel in PAGES:
        if os.path.exists(os.path.join(ROOT, src_rel)):
            build_page(src_rel, out_rel, out_dir, a.pandoc, groups)
    for src_rel in SOURCES:
        build_source_page(src_rel, out_dir, a.pandoc, groups)
    for rel in COPIES:
        dst = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(os.path.join(ROOT, rel), dst)
    open(os.path.join(out_dir, ".nojekyll"), "w").close()
    print("built %d pages and %d source pages into %s" % (len(PAGES), len(SOURCES), out_dir))
    if a.check and not check(out_dir):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
