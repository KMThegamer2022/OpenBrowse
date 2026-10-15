// The openbrowse://settings page.
//
// Rendered with NavigateToString, so the markup lives only in memory and no
// .html or .js file is ever written to disk. That keeps the app clean under
// no-script execution policies, which block script files rather than browsing.
#pragma once

#include <string>
#include <vector>

#include "history.hpp"
#include "settings.hpp"
#include "url_utils.hpp"

namespace ob {

inline std::string escapeHtml(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

// Emits a JavaScript string literal. HTML escaping is wrong inside <script>,
// where entities are not decoded, so the value is JSON encoded, and any '<'
// is escaped so a stray "</script>" in the data cannot end the block early.
inline std::string jsStringLiteral(const std::string& text) {
  const std::string encoded = json::serialize(json::Value(text));
  std::string out;
  out.reserve(encoded.size());
  for (const char c : encoded) {
    if (c == '<') {
      out += "\\u003c";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// Message payloads posted from the settings page to the host.
namespace msg {
inline constexpr char kSave[] = "save";
inline constexpr char kClearHistory[] = "clear_history";
inline constexpr char kClearCookies[] = "clear_cookies";
inline constexpr char kClearCache[] = "clear_cache";
inline constexpr char kClearAll[] = "clear_all";
inline constexpr char kRefreshHistory[] = "refresh_history";
inline constexpr char kOpenHistory[] = "open_history";

// Host to page.
inline constexpr char kStatus[] = "status";
inline constexpr char kHistory[] = "history";
}  // namespace msg

inline std::string providerOptions(const std::string& selected) {
  std::string out;
  for (const SearchProvider& provider : kSearchProviders) {
    const bool isSelected = selected == provider.url_template;
    out += "<option value=\"";
    out += escapeHtml(provider.url_template);
    out += "\"";
    if (isSelected) out += " selected";
    out += ">";
    out += escapeHtml(provider.label);
    out += "</option>";
  }
  return out;
}

inline std::string historyRows(const std::vector<HistoryEntry>& entries) {
  if (entries.empty()) {
    return "<p class=\"empty\">No saved history yet.</p>";
  }
  std::string out;
  out.reserve(entries.size() * 200);
  for (const HistoryEntry& entry : entries) {
    out += "<li class=\"entry\" data-url=\"";
    out += escapeHtml(entry.url);
    out += "\"><div class=\"title\">";
    out += escapeHtml(entry.title);
    out += "</div><div class=\"url\">";
    out += escapeHtml(entry.url);
    out += "</div>";
    if (!entry.visitedAt.empty()) {
      out += "<div class=\"when\">Visited: ";
      out += escapeHtml(entry.visitedAt);
      out += "</div>";
    }
    out += "</li>";
  }
  return out;
}

inline std::string settingsHtml(const AppSettings& settings,
                                const std::vector<HistoryEntry>& history) {
  std::string html;
  html.reserve(16384);
  html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline';">
<title>OpenBrowse Settings</title>
<style>
  :root { color-scheme: light dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 32px;
    font: 14px/1.5 "Segoe UI", system-ui, sans-serif;
    background: #f6f7f9; color: #1b1f24;
  }
  h1 { font-size: 22px; margin: 0 0 4px; }
  h2 { font-size: 15px; margin: 0 0 14px; color: #57606a; font-weight: 600; }
  .intro { color: #57606a; margin: 0 0 28px; max-width: 60ch; }
  section {
    background: #fff; border: 1px solid #d8dee4; border-radius: 8px;
    padding: 20px 22px; margin-bottom: 20px; max-width: 760px;
  }
  .row { display: flex; align-items: center; gap: 12px; margin-bottom: 14px; }
  .row label { width: 140px; flex: none; color: #24292f; }
  input[type=text], select {
    flex: 1; padding: 7px 10px; font: inherit;
    border: 1px solid #d0d7de; border-radius: 6px; background: #fff; color: inherit;
  }
  input[type=text]:focus, select:focus { outline: 2px solid #0969da; outline-offset: -1px; }
  .check { display: flex; align-items: center; gap: 9px; margin-bottom: 11px; }
  .check span { margin-left: 0; width: auto; }
  .hint { color: #57606a; font-size: 12.5px; margin: 2px 0 16px 152px; max-width: 52ch; }
  button {
    font: inherit; padding: 7px 15px; margin-right: 8px; cursor: pointer;
    border: 1px solid #d0d7de; border-radius: 6px; background: #f6f8fa; color: #24292f;
  }
  button:hover { background: #eef1f4; }
  button.primary { background: #1f883d; border-color: #1a7f37; color: #fff; }
  button.primary:hover { background: #1a7f37; }
  button.danger { border-color: #cf222e; color: #cf222e; }
  button.danger:hover { background: #fff5f5; }
  ul { list-style: none; margin: 0; padding: 0; max-height: 340px; overflow-y: auto; }
  .entry { padding: 9px 11px; border: 1px solid #eaeef2; border-radius: 6px; margin-bottom: 7px; cursor: pointer; }
  .entry:hover { background: #f6f8fa; border-color: #d0d7de; }
  .title { font-weight: 600; }
  .url, .when { color: #57606a; font-size: 12.5px; word-break: break-all; }
  .empty { color: #57606a; font-style: italic; }
  #status { min-height: 20px; font-weight: 600; }
  #status.ok { color: #1a7f37; }
  #status.warn { color: #9a6700; }
  #status.error { color: #cf222e; }
  @media (prefers-color-scheme: dark) {
    body { background: #0d1117; color: #e6edf3; }
    .intro, .url, .when, .empty { color: #9198a1; }
    section { background: #161b22; border-color: #30363d; }
    .row label { color: #e6edf3; }
    input[type=text], select { background: #0d1117; border-color: #30363d; color: #e6edf3; }
    button { background: #21262d; border-color: #30363d; color: #e6edf3; }
    button:hover { background: #30363d; }
    button.primary { background: #238636; border-color: #2ea043; color: #fff; }
    button.danger { border-color: #f85149; color: #f85149; }
    button.danger:hover { background: #2d1214; }
    .entry:hover { background: #21262d; border-color: #30363d; }
    #status.ok { color: #3fb950; }
    #status.warn { color: #d29922; }
    #status.error { color: #f85149; }
  }
</style>
</head>
<body>
<h1>Settings</h1>
<p class="intro">Manage browser behavior, browsing data, and performance.</p>

<section>
  <h2>Preferences</h2>
  <div class="row"><label for="home">Homepage</label>
    <input type="text" id="home" spellcheck="false"></div>
  <div class="row"><label for="engine">Search engine</label>
    <select id="engine">)HTML";

  html += providerOptions(settings.searchUrl);

  html += R"HTML(</select></div>
  <label class="check"><input type="checkbox" id="js"><span>Enable JavaScript</span></label>
  <label class="check"><input type="checkbox" id="smooth"><span>Enable smooth scrolling</span></label>
  <label class="check"><input type="checkbox" id="gpu"><span>Use hardware acceleration when available</span></label>
  <p class="hint">JavaScript, smooth scrolling, and graphics changes apply the next time OpenBrowse starts.</p>
  <button class="primary" id="save">Save Settings</button>
</section>

<section>
  <h2>Browsing Data</h2>
  <button id="clear-history">Clear History</button>
  <button id="clear-cookies">Clear Cookies</button>
  <button id="clear-cache">Clear Cache</button>
  <button class="danger" id="clear-all">Clear All</button>
</section>

<section>
  <h2>Recent History</h2>
  <ul id="history">)HTML";

  html += historyRows(history);

  html += R"HTML(</ul>
  <button id="refresh" style="margin-top:14px">Refresh History</button>
</section>

<p id="status"></p>

<script>
(function () {
  var status = document.getElementById('status');

  function send(payload) {
    window.chrome.webview.postMessage(JSON.stringify(payload));
  }

  function show(text, kind) {
    status.textContent = text || '';
    status.className = kind || '';
  }

  document.getElementById('home').value = )HTML";

  html += jsStringLiteral(settings.homeUrl);

  html += R"HTML(;
  document.getElementById('js').checked = )HTML";

  html += settings.javascriptEnabled ? "true" : "false";

  html += R"HTML(;
  document.getElementById('smooth').checked = )HTML";

  html += settings.smoothScrollingEnabled ? "true" : "false";

  html += R"HTML(;
  document.getElementById('gpu').checked = )HTML";

  html += settings.hardwareAccelerationEnabled ? "true" : "false";

  html += R"HTML(;

  document.getElementById('save').addEventListener('click', function () {
    send({
      action: 'save',
      home_url: document.getElementById('home').value,
      search_url: document.getElementById('engine').value,
      javascript: document.getElementById('js').checked,
      smooth_scrolling: document.getElementById('smooth').checked,
      hardware_acceleration: document.getElementById('gpu').checked
    });
  });

  function bind(id, action) {
    document.getElementById(id).addEventListener('click', function () {
      send({ action: action });
    });
  }
  bind('clear-history', 'clear_history');
  bind('clear-cookies', 'clear_cookies');
  bind('clear-cache', 'clear_cache');
  bind('clear-all', 'clear_all');
  bind('refresh', 'refresh_history');

  document.getElementById('history').addEventListener('click', function (event) {
    var item = event.target.closest('.entry');
    if (item) send({ action: 'open_history', url: item.getAttribute('data-url') });
  });

  // Host replies with status text and refreshed history rows.
  window.chrome.webview.addEventListener('message', function (event) {
    var data = event.data;
    if (typeof data !== 'string') return;
    var message = JSON.parse(data);
    if (message.kind === 'status') {
      show(message.text, message.level);
    } else if (message.kind === 'history') {
      document.getElementById('history').innerHTML = message.rows;
    } else if (message.kind === 'reload') {
      show('', '');
    }
  });
})();
</script>
</body>
</html>
)HTML";

  return html;
}

}  // namespace ob