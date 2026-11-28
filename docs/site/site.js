// K Language Documentation — KUI UI Interactions
(function () {
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
      var swatches = pop.querySelectorAll(".accent-swatch");
      swatches.forEach(function (el) {
        el.setAttribute("aria-pressed", el.getAttribute("data-accent-val") === cur ? "true" : "false");
      });
    }
    syncActive();

    btn.addEventListener("click", function (e) {
      e.stopPropagation();
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

    var links = document.querySelectorAll("aside.aside .sb-row");
    links.forEach(function (a) {
      a.addEventListener("click", closeDrawer);
    });
  }

  function copyText(text, btn) {
    if (!navigator.clipboard) return;
    navigator.clipboard.writeText(text).then(function () {
      var orig = btn.getAttribute("data-orig-label") || btn.textContent;
      btn.setAttribute("data-orig-label", orig);
      btn.setAttribute("data-copied", "true");
      btn.textContent = "Copied";
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
    var blocks = body.querySelectorAll("div.sourceCode, pre");
    blocks.forEach(function (block) {
      if (block.tagName === "PRE" && block.parentElement && block.parentElement.classList.contains("sourceCode")) {
        return;
      }
      if (block.parentElement && block.parentElement.classList.contains("kui-code-wrap")) {
        return;
      }
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
      btn.textContent = "Copy";
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
    var tables = document.querySelectorAll(".doc-body table");
    tables.forEach(function (tbl) {
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
    if (ttl) ttl.textContent = title || "Kense OS Diagram";
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

    function close() {
      modal.classList.remove("open");
    }

    if (closeBtn) closeBtn.addEventListener("click", close);
    modal.addEventListener("click", function (e) {
      if (e.target === modal) close();
    });
    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape") close();
    });

    // Attach zoom action to diagram images inside article body
    var imgs = document.querySelectorAll('.doc-body img[src*="diagrams/"]');
    imgs.forEach(function (img) {
      var parentLink = img.closest("a");
      var svgHref = parentLink ? parentLink.getAttribute("href") : img.getAttribute("src");
      var container = img.closest("p");
      if (container && !container.querySelector(".diagram-frame-bar")) {
        var bar = document.createElement("div");
        bar.className = "diagram-frame-bar";

        var desc = document.createElement("span");
        desc.textContent = "Kense OS Architecture Diagram";

        var acts = document.createElement("span");
        acts.className = "diagram-frame-actions";

        var zoomBtn = document.createElement("button");
        zoomBtn.type = "button";
        zoomBtn.className = "stage-chip";
        zoomBtn.textContent = "Inspect";
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
    if (active) {
      active.scrollIntoView({ block: "nearest" });
    }
  }

  function init() {
    initTheme();
    initAccent();
    initMobileNav();
    enhanceCodeBlocks();
    enhanceTables();
    initLightbox();
    initHeroStage();
    scrollActiveSidebar();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
