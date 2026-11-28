"""Flagship KUI-style landing page generator for the K programming language site."""
import html


def _t(zh, zh_text, en_text):
    return zh_text if zh else en_text


def _product_intro():
    return """<div class="k-product-intro" data-k-product-intro data-intro-key="klang:product-intro:v1" hidden aria-hidden="true">
  <div class="k-intro-backdrop"></div>
  <div class="k-intro-word k-intro-word--top" data-k-intro-word data-intro-direction="right">KLANG</div>
  <div class="k-intro-word k-intro-word--bottom" data-k-intro-word data-intro-direction="left">CORDIS</div>
  <div class="k-intro-stage">
    <svg class="k-intro-draft" viewBox="-120 -120 760 680" preserveAspectRatio="xMidYMid meet" fill="none" aria-hidden="true">
      <g class="k-intro-guides">
        <path d="M-40 72H560" data-k-trace pathLength="1"/>
        <path d="M-40 220H560" data-k-trace pathLength="1"/>
        <path d="M-40 368H560" data-k-trace pathLength="1"/>
        <path d="M128 -40V480" data-k-trace pathLength="1"/>
        <path d="M260 -40V480" data-k-trace pathLength="1"/>
        <path d="M424 -40V480" data-k-trace pathLength="1"/>
        <path d="M64 24L456 416" data-k-trace pathLength="1"/>
        <circle cx="260" cy="220" r="204" data-k-trace pathLength="1"/>
        <circle cx="162" cy="106" r="34" data-k-trace pathLength="1"/>
        <circle cx="388" cy="334" r="34" data-k-trace pathLength="1"/>
      </g>
      <g class="k-intro-extrude-base">
        <path d="M146 72H178A18 18 0 0 1 196 90V350A18 18 0 0 1 178 368H146A18 18 0 0 1 128 350V90A18 18 0 0 1 146 72Z"/>
        <path d="M214 206L342 72H424L292 220H214Z"/>
        <path d="M284 206L432 368H344L214 236V206Z"/>
        <path d="M248 188L280 220L248 252L216 220Z"/>
        <path d="M96 124H76V316H96"/>
        <path d="M456 124H476V316H456"/>
      </g>
      <g class="k-intro-extrude-edges"></g>
      <g class="k-intro-outline">
        <path d="M146 72H178A18 18 0 0 1 196 90V350A18 18 0 0 1 178 368H146A18 18 0 0 1 128 350V90A18 18 0 0 1 146 72Z" data-k-trace pathLength="1"/>
        <path d="M214 206L342 72H424L292 220H214Z" data-k-trace pathLength="1"/>
        <path d="M284 206L432 368H344L214 236V206Z" data-k-trace pathLength="1"/>
        <path d="M248 188L280 220L248 252L216 220Z" data-k-trace pathLength="1"/>
        <path d="M96 124H76V316H96" data-k-trace pathLength="1"/>
        <path d="M456 124H476V316H456" data-k-trace pathLength="1"/>
      </g>
    </svg>
  </div>
</div>"""


def _reel(zh):
    native_svg = '<path d="M6 8.5 11.5 13 6 17.5M13.5 17.5h6.5"/><rect x="3" y="4" width="20" height="18" rx="3"/>'
    wasi_svg = '<path d="m13 3-8.5 4.8v9.4L13 22l8.5-4.8V7.8L13 3Z"/><path d="M13 12.5 4.5 7.8M13 12.5l8.5-4.7M13 12.5V22"/>'
    jit_svg = '<path d="M14.5 3 6 14h6l-1.5 9L20 11.5h-6L14.5 3Z"/>'
    items = [
        ("native", native_svg, _t(zh, "原生 C/LTO", "native C/LTO")),
        ("wasi", wasi_svg, _t(zh, "WASI 模块", "WASI modules")),
        ("jit", jit_svg, _t(zh, "TinyCC 直跑", "TinyCC JIT")),
    ]
    tickets = []
    for idx, (mode, glyph, label) in enumerate(items):
        slot = 0 if idx == 0 else (1 if idx == 1 else -1)
        tickets.append(
            f'<span class="k-ticket k-ticket--{mode}" data-target-mode="{mode}" data-slot="{slot}">'
            f'<span class="k-ticket-tile"><svg width="24" height="24" viewBox="0 0 26 26" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round">{glyph}</svg></span>'
            f'<span>{html.escape(label)}</span></span>'
        )
    return (
        '<span class="k-reel" data-k-reel role="button" tabindex="0" '
        f'aria-label="{_t(zh, "点击切换编译目标", "Click to cycle compile target")}">'
        f'<span class="k-aperture">{"".join(tickets)}</span>'
        '</span>'
    )


def _guide_bot(root, zh):
    open_lbl = _t(zh, "打开 K 语言向导", "Open K guide")
    close_lbl = _t(zh, "收起 K 语言向导", "Close K guide")
    name = _t(zh, "K 语言向导", "K Guide")
    kense_home = "https://kense-labs.github.io/" if zh else "https://kense-labs.github.io/en/"
    items = [
        (f"{root}docs/zh/quickstart.html" if zh else f"{root}docs/tutorial.html", _t(zh, "10 分钟快速上手", "Start Tutorial"), "Start"),
        ("#k-live-studio", _t(zh, "实时推演数据栈与 BCE", "Live Compiler Studio"), "Live"),
        (f"{root}docs/zh/reference.html" if zh else f"{root}docs/reference.html", _t(zh, "查阅语言参考手册", "Language Reference"), "Spec"),
        (f"{root}docs/zh/bench/summary.html" if zh else f"{root}bench/summary.html", _t(zh, "16 项性能与内存基准", "16-Suite Benchmarks"), "1.19x"),
        (kense_home, _t(zh, "返回 Kense 官网首页", "Back to Kense Home"), "Kense"),
    ]
    links = "".join(
        f'<a href="{href}" style="--choice-delay:{65 + idx * 24}ms">'
        f'<span class="k-choice-bracket" aria-hidden="true">&gt;</span>'
        f'<span class="k-choice-text">{html.escape(lbl)}</span>'
        f'<span class="k-choice-tag">{html.escape(tag)}</span></a>'
        for idx, (href, lbl, tag) in enumerate(items)
    )
    return f"""<div class="k-guide" id="k-guide" data-copy-msg="{_t(zh, "命令已复制 · 可直接粘贴至终端运行", "Copied · ready in your terminal")}">
  <div class="k-guide-shell">
    <svg class="k-guide-ink" aria-hidden="true" focusable="false">
      <rect width="100%" height="100%" rx="10" fill="currentColor"/>
      <rect class="k-guide-rim" x="0.5" y="0.5" width="calc(100% - 1px)" height="calc(100% - 1px)" rx="9.5" fill="none"/>
      <svg x="49%" y="100%" width="1" height="1" overflow="visible">
        <path class="k-guide-tail" fill="currentColor" d="M-7-1H10v1H9q-1.3 0-2.4 1.1l-11 10.4q-2.1 1.9-1.7-1.3l1.2-8.8Q-4.7 0-6.3 0H-7Z"/>
      </svg>
    </svg>
    <button type="button" class="k-guide-trigger" id="k-guide-trigger" aria-expanded="false" aria-controls="k-guide-choices" aria-label="{open_lbl}" data-open-label="{open_lbl}" data-close-label="{close_lbl}">
      <span class="k-guide-gaze" aria-hidden="true"><span class="k-guide-eyes"><i></i><i></i></span></span>
      <span class="k-guide-name" aria-hidden="true">{name}</span>
      <svg class="k-guide-cross" width="15" height="15" viewBox="0 0 16 16" fill="none" aria-hidden="true"><path d="m4 4 8 8m0-8-8 8" stroke="currentColor" stroke-width="1.45" stroke-linecap="round"/></svg>
    </button>
    <div class="k-guide-speech" id="k-guide-speech" hidden><strong class="k-speech-title" id="k-speech-title"></strong></div>
    <nav class="k-guide-choices" id="k-guide-choices" aria-label="{name}" hidden>{links}</nav>
  </div>
</div>"""


