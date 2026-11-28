// KUI Command Palette Bilingual Search for K docs — zero dependencies
(function () {
  var searchIndex = [];
  var loading = false;
  var currentResults = [];
  var selectedIndex = -1;

  var backdrop, input, resultsEl, countEl, openBtn, closeBtn;

  function activeLang() {
    return document.documentElement.getAttribute("data-lang") || "en";
  }

  function loadSearchIndex() {
    if (loading || !window.searchFile) return;
    loading = true;
    fetch(root + searchFile, { cache: "force-cache" })
      .then(function (r) { return r.json(); })
      .then(function (data) {
        data.forEach(function (d) { d.lower = d.content.toLowerCase(); });
        searchIndex = data;
        if (input && input.value.trim().length > 0) {
          runSearch(input.value);
        } else if (countEl && backdrop && backdrop.classList.contains("open")) {
          countEl.textContent = activeLang() === "zh"
            ? ("已索引 " + searchIndex.length + " 个章节")
            : (searchIndex.length + " sections indexed");
        }
      })
      .catch(function (err) {
        console.error("Failed to load search index:", err);
      });
  }

  function termScore(term, heading, text) {
    var score = 0;
    var h = heading.indexOf(term);
    if (h !== -1) score += (h === 0 ? 12 : 8);
    var t = text.indexOf(term);
    if (t === -1) return h === -1 ? -1 : score;
    score += 1;
    var before = t === 0 ? "" : text[t - 1];
    if (!/[a-z0-9_]/.test(before)) score += 2;
    var after = text[t + term.length] || "";
    if (!/[a-z0-9_]/.test(after)) score += 2;
    return score;
  }

  function scoreMatch(item, terms, uiLang) {
    var heading = (item.heading || item.title).toLowerCase();
    var text = item.lower;
    var score = 0;
    for (var i = 0; i < terms.length; i++) {
      var s = termScore(terms[i], heading, text);
      if (s < 0) return 0;
      score += s;
    }
    if (item.heading === "") score += 0.5;
    if (item.lang === uiLang) score += 5;
    return score;
  }

  function getSnippet(content, query) {
    var c = content.toLowerCase();
    var terms = query.toLowerCase().split(/\s+/).filter(Boolean);
    var idx = -1;
    for (var i = 0; i < terms.length; i++) {
      idx = c.indexOf(terms[i]);
      if (idx !== -1) break;
    }
    if (idx === -1) idx = 0;
    var start = Math.max(0, idx - 55);
    var end = Math.min(content.length, idx + 95);
    var snippet = content.substring(start, end);
    if (start > 0) snippet = "..." + snippet;
    if (end < content.length) snippet = snippet + "...";
    return snippet;
  }

  function escapeHtml(text) {
    var div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
  }

  function updateSelection() {
    var nodes = resultsEl.querySelectorAll(".kui-cmdk-item");
    nodes.forEach(function (node, idx) {
      node.classList.toggle("active", idx === selectedIndex);
    });
    if (selectedIndex >= 0 && nodes[selectedIndex]) {
      nodes[selectedIndex].scrollIntoView({ block: "nearest" });
    }
  }

  function runSearch(query) {
    if (!resultsEl) return;
    var uiLang = activeLang();
    var trimmed = (query || "").trim();
    var minLen = /[\u4e00-\u9fff]/.test(trimmed) ? 1 : 2;
    if (trimmed.length < minLen) {
      currentResults = [];
      selectedIndex = -1;
      resultsEl.innerHTML = '<div class="kui-cmdk-empty">' +
        (uiLang === "zh"
          ? "输入中文或英文关键词，搜索 K 语言文档、图解、规范与基准报告。"
          : "Type at least 2 characters to search K language docs, benchmarks, and design notes.") +
        "</div>";
      if (countEl) {
        countEl.textContent = searchIndex.length
          ? (uiLang === "zh" ? ("已索引 " + searchIndex.length + " 个章节") : (searchIndex.length + " sections indexed"))
          : "";
      }
      return;
    }

    var terms = trimmed.toLowerCase().split(/\s+/).filter(Boolean);
    currentResults = searchIndex
      .map(function (item) { return { item: item, score: scoreMatch(item, terms, uiLang) }; })
      .filter(function (r) { return r.score > 0; })
      .sort(function (a, b) { return b.score - a.score; })
      .slice(0, 18);

    selectedIndex = currentResults.length > 0 ? 0 : -1;
    if (countEl) {
      countEl.textContent = uiLang === "zh"
        ? (currentResults.length + " 条匹配")
        : (currentResults.length + " results");
    }

    if (currentResults.length === 0) {
      resultsEl.innerHTML = '<div class="kui-cmdk-empty">' +
        (uiLang === "zh" ? "未找到匹配的文档章节。" : "No matching documentation sections found.") +
        "</div>";
      return;
    }

    resultsEl.innerHTML = currentResults.map(function (r, idx) {
      var item = r.item;
      var snippet = getSnippet(item.content, trimmed);
      var title = item.heading ? item.heading : item.title;
      var page = item.heading ? item.title : item.url;
      var badge = item.lang === "zh" ? "中文" : "EN";
      return (
        '<a href="' + root + item.url + '" class="kui-cmdk-item' + (idx === 0 ? " active" : "") + '" data-index="' + idx + '">' +
        '<div class="kui-cmdk-item-title"><span>' + escapeHtml(title) + '</span><span class="kui-cmdk-item-page"><span class="kui-cmdk-lang-pill">' + badge + '</span> ' + escapeHtml(page) + "</span></div>" +
        (snippet ? '<div class="kui-cmdk-item-snippet">' + escapeHtml(snippet) + "</div>" : "") +
        "</a>"
      );
    }).join("");
  }

  function openModal() {
    if (!backdrop || !input) return;
    loadSearchIndex();
    backdrop.classList.add("open");
    runSearch(input.value);
    setTimeout(function () {
      input.focus();
      input.select();
    }, 10);
  }

  function closeModal() {
    if (!backdrop) return;
    backdrop.classList.remove("open");
  }

  function init() {
    backdrop = document.getElementById("k-cmdk-backdrop");
    input = document.getElementById("k-cmdk-input");
    resultsEl = document.getElementById("k-cmdk-results");
    countEl = document.getElementById("k-cmdk-count");
    openBtn = document.getElementById("k-search-open");
    closeBtn = document.getElementById("k-cmdk-close");
    if (!backdrop || !input || !resultsEl) return;

    if (openBtn) openBtn.addEventListener("click", openModal);
    if (closeBtn) closeBtn.addEventListener("click", closeModal);
    backdrop.addEventListener("click", function (e) {
      if (e.target === backdrop) closeModal();
    });

    input.addEventListener("input", function (e) {
      loadSearchIndex();
      runSearch(e.target.value);
    });

    input.addEventListener("keydown", function (e) {
      if (e.key === "Escape") { closeModal(); return; }
      if (e.key === "ArrowDown") {
        if (!currentResults.length) return;
        e.preventDefault();
        selectedIndex = (selectedIndex + 1) % currentResults.length;
        updateSelection();
      } else if (e.key === "ArrowUp") {
        if (!currentResults.length) return;
        e.preventDefault();
        selectedIndex = (selectedIndex - 1 + currentResults.length) % currentResults.length;
        updateSelection();
      } else if (e.key === "Enter") {
        if (selectedIndex >= 0 && currentResults[selectedIndex]) {
          e.preventDefault();
          window.location.href = root + currentResults[selectedIndex].item.url;
        }
      }
    });

    resultsEl.addEventListener("click", function (e) {
      if (e.target.closest(".kui-cmdk-item")) closeModal();
    });

    document.addEventListener("keydown", function (e) {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "k") {
        e.preventDefault();
        if (backdrop.classList.contains("open")) closeModal();
        else openModal();
        return;
      }
      if (e.target && (e.target.tagName === "INPUT" || e.target.tagName === "TEXTAREA" || e.target.isContentEditable)) {
        if (e.key === "Escape" && backdrop.classList.contains("open")) closeModal();
        return;
      }
      if (e.key === "/") {
        e.preventDefault();
        openModal();
      } else if (e.key === "Escape" && backdrop.classList.contains("open")) {
        closeModal();
      }
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
