// K Language Documentation — KUI UI & Bilingual i18n Interactions
(function () {
  function getActiveLang() {
    return document.documentElement.getAttribute("data-lang") || "en";
  }

  function applyLocale(lang) {
    document.documentElement.setAttribute("data-lang", lang);

    document.querySelectorAll("[data-en][data-zh]").forEach(function (el) {
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
  }

  function initTheme() {
    var btn = document.getElementById("k-theme-toggle");
    if (!btn) return;
    btn.addEventListener("click", function () {
      var cur = document.documentElement.getAttribute("data-theme") || "light";
      var next = cur === "dark" ? "light" : "dark";
      document.documentElement.setAttribute("data-theme", next);
      try { localStorage.setItem("k-docs-theme", next); } catch (e) {}
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
      try { localStorage.setItem("k-docs-accent", val); } catch (err) {}
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

    if (backdrop) {
      backdrop.addEventListener("click", closeDrawer);
    }

    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape") closeDrawer();
    });

    document.querySelectorAll("aside.aside .sb-row").forEach(function (a) {
      a.addEventListener("click", closeDrawer);
    });
  }

  function copyText(text, btn) {
    if (!navigator.clipboard) return;
    navigator.clipboard.writeText(text).then(function () {
      var lang = getActiveLang();
      var orig = lang === "zh" ? (btn.getAttribute("data-zh") || "复制") : (btn.getAttribute("data-en") || "Copy");
      btn.setAttribute("data-copied", "true");
      btn.textContent = lang === "zh" ? "已复制" : "Copied";
      clearTimeout(btn._copyTimer);
      btn._copyTimer = setTimeout(function () {
        btn.removeAttribute("data-copied");
        btn.textContent = orig;
      }, 1400);
    }).catch(function () {});
  }

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

  function openLightbox(src, title, svgUrl) {
    var modal = document.getElementById("k-lightbox");
    var img = document.getElementById("k-lightbox-img");
    var ttl = document.getElementById("k-lightbox-title");
    var openLink = document.getElementById("k-lightbox-open");
    if (!modal || !img) return;
    img.src = src;
    img.alt = title || "Diagram";
    if (ttl) ttl.textContent = title || (getActiveLang() === "zh" ? "Kense OS 架构图解" : "Kense OS Diagram");
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

  function initLightbox() {
    var modal = document.getElementById("k-lightbox");
    var closeBtn = document.getElementById("k-lightbox-close");
    if (!modal) return;

    function close() { modal.classList.remove("open"); }
    if (closeBtn) closeBtn.addEventListener("click", close);
    modal.addEventListener("click", function (e) { if (e.target === modal) close(); });
    document.addEventListener("keydown", function (e) { if (e.key === "Escape") close(); });

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

  function initHeroStage() {
    var tabs = document.querySelectorAll(".stage-tab");
    var stageImg = document.getElementById("k-stage-img");
    var stageCaption = document.getElementById("k-stage-caption-text");
    var stageSvgLink = document.getElementById("k-stage-open-svg");
    var expandBtn = document.getElementById("k-stage-expand");

    if (tabs.length && stageImg) {
      tabs.forEach(function (tab) {
        tab.addEventListener("click", function () {
          tabs.forEach(function (t) { t.setAttribute("aria-selected", "false"); });
          tab.setAttribute("aria-selected", "true");
          var src = tab.getAttribute("data-src");
          var cap = tab.getAttribute("data-caption");
          var alt = tab.getAttribute("data-alt");
          if (src) {
            stageImg.src = src;
            if (stageSvgLink) stageSvgLink.href = src;
          }
          if (alt) stageImg.alt = alt;
          if (stageCaption && cap) stageCaption.textContent = cap;
        });
      });

      var triggerZoom = function () {
        openLightbox(stageImg.src, stageImg.alt, stageSvgLink ? stageSvgLink.href : stageImg.src);
      };
      stageImg.addEventListener("click", triggerZoom);
      if (expandBtn) expandBtn.addEventListener("click", triggerZoom);
    }

    var osTabs = document.querySelectorAll(".install-os-tab");
    var cmdEl = document.getElementById("k-install-cmd");
    var copyInstallBtn = document.getElementById("k-install-copy");
    if (osTabs.length && cmdEl) {
      osTabs.forEach(function (tab) {
        tab.addEventListener("click", function () {
          osTabs.forEach(function (t) { t.setAttribute("aria-selected", "false"); });
          tab.setAttribute("aria-selected", "true");
          cmdEl.textContent = tab.getAttribute("data-cmd") || "";
        });
      });
    }
    if (copyInstallBtn && cmdEl) {
      copyInstallBtn.addEventListener("click", function () {
        copyText(cmdEl.textContent.trim(), copyInstallBtn);
      });
    }
  }

  function scrollActiveSidebar() {
    var active = document.querySelector('aside.aside .sb-row[aria-current="page"]');
    if (active) active.scrollIntoView({ block: "nearest" });
  }

  function init() {
    initTheme();
    initAccent();
    initMobileNav();
    enhanceCodeBlocks();
    enhanceTables();
    initLightbox();
    initHeroStage();
    initLanguageDropdown();
    scrollActiveSidebar();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