def render_landing(root, zh):
    unix_cmd = "curl -fsSL https://raw.githubusercontent.com/kense-labs/klang/main/scripts/install.sh | sh"
    win_cmd = "irm https://raw.githubusercontent.com/kense-labs/klang/main/scripts/install.ps1 | iex"
    wasi_cmd = "k build --target wasm32-wasip1 hello.k -o build/hello.wasm && wasmtime run build/hello.wasm"
    agent_prompt = _t(
        zh,
        "请阅读 https://kense-labs.github.io/klang/README.zh-CN.html 与 docs/zh/reference.html，使用 K 语言编写带 checkfree 边界消除与数据栈作用域管理的模块，并运行 k check 验证。",
        "Read https://kense-labs.github.io/klang/ and docs/reference.html, implement the module in K using address-stable data stacks and checkfree bounds proofs, and verify with k check.",
    )

    tut_href = f"{root}docs/zh/quickstart.html" if zh else f"{root}docs/tutorial.html"
    ref_href = f"{root}docs/zh/reference.html" if zh else f"{root}docs/reference.html"
    bench_href = f"{root}docs/zh/bench/summary.html" if zh else f"{root}bench/summary.html"
    diag_href = f"{root}docs/zh/diagrams.html" if zh else f"{root}docs/diagrams.html"
    cordis_href = f"{root}docs/zh/design/cordis_microkernel.html" if zh else f"{root}docs/design/cordis_microkernel.html"
    wasm_href = f"{root}docs/zh/design/wasm.html" if zh else f"{root}docs/design/wasm.html"
    samples_href = f"{root}docs/zh/samples.html" if zh else f"{root}samples/index.html"
    stdlib_href = f"{root}docs/zh/stdlib.html" if zh else f"{root}docs/stdlib.html"

    svg_prefix = f"{root}docs/diagrams/svg/zh/" if zh else f"{root}docs/diagrams/svg/"
    atlas_tabs = [
        ("core", _t(zh, "系统核心架构", "Core Architecture"), svg_prefix + "core-architecture.svg",
         _t(zh, "共享语义与生命周期检查器，统一驱动原生 C/LTO、WASI Preview 1 与进程内 TinyCC JIT。", "Unified semantic and lifetime checker driving native C/LTO, WASI Preview 1, and in-process TinyCC JIT.")),
        ("stacks", _t(zh, "数据栈与生命周期", "Data Stacks & Lifetimes"), svg_prefix + "data-stacks-memory.svg",
         _t(zh, "变长对象定居于地址稳定的数据栈，作用域退出时单指令回退栈指针，零 GC 停顿。", "Growable values live on address-stable data stacks with single-instruction O(1) scope-exit rewind.")),
        ("bce", _t(zh, "优化与 BCE", "BCE & Optimizer"), svg_prefix + "optimization-bce-pipeline.svg",
         _t(zh, "通过 u32 in / upto 值域类型与 checkfree 契约，在编译期彻底消除内层循环越界分支。", "Domain-typed indices (u32 in / upto) and checkfree contracts eliminate hot-loop bounds checks at compile time.")),
        ("async", _t(zh, "异步与工作线程", "Async & Workers"), svg_prefix + "async-tasks-workers.svg",
         _t(zh, "词法任务作用域管理协作式挂起帧，隔离原生线程通过类型化队列零共享通信。", "Lexical task scopes manage cooperative suspension frames; isolated native workers communicate over typed queues.")),
        ("cordis", _t(zh, "Cordis 微内核", "Cordis Microkernel"), svg_prefix + "cordis.svg",
         _t(zh, "锁文件绑定服务图摘要与 ABI，支持在途请求代际钉住、热替换与异常自动回滚。", "Verified lock digests and ABI contracts pin in-flight request generations across hot-swaps and rollbacks.")),
        ("wasm", _t(zh, "WASI 目标", "WASI Target"), svg_prefix + "webassembly-target.svg",
         _t(zh, "同一份 .k 源码以 32 位线性内存校验栈增长，经 WASI SDK 产出标准 .wasm 命令模块。", "Same .k source checked under 32-bit layout with verified linear-memory stack growth for WASI hosts.")),
    ]

    atlas_btns = "".join(
        f'<button type="button" class="k-pill-tab" data-atlas-tab="{tid}" data-src="{tsrc}" '
        f'data-caption="{html.escape(tcap)}" aria-pressed="{"true" if i == 0 else "false"}">{html.escape(tlbl)}</button>'
        for i, (tid, tlbl, tsrc, tcap) in enumerate(atlas_tabs)
    )

    gateway_cards = [
        ("01 / TUTORIAL", _t(zh, "完整语言教程", "Language Tutorial"), tut_href,
         _t(zh, "循序渐进掌握数据栈、数组族、受检引用、可变模式枚举与隔离工作线程。", "Step-by-step tour of data stacks, array families, checked references, enums, and isolated threads.")),
        ("02 / REFERENCE", _t(zh, "语言参考与形式规范", "Reference & Formal Spec"), ref_href,
         _t(zh, "完整查阅值域索引类型、生命周期根规则、checkfree 契约与 C ABI 互操作。", "Complete specification of domain index types, reference roots, checkfree contracts, and C ABI handles.")),
        ("03 / STDLIB", _t(zh, "标准库与原生包", "Stdlib & Native Packages"), stdlib_href,
         _t(zh, "涵盖集合、I/O、确定性资源清理，以及由 .build 驱动的图形、UI、音频与物理扩展。", "Core collections, I/O, deterministic cleanup, plus graphics, UI, audio, and physics native packages.")),
        ("04 / SAMPLES", _t(zh, "31 个可运行示例", "31 Runnable Samples"), samples_href,
         _t(zh, "浏览可直接由 k run 或 k build 编译执行的真实系统、算法与并发代码例程。", "Inspect real systems, algorithm, and concurrency programs ready to run with k run or k build.")),
    ]
    gateway_html = "".join(
        f'<a class="k-gateway-card" href="{href}" data-k-hint="{html.escape(title)}">'
        f'<span class="k-serial">{serial}</span>'
        f'<strong>{html.escape(title)} <span aria-hidden="true">→</span></strong>'
        f'<p>{html.escape(desc)}</p></a>'
        for serial, title, href, desc in gateway_cards
    )

    return f"""<div class="k-landing-page">
  {_product_intro()}
  <section class="k-cover" data-k-intro-hero aria-labelledby="k-cover-title">
    <div class="k-optics" aria-hidden="true">
      <div class="k-optics-glow"></div>
      <canvas class="k-optics-canvas" data-k-optics width="720" height="380"></canvas>
    </div>

    <div class="k-hero-copy">
      <p class="k-eyebrow"><span class="k-eyebrow-dot" aria-hidden="true"></span><span>{_t(zh, "KENSE 系统栈 · K 语言 v0.1.0 · 无 GC 内存安全", "KENSE SYSTEMS · K LANGUAGE v0.1.0 · ZERO-GC SAFETY")}</span></p>
      <h1 id="k-cover-title" data-k-intro-title>
        <span class="k-title-prefix">{_t(zh, "内存安全的系统速度，编译至", "Zero-GC systems speed for")}</span>
        {_reel(zh)}
      </h1>
      <p class="k-lead" id="k-cover-lead" data-k-intro-lead>{_t(zh, "K 在编译期证明引用生命周期并消除热循环越界检查，将变长数据存放在地址稳定的数据栈中，于作用域退出时以 O(1) 单指令瞬时回收。", "Compile-time reference lifetime proofs and inner-loop bounds-check elimination over address-stable data stacks with O(1) scope-exit rewind.")}</p>

      <div class="k-cta-row">
        <a class="k-cover-btn k-cover-btn--primary" href="{tut_href}" data-k-hint="{_t(zh, "10 分钟写出第一个零 GC 的 K 程序", "Write your first zero-GC K program in 10 minutes")}">{_t(zh, "10 分钟快速上手", "Start Tutorial")} <span aria-hidden="true">→</span></a>
        <a class="k-cover-btn k-cover-btn--secondary" href="#k-live-studio" data-k-hint="{_t(zh, "在网页里单步推演数据栈回退、E0312 逃逸拦截与 checkfree 汇编", "Step interactively through data-stack rewind, E0312 diagnostics, and checkfree codegen")}">{_t(zh, "实时推演编译器", "Live Compiler Studio")} <span aria-hidden="true">↓</span></a>
        <a class="k-cover-btn k-cover-btn--ghost" href="{ref_href}" data-k-hint="{_t(zh, "查阅完整语法、生命周期与 C ABI 规范", "Browse syntax, lifetime rules, and C ABI reference")}">{_t(zh, "语言参考手册", "Language Reference")}</a>
      </div>
    </div>

    <div class="k-agent-row">
      <div class="k-guide-dock" data-k-guide-home aria-hidden="true"></div>
      <div class="k-cmd-dock" role="region" aria-label="{_t(zh, "快速安装与编译命令", "Quick install and build commands")}">
        <div class="k-cmd-tabs" role="tablist">
          <button type="button" class="k-cmd-tab" aria-selected="true" data-cmd-target="native" data-cmd="{html.escape(unix_cmd)}">macOS / Linux</button>
          <button type="button" class="k-cmd-tab" aria-selected="false" data-cmd-target="native" data-cmd="{html.escape(win_cmd)}">Windows</button>
          <button type="button" class="k-cmd-tab" aria-selected="false" data-cmd-target="wasi" data-cmd="{html.escape(wasi_cmd)}">WASI .wasm</button>
          <button type="button" class="k-cmd-tab" aria-selected="false" data-cmd-target="jit" data-cmd="k check hello.k &amp;&amp; k run hello.k">TinyCC JIT</button>
        </div>
        <div class="k-cmd-bar">
          <span class="k-cmd-prompt" aria-hidden="true">$</span>
          <code class="k-cmd-code" id="k-hero-cmd">{html.escape(unix_cmd)}</code>
          <button type="button" class="k-cmd-copy" id="k-hero-cmd-copy" data-en="Copy" data-zh="复制" data-k-hint="{_t(zh, "复制当前终端命令", "Copy terminal command")}">{_t(zh, "复制", "Copy")}</button>
          <button type="button" class="k-cmd-agent" id="k-hero-agent-copy" data-copy="{html.escape(agent_prompt)}" data-en="Agent Prompt" data-zh="Agent 提示词" data-k-hint="{_t(zh, "复制给编程 Agent 的 K 语言开发提示词", "Copy K language prompt for your coding agent")}">
            <svg width="12" height="12" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M12 2.5 14.7 9.3 21.5 12l-6.8 2.7L12 21.5l-2.7-6.8L2.5 12l6.8-2.7L12 2.5Z"/></svg>
            <span>{_t(zh, "Agent 提示词", "Agent Prompt")}</span>
          </button>
        </div>
      </div>
    </div>

    <div class="k-rail k-bounds">
      <div class="k-rail-telemetry">
        <span class="k-live-dot" aria-hidden="true"></span>
        <span>{_t(zh, "实测 1.19x 领先安全 Rust · 峰值内存省 1.25x · MIT 开源", "1.19x vs Safe Rust · 1.25x Lower Peak RAM · MIT License")}</span>
      </div>
      <div class="k-rail-controls">
        <div class="k-target-seg" role="group" aria-label="{_t(zh, "编译目标演示", "Target preview")}">
          <span class="k-target-thumb" id="k-target-thumb" aria-hidden="true"></span>
          <button type="button" class="k-target-btn" data-set-target="native" aria-pressed="true">Native</button>
          <button type="button" class="k-target-btn" data-set-target="wasi" aria-pressed="false">WASI</button>
          <button type="button" class="k-target-btn" data-set-target="jit" aria-pressed="false">JIT</button>
        </div>
        <button type="button" class="k-rail-icon-btn" id="k-motion-toggle" aria-label="{_t(zh, "暂停或播放背景光效", "Pause or play ambient motion")}" title="{_t(zh, "暂停 / 播放背景动效", "Pause / Play motion")}">
          <svg class="k-icon-pause" width="13" height="13" viewBox="0 0 16 16" fill="currentColor"><path d="M4 3h2.8v10H4zm5.2 0H12v10H9.2z"/></svg>
          <svg class="k-icon-play" width="13" height="13" viewBox="0 0 16 16" fill="currentColor"><path d="M4.5 2.8 13 8l-8.5 5.2z"/></svg>
        </button>
        <button type="button" class="k-rail-replay" id="k-intro-replay" data-k-hint="{_t(zh, "重新播放 K 建筑线框开场动画", "Replay the K 3D blueprint entrance animation")}">
          <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 8a5.5 5.5 0 1 1 1.4 3.7M2.5 4v4h4"/></svg>
          <span>{_t(zh, "重播开场", "Replay")}</span>
        </button>
      </div>
    </div>
  </section>

  {_guide_bot(root, zh)}

  <!-- 01: LIVE COMPILER & MEMORY STUDIO -->
  <section class="k-section k-bounds" id="k-live-studio" aria-labelledby="k-studio-heading">
    <p class="k-section-label">01 <i></i> LIVE COMPILER &amp; DATA STACK</p>
    <div class="k-section-head">
      <div>
        <h2 id="k-studio-heading">{_t(zh, "编译器与数据栈，实时交互推演", "Compiler & Data Stack, interactive")}</h2>
        <p class="k-section-note">{_t(zh, "不用看枯燥的静态长文——直接点击下方工作台，单步观察地址稳定数据栈如何 O(1) 瞬时回退、编译器如何拦截栈引用逃逸、checkfree 如何抹平越界分支，以及 Cordis 如何无损热更新服务。", "Skip walls of static prose — click through the live workbench to step scope-exit O(1) data-stack rewinds, trigger E0312 escape diagnostics, diff checkfree machine loops, and hot-swap Cordis providers.")}</p>
      </div>
      <a class="k-more-link" href="{tut_href}">{_t(zh, "深入语言教程", "Read full tutorial")} <span aria-hidden="true">→</span></a>
    </div>

    <div class="k-live-toolbar">
      <div class="k-pill-tabs" data-k-pill-bar="studio" role="tablist" aria-label="{_t(zh, "编译器交互视图", "Interactive compiler views")}">
        <span class="k-pill-indicator" aria-hidden="true"></span>
        <button type="button" class="k-pill-tab" data-studio-tab="stack" aria-pressed="true">{_t(zh, "数据栈 O(1) 回退", "Data Stack Rewind")}</button>
        <button type="button" class="k-pill-tab" data-studio-tab="lifetime" aria-pressed="false">{_t(zh, "生命周期逃逸拦截", "Lifetime Checker")}</button>
        <button type="button" class="k-pill-tab" data-studio-tab="bce" aria-pressed="false">{_t(zh, "checkfree 边界消除", "checkfree & ASM")}</button>
        <button type="button" class="k-pill-tab" data-studio-tab="cordis" aria-pressed="false">{_t(zh, "Cordis 微内核热切换", "Cordis Hot-Swap")}</button>
      </div>
      <span class="k-live-runtime"><i aria-hidden="true"></i>{_t(zh, "交互式语义与内存推演台", "Interactive Semantic & Memory Workbench")}</span>
    </div>

    <div class="k-live-shell" id="k-studio-shell">
      <!-- Applet 1: Data Stack O(1) Rewind -->
      <div class="k-studio-pane" data-studio-pane="stack">
        <div class="k-pane-head">
          <div class="k-step-pills" id="k-stack-steps">
            <button type="button" class="k-step-pill" data-stack-step="0" aria-pressed="true">{_t(zh, "① main 根帧", "① main Root")}</button>
            <button type="button" class="k-step-pill" data-stack-step="1" aria-pressed="false">{_t(zh, "② 进入请求作用域", "② Enter Scope")}</button>
            <button type="button" class="k-step-pill" data-stack-step="2" aria-pressed="false">{_t(zh, "③ 变长数组原位增长", "③ Grow Slice")}</button>
            <button type="button" class="k-step-pill" data-stack-step="3" aria-pressed="false">{_t(zh, "④ 退出作用域 O(1) 回退", "④ O(1) Rewind")}</button>
          </div>
          <div class="k-pane-actions">
            <button type="button" class="k-mini-btn" id="k-stack-grow">{_t(zh, "+ 追加 16 KB 变长块", "+ Push 16 KB Slice")}</button>
            <button type="button" class="k-mini-btn" id="k-stack-auto" aria-pressed="false">{_t(zh, "自动循环", "Auto-Play")}</button>
            <button type="button" class="k-mini-btn k-mini-btn--solid" id="k-stack-next">{_t(zh, "单步执行 →", "Step Scope →")}</button>
          </div>
        </div>
        <div class="k-studio-split">
          <div class="k-code-card">
            <div class="k-code-card-bar"><span>request_batch.k</span><span class="k-code-tag" id="k-stack-line-tag">line 4 · frame #0</span></div>
            <div class="k-sim-lines" id="k-stack-code">
              <div class="k-sim-line" data-line="0"><i>1</i><span><b class="kw">fn</b> <b>serve_batch</b>(packets: <em class="ty">Packet</em>[]) -&gt; <em class="ty">Summary</em> {{</span></div>
              <div class="k-sim-line is-active" data-line="0"><i>2</i><span>    <b class="kw">var</b> out: <em class="ty">Summary</em> = Summary::new(); <em class="cm">// frame #0 (8 KB)</em></span></div>
              <div class="k-sim-line" data-line="1"><i>3</i><span>    <b class="kw">for</b> pkt <b class="kw">in</b> packets {{ <em class="cm">// 进入内层数据栈作用域</em></span></div>
              <div class="k-sim-line" data-line="1"><i>4</i><span>        <b class="kw">var</b> tokens: <em class="ty">Token</em>[] = parse_arena(pkt); <em class="cm">// 24 KB 连续分配</em></span></div>
              <div class="k-sim-line" data-line="2"><i>5</i><span>        tokens.push_all(expand_macros(pkt)); <em class="cm">// 地址稳定原位增长</em></span></div>
              <div class="k-sim-line" data-line="2"><i>6</i><span>        out.commit(tokens.len()); <em class="cm">// 仅提交值，不逃逸内部引用</em></span></div>
              <div class="k-sim-line" data-line="3"><i>7</i><span>    }} <em class="cm">// ← 作用域退出：SP 单指令回退至 0x02000，0 次 free()！</em></span></div>
              <div class="k-sim-line" data-line="0"><i>8</i><span>    <b class="kw">return</b> out;</span></div>
              <div class="k-sim-line" data-line="0"><i>9</i><span>}}</span></div>
            </div>
          </div>

          <div class="k-mem-stage">
            <div class="k-mem-track-card">
              <div class="k-mem-head">
                <strong>{_t(zh, "K 地址稳定数据栈 (Contiguous datastack)", "K Address-Stable Data Stack")}</strong>
                <span class="k-sp-badge" id="k-sp-readout">SP = 0x02000 · 8 KB</span>
              </div>
              <div class="k-stack-bar" id="k-stack-bar">
                <div class="k-stack-seg k-stack-seg--root" style="width:16%"><span>#0 out · 8K</span></div>
                <div class="k-stack-seg k-stack-seg--scope" id="k-seg-scope" style="width:0%"><span>#1 tokens · 24K</span></div>
                <div class="k-stack-seg k-stack-seg--grow" id="k-seg-grow" style="width:0%"><span id="k-seg-grow-lbl">+16K grow</span></div>
                <div class="k-sp-cursor" id="k-sp-cursor" style="left:16%"><i>SP</i></div>
              </div>
              <div class="k-mem-stats">
                <div><span>{_t(zh, "堆分配 malloc/free", "Heap malloc/free")}</span><strong>0 / 0</strong></div>
                <div><span>{_t(zh, "GC 扫描停顿", "GC mark pauses")}</span><strong>0.00 ms</strong></div>
                <div><span>{_t(zh, "作用域回收开销", "Scope exit cost")}</span><strong id="k-rewind-cost">{_t(zh, "1 条指令 (O(1))", "1 instruction (O(1))")}</strong></div>
              </div>
            </div>

            <div class="k-mem-track-card k-mem-track-card--muted">
              <div class="k-mem-head">
                <span>{_t(zh, "对比：传统逐对象堆分配 / GC 运行时", "Contrast: Traditional Heap / GC Allocator")}</span>
                <span class="k-heap-badge" id="k-heap-status">{_t(zh, "142 次离散分配 · 指针追逐", "142 scattered allocs · pointer chasing")}</span>
              </div>
              <div class="k-heap-grid" id="k-heap-grid" aria-hidden="true">
                <i class="on"></i><i></i><i class="on"></i><i class="frag"></i><i class="on"></i><i></i><i class="on"></i><i class="on"></i><i class="frag"></i><i></i><i class="on"></i><i class="frag"></i><i class="on"></i><i></i><i class="on"></i><i class="frag"></i><i class="on"></i><i class="on"></i>
              </div>
              <p class="k-mem-caption" id="k-stack-narr">{_t(zh, "点击「单步执行 →」或「+ 追加 16 KB 变长块」，观察 K 如何在线性地址空间内完成变长增长与单指令瞬时回收。", "Click “Step Scope →” or “+ Push 16 KB Slice” to watch K grow slices in-place and reclaim the entire scope in one instruction.")}</p>
            </div>
          </div>
        </div>
      </div>

      <!-- Applet 2: Compile-Time Lifetime & Escape Checker -->
      <div class="k-studio-pane" data-studio-pane="lifetime" hidden>
        <div class="k-pane-head">
          <div class="k-step-pills">
            <button type="button" class="k-step-pill" data-lt-case="safe" aria-pressed="true">{_t(zh, "✓ 合法值提交 (0 逃逸)", "✓ Safe Value Commit")}</button>
            <button type="button" class="k-step-pill" data-lt-case="escape" aria-pressed="false">{_t(zh, "× 拦截栈引用逃逸 (E0312)", "× Reject Ref Escape (E0312)")}</button>
            <button type="button" class="k-step-pill" data-lt-case="async" aria-pressed="false">{_t(zh, "⚡ 异步挂起帧根校验", "⚡ Async Suspension Frame")}</button>
          </div>
          <span class="k-diag-pill k-diag-pill--ok" id="k-lt-badge">{_t(zh, "k check · 通过 (1.2 ms)", "k check · PASS (1.2 ms)")}</span>
        </div>
        <div class="k-studio-split">
          <div class="k-code-card">
            <div class="k-code-card-bar"><span id="k-lt-file">lifetime_commit.k</span><span class="k-code-tag">semantic-checker</span></div>
            <pre class="k-live-pre" id="k-lt-code"></pre>
          </div>
          <div class="k-diag-stage">
            <div class="k-diag-console">
              <div class="k-code-card-bar"><span>$ k check --explain-roots</span><span class="k-code-tag" id="k-lt-status-tag">EXIT 0</span></div>
              <div class="k-diag-body" id="k-lt-output"></div>
            </div>
            <div class="k-root-graph" id="k-lt-graph"></div>
          </div>
        </div>
      </div>

      <!-- Applet 3: checkfree Bounds-Check Elimination -->
      <div class="k-studio-pane" data-studio-pane="bce" hidden>
        <div class="k-pane-head">
          <div class="k-step-pills">
            <button type="button" class="k-step-pill" data-bce-mode="on" aria-pressed="true">{_t(zh, "开启 u32 in + checkfree 证明", "Domain Index + checkfree ON")}</button>
            <button type="button" class="k-step-pill" data-bce-mode="off" aria-pressed="false">{_t(zh, "对比：常规运行时越界分支", "Contrast: Runtime Bounds Traps")}</button>
          </div>
          <span class="k-diag-pill k-diag-pill--ok" id="k-bce-badge">{_t(zh, "0 条越界分支 · 领先安全 Rust 1.32x", "0 bounds traps · 1.32x vs Safe Rust")}</span>
        </div>
        <div class="k-studio-split">
          <div class="k-code-card">
            <div class="k-code-card-bar"><span>graph_csr_bfs.k</span><span class="k-code-tag">bench/graph-csr</span></div>
            <pre class="k-live-pre" id="k-bce-kcode"></pre>
          </div>
          <div class="k-code-card">
            <div class="k-code-card-bar"><span id="k-bce-target-title">ARM64 / x86_64 Inner Loop (Clang -O3 --lto)</span><span class="k-code-tag" id="k-bce-insn-tag">4 insns / edge</span></div>
            <pre class="k-live-pre k-asm-pre" id="k-bce-asm"></pre>
          </div>
        </div>
      </div>

      <!-- Applet 4: Cordis Microkernel Service Graph -->
      <div class="k-studio-pane" data-studio-pane="cordis" hidden>
        <div class="k-pane-head">
          <div class="k-step-pills">
            <span class="k-cordis-lock-pill">cordis.lock · sha256:9f4c…82a1</span>
            <span class="k-cordis-gen-pill" id="k-cordis-gen-badge">{_t(zh, "当前激活代际：gen #1 (稳定)", "Active generation: gen #1 (healthy)")}</span>
          </div>
          <div class="k-pane-actions">
            <button type="button" class="k-mini-btn k-mini-btn--solid" id="k-cordis-swap">{_t(zh, "热切换推理提供方 v1 → v2", "Hot-Swap ONNX Provider v1 → v2")}</button>
            <button type="button" class="k-mini-btn" id="k-cordis-fault">{_t(zh, "模拟坏版本自动回滚", "Simulate Bad Digest Rollback")}</button>
          </div>
        </div>
        <div class="k-cordis-grid">
          <div class="k-cordis-nodes" id="k-cordis-nodes">
            <button type="button" class="k-cordis-node is-selected" data-node="onnx">
              <span class="k-node-dot is-live"></span>
              <div><strong>onnx-runtime-cpu</strong><small id="k-onnx-ver">provider · gen #1 · pinned</small></div>
              <span class="k-node-tag">AI</span>
            </button>
            <button type="button" class="k-cordis-node" data-node="tcp">
              <span class="k-node-dot is-live"></span>
              <div><strong>bounded-tcp-gateway</strong><small>ingress · max 4,096 streams</small></div>
              <span class="k-node-tag">NET</span>
            </button>
            <button type="button" class="k-cordis-node" data-node="ts">
              <span class="k-node-dot is-live"></span>
              <div><strong>k-cordis-ts (QuickJS)</strong><small>guest ESM · top-level await</small></div>
              <span class="k-node-tag">ESM</span>
            </button>
            <button type="button" class="k-cordis-node" data-node="wasi">
              <span class="k-node-dot is-live"></span>
              <div><strong>wasm32-wasip1-host</strong><small>sandbox · preopened caps</small></div>
              <span class="k-node-tag">WASI</span>
            </button>
          </div>
          <div class="k-cordis-inspector">
            <div class="k-code-card-bar"><span id="k-cordis-title">Service Contract · onnx-runtime-cpu</span><span class="k-code-tag" id="k-cordis-abi">ABI v1 · Verified</span></div>
            <div class="k-cordis-detail" id="k-cordis-detail"></div>
            <div class="k-cordis-log" id="k-cordis-log"></div>
          </div>
        </div>
      </div>
    </div>

    <div class="k-live-meta">
      <span>{_t(zh, "所有机制均由单二进制编译器 k 静态验证 · 无需垃圾回收器或重型虚拟机", "All guarantees statically verified by the k compiler · zero garbage collector or runtime VM")}</span>
      <a href="{cordis_href}">{_t(zh, "阅读 Cordis 微内核设计说明", "Read Cordis microkernel design")} <span aria-hidden="true">→</span></a>
    </div>
  </section>

  <!-- 02: BENCHMARK TELEMETRY -->
  <section class="k-section k-bounds" id="k-benchmarks" aria-labelledby="k-bench-heading">
    <p class="k-section-label">02 <i></i> BENCHMARK TELEMETRY</p>
    <div class="k-section-head">
      <div>
        <h2 id="k-bench-heading">{_t(zh, "与最佳安全 Rust 及 C++ 正面实测对比", "Measured head-to-head against Safe Rust & C++")}</h2>
        <p class="k-section-note">{_t(zh, "全套 16 项大型负载几何平均与 Apple M6 成对实测（k -O3 --lto --cpu=native 对比 rustc 1.98.1 fat-LTO）：紧凑可变模式枚举、数据栈连续局部性与编译期 checkfree 让 K 同时拿到更高吞吐与更低峰值内存。", "16-workload geometric mean and Apple M6 paired runs (k -O3 --lto --cpu=native vs rustc 1.98.1 fat-LTO): compact variable-mode enums, contiguous data stacks, and checkfree proofs deliver higher speed and lower RAM simultaneously.")}</p>
      </div>
      <a class="k-more-link" href="{bench_href}">{_t(zh, "查看完整基准报告", "Full benchmark report")} <span aria-hidden="true">→</span></a>
    </div>

    <div class="k-metric-bento">
      <div class="k-metric-tile">
        <span class="k-serial">GEOMEAN · CLANG LTO</span>
        <strong class="k-metric-big">1.19<span>x</span></strong>
        <p>{_t(zh, "整体速度快于最佳安全 Rust 基准行", "Faster than Best Safe Rust baseline")}</p>
      </div>
      <div class="k-metric-tile">
        <span class="k-serial">IDIOMATIC C++</span>
        <strong class="k-metric-big">3.54<span>x</span></strong>
        <p>{_t(zh, "整体速度快于惯用现代 C++ 实现", "Faster than idiomatic C++ (1.26x vs expert C++)")}</p>
      </div>
      <div class="k-metric-tile">
        <span class="k-serial">PEAK MEMORY</span>
        <strong class="k-metric-big">1.25<span>x</span></strong>
        <p>{_t(zh, "峰值内存占用低于安全 Rust（较 C++ 省 1.95x）", "Lower peak RAM vs Rust (1.95x lower vs C++)")}</p>
      </div>
      <div class="k-metric-tile">
        <span class="k-serial">DETERMINISTIC</span>
        <strong class="k-metric-big">0<span>ms</span></strong>
        <p>{_t(zh, "运行时 GC 停顿与引用计数原子争用", "Runtime GC pauses or refcount atomic contention")}</p>
      </div>
    </div>

    <div class="k-bench-explorer">
      <div class="k-bench-bar-head">
        <div class="k-pill-tabs" data-k-pill-bar="bench">
          <span class="k-pill-indicator" aria-hidden="true"></span>
          <button type="button" class="k-pill-tab" data-bench-metric="speed" aria-pressed="true">{_t(zh, "运行速度倍率 (Rust 耗时 / K 耗时)", "Throughput Speedup (Rust / K)")}</button>
          <button type="button" class="k-pill-tab" data-bench-metric="mem" aria-pressed="false">{_t(zh, "峰值内存优势 (Rust 内存 / K 内存)", "Peak Memory Advantage (Rust / K)")}</button>
        </div>
        <span class="k-bench-legend"><i></i>{_t(zh, "数值 > 1.0x 表示 K 更快 / 更省内存（1.0x 虚线为安全 Rust 基线）", "Values > 1.0x mean K is faster / uses less RAM (1.0x dashed line = Safe Rust)")}</span>
      </div>
      <div class="k-bench-rows" id="k-bench-rows"></div>
    </div>
  </section>

  <!-- 03: LANGUAGE SPECIMENS -->
  <section class="k-section k-bounds" id="k-specimens" aria-labelledby="k-specimens-heading">
    <p class="k-section-label">03 <i></i> CORE SPECIMENS</p>
    <div class="k-section-head">
      <div>
        <h2 id="k-specimens-heading">{_t(zh, "三项核心原语，重塑系统编程手感", "Three core primitives, zero ceremony")}</h2>
        <p class="k-section-note">{_t(zh, "没有悬垂指针、没有复杂生命周期标注体操、也没有运行时 GC 税。点击任意卡片深入对应设计规范。", "No dangling pointers, no lifetime-annotation gymnastics, and no GC tax. Open any specimen to inspect its full specification.")}</p>
      </div>
      <a class="k-more-link" href="{samples_href}">{_t(zh, "浏览全部 31 个源码示例", "View all 31 code samples")} <span aria-hidden="true">→</span></a>
    </div>

    <div class="k-specimens-grid">
      <article class="k-specimen-card" data-k-hint="{_t(zh, "变长枚举按实际载荷紧凑内联存储，比 Rust 省 3.98x 内存", "Variable-mode enums pack payloads inline — 3.98x less RAM than Rust")}">
        <div class="k-specimen-art k-specimen-art--rose" aria-hidden="true">
          <svg viewBox="0 0 320 128" fill="none">
            <rect x="22" y="28" width="276" height="72" rx="10" class="sp-frame"/>
            <rect x="34" y="40" width="76" height="48" rx="6" class="sp-slot sp-slot--a"/>
            <rect x="118" y="40" width="92" height="48" rx="6" class="sp-slot sp-slot--b"/>
            <rect x="218" y="40" width="68" height="48" rx="6" class="sp-slot sp-slot--c"/>
            <path d="M286 20v88" class="sp-cursor"/>
            <text x="46" y="68" class="sp-mono">Frame #0</text>
            <text x="132" y="68" class="sp-mono">Inline Enum</text>
            <text x="229" y="68" class="sp-mono">O(1) SP</text>
          </svg>
        </div>
        <div class="k-specimen-body">
          <span class="k-serial">01 / STORAGE &amp; LAYOUT</span>
          <h3><a href="{tut_href}">{_t(zh, "地址稳定数据栈与紧凑枚举", "Address-stable stacks & inline enums")} <span aria-hidden="true">→</span></a></h3>
          <p>{_t(zh, "变长容器与可变模式枚举直接按真实载荷大小紧凑排列在数据栈上，配合 defer / errdefer 确定性清理。", "Growable slices and variable-mode enums pack inline by actual payload size with deterministic defer / errdefer cleanup.")}</p>
        </div>
      </article>

      <article class="k-specimen-card" data-k-hint="{_t(zh, "u32 in dist 与 checkfree 契约在编译期证明零越界访问", "u32 in dist + checkfree proves zero out-of-bounds accesses at compile time")}">
        <div class="k-specimen-art k-specimen-art--indigo" aria-hidden="true">
          <svg viewBox="0 0 320 128" fill="none">
            <rect x="24" y="26" width="272" height="32" rx="7" class="sp-frame"/>
            <rect x="24" y="70" width="272" height="32" rx="7" class="sp-frame"/>
            <path d="M40 42h170" class="sp-proof-line"/>
            <circle cx="268" cy="42" r="9" class="sp-badge"/>
            <path d="m264 42 3 3 6-6" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>
            <text x="40" y="90" class="sp-mono">checkfree fn kernel() · 0 traps</text>
          </svg>
        </div>
        <div class="k-specimen-body">
          <span class="k-serial">02 / ZERO-BRANCH KERNELS</span>
          <h3><a href="{ref_href}">{_t(zh, "值域索引类型与 checkfree 门禁", "Domain indices & checkfree proofs")} <span aria-hidden="true">→</span></a></h3>
          <p>{_t(zh, "用 u32 in arr 与 u32 upto end 表达索引不变量；若热循环仍残留任何越界检查，checkfree 会在编译期直接报错。", "Express index invariants via u32 in arr and u32 upto end; checkfree fails compilation if any bounds check remains.")}</p>
        </div>
      </article>

      <article class="k-specimen-card" data-k-hint="{_t(zh, "一套前端同时产出原生可执行程序、WASI 模块与 C ABI 库", "One frontend builds native binaries, WASI modules, and C ABI libraries")}">
        <div class="k-specimen-art k-specimen-art--teal" aria-hidden="true">
          <svg viewBox="0 0 320 128" fill="none">
            <rect x="24" y="42" width="76" height="44" rx="8" class="sp-slot sp-slot--a"/>
            <path d="M104 64h46m0 0 18-26m-18 26h18m-18 0 18 26" class="sp-proof-line"/>
            <rect x="174" y="20" width="120" height="24" rx="6" class="sp-frame"/>
            <rect x="174" y="52" width="120" height="24" rx="6" class="sp-frame"/>
            <rect x="174" y="84" width="120" height="24" rx="6" class="sp-frame"/>
            <text x="45" y="68" class="sp-mono">hello.k</text>
            <text x="188" y="36" class="sp-mono">Native C/LTO</text>
            <text x="188" y="68" class="sp-mono">wasm32-wasip1</text>
            <text x="188" y="100" class="sp-mono">TinyCC In-Proc</text>
          </svg>
        </div>
        <div class="k-specimen-body">
          <span class="k-serial">03 / MULTI-TARGET &amp; WASI</span>
          <h3><a href="{wasm_href}">{_t(zh, "原生、WASI 与零胶水 C 互操作", "Native, WASI & direct C ABI")} <span aria-hidden="true">→</span></a></h3>
          <p>{_t(zh, "通过 extern / export fn 直连系统 C 库并显式移交句柄所有权；加一行 --target wasm32-wasip1 即可构建 WASI 模块。", "Bind C libraries directly via extern / export fn with explicit handle ownership, or target wasm32-wasip1 in one flag.")}</p>
        </div>
      </article>
    </div>
  </section>

  <!-- 04: ARCHITECTURE ATLAS -->
  <section class="k-section k-bounds" id="k-atlas" aria-labelledby="k-atlas-heading">
    <p class="k-section-label">04 <i></i> ARCHITECTURE ATLAS</p>
    <div class="k-section-head">
      <div>
        <h2 id="k-atlas-heading">{_t(zh, "K 编译器与 Kense OS 架构蓝图", "K Compiler & Kense OS Blueprints")}</h2>
        <p class="k-section-note">{_t(zh, "全套 18 张高精矢量架构图解自动适配深浅色主题，点击画布即可全屏检视细节或打开原始 SVG。", "All 18 vector architecture blueprints adapt automatically to dark and light themes — click any blueprint to zoom full-screen.")}</p>
      </div>
      <a class="k-more-link" href="{diag_href}">{_t(zh, "打开全部 18 张架构图解", "Explore all 18 diagrams")} <span aria-hidden="true">→</span></a>
    </div>

    <div class="k-live-toolbar">
      <div class="k-pill-tabs" data-k-pill-bar="atlas" role="tablist">
        <span class="k-pill-indicator" aria-hidden="true"></span>
        {atlas_btns}
      </div>
      <div class="k-atlas-actions">
        <a class="stage-chip" id="k-atlas-svg-link" href="{atlas_tabs[0][2]}" target="_blank" rel="noopener">{_t(zh, "打开 SVG", "Open SVG")}</a>
        <button type="button" class="stage-chip" id="k-atlas-zoom">{_t(zh, "全屏放大", "Full-screen")}</button>
      </div>
    </div>

    <div class="k-atlas-frame">
      <img id="k-atlas-img" class="k-atlas-img" src="{atlas_tabs[0][2]}" alt="{html.escape(atlas_tabs[0][1])}">
      <div class="k-atlas-foot">
        <span id="k-atlas-caption">{html.escape(atlas_tabs[0][3])}</span>
        <span>{_t(zh, "点击图片可放大检视", "Click blueprint to inspect")}</span>
      </div>
    </div>

    <div class="k-gateway-grid">
      {gateway_html}
    </div>
  </section>
</div>"""


