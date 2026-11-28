// K Language Flagship Website & Documentation — KUI Design & Interactive Studio
(function () {
  var SVG_NS = "http://www.w3.org/2000/svg";

  function getActiveLang() {
    return document.documentElement.getAttribute("data-lang") || "en";
  }

  function isZh() {
    return getActiveLang() === "zh";
  }

  function clamp01(v) {
    return v < 0 ? 0 : v > 1 ? 1 : v;
  }

  function easeOutCubic(t) {
    var u = 1 - clamp01(t);
    return 1 - u * u * u;
  }

  function easeOutQuart(t) {
    var u = 1 - clamp01(t);
    return 1 - u * u * u * u;
  }

  function easeInCubic(t) {
    var c = clamp01(t);
    return c * c * c;
  }

  function easeInOut(t) {
    var c = clamp01(t);
    return c * c * (3 - 2 * c);
  }

  function easeMove(t) {
    var c = clamp01(t);
    if (c < 0.38) {
      var u = c / 0.38;
      return 0.42 * u * u;
    }
    var v = 1 - (c - 0.38) / 0.62;
    return 1 - 0.58 * v * v * v;
  }

  function lerpU8(a, b, t) {
    return Math.max(0, Math.min(255, Math.round(a + (b - a) * t)));
  }

  // ---------- Sliding Capsule Pill Indicators ----------
  function syncPillBar(bar, animate) {
    if (!bar) return;
    var active = bar.querySelector('[aria-current="true"], [aria-pressed="true"]');
    if (!active) return;
    var br = bar.getBoundingClientRect();
    var ar = active.getBoundingClientRect();
    if (br.width <= 0 || ar.width <= 0) return;
    if (animate) bar.setAttribute("data-indicator-animate", "");
    bar.style.setProperty("--pill-x", (ar.left - br.left + bar.scrollLeft).toFixed(1) + "px");
    bar.style.setProperty("--pill-y", (ar.top - br.top).toFixed(1) + "px");
    bar.style.setProperty("--pill-w", ar.width.toFixed(1) + "px");
    bar.style.setProperty("--pill-h", ar.height.toFixed(1) + "px");
    bar.setAttribute("data-indicator-ready", "");
  }

  function syncAllPillBars(animate) {
    document.querySelectorAll("[data-k-pill-bar]").forEach(function (bar) {
      syncPillBar(bar, animate);
    });
  }

  // ---------- Locale, Theme & Accent ----------
  function applyLocale(lang) {
    document.documentElement.setAttribute("data-lang", lang);

    document.querySelectorAll("[data-en][data-zh]").forEach(function (el) {
      if (el.hasAttribute("data-copied")) return;
      el.textContent = lang === "zh" ? el.getAttribute("data-zh") : el.getAttribute("data-en");
    });

    document.querySelectorAll("[data-en-href][data-zh-href]").forEach(function (el) {
      el.setAttribute("href", lang === "zh" ? el.getAttribute("data-zh-href") : el.getAttribute("data-en-href"));
    });

    document.querySelectorAll("[data-en-placeholder][data-zh-placeholder]").forEach(function (el) {
      el.setAttribute("placeholder", lang === "zh" ? el.getAttribute("data-zh-placeholder") : el.getAttribute("data-en-placeholder"));
    });

    document.querySelectorAll(".lang-option").forEach(function (opt) {
      opt.setAttribute("aria-checked", opt.getAttribute("data-lang-val") === lang ? "true" : "false");
    });

    requestAnimationFrame(function () { syncAllPillBars(false); });
  }

  function initTheme() {
    var btn = document.getElementById("k-theme-toggle");
    if (!btn) return;
    btn.addEventListener("click", function () {
      var cur = document.documentElement.getAttribute("data-theme") || "light";
      var next = cur === "dark" ? "light" : "dark";
      document.documentElement.setAttribute("data-theme", next);
      try {
        localStorage.setItem("k-docs-theme", next);
        localStorage.setItem("theme", next);
      } catch (e) {}
    });
  }

  function initAccent() {
    var btn = document.getElementById("k-accent-btn");
    var pop = document.getElementById("k-accent-popover");
    if (!btn || !pop) return;

    function syncActive() {
      var cur = document.documentElement.getAttribute("data-accent") || "neutral";
      pop.querySelectorAll(".accent-swatch").forEach(function (el) {
        el.setAttribute("aria-pressed", el.getAttribute("data-accent-val") === cur ? "true" : "false");
      });
    }
    syncActive();

    btn.addEventListener("click", function (e) {
      e.stopPropagation();
      var langPop = document.getElementById("k-lang-popover");
      if (langPop) langPop.classList.remove("open");
      var open = pop.classList.toggle("open");
      btn.setAttribute("aria-expanded", open ? "true" : "false");
    });

    pop.addEventListener("click", function (e) {
      var sw = e.target.closest(".accent-swatch");
      if (!sw) return;
      var val = sw.getAttribute("data-accent-val");
      document.documentElement.setAttribute("data-accent", val);
      try {
        localStorage.setItem("k-docs-accent", val);
        localStorage.setItem("kui-accent", val);
      } catch (err) {}
      syncActive();
    });

    document.addEventListener("click", function (e) {
      if (!pop.contains(e.target) && e.target !== btn) {
        pop.classList.remove("open");
        btn.setAttribute("aria-expanded", "false");
      }
    });
  }

  function initLanguageDropdown() {
    var btn = document.getElementById("k-lang-btn");
    var pop = document.getElementById("k-lang-popover");
    if (!btn || !pop) return;

    applyLocale(getActiveLang());

    btn.addEventListener("click", function (e) {
      e.stopPropagation();
      var accPop = document.getElementById("k-accent-popover");
      if (accPop) accPop.classList.remove("open");
      var open = pop.classList.toggle("open");
      btn.setAttribute("aria-expanded", open ? "true" : "false");
    });

    pop.addEventListener("click", function (e) {
      var opt = e.target.closest(".lang-option");
      if (!opt) return;
      var targetLang = opt.getAttribute("data-lang-val");
      var targetHref = opt.getAttribute("data-target-href");
      var isPaired = opt.getAttribute("data-paired") === "true";
      var pageLang = document.documentElement.getAttribute("data-page-lang") || "en";

      try { localStorage.setItem("k-docs-lang", targetLang); } catch (err) {}
      pop.classList.remove("open");
      btn.setAttribute("aria-expanded", "false");

      if (isPaired && targetLang !== pageLang && targetHref) {
        window.location.href = targetHref;
        return;
      }
      applyLocale(targetLang);
    });

    document.addEventListener("click", function (e) {
      if (!pop.contains(e.target) && e.target !== btn) {
        pop.classList.remove("open");
        btn.setAttribute("aria-expanded", "false");
      }
    });
  }

  function initMobileNav() {
    var btn = document.getElementById("k-mobile-nav");
    var backdrop = document.getElementById("k-aside-backdrop");
    if (!btn) return;

    function closeDrawer() {
      document.body.classList.remove("aside-open");
      btn.setAttribute("aria-expanded", "false");
    }

    btn.addEventListener("click", function () {
      var open = document.body.classList.toggle("aside-open");
      btn.setAttribute("aria-expanded", open ? "true" : "false");
    });

    if (backdrop) backdrop.addEventListener("click", closeDrawer);
    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape") closeDrawer();
    });
    document.querySelectorAll("aside.aside .sb-row").forEach(function (a) {
      a.addEventListener("click", closeDrawer);
    });
  }

  function copyText(text, btn, flashGuideMsg) {
    if (!navigator.clipboard) return;
    navigator.clipboard.writeText(text).then(function () {
      if (btn) {
        var span = btn.querySelector("span") || btn;
        var orig = isZh() ? (btn.getAttribute("data-zh") || span.textContent) : (btn.getAttribute("data-en") || span.textContent);
        btn.setAttribute("data-copied", "true");
        span.textContent = isZh() ? "已复制" : "Copied";
        clearTimeout(btn._copyTimer);
        btn._copyTimer = setTimeout(function () {
          btn.removeAttribute("data-copied");
          span.textContent = orig;
        }, 1450);
      }
      if (flashGuideMsg && window.__kShowGuideHint) {
        window.__kShowGuideHint(flashGuideMsg, 1800);
      }
    }).catch(function () {});
  }

  // ---------- Zero-Reflow 60fps KProductIntro ----------
  var K_EXTRUDE_VERTICES = [
    [146, 72], [178, 72], [196, 90], [196, 350], [178, 368], [146, 368], [128, 350], [128, 90],
    [214, 206], [342, 72], [424, 72], [292, 220],
    [284, 206], [432, 368], [344, 368], [214, 236],
    [248, 188], [280, 220], [248, 252], [216, 220],
    [76, 124], [76, 316], [476, 124], [476, 316]
  ];

  var activeIntroCancel = null;

  function playProductIntro(forceReplay) {
    var hero = document.querySelector("[data-k-intro-hero]");
    var curtain = document.querySelector("[data-k-product-intro]");
    if (!hero || !curtain) return false;

    var reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    if ((reduced || document.hidden) && !forceReplay) return false;

    if (!forceReplay) {
      if (window.scrollY > 12 || (window.location.hash && window.location.hash.length > 1)) {
        return false;
      }
      var key = curtain.getAttribute("data-intro-key") || "klang:product-intro:v1";
      var shouldPlay = false;
      try {
        var params = new URLSearchParams(window.location.search);
        if (params.get("intro") === "1") shouldPlay = true;
        var armed = sessionStorage.getItem("klang:product-intro:armed") === "1";
        if (armed) {
          sessionStorage.removeItem("klang:product-intro:armed");
          shouldPlay = true;
        }
        var ref = document.referrer || "";
        if (ref && ref.indexOf("/klang/") === -1 && ref.indexOf("README.zh-CN") === -1) {
          shouldPlay = true;
        }
        if (!shouldPlay && sessionStorage.getItem(key) !== "1") {
          shouldPlay = true;
        }
        if (!shouldPlay) return false;
        sessionStorage.setItem(key, "1");
      } catch (e) {}
    } else {
      window.scrollTo(0, 0);
    }

    if (activeIntroCancel) activeIntroCancel();

    var backdrop = curtain.querySelector(".k-intro-backdrop");
    var stage = curtain.querySelector(".k-intro-stage");
    var baseGroup = curtain.querySelector(".k-intro-extrude-base");
    var edgesGroup = curtain.querySelector(".k-intro-extrude-edges");
    var targetTitle = hero.querySelector("[data-k-intro-title]");
    var targetLead = hero.querySelector("[data-k-intro-lead]");
    if (!backdrop || !stage || !baseGroup || !edgesGroup || !targetTitle || !targetLead) return false;

    var EXTRUDE_DX = 26;
    var EXTRUDE_DY = -34;
    var DRAW_END = 720;
    var EXTRUDE_AT = 720;
    var EXTRUDE_DUR = 380;
    var HANDOFF_AT = 1060;
    var MOVE_DUR = 640;
    var REVEAL_DUR = 280;
    var HANDOFF_END = HANDOFF_AT + MOVE_DUR;
    var TOTAL_MS = HANDOFF_END + REVEAL_DUR;

    edgesGroup.innerHTML = "";
    edgesGroup.setAttribute("stroke-dasharray", "1");
    edgesGroup.setAttribute("stroke-dashoffset", "1");
    K_EXTRUDE_VERTICES.forEach(function (pt) {
      var line = document.createElementNS(SVG_NS, "line");
      line.setAttribute("x1", pt[0]);
      line.setAttribute("y1", pt[1]);
      line.setAttribute("x2", pt[0] + EXTRUDE_DX);
      line.setAttribute("y2", pt[1] + EXTRUDE_DY);
      line.setAttribute("pathLength", "1");
      edgesGroup.appendChild(line);
    });

    var guidePaths = Array.prototype.map.call(
      curtain.querySelectorAll(".k-intro-guides [data-k-trace]"),
      function (el) { el.setAttribute("style", "opacity:0;stroke-dashoffset:1"); return { el: el, settled: false }; }
    );
    var outlinePaths = Array.prototype.map.call(
      curtain.querySelectorAll(".k-intro-outline [data-k-trace]"),
      function (el) { el.setAttribute("style", "opacity:0;stroke-dashoffset:1"); return { el: el, settled: false }; }
    );

    hero.removeAttribute("data-k-intro-playing");
    hero.removeAttribute("data-k-intro-settled");
    var vw = window.innerWidth || 1440;
    var vh = window.innerHeight || 900;
    var titleRect = targetTitle.getBoundingClientRect();
    var leadRect = targetLead.getBoundingClientRect();
    var gap = Math.max(18, Math.min(72, leadRect.top - titleRect.bottom));
    var stageAnchor = Math.max(vh * 0.54, Math.min(vh * 0.74, vh - titleRect.height - leadRect.height - 56));
    var titleDeltaY = stageAnchor - titleRect.top;
    var leadDeltaY = stageAnchor + titleRect.height + Math.min(gap, 26) - leadRect.top;

    var floatTitle = targetTitle.cloneNode(true);
    floatTitle.removeAttribute("id");
    floatTitle.removeAttribute("data-k-intro-title");
    floatTitle.setAttribute("aria-hidden", "true");
    floatTitle.className = "k-intro-floater k-intro-floater--title";
    floatTitle.style.cssText =
      "display:flex;flex-wrap:wrap;justify-content:center;align-items:center;gap:0.22em 0.26em;" +
      "font-family:var(--font-display);font-size:" + getComputedStyle(targetTitle).fontSize + ";" +
      "font-weight:580;letter-spacing:-0.055em;line-height:1.14;" +
      "left:" + titleRect.left.toFixed(2) + "px;top:" + titleRect.top.toFixed(2) + "px;" +
      "width:" + titleRect.width.toFixed(2) + "px;height:" + titleRect.height.toFixed(2) + "px;opacity:0;";
    var floatPrefix = floatTitle.querySelector(".k-title-prefix");

    var floatLead = targetLead.cloneNode(true);
    floatLead.removeAttribute("id");
    floatLead.removeAttribute("data-k-intro-lead");
    floatLead.setAttribute("aria-hidden", "true");
    floatLead.className = "k-lead k-intro-floater k-intro-floater--lead";
    floatLead.style.cssText =
      "left:" + leadRect.left.toFixed(2) + "px;top:" + leadRect.top.toFixed(2) + "px;" +
      "width:" + leadRect.width.toFixed(2) + "px;height:" + leadRect.height.toFixed(2) + "px;opacity:0;";

    var topbar = document.querySelector("header.topbar");
    var topbarH = topbar ? Math.max(48, topbar.getBoundingClientRect().height) : 56;
    var dark = document.documentElement.getAttribute("data-theme") === "dark";

    hero.setAttribute("data-k-intro-playing", "");
    document.body.setAttribute("data-k-intro-active", "");
    curtain.hidden = false;
    curtain.style.opacity = "1";
    curtain.appendChild(floatTitle);
    curtain.appendChild(floatLead);

    var words = Array.prototype.map.call(
      curtain.querySelectorAll("[data-k-intro-word]"),
      function (el, idx) {
        el.style.removeProperty("transform");
        var r = el.getBoundingClientRect();
        var right = el.getAttribute("data-intro-direction") === "right";
        return {
          el: el,
          delay: idx * 65,
          startX: right ? (-r.right - 40) : (vw - r.left + 40),
          driftX: right ? (vw * 0.045) : (-vw * 0.045),
          exitX: right ? (vw - r.left + vw * 0.08) : (-r.right - vw * 0.08)
        };
      }
    );

    var done = false;
    var settled = false;
    var topbarCleared = false;
    var startTs = null;
    var framesRun = 0;
    var rafId = 0;

    function clearTopbar() {
      if (topbarCleared) return;
      topbarCleared = true;
      if (topbar) {
        topbar.style.removeProperty("opacity");
        topbar.style.removeProperty("transform");
        topbar.style.removeProperty("pointer-events");
      }
    }

    function finish() {
      if (done) return;
      done = true;
      if (rafId) cancelAnimationFrame(rafId);
      clearTimeout(safetyTimer);
      window.removeEventListener("scroll", onScroll);
      window.removeEventListener("resize", finish);
      document.removeEventListener("keydown", onKey);
      if (floatTitle.parentNode) floatTitle.parentNode.removeChild(floatTitle);
      if (floatLead.parentNode) floatLead.parentNode.removeChild(floatLead);
      curtain.hidden = true;
      curtain.style.removeProperty("opacity");
      hero.removeAttribute("data-k-intro-playing");
      if (framesRun > 2) {
        hero.setAttribute("data-k-intro-settled", "");
      }
      document.body.removeAttribute("data-k-intro-active");
      clearTopbar();
      activeIntroCancel = null;
      document.dispatchEvent(new CustomEvent("klang:intro-done"));
    }

    function onScroll() {
      if (window.scrollY > 24) finish();
    }
    function onKey(e) {
      if (e.key === "Escape" || e.key === "Tab") finish();
    }
    window.addEventListener("scroll", onScroll, { passive: true });
    window.addEventListener("resize", finish, { passive: true });
    document.addEventListener("keydown", onKey);
    var safetyTimer = setTimeout(finish, TOTAL_MS + 360);
    activeIntroCancel = finish;

    function tick(now) {
      if (done) return;
      if (startTs === null) startTs = now;
      framesRun++;
      var elapsed = Math.max(0, now - startTs);
      if (elapsed >= TOTAL_MS) {
        finish();
        return;
      }

      var guideFade = 1 - easeOutCubic((elapsed - EXTRUDE_AT) / 220);
      if (guideFade > 0.001 || elapsed < EXTRUDE_AT + 240) {
        for (var i = 0; i < guidePaths.length; i++) {
          var gp = guidePaths[i];
          if (gp.settled) continue;
          var gDelay = i * 14;
          if (elapsed < gDelay) continue;
          var p = easeOutCubic((elapsed - gDelay) / 640);
          var ga = easeOutCubic((elapsed - gDelay) / 90) * 0.9 * guideFade;
          gp.el.setAttribute("style", "opacity:" + ga.toFixed(3) + ";stroke-dashoffset:" + (1 - p).toFixed(4));
          if (guideFade <= 0.001) gp.settled = true;
        }
      }

      for (var j = 0; j < outlinePaths.length; j++) {
        var op = outlinePaths[j];
        if (op.settled) continue;
        var oDelay = Math.min(56, j * 22);
        if (elapsed < oDelay) continue;
        var opP = easeInOut((elapsed - oDelay) / (DRAW_END - oDelay));
        var opA = easeOutCubic((elapsed - oDelay) / 85);
        op.el.setAttribute("style", "opacity:" + opA.toFixed(3) + ";stroke-dashoffset:" + (1 - opP).toFixed(4));
        if (opP >= 0.999 && opA >= 0.999) op.settled = true;
      }

      for (var w = 0; w < words.length; w++) {
        var ws = words[w];
        var arriveEnd = ws.delay + 540;
        var tx;
        if (elapsed <= arriveEnd) {
          tx = ws.startX * (1 - easeOutQuart((elapsed - ws.delay) / 540));
        } else if (elapsed <= HANDOFF_AT) {
          tx = ws.driftX * clamp01((elapsed - arriveEnd) / Math.max(1, HANDOFF_AT - arriveEnd));
        } else {
          tx = ws.driftX + (ws.exitX - ws.driftX) * easeInCubic((elapsed - HANDOFF_AT) / 560);
        }
        ws.el.style.transform = "translate3d(" + tx.toFixed(2) + "px,0,0)";
      }

      var ext = easeOutCubic((elapsed - EXTRUDE_AT) / EXTRUDE_DUR);
      baseGroup.setAttribute(
        "style",
        "opacity:" + (ext * 0.92).toFixed(3) + ";transform:translate3d(" + (EXTRUDE_DX * ext).toFixed(2) + "px," + (EXTRUDE_DY * ext).toFixed(2) + "px,0)"
      );
      edgesGroup.setAttribute(
        "style",
        "opacity:" + (ext * 0.84).toFixed(3) + ";stroke-dashoffset:" + (1 - ext).toFixed(4)
      );

      var stageOut = easeOutCubic((elapsed - (HANDOFF_AT - 30)) / 340);
      var stageScale = 1 + ext * 0.032 - stageOut * 0.085;
      var stageY = -50 - stageOut * 12;
      stage.style.transform = "translate3d(-50%," + stageY.toFixed(2) + "%,0) scale(" + stageScale.toFixed(4) + ")";
      stage.style.opacity = (1 - stageOut).toFixed(3);

      var appear = easeOutCubic((elapsed - EXTRUDE_AT) / 260);
      var moveP = easeMove((elapsed - HANDOFF_AT) / MOVE_DUR);
      var colorP = easeInOut((elapsed - HANDOFF_AT) / MOVE_DUR);

      floatTitle.style.opacity = appear.toFixed(3);
      floatTitle.style.transform = "translate3d(0," + (titleDeltaY * (1 - moveP)).toFixed(2) + "px,0)";
      var toInk = dark ? 250 : 9;
      var titleRgb = "rgb(" + lerpU8(245, toInk, colorP) + "," + lerpU8(247, toInk, colorP) + "," + lerpU8(250, toInk + (dark ? 0 : 2), colorP) + ")";
      floatTitle.style.color = titleRgb;
      if (floatPrefix) floatPrefix.style.color = titleRgb;

      floatLead.style.opacity = appear.toFixed(3);
      floatLead.style.transform = "translate3d(0," + (leadDeltaY * (1 - moveP)).toFixed(2) + "px,0)";
      var lr = dark ? 161 : 82;
      floatLead.style.color = "rgb(" + lerpU8(216, lr, colorP) + "," + lerpU8(220, lr, colorP) + "," + lerpU8(228, lr + 8, colorP) + ")";

      var bgTo = dark ? 0 : 255;
      backdrop.style.backgroundColor = "rgb(" + lerpU8(23, bgTo, colorP) + "," + lerpU8(23, bgTo, colorP) + "," + lerpU8(23, bgTo, colorP) + ")";

      if (elapsed >= HANDOFF_AT + 540) {
        clearTopbar();
      } else if (topbar) {
        var barP = easeOutCubic((elapsed - HANDOFF_AT) / 540);
        topbar.style.opacity = barP.toFixed(3);
        topbar.style.transform = "translate3d(0," + (-(topbarH + 16) * (1 - barP)).toFixed(2) + "px,0)";
        topbar.style.pointerEvents = "none";
      }

      if (elapsed >= HANDOFF_END) {
        if (!settled) {
          settled = true;
          hero.removeAttribute("data-k-intro-playing");
          hero.setAttribute("data-k-intro-settled", "");
          document.body.removeAttribute("data-k-intro-active");
        }
        var cAlpha = 1 - easeInOut((elapsed - HANDOFF_END) / REVEAL_DUR);
        curtain.style.opacity = cAlpha.toFixed(3);
      }

      rafId = requestAnimationFrame(tick);
    }

    window.__kStepIntroAt = function (ms) {
      if (done) return false;
      if (rafId) {
        cancelAnimationFrame(rafId);
        rafId = 0;
      }
      clearTimeout(safetyTimer);
      startTs = 0;
      tick(ms);
      if (rafId) {
        cancelAnimationFrame(rafId);
        rafId = 0;
      }
      return true;
    };

    tick(window.performance ? performance.now() : 0);
    return true;
  }

  // ---------- Viewport-Gated Ambient Optics Canvas ----------
  function initOptics() {
    var canvas = document.querySelector("[data-k-optics]");
    var hero = document.querySelector(".k-cover");
    if (!canvas || !hero || !canvas.getContext) return;
    var ctx = canvas.getContext("2d");
    var inView = true;
    var paused = false;
    var raf = 0;
    var pointerX = 0.5;
    var pointerY = 0.42;

    function drawFrame(now) {
      raf = 0;
      if (!inView || paused || document.hidden || hero.hasAttribute("data-k-intro-playing")) return;
      var w = canvas.width;
      var h = canvas.height;
      var dark = document.documentElement.getAttribute("data-theme") === "dark";
      ctx.clearRect(0, 0, w, h);

      var t = now * 0.00055;
      ctx.lineWidth = 1;
      for (var i = 0; i < 5; i++) {
        var phase = t + i * 0.92;
        var yBase = h * (0.24 + i * 0.13) + (pointerY - 0.5) * 18;
        ctx.beginPath();
        for (var x = 0; x <= w; x += 24) {
          var u = x / w;
          var wave = Math.sin(u * 5.2 + phase) * 14 + Math.cos(u * 2.8 - phase * 0.7) * 9;
          var y = yBase + wave + (u - pointerX) * 8;
          if (x === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.strokeStyle = i % 2 === 0
          ? (dark ? "rgba(251, 113, 133, 0.16)" : "rgba(225, 29, 72, 0.12)")
          : (dark ? "rgba(129, 140, 248, 0.15)" : "rgba(79, 70, 229, 0.11)");
        ctx.stroke();
      }

      if (!window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
        raf = requestAnimationFrame(drawFrame);
      }
    }

    function kick() {
      if (!raf && inView && !paused && !document.hidden) {
        raf = requestAnimationFrame(drawFrame);
      }
    }

    hero.addEventListener("pointermove", function (e) {
      var r = hero.getBoundingClientRect();
      if (r.width > 0 && r.height > 0) {
        pointerX = (e.clientX - r.left) / r.width;
        pointerY = (e.clientY - r.top) / r.height;
      }
    }, { passive: true });

    if ("IntersectionObserver" in window) {
      var io = new IntersectionObserver(function (entries) {
        inView = entries[0] && entries[0].isIntersecting;
        if (inView) kick();
      }, { threshold: 0.05 });
      io.observe(hero);
    }

    var toggleBtn = document.getElementById("k-motion-toggle");
    if (toggleBtn) {
      toggleBtn.addEventListener("click", function () {
        paused = !paused;
        if (paused) hero.setAttribute("data-paused", "");
        else {
          hero.removeAttribute("data-paused");
          kick();
        }
      });
    }

    document.addEventListener("klang:intro-done", kick);
    document.addEventListener("visibilitychange", kick);
    kick();
  }

  // ---------- 3D Target Reel + Hero Command Dock ----------
  function initHeroReelAndCmds() {
    var reel = document.querySelector("[data-k-reel]");
    var tickets = document.querySelectorAll(".k-ticket");
    var segBtns = document.querySelectorAll("[data-set-target]");
    var thumb = document.getElementById("k-target-thumb");
    var cmdTabs = document.querySelectorAll(".k-cmd-tab");
    var cmdCode = document.getElementById("k-hero-cmd");
    var cmdCopy = document.getElementById("k-hero-cmd-copy");
    var agentCopy = document.getElementById("k-hero-agent-copy");
    var replayBtn = document.getElementById("k-intro-replay");

    var modes = ["native", "wasi", "jit"];
    var curIdx = 0;

    function setTargetMode(mode, syncCmd) {
      var idx = modes.indexOf(mode);
      if (idx === -1) idx = 0;
      curIdx = idx;

      tickets.forEach(function (tk) {
        var m = tk.getAttribute("data-target-mode");
        var mIdx = modes.indexOf(m);
        var rel = (mIdx - curIdx + 3) % 3;
        var slot = rel === 0 ? 0 : (rel === 1 ? 1 : -1);
        tk.setAttribute("data-slot", String(slot));
      });

      segBtns.forEach(function (b) {
        b.setAttribute("aria-pressed", b.getAttribute("data-set-target") === mode ? "true" : "false");
      });
      if (thumb) {
        thumb.style.transform = "translateX(" + (curIdx * 100) + "%)";
      }

      if (syncCmd && cmdTabs.length && cmdCode) {
        for (var i = 0; i < cmdTabs.length; i++) {
          if (cmdTabs[i].getAttribute("data-cmd-target") === mode) {
            cmdTabs.forEach(function (t) { t.setAttribute("aria-selected", "false"); });
            cmdTabs[i].setAttribute("aria-selected", "true");
            cmdCode.textContent = cmdTabs[i].getAttribute("data-cmd") || "";
            break;
          }
        }
      }
    }

    if (reel) {
      reel.addEventListener("click", function () {
        setTargetMode(modes[(curIdx + 1) % modes.length], true);
      });
      reel.addEventListener("keydown", function (e) {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          setTargetMode(modes[(curIdx + 1) % modes.length], true);
        }
      });
    }

    segBtns.forEach(function (btn) {
      btn.addEventListener("click", function () {
        setTargetMode(btn.getAttribute("data-set-target"), true);
      });
    });

    cmdTabs.forEach(function (tab) {
      tab.addEventListener("click", function () {
        cmdTabs.forEach(function (t) { t.setAttribute("aria-selected", "false"); });
        tab.setAttribute("aria-selected", "true");
        if (cmdCode) cmdCode.textContent = tab.getAttribute("data-cmd") || "";
        var tgt = tab.getAttribute("data-cmd-target");
        if (tgt) setTargetMode(tgt, false);
      });
    });

    if (cmdCopy && cmdCode) {
      cmdCopy.addEventListener("click", function () {
        copyText(
          cmdCode.textContent.trim(),
          cmdCopy,
          isZh() ? "终端命令已复制 · 可直接粘贴运行" : "Command copied · ready in terminal"
        );
      });
    }

    if (agentCopy) {
      agentCopy.addEventListener("click", function () {
        copyText(
          agentCopy.getAttribute("data-copy") || "",
          agentCopy,
          isZh() ? "Agent 提示词已复制 · 粘贴给编程助手即可" : "Agent prompt copied · paste into your coding agent"
        );
      });
    }

    if (replayBtn) {
      replayBtn.addEventListener("click", function () {
        playProductIntro(true);
      });
    }
  }

  // ---------- 01: Interactive Live Compiler & Data-Stack Studio ----------
  function initLiveStudio() {
    var shell = document.getElementById("k-studio-shell");
    if (!shell) return;

    // Studio top-level tabs
    var studioTabs = document.querySelectorAll("[data-studio-tab]");
    var panes = shell.querySelectorAll("[data-studio-pane]");
    studioTabs.forEach(function (btn) {
      btn.addEventListener("click", function () {
        var target = btn.getAttribute("data-studio-tab");
        studioTabs.forEach(function (b) { b.setAttribute("aria-pressed", b === btn ? "true" : "false"); });
        panes.forEach(function (p) { p.hidden = p.getAttribute("data-studio-pane") !== target; });
        syncPillBar(btn.closest("[data-k-pill-bar]"), true);
        stopStackAuto();
      });
    });

    // --- Applet 1: Data Stack O(1) Rewind ---
    var stepBtns = shell.querySelectorAll("[data-stack-step]");
    var segScope = document.getElementById("k-seg-scope");
    var segGrow = document.getElementById("k-seg-grow");
    var segGrowLbl = document.getElementById("k-seg-grow-lbl");
    var spCursor = document.getElementById("k-sp-cursor");
    var spReadout = document.getElementById("k-sp-readout");
    var lineTag = document.getElementById("k-stack-line-tag");
    var narr = document.getElementById("k-stack-narr");
    var heapStatus = document.getElementById("k-heap-status");
    var codeLines = shell.querySelectorAll("#k-stack-code .k-sim-line");
    var growBtn = document.getElementById("k-stack-grow");
    var nextBtn = document.getElementById("k-stack-next");
    var autoBtn = document.getElementById("k-stack-auto");

    var curStep = 0;
    var extraKb = 16;
    var autoTimer = null;

    function stopStackAuto() {
      if (autoTimer) {
        clearInterval(autoTimer);
        autoTimer = null;
      }
      if (autoBtn) autoBtn.setAttribute("aria-pressed", "false");
    }

    function renderStackStep(step) {
      curStep = step % 4;
      stepBtns.forEach(function (b) {
        b.setAttribute("aria-pressed", Number(b.getAttribute("data-stack-step")) === curStep ? "true" : "false");
      });
      codeLines.forEach(function (ln) {
        ln.classList.toggle("is-active", Number(ln.getAttribute("data-line")) === curStep);
      });

      var zh = isZh();
      var growPct = Math.min(44, (extraKb / 16) * 18);
      if (curStep === 0) {
        if (segScope) segScope.style.width = "0%";
        if (segGrow) segGrow.style.width = "0%";
        if (spCursor) spCursor.style.left = "16.5%";
        if (spReadout) spReadout.textContent = "SP = 0x02000 · 8 KB";
        if (lineTag) lineTag.textContent = "line 2 · frame #0";
        if (heapStatus) heapStatus.textContent = zh ? "根作用域就绪 · 0 次堆分配" : "Root frame ready · 0 heap allocs";
        if (narr) narr.textContent = zh
          ? "根帧 out 占用固定 8 KB 起始区；准备进入批处理循环作用域。"
          : "Root Summary occupies a fixed 8 KB base slot; ready to enter the request loop scope.";
      } else if (curStep === 1) {
        if (segScope) segScope.style.width = "34%";
        if (segGrow) segGrow.style.width = "0%";
        if (spCursor) spCursor.style.left = "51%";
        if (spReadout) spReadout.textContent = "SP = 0x08000 · 32 KB";
        if (lineTag) lineTag.textContent = "line 4 · scope #1";
        if (heapStatus) heapStatus.textContent = zh ? "传统堆：96 次 malloc · 缓存离散" : "Heap: 96 malloc calls · cache misses";
        if (narr) narr.textContent = zh
          ? "parse_arena 在连续数据栈上直接推进栈指针分配 24 KB，元素物理相邻、L1 缓存友好。"
          : "parse_arena bumps SP by 24 KB on the contiguous data stack — zero allocator locks, L1 cache hot.";
      } else if (curStep === 2) {
        if (segScope) segScope.style.width = "34%";
        if (segGrow) segGrow.style.width = growPct + "%";
        if (segGrowLbl) segGrowLbl.textContent = "+" + extraKb + "K grow";
        if (spCursor) spCursor.style.left = Math.min(96, 51.5 + growPct) + "%";
        var totalKb = 32 + extraKb;
        if (spReadout) spReadout.textContent = "SP = 0x" + (totalKb * 1024).toString(16).toUpperCase().padStart(5, "0") + " · " + totalKb + " KB";
        if (lineTag) lineTag.textContent = "line 5 · address-stable grow";
        if (heapStatus) heapStatus.textContent = zh ? "传统堆：realloc 搬移拷贝 + 悬垂风险" : "Heap: realloc copy + pointer invalidation";
        if (narr) narr.textContent = zh
          ? "变长数组在地址稳定数据栈上追加 +" + extraKb + " KB，既有受检引用地址绝不失效！"
          : "Growable slice appends +" + extraKb + " KB on the address-stable data stack without invalidating live references.";
      } else {
        if (segScope) segScope.style.width = "0%";
        if (segGrow) segGrow.style.width = "0%";
        if (spCursor) spCursor.style.left = "16.5%";
        if (spReadout) spReadout.textContent = "SP → 0x02000 · O(1) REWIND!";
        if (lineTag) lineTag.textContent = "line 7 · scope exit O(1)";
        if (heapStatus) heapStatus.textContent = zh ? "传统堆：142 次逐节点 free() 或 GC 停顿" : "Heap: 142 per-node free() calls or GC pause";
        if (narr) narr.textContent = zh
          ? "作用域右花括号退出：单条机器指令将 SP 瞬时回退至 0x02000，整块 " + (24 + extraKb) + " KB 瞬间归零释放！"
          : "Scope exit '}' rewinds SP to 0x02000 in ONE CPU instruction — instant " + (24 + extraKb) + " KB reclamation, zero GC!";
      }
    }

    stepBtns.forEach(function (b) {
      b.addEventListener("click", function () {
        stopStackAuto();
        renderStackStep(Number(b.getAttribute("data-stack-step")));
      });
    });

    if (nextBtn) {
      nextBtn.addEventListener("click", function () {
        stopStackAuto();
        renderStackStep((curStep + 1) % 4);
      });
    }

    if (growBtn) {
      growBtn.addEventListener("click", function () {
        stopStackAuto();
        extraKb = extraKb >= 48 ? 16 : extraKb + 16;
        renderStackStep(2);
      });
    }

    if (autoBtn) {
      autoBtn.addEventListener("click", function () {
        if (autoTimer) {
          stopStackAuto();
        } else {
          autoBtn.setAttribute("aria-pressed", "true");
          renderStackStep((curStep + 1) % 4);
          autoTimer = setInterval(function () {
            renderStackStep((curStep + 1) % 4);
          }, 1350);
        }
      });
    }
    renderStackStep(0);

    // --- Applet 2: Lifetime & Escape Checker ---
    var ltBtns = shell.querySelectorAll("[data-lt-case]");
    var ltCode = document.getElementById("k-lt-code");
    var ltOut = document.getElementById("k-lt-output");
    var ltGraph = document.getElementById("k-lt-graph");
    var ltBadge = document.getElementById("k-lt-badge");
    var ltStatus = document.getElementById("k-lt-status-tag");
    var ltFile = document.getElementById("k-lt-file");

    function renderLifetimeCase(kind) {
      var zh = isZh();
      ltBtns.forEach(function (b) {
        b.setAttribute("aria-pressed", b.getAttribute("data-lt-case") === kind ? "true" : "false");
      });
      if (kind === "safe") {
        if (ltFile) ltFile.textContent = "lifetime_commit.k";
        if (ltBadge) {
          ltBadge.className = "k-diag-pill k-diag-pill--ok";
          ltBadge.textContent = zh ? "k check · 通过 (1.2 ms)" : "k check · PASS (1.2 ms)";
        }
        if (ltStatus) ltStatus.textContent = "EXIT 0";
        if (ltCode) ltCode.innerHTML =
          '<span class="kw">fn</span> <span class="fn"> summarize_header</span>(raw: <span class="ty">u8</span>[]) -&gt; <span class="ty">u64</span> {\n' +
          '    <span class="kw">var</span> scratch: <span class="ty">u64</span>[] = decode_varints(raw); <span class="cm">// root: scope #1</span>\n' +
          '    <span class="kw">let</span> head_ref: &amp;<span class="ty">u64</span> = &amp;scratch[0];        <span class="cm">// borrow tied to scratch</span>\n' +
          '    <span class="kw">return</span> *head_ref + scratch.len();       <span class="cm">// ✓ copies value, ref dies here</span>\n' +
          '}';
        if (ltOut) ltOut.innerHTML =
          '<span class="diag-ok">✓ semantic check passed</span> (0 errors, 0 warnings)\n' +
          '  • root `scratch` allocated on local datastack frame #1\n' +
          '  • borrow `head_ref` consumed before scope exit rewind';
        if (ltGraph) ltGraph.innerHTML = zh
          ? "<strong>根溯源证明：</strong><code>head_ref</code> 绑定至内层数据栈根 <code>scratch</code>，返回值在栈回退前已解引用为纯值 <code>u64</code>，无需生命周期泛型标注即可安全放行。"
          : "<strong>Root Provenance:</strong> <code>head_ref</code> is rooted in local frame <code>scratch</code> and dereferenced to an owned <code>u64</code> before scope rewind — zero lifetime annotations required.";
      } else if (kind === "escape") {
        if (ltFile) ltFile.textContent = "escape_bug.k";
        if (ltBadge) {
          ltBadge.className = "k-diag-pill k-diag-pill--err";
          ltBadge.textContent = zh ? "k check · 拦截逃逸 (E0312)" : "k check · REJECTED (E0312)";
        }
        if (ltStatus) ltStatus.textContent = "EXIT 1";
        if (ltCode) ltCode.innerHTML =
          '<span class="kw">fn</span> <span class="fn">dangling_header</span>(raw: <span class="ty">u8</span>[]) -&gt; &amp;<span class="ty">u64</span> {\n' +
          '    <span class="kw">var</span> scratch: <span class="ty">u64</span>[] = decode_varints(raw); <span class="cm">// root: scope #1</span>\n' +
          '    <span class="kw">return</span> <span class="err-wave">&amp;scratch[0]</span>;                      <span class="cm">// × E0312: escapes scope #1!</span>\n' +
          '}';
        if (ltOut) ltOut.innerHTML =
          '<span class="diag-err">error[E0312]</span>: reference to data-stack root `scratch` cannot escape `dangling_header`\n' +
          '  --&gt; escape_bug.k:3:12\n' +
          '   | note: `scratch` rewinds at line 4 (`SP` reset); caller would hold dangling address';
        if (ltGraph) ltGraph.innerHTML = zh
          ? "<strong>编译期拦截：</strong>编译器沿引用根图发现返回值依赖即将回退的 <code>scratch</code> 数据栈帧，在生成 C 代码前直接阻断悬垂指针。"
          : "<strong>Compile-Time Block:</strong> Root analysis proves the returned address points into frame <code>scratch</code>, rejecting the dangling reference before codegen.";
      } else {
        if (ltFile) ltFile.textContent = "async_task_scope.k";
        if (ltBadge) {
          ltBadge.className = "k-diag-pill k-diag-pill--ok";
          ltBadge.textContent = zh ? "k check · 挂起帧已固定" : "k check · Task Frame Pinned";
        }
        if (ltStatus) ltStatus.textContent = "EXIT 0";
        if (ltCode) ltCode.innerHTML =
          '<span class="kw">async fn</span> <span class="fn">fetch_pipeline</span>(ch: &amp;<span class="ty">Stream</span>) -&gt; <span class="ty">usize</span> {\n' +
          '    <span class="kw">using</span> scope = TaskScope::open();         <span class="cm">// lexical structured scope</span>\n' +
          '    <span class="kw">let</span> msg = <span class="kw">await</span> ch.read_frame();         <span class="cm">// live locals pinned in task frame</span>\n' +
          '    <span class="kw">return</span> msg.payload.len();                <span class="cm">// deterministic cleanup on cancel</span>\n' +
          '}';
        if (ltOut) ltOut.innerHTML =
          '<span class="diag-ok">✓ async suspension &amp; lexical scope verified</span>\n' +
          '  • live state promoted to address-stable task frame across `await`\n' +
          '  • `using` guarantees cancellation unwind in reverse lexical order';
        if (ltGraph) ltGraph.innerHTML = zh
          ? "<strong>异步帧安全：</strong>跨越 <code>await</code> 存活的值由编译器固定在任务帧中，而瞬时临时缓冲在挂起前即在数据栈上回退，兼顾零泄漏与低内存。"
          : "<strong>Suspension Safety:</strong> Values surviving across <code>await</code> are pinned in the task frame while transient buffers rewind before suspension.";
      }
    }

    ltBtns.forEach(function (b) {
      b.addEventListener("click", function () { renderLifetimeCase(b.getAttribute("data-lt-case")); });
    });
    renderLifetimeCase("safe");

    // --- Applet 3: checkfree Bounds-Check Elimination ---
    var bceBtns = shell.querySelectorAll("[data-bce-mode]");
    var bceK = document.getElementById("k-bce-kcode");
    var bceAsm = document.getElementById("k-bce-asm");
    var bceBadge = document.getElementById("k-bce-badge");
    var bceInsn = document.getElementById("k-bce-insn-tag");

    function renderBceMode(mode) {
      var zh = isZh();
      bceBtns.forEach(function (b) {
        b.setAttribute("aria-pressed", b.getAttribute("data-bce-mode") === mode ? "true" : "false");
      });
      if (mode === "on") {
        if (bceBadge) {
          bceBadge.className = "k-diag-pill k-diag-pill--ok";
          bceBadge.textContent = zh ? "0 条越界分支 · CSR BFS 617ms (对比 Rust 811ms)" : "0 bounds traps · CSR BFS 617ms (vs Rust 811ms)";
        }
        if (bceInsn) bceInsn.textContent = "4 insns / edge · 0 traps";
        if (bceK) bceK.innerHTML =
          '<span class="cm">// 值域索引在构造期校验一次，遍历期编译期证明零越界</span>\n' +
          '<span class="kw">checkfree fn</span> <span class="fn">bfs_step</span>(\n' +
          '    row_off: (<span class="ty">u32</span> <span class="kw">upto</span> edges)[],\n' +
          '    edges:   (<span class="ty">u32</span> <span class="kw">in</span> dist)[],\n' +
          '    dist:    <span class="ty">i32</span>[], u: <span class="ty">u32</span> <span class="kw">in</span> dist\n' +
          ') {\n' +
          '    <span class="kw">for</span> e <span class="kw">in</span> row_off[u] .. row_off[u + 1] {\n' +
          '        <span class="kw">let</span> v = edges[e];      <span class="cm">// proven: v &lt; dist.len</span>\n' +
          '        <span class="kw">if</span> dist[v] &lt; 0 { dist[v] = dist[u] + 1; }\n' +
          '    }\n' +
          '}';
        if (bceAsm) bceAsm.innerHTML =
          '<span class="cm">; Emitted inner loop (Clang -O3 --lto, ARM64)</span>\n' +
          'LBB0_2:\n' +
          '  <span class="asm-fast">ldr   w11, [x1, x10, lsl #2]</span>   <span class="cm">; v = edges[e] (no bounds cmp!)</span>\n' +
          '  <span class="asm-fast">ldr   w12, [x2, x11, lsl #2]</span>   <span class="cm">; dist[v]      (proven in-range!)</span>\n' +
          '  <span class="asm-fast">tbz   w12, #31, LBB0_4</span>\n' +
          '  <span class="asm-fast">str   w13, [x2, x11, lsl #2]</span>   <span class="cm">; dist[v] = next_d</span>\n' +
          'LBB0_4:\n' +
          '  add   x10, x10, #1\n' +
          '  cmp   x10, x9\n' +
          '  b.lo  LBB0_2';
      } else {
        if (bceBadge) {
          bceBadge.className = "k-diag-pill k-diag-pill--err";
          bceBadge.textContent = zh ? "每次迭代残留 2 条越界比较分支 · 阻碍流水线" : "2 bounds traps per edge · stalls loop pipeline";
        }
        if (bceInsn) bceInsn.textContent = "8 insns + 2 panic branches";
        if (bceK) bceK.innerHTML =
          '<span class="cm">// 普通无约束 u32 索引：编译器必须保留运行时边界检查</span>\n' +
          '<span class="kw">fn</span> <span class="fn">bfs_unchecked_domain</span>(\n' +
          '    row_off: <span class="ty">u32</span>[], edges: <span class="ty">u32</span>[], dist: <span class="ty">i32</span>[], u: <span class="ty">u32</span>\n' +
          ') {\n' +
          '    <span class="kw">for</span> e <span class="kw">in</span> row_off[u] .. row_off[u + 1] {\n' +
          '        <span class="kw">let</span> v = edges[e];      <span class="cm">// runtime check: e &lt; edges.len</span>\n' +
          '        <span class="kw">if</span> dist[v] &lt; 0 {       <span class="cm">// runtime check: v &lt; dist.len</span>\n' +
          '            dist[v] = dist[u] + 1;\n' +
          '        }\n' +
          '    }\n' +
          '}';
        if (bceAsm) bceAsm.innerHTML =
          '<span class="cm">; Inner loop with unproved u32 indices</span>\n' +
          'LBB1_2:\n' +
          '  <span class="asm-trap">cmp   x10, x8</span>                  <span class="cm">; bounds check #1: e &gt;= edges.len?</span>\n' +
          '  <span class="asm-trap">b.hs  __k_bounds_panic</span>\n' +
          '  ldr   w11, [x1, x10, lsl #2]\n' +
          '  <span class="asm-trap">cmp   x11, x4</span>                  <span class="cm">; bounds check #2: v &gt;= dist.len?</span>\n' +
          '  <span class="asm-trap">b.hs  __k_bounds_panic</span>\n' +
          '  ldr   w12, [x2, x11, lsl #2]';
      }
    }

    bceBtns.forEach(function (b) {
      b.addEventListener("click", function () { renderBceMode(b.getAttribute("data-bce-mode")); });
    });
    renderBceMode("on");

    // --- Applet 4: Cordis Microkernel Service Graph ---
    var nodeBtns = shell.querySelectorAll("[data-node]");
    var cordisTitle = document.getElementById("k-cordis-title");
    var cordisAbi = document.getElementById("k-cordis-abi");
    var cordisDetail = document.getElementById("k-cordis-detail");
    var cordisLog = document.getElementById("k-cordis-log");
    var cordisGenBadge = document.getElementById("k-cordis-gen-badge");
    var onnxVer = document.getElementById("k-onnx-ver");
    var swapBtn = document.getElementById("k-cordis-swap");
    var faultBtn = document.getElementById("k-cordis-fault");

    var curGen = 1;
    var curNode = "onnx";

    var NODE_SPECS = {
      onnx: {
        abi: "cordis.inference.v1",
        zh: "可选 Linux 云端推理提供方。由监督进程限制并发队列与内存上限；热替换时在途请求钉住旧代际直至完成，零请求中断。",
        en: "Optional Linux cloud inference provider with bounded admission and supervised memory recovery. In-flight requests pin their active generation across hot-swaps."
      },
      tcp: {
        abi: "cordis.stream.tcp.v1",
        zh: "有界 TCP 流与背压队列提供方，由已验证的 cordis.lock 静态或动态接入应用宿主，绝不污染 K 编译器核心依赖。",
        en: "Bounded TCP ingress and queue-policy provider activated from verified cordis.lock contracts without bloating the core K runtime."
      },
      ts: {
        abi: "cordis.guest.quickjs.v1",
        zh: "独立 k-cordis-ts 运行器通过锁定前端与 QuickJS 服务直接执行本地 TypeScript ESM 与顶级 await。",
        en: "Standalone k-cordis-ts runner executing local TypeScript ESM with top-level await through locked QuickJS service contracts."
      },
      wasi: {
        abi: "wasi_snapshot_preview1",
        zh: "基于能力授权的 WebAssembly 沙箱宿主，仅向 .wasm 命令模块暴露预打开目录与受控时钟句柄。",
        en: "Capability-based WebAssembly host granting .wasm command modules strictly scoped preopened directories and clocks."
      }
    };

    function renderCordisNode(id, customLog) {
      curNode = id;
      nodeBtns.forEach(function (b) {
        b.classList.toggle("is-selected", b.getAttribute("data-node") === id);
      });
      var spec = NODE_SPECS[id] || NODE_SPECS.onnx;
      if (cordisTitle) cordisTitle.textContent = "Service Contract · " + id;
      if (cordisAbi) cordisAbi.textContent = spec.abi;
      if (cordisDetail) cordisDetail.textContent = isZh() ? spec.zh : spec.en;
      if (cordisLog) {
        cordisLog.innerHTML = customLog || (
          "[cordis] lock verified · sha256:9f4c…82a1 · active gen #" + curGen + "\n" +
          "[host]   in-flight requests pinned to gen #" + curGen + " · 0 dropped streams"
        );
      }
    }

    nodeBtns.forEach(function (b) {
      b.addEventListener("click", function () { renderCordisNode(b.getAttribute("data-node")); });
    });

    if (swapBtn) {
      swapBtn.addEventListener("click", function () {
        var prev = curGen;
        curGen = (curGen % 5) + 1;
        if (onnxVer) onnxVer.textContent = "provider · gen #" + curGen + " · pinned";
        if (cordisGenBadge) {
          cordisGenBadge.textContent = isZh()
            ? ("当前激活代际：gen #" + curGen + " (零停机切换完成)")
            : ("Active generation: gen #" + curGen + " (atomic cutover)");
        }
        renderCordisNode(
          "onnx",
          "[cordis] verifying candidate onnx-runtime-cpu@gen#" + curGen + " ABI & SHA-256... OK\n" +
          "[cordis] req #408..#419 drained on gen #" + prev + " · new traffic routed to gen #" + curGen
        );
      });
    }

    if (faultBtn) {
      faultBtn.addEventListener("click", function () {
        if (cordisGenBadge) {
          cordisGenBadge.textContent = isZh()
            ? ("异常已拦截：保留稳定代际 gen #" + curGen)
            : ("Fault blocked: preserved healthy gen #" + curGen);
        }
        renderCordisNode(
          "onnx",
          "[cordis] candidate gen #" + (curGen + 1) + " failed SHA-256 / health probe!\n" +
          "[cordis] ROLLBACK: kept active gen #" + curGen + " online · 0 failed requests"
        );
      });
    }
    renderCordisNode("onnx");
  }

  // ---------- 02: Interactive Benchmark Explorer ----------
  function initBenchExplorer() {
    var container = document.getElementById("k-bench-rows");
    if (!container) return;
    var metricBtns = document.querySelectorAll("[data-bench-metric]");

    var WORKLOADS = [
      { name: "records", subZh: "内联可变模式枚举", subEn: "Variable-mode inline enum", speed: 2.26, mem: 3.98 },
      { name: "respond-dto", subZh: "DTO 构造与序列化 (400K)", subEn: "DTO serialize (400K, M6)", speed: 2.04, mem: 1.85 },
      { name: "word-count", subZh: "词频统计 (400 万词)", subEn: "Token frequency (4M, M6)", speed: 1.84, mem: 1.42 },
      { name: "interp", subZh: "AST 表达式树求值", subEn: "AST tree interpreter", speed: 1.41, mem: 2.04 },
      { name: "sexp-owned", subZh: "S-表达式所有权树", subEn: "Owned S-expression tree", speed: 1.34, mem: 2.43 },
      { name: "graph-csr", subZh: "checkfree 无越界图遍历", subEn: "checkfree CSR BFS", speed: 1.32, mem: 1.18 },
      { name: "bintrees", subZh: "数据栈类型化引用树", subEn: "Data-stack binary trees", speed: 1.15, mem: 1.28 }
    ];

    function renderRows(metric) {
      var zh = isZh();
      var maxVal = metric === "mem" ? 4.0 : 2.5;
      var basePct = (1.0 / maxVal) * 100;
      container.innerHTML = WORKLOADS.map(function (w) {
        var val = metric === "mem" ? w.mem : w.speed;
        var pct = Math.min(100, (val / maxVal) * 100);
        return (
          '<div class="k-bench-row">' +
            '<div class="k-bench-name">' + w.name + '<small>' + (zh ? w.subZh : w.subEn) + '</small></div>' +
            '<div class="k-bench-track">' +
              '<div class="k-bench-baseline" style="left:' + basePct.toFixed(1) + '%" title="Safe Rust = 1.00x"></div>' +
              '<div class="k-bench-fill" style="width:' + pct.toFixed(1) + '%"></div>' +
            '</div>' +
            '<div class="k-bench-val">' + val.toFixed(2) + 'x</div>' +
          '</div>'
        );
      }).join("");
    }

    metricBtns.forEach(function (btn) {
      btn.addEventListener("click", function () {
        var m = btn.getAttribute("data-bench-metric");
        metricBtns.forEach(function (b) { b.setAttribute("aria-pressed", b === btn ? "true" : "false"); });
        syncPillBar(btn.closest("[data-k-pill-bar]"), true);
        renderRows(m);
      });
    });

    renderRows("speed");
  }

  // ---------- 04: Architecture Atlas & Lightbox ----------
  function openLightbox(src, title, svgUrl) {
    var modal = document.getElementById("k-lightbox");
    var img = document.getElementById("k-lightbox-img");
    var ttl = document.getElementById("k-lightbox-title");
    var openLink = document.getElementById("k-lightbox-open");
    if (!modal || !img) return;
    img.src = src;
    img.alt = title || "Diagram";
    if (ttl) ttl.textContent = title || (isZh() ? "Kense OS 架构图解" : "Kense OS Diagram");
    if (openLink) {
      if (svgUrl) {
        openLink.href = svgUrl;
        openLink.style.display = "";
      } else {
        openLink.style.display = "none";
      }
    }
    modal.classList.add("open");
  }

  function initAtlasAndLightbox() {
    var modal = document.getElementById("k-lightbox");
    var closeBtn = document.getElementById("k-lightbox-close");
    if (modal) {
      var close = function () { modal.classList.remove("open"); };
      if (closeBtn) closeBtn.addEventListener("click", close);
      modal.addEventListener("click", function (e) { if (e.target === modal) close(); });
      document.addEventListener("keydown", function (e) { if (e.key === "Escape") close(); });
    }

    var atlasTabs = document.querySelectorAll("[data-atlas-tab]");
    var atlasImg = document.getElementById("k-atlas-img");
    var atlasCap = document.getElementById("k-atlas-caption");
    var atlasSvg = document.getElementById("k-atlas-svg-link");
    var atlasZoom = document.getElementById("k-atlas-zoom");

    if (atlasTabs.length && atlasImg) {
      atlasTabs.forEach(function (tab) {
        tab.addEventListener("click", function () {
          atlasTabs.forEach(function (t) { t.setAttribute("aria-pressed", t === tab ? "true" : "false"); });
          syncPillBar(tab.closest("[data-k-pill-bar]"), true);
          var src = tab.getAttribute("data-src");
          if (src) {
            atlasImg.src = src;
            atlasImg.alt = tab.textContent;
            if (atlasSvg) atlasSvg.href = src;
          }
          if (atlasCap) atlasCap.textContent = tab.getAttribute("data-caption") || "";
        });
      });
      var zoomAtlas = function () {
        openLightbox(atlasImg.src, atlasImg.alt, atlasSvg ? atlasSvg.href : atlasImg.src);
      };
      atlasImg.addEventListener("click", zoomAtlas);
      if (atlasZoom) atlasZoom.addEventListener("click", zoomAtlas);
    }

    var activeLang = getActiveLang();
    document.querySelectorAll('.doc-body img[src*="diagrams/"]').forEach(function (img) {
      var parentLink = img.closest("a");
      var svgHref = parentLink ? parentLink.getAttribute("href") : img.getAttribute("src");
      var container = img.closest("p");
      if (container && !container.querySelector(".diagram-frame-bar")) {
        var bar = document.createElement("div");
        bar.className = "diagram-frame-bar";
        var desc = document.createElement("span");
        desc.setAttribute("data-en", "Kense OS Architecture Diagram");
        desc.setAttribute("data-zh", "Kense OS 架构图解");
        desc.textContent = activeLang === "zh" ? "Kense OS 架构图解" : "Kense OS Architecture Diagram";
        var acts = document.createElement("span");
        acts.className = "diagram-frame-actions";
        var zoomBtn = document.createElement("button");
        zoomBtn.type = "button";
        zoomBtn.className = "stage-chip";
        zoomBtn.setAttribute("data-en", "Inspect");
        zoomBtn.setAttribute("data-zh", "放大查看");
        zoomBtn.textContent = activeLang === "zh" ? "放大查看" : "Inspect";
        zoomBtn.addEventListener("click", function () {
          openLightbox(svgHref || img.src, img.alt || "Kense OS Diagram", svgHref);
        });
        acts.appendChild(zoomBtn);
        bar.appendChild(desc);
        bar.appendChild(acts);
        container.appendChild(bar);
      }
    });
  }

  // ---------- KUI Skewed Companion Guide Bot (#k-guide) ----------
  function initGuideBot() {
    var guide = document.getElementById("k-guide");
    var home = document.querySelector("[data-k-guide-home]");
    var trigger = document.getElementById("k-guide-trigger");
    var choices = document.getElementById("k-guide-choices");
    var speech = document.getElementById("k-guide-speech");
    var speechTitle = document.getElementById("k-speech-title");
    if (!guide || !home || !trigger || !choices || !speech || !speechTitle) return;

    var hintTimer = null;

    function placeBot() {
      var hr = home.getBoundingClientRect();
      var studio = document.getElementById("k-live-studio");
      var x = hr.left + hr.width * 0.5;
      var y = hr.top + hr.height * 0.85;

      if (window.scrollY > 340 && studio) {
        var sr = studio.getBoundingClientRect();
        if (sr.top < window.innerHeight * 0.78 && sr.bottom > 180) {
          x = Math.min(window.innerWidth - 72, sr.right - 42);
          y = Math.max(88, sr.top + 38);
        } else {
          x = window.innerWidth - 52;
          y = window.innerHeight - 36;
        }
      }

      x = Math.max(44, Math.min(window.innerWidth - 44, x));
      y = Math.max(76, Math.min(window.innerHeight - 24, y));
      guide.style.setProperty("--guide-x", x.toFixed(1) + "px");
      guide.style.setProperty("--guide-y", y.toFixed(1) + "px");
      guide.setAttribute("data-placed", "");
    }

    function showHint(text, autoHideMs) {
      if (guide.hasAttribute("data-open")) return;
      clearTimeout(hintTimer);
      speechTitle.textContent = text;
      speech.hidden = false;
      var w = Math.min(340, Math.max(160, text.length * 11 + 64));
      guide.style.setProperty("--shell-w", w + "px");
      guide.style.setProperty("--shell-h", "40px");
      guide.setAttribute("data-hint", "");
      if (autoHideMs) {
        hintTimer = setTimeout(clearHint, autoHideMs);
      }
    }

    function clearHint() {
      clearTimeout(hintTimer);
      if (guide.hasAttribute("data-open")) return;
      speech.hidden = true;
      guide.removeAttribute("data-hint");
      guide.style.removeProperty("--shell-w");
      guide.style.removeProperty("--shell-h");
    }

    window.__kShowGuideHint = showHint;

    function setOpen(open) {
      clearHint();
      if (open) {
        guide.setAttribute("data-open", "");
        choices.hidden = false;
        trigger.setAttribute("aria-expanded", "true");
        trigger.setAttribute("aria-label", trigger.getAttribute("data-close-label") || "Close");
      } else {
        guide.removeAttribute("data-open");
        choices.hidden = true;
        trigger.setAttribute("aria-expanded", "false");
        trigger.setAttribute("aria-label", trigger.getAttribute("data-open-label") || "Open");
      }
    }

    trigger.addEventListener("click", function (e) {
      e.stopPropagation();
      setOpen(!guide.hasAttribute("data-open"));
    });

    choices.addEventListener("click", function (e) {
      if (e.target.closest("a")) setOpen(false);
    });

    document.addEventListener("click", function (e) {
      if (guide.hasAttribute("data-open") && !guide.contains(e.target)) {
        setOpen(false);
      }
    });

    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape" && guide.hasAttribute("data-open")) {
        setOpen(false);
      }
    });

    document.querySelectorAll("[data-k-hint]").forEach(function (el) {
      el.addEventListener("mouseenter", function () {
        var h = el.getAttribute("data-k-hint");
        if (h) showHint(h, 0);
      });
      el.addEventListener("mouseleave", clearHint);
    });

    window.addEventListener("pointermove", function (e) {
      if (guide.hasAttribute("data-open")) return;
      var gx = parseFloat(guide.style.getPropertyValue("--guide-x")) || (window.innerWidth * 0.5);
      var gy = parseFloat(guide.style.getPropertyValue("--guide-y")) || 360;
      var dx = Math.max(-3.2, Math.min(3.2, (e.clientX - gx) / 140));
      var dy = Math.max(-2.2, Math.min(2.2, (e.clientY - gy) / 140));
      guide.style.setProperty("--look-x", dx.toFixed(2) + "px");
      guide.style.setProperty("--look-y", dy.toFixed(2) + "px");
    }, { passive: true });

    window.addEventListener("scroll", placeBot, { passive: true });
    window.addEventListener("resize", placeBot, { passive: true });
    placeBot();
  }

  // ---------- Code Blocks, Tables & Full-Spec Hash Auto-Open ----------
  function detectLang(el) {
    var cls = (el.className || "").split(/\s+/);
    for (var i = 0; i < cls.length; i++) {
      var c = cls[i];
      if (c && c !== "sourceCode" && c !== "numberLines") {
        return c === "klang" ? "k" : c;
      }
    }
    var code = el.querySelector("code");
    if (code && code.className) {
      var m = code.className.match(/language-([a-zA-Z0-9_+-]+)/);
      if (m) return m[1] === "klang" ? "k" : m[1];
    }
    return "code";
  }

  function enhanceCodeBlocks() {
    var body = document.querySelector(".doc-body");
    if (!body) return;
    var activeLang = getActiveLang();
    body.querySelectorAll("div.sourceCode, pre").forEach(function (block) {
      if (block.tagName === "PRE" && block.parentElement && block.parentElement.classList.contains("sourceCode")) return;
      if (block.parentElement && block.parentElement.classList.contains("kui-code-wrap")) return;

      var lang = detectLang(block);
      var wrap = document.createElement("div");
      wrap.className = "kui-code-wrap";

      var head = document.createElement("div");
      head.className = "kui-code-head";

      var label = document.createElement("span");
      label.className = "kui-code-lang";
      label.textContent = lang;

      var btn = document.createElement("button");
      btn.type = "button";
      btn.className = "kui-copy-btn";
      btn.setAttribute("data-en", "Copy");
      btn.setAttribute("data-zh", "复制");
      btn.textContent = activeLang === "zh" ? "复制" : "Copy";
      btn.addEventListener("click", function () {
        copyText(block.innerText.replace(/\n$/, ""), btn);
      });

      head.appendChild(label);
      head.appendChild(btn);
      block.parentNode.insertBefore(wrap, block);
      wrap.appendChild(head);
      wrap.appendChild(block);
    });
  }

  function enhanceTables() {
    document.querySelectorAll(".doc-body table").forEach(function (tbl) {
      if (tbl.parentElement && tbl.parentElement.classList.contains("table-wrap")) return;
      var wrap = document.createElement("div");
      wrap.className = "table-wrap";
      tbl.parentNode.insertBefore(wrap, tbl);
      wrap.appendChild(tbl);
    });
  }

  function initFullSpecHash() {
    var spec = document.getElementById("k-full-spec");
    if (!spec) return;
    function checkHash() {
      var h = window.location.hash;
      if (!h || h.length <= 1) return;
      var id = decodeURIComponent(h.slice(1));
      var target = document.getElementById(id) || document.querySelector('a[name="' + CSS.escape(id) + '"]');
      if (target && spec.contains(target)) {
        spec.open = true;
        setTimeout(function () {
          target.scrollIntoView({ behavior: "smooth", block: "start" });
        }, 40);
      }
    }
    window.addEventListener("hashchange", checkHash);
    checkHash();
  }

  function scrollActiveSidebar() {
    if (document.documentElement.getAttribute("data-layout") === "landing") return;
    var active = document.querySelector('aside.aside .sb-row[aria-current="page"]');
    if (active) active.scrollIntoView({ block: "nearest" });
  }

  function init() {
    initTheme();
    initAccent();
    initMobileNav();
    enhanceCodeBlocks();
    enhanceTables();
    initAtlasAndLightbox();
    initHeroReelAndCmds();
    initLiveStudio();
    initBenchExplorer();
    initGuideBot();
    initLanguageDropdown();
    initFullSpecHash();
    scrollActiveSidebar();
    syncAllPillBars(false);
    window.addEventListener("resize", function () { syncAllPillBars(false); }, { passive: true });
    playProductIntro(false);
    initOptics();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
