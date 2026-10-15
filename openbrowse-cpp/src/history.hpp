// Browsing history backed by history.json, matching the Python build's
// on-disk shape so existing history files keep working.
//
// Free of Windows headers so it can be unit tested anywhere.
#pragma once

#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "json.hpp"
#include "url_utils.hpp"

namespace ob {

struct HistoryEntry {
  std::string url;
  std::string title;
  std::string visitedAt;  // "YYYY-MM-DD HH:MM:SS"
};

inline std::string timestampNow() {
  std::time_t now = std::time(nullptr);
  std::tm tmValue{};
#if defined(_WIN32)
  localtime_s(&tmValue, &now);
#else
  localtime_r(&now, &tmValue);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmValue);
  return buffer;
}

class HistoryStore {
 public:
  explicit HistoryStore(std::string path) : path_(std::move(path)) {}

  // Ports load_history_entries: bad JSON or unexpected shapes yield an empty
  // list rather than an error.
  std::vector<HistoryEntry> load() const {
    std::vector<HistoryEntry> entries;
    std::ifstream in(path_);
    if (!in) return entries;

    std::stringstream buffer;
    buffer << in.rdbuf();

    json::Value root;
    try {
      root = json::parse(buffer.str());
    } catch (const std::exception&) {
      return entries;
    }
    if (!root.isArray()) return entries;

    for (const json::Value& item : root.asArray()) {
      if (!item.isObject()) continue;
      HistoryEntry entry;
      entry.url = trim(item.str("url"));
      entry.title = trim(item.str("title"));
      entry.visitedAt = trim(item.str("visited_at"));
      if (entry.url.empty()) continue;
      if (entry.title.empty()) entry.title = entry.url;
      entries.push_back(std::move(entry));
      if (entries.size() >= kHistoryLimit) break;
    }
    return entries;
  }

  void save(const std::vector<HistoryEntry>& entries) const {
    json::Array array;
    const size_t count = entries.size() < kHistoryLimit ? entries.size() : kHistoryLimit;
    for (size_t i = 0; i < count; ++i) {
      json::Object item;
      item["url"] = entries[i].url;
      item["title"] = entries[i].title;
      item["visited_at"] = entries[i].visitedAt;
      array.push_back(json::Value(std::move(item)));
    }

    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    out << json::serialize(json::Value(std::move(array)), 2) << "\n";
  }

  // Ports BrowserWindow.record_history: newest first, de-duplicated on the
  // leading URL, truncated to kHistoryLimit.
  static void record(std::vector<HistoryEntry>* entries, std::string url, std::string title) {
    url = trim(std::move(url));
    if (url.empty() || url == kSettingsUrl) return;
    if (!isHttpOrHttps(url)) return;

    title = trim(std::move(title));
    if (title.empty()) title = url;
    const std::string visitedAt = timestampNow();

    if (!entries->empty() && entries->front().url == url) {
      entries->front().title = title;
      entries->front().visitedAt = visitedAt;
    } else {
      entries->insert(entries->begin(), HistoryEntry{url, title, visitedAt});
    }

    if (entries->size() > kHistoryLimit) entries->resize(kHistoryLimit);
  }

  static void clear(std::vector<HistoryEntry>* entries) { entries->clear(); }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace ob