def render_spec_open(zh):
    title = _t(
        zh,
        "展开完整子系统规范、从源码构建与命令行参考手册（15 个架构章节）",
        "Expand Complete Subsystem Specification, Source Build & CLI Reference (15 Sections)",
    )
    sub = _t(
        zh,
        "包含前端、语义分析、C/LTO 后端、WASI、目标运行时、LSP、Cordis、客体语言与安装构建全量说明",
        "Includes front end, semantic checker, C/LTO backend, WASI, runtime, LSP, Cordis, guest languages, and build instructions",
    )
    return (
        '<section class="k-spec-section k-bounds" aria-label="Full technical specification">'
        '<details class="k-full-spec" id="k-full-spec">'
        '<summary class="k-full-spec-summary">'
        '<div class="k-full-spec-copy">'
        '<span class="k-serial">05 / COMPLETE TECHNICAL SPECIFICATION &amp; MANUAL</span>'
        f'<strong>{html.escape(title)}</strong>'
        f'<span>{html.escape(sub)}</span>'
        '</div>'
        '<span class="k-full-spec-btn">'
        f'<span data-en="Expand" data-zh="展开全文">{_t(zh, "展开全文", "Expand")}</span>'
        '<svg class="k-full-spec-chevron" width="15" height="15" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6l4 4 4-4"/></svg>'
        '</span>'
        '</summary>'
        '<div class="k-full-spec-body">'
    )


