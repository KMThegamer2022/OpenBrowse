// Preference storage. The Python build used QSettings; here the same keys are
// kept in a plain INI file so existing behaviour (and key names) carries over.
//
// Free of Windows headers so it can be unit tested anywhere.
#pragma once

#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

#include "url_utils.hpp"

namespace ob {

// Matches SettingsPage.settings_bool in the Python build.
inline bool parseBoolText(const std::string& raw, bool fallback) {
  const std::string v = toLower(trim(raw));
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  return fallback;
}

inline bool envFlagEnabled(const char* name) {
  const char* raw = std::getenv(name);
  if (!raw) return false;
  const std::string v = toLower(trim(raw));
  return v == "1" || v == "true" || v == "yes" || v == "on";
}

struct AppSettings {
  std::string homeUrl = kDefaultHomeUrl;
  std::string searchUrl = kDefaultSearchUrl;
  bool javascriptEnabled = true;
  bool smoothScrollingEnabled = true;
  bool hardwareAccelerationEnabled = true;

  // Mirrors SettingsPage.configured_search_url: reject a template that lost
  // its placeholder.
  void normalizeSearchUrl() {
    if (!isValidSearchTemplate(searchUrl)) searchUrl = kDefaultSearchUrl;
  }

  // Mirrors SettingsPage.configured_home_url.
  void normalizeHomeUrl() {
    homeUrl = trim(homeUrl);
    if (homeUrl.empty()) homeUrl = kDefaultHomeUrl;
  }
};

class SettingsStore {
 public:
  explicit SettingsStore(std::string path) : path_(std::move(path)) {}

  // Reads the file if present. Missing or malformed lines fall back to
  // defaults, matching the Python implementation's tolerance.
  AppSettings load() const {
    AppSettings settings;
    settings.homeUrl = kDefaultHomeUrl;
    settings.searchUrl = kDefaultSearchUrl;

    std::ifstream in(path_);
    if (!in) return settings;

    std::string line;
    std::string section;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const std::string trimmed = trim(line);
      if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;

      if (trimmed.front() == '[' && trimmed.back() == ']') {
        section = trim(trimmed.substr(1, trimmed.size() - 2));
        continue;
      }

      const size_t eq = trimmed.find('=');
      if (eq == std::string::npos) continue;

      const std::string name = trim(trimmed.substr(0, eq));
      const std::string value = trim(trimmed.substr(eq + 1));
      // QSettings stored these as "group/key"; keep the same names.
      const std::string key = section.empty() ? name : section + "/" + name;

      if (key == "browser/home_url") {
        settings.homeUrl = value;
      } else if (key == "browser/search_url") {
        settings.searchUrl = value;
      } else if (key == "browser/javascript_enabled") {
        settings.javascriptEnabled = parseBoolText(value, true);
      } else if (key == "browser/smooth_scrolling_enabled") {
        settings.smoothScrollingEnabled = parseBoolText(value, true);
      } else if (key == "graphics/hardware_acceleration") {
        settings.hardwareAccelerationEnabled = parseBoolText(value, true);
      }
    }

    settings.normalizeSearchUrl();
    settings.normalizeHomeUrl();
    return settings;
  }

  void save(const AppSettings& settings) const {
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;
    out << "[browser]\n";
    out << "home_url=" << settings.homeUrl << "\n";
    out << "search_url=" << settings.searchUrl << "\n";
    out << "javascript_enabled=" << (settings.javascriptEnabled ? "true" : "false") << "\n";
    out << "smooth_scrolling_enabled=" << (settings.smoothScrollingEnabled ? "true" : "false")
        << "\n";
    out << "\n[graphics]\n";
    out << "hardware_acceleration=" << (settings.hardwareAccelerationEnabled ? "true" : "false")
        << "\n";
  }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace ob