def render_spec_close():
    return "</div></details></section>"


def render_landing_footer(root, zh, source_rel):
    kense_home = "https://kense-labs.github.io/" if zh else "https://kense-labs.github.io/en/"
    kui_home = "https://kense-labs.github.io/kui/" if zh else "https://kense-labs.github.io/en/kui/"
    tut = f"{root}docs/zh/tutorial.html" if zh else f"{root}docs/tutorial.html"
    ref = f"{root}docs/zh/reference.html" if zh else f"{root}docs/reference.html"
    diag = f"{root}docs/zh/diagrams.html" if zh else f"{root}docs/diagrams.html"
    bench = f"{root}docs/zh/bench/summary.html" if zh else f"{root}bench/summary.html"
    samples = f"{root}docs/zh/samples.html" if zh else f"{root}samples/index.html"
    cordis = f"{root}docs/zh/design/cordis_microkernel.html" if zh else f"{root}docs/design/cordis_microkernel.html"
    return f"""<footer class="k-company-footer k-bounds">
  <div class="k-footer-card">
    <div class="k-footer-grid">
      <div class="k-footer-brand">
        <a class="k-footer-lockup" href="{root}{"README.zh-CN.html" if zh else "index.html"}">
          <svg width="20" height="20" viewBox="0 0 64 64" fill="none" aria-hidden="true">
            <path fill="#FB7185" d="M11 12a3 3 0 0 1 3-3h5a3 3 0 0 1 3 3v40a3 3 0 0 1-3 3h-5a3 3 0 0 1-3-3Z"/>
            <path fill="#FDA4AF" d="M23 29.5 40.5 9H52.5L33.5 31.5H23Z"/>
            <path fill="#E11D48" d="M32.5 29.5 53.5 55H40.5L23 34V29.5Z"/>
            <path fill="#FFF1F2" d="m27.5 27.2 4.8 4.8-4.8 4.8-4.8-4.8Z"/>
          </svg>
          <span>{_t(zh, "K 编程语言", "K Language")}</span>
        </a>
        <p>{_t(zh, "编译期引用生命周期检查、地址稳定数据栈与 Cordis 微内核架构驱动的无 GC 系统编程语言。", "Memory-safe systems programming language with checked reference lifetimes, address-stable data stacks, and the Cordis microkernel.")}</p>
      </div>
      <div class="k-footer-col">
        <h3>{_t(zh, "学习与参考", "Learn & Reference")}</h3>
        <ul>
          <li><a href="{tut}">{_t(zh, "语言教程", "Tutorial")}</a></li>
          <li><a href="{ref}">{_t(zh, "语言参考手册", "Language Reference")}</a></li>
          <li><a href="{samples}">{_t(zh, "31 个实战示例", "31 Code Samples")}</a></li>
        </ul>
      </div>
      <div class="k-footer-col">
        <h3>{_t(zh, "架构与实测", "Architecture & Data")}</h3>
        <ul>
          <li><a href="{diag}">{_t(zh, "18 张架构图解", "18 System Diagrams")}</a></li>
          <li><a href="{cordis}">{_t(zh, "Cordis 微内核", "Cordis Microkernel")}</a></li>
          <li><a href="{bench}">{_t(zh, "16 项性能基准", "Benchmark Suite")}</a></li>
        </ul>
      </div>
      <div class="k-footer-col">
        <h3>{_t(zh, "Kense 产品栈", "Kense Ecosystem")}</h3>
        <ul>
          <li><a href="{kense_home}">{_t(zh, "Kense 官网首页", "Kense Home")}</a></li>
          <li><a href="{kui_home}">{_t(zh, "KUI 跨端组件站", "KUI Components")}</a></li>
          <li><a href="https://github.com/kense-labs/klang">GitHub · kense-labs/klang</a></li>
        </ul>
      </div>
    </div>
    <div class="k-footer-legal">
      <span>© Kense · MIT License</span>
      <span><a href="https://github.com/kense-labs/klang/blob/main/{html.escape(source_rel)}"><code>{html.escape(source_rel)}</code></a></span>
    </div>
  </div>
</footer>"""
