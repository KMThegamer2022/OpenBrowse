// Host-side tests for the platform-independent half of the port
// (JSON, URL heuristics, settings, history). Build and run anywhere:
//
//   clang++ -std=c++17 -I../src tests_portable.cpp -o /tmp/ob_tests && /tmp/ob_tests
//
// The Windows/WebView2 layer is verified by the CI build on AppVeyor-equivalent
// runners, but everything tested here runs identically on both platforms.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "history.hpp"
#include "json.hpp"
#include "settings.hpp"
#include "url_utils.hpp"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool condition, const std::string& what) {
  ++gChecks;
  if (!condition) {
    ++gFailures;
    std::cout << "  FAIL  " << what << "\n";
  }
}

void checkEq(const std::string& actual, const std::string& expected, const std::string& what) {
  ++gChecks;
  if (actual != expected) {
    ++gFailures;
    std::cout << "  FAIL  " << what << "\n        expected: " << expected
              << "\n        actual:   " << actual << "\n";
  }
}

const std::string kHome = ob::kDefaultHomeUrl;
const std::string kSearch = ob::kDefaultSearchUrl;

void testUrlHeuristics() {
  std::cout << "url heuristics\n";

  checkEq(ob::ensureScheme("", kHome, kSearch), kHome, "empty input falls back to home");
  checkEq(ob::ensureScheme("   ", kHome, kSearch), kHome, "whitespace falls back to home");

  checkEq(ob::ensureScheme("openbrowse://settings", kHome, kSearch), "openbrowse://settings",
          "settings URL is passed through untouched");
  checkEq(ob::ensureScheme("OpenBrowse://Settings", kHome, kSearch), "OpenBrowse://Settings",
          "settings URL match is case-insensitive");

  checkEq(ob::ensureScheme("example.com", kHome, kSearch), "https://example.com",
          "bare host gains https");
  checkEq(ob::ensureScheme("sub.example.co.uk/page", kHome, kSearch),
          "https://sub.example.co.uk/page", "host with path gains https");
  checkEq(ob::ensureScheme("https://example.com", kHome, kSearch), "https://example.com",
          "explicit https preserved");
  checkEq(ob::ensureScheme("http://example.com", kHome, kSearch), "http://example.com",
          "explicit http preserved");
  checkEq(ob::ensureScheme("//example.com", kHome, kSearch), "https://example.com",
          "protocol relative gains https");

  // These are host:port pairs, not schemes.
  checkEq(ob::ensureScheme("localhost:3000", kHome, kSearch), "http://localhost:3000",
          "localhost with port is a URL");
  checkEq(ob::ensureScheme("example.com:8080", kHome, kSearch), "http://example.com:8080",
          "host with port is a URL, not a scheme");

  checkEq(ob::ensureScheme("hello world", kHome, kSearch),
          "https://duckduckgo.com/?q=hello+world", "text with spaces becomes a search");
  checkEq(ob::ensureScheme("what is c++", kHome, kSearch),
          "https://duckduckgo.com/?q=what+is+c%2B%2B", "punctuation is percent encoded");
  checkEq(ob::ensureScheme("hello world", kHome, "https://www.bing.com/search?q={query}"),
          "https://www.bing.com/search?q=hello+world", "configured provider is used");

  check(ob::isHomeUrl("https://landpg.kmtg.online", kHome), "home URL matches without slash");
  check(ob::isHomeUrl("https://landpg.kmtg.online/", kHome), "home URL matches with slash");
  check(!ob::isHomeUrl("https://example.com", kHome), "other URLs are not home");

  check(ob::isHttpOrHttps("https://a.com"), "https recognised");
  check(ob::isHttpOrHttps("HTTP://a.com"), "scheme check is case-insensitive");
  check(!ob::isHttpOrHttps("ftp://a.com"), "ftp is not http");

  checkEq(ob::normalizeHomeUrl(""), kHome, "blank homepage restores the default");
  checkEq(ob::normalizeHomeUrl("not a url"), kHome, "non-URL homepage restores the default");
  checkEq(ob::normalizeHomeUrl("example.com"), "https://example.com", "homepage normalised");

  checkEq(ob::baseName("C:\\Users\\me\\Downloads\\file.zip"), "file.zip", "windows base name");
  checkEq(ob::baseName("/home/me/file.zip"), "file.zip", "posix base name");
  checkEq(ob::dirName("C:\\Users\\me\\file.zip"), "C:\\Users\\me", "windows dir name");
}

void testJson() {
  std::cout << "json\n";

  ob::json::Value parsed = ob::json::parse(R"([{"url":"https://a.com","title":"A & B","n":3}])");
  check(parsed.isArray(), "parses an array");
  check(parsed.asArray().size() == 1, "array has one element");

  const ob::json::Value& item = parsed.asArray()[0];
  checkEq(item.str("url"), "https://a.com", "string member");
  checkEq(item.str("missing", "fallback"), "fallback", "missing member falls back");
  check(item.find("n") && item.find("n")->asNumber() == 3.0, "numeric member");

  // Round trip through serialization.
  std::string text = ob::json::serialize(parsed, 2);
  ob::json::Value again = ob::json::parse(text);
  checkEq(again.asArray()[0].str("title"), "A & B", "ampersand survives round trip");

  ob::json::Object obj;
  obj["escaped"] = std::string("quote\" backslash\\ newline\n tab\t");
  std::string encoded = ob::json::serialize(ob::json::Value(obj));
  ob::json::Value decoded = ob::json::parse(encoded);
  checkEq(decoded.str("escaped"), "quote\" backslash\\ newline\n tab\t",
          "control characters round trip");

  checkEq(ob::json::serialize(ob::json::parse(R"({"u":"café"})")).substr(0, 0) + "", "",
          "utf8 serialisation does not throw");

  // Malformed input must throw so callers can fall back.
  bool threw = false;
  try {
    ob::json::parse("{oops");
  } catch (const std::exception&) {
    threw = true;
  }
  check(threw, "malformed JSON throws");
}

void testHistory() {
  std::cout << "history\n";

  const std::string path = "/tmp/openbrowse_test_history.json";
  std::remove(path.c_str());

  ob::HistoryStore store(path);
  check(store.load().empty(), "missing file yields empty history");

  std::vector<ob::HistoryEntry> entries;
  ob::HistoryStore::record(&entries, "https://a.com", "First");
  ob::HistoryStore::record(&entries, "https://b.com", "Second");
  check(entries.size() == 2, "two entries recorded");
  checkEq(entries[0].url, "https://b.com", "newest entry is first");

  // Re-visiting the same URL updates in place rather than duplicating.
  ob::HistoryStore::record(&entries, "https://b.com", "Second, revisited");
  check(entries.size() == 2, "revisit does not duplicate");
  checkEq(entries[0].title, "Second, revisited", "revisit updates the title");

  // Non-http schemes and the settings URL are ignored.
  const size_t before = entries.size();
  ob::HistoryStore::record(&entries, "openbrowse://settings", "Settings");
  ob::HistoryStore::record(&entries, "about:blank", "Blank");
  ob::HistoryStore::record(&entries, "  ", "Blank");
  check(entries.size() == before, "non-http URLs are not recorded");

  store.save(entries);
  std::vector<ob::HistoryEntry> reloaded = store.load();
  check(reloaded.size() == 2, "history persists across save/load");
  checkEq(reloaded[0].title, "Second, revisited", "title persists");
  check(!reloaded[0].visitedAt.empty(), "timestamp recorded");

  // Cap at kHistoryLimit.
  std::vector<ob::HistoryEntry> many;
  for (size_t i = 0; i < ob::kHistoryLimit + 25; ++i) {
    ob::HistoryStore::record(&many, "https://site" + std::to_string(i) + ".com", "T");
  }
  check(many.size() == ob::kHistoryLimit, "history is capped at the limit");

  // Corrupt files degrade to empty rather than throwing.
  {
    std::ofstream bad(path);
    bad << "{not json at all";
  }
  check(store.load().empty(), "corrupt history file yields empty history");

  ob::HistoryStore::clear(&entries);
  store.save(entries);
  check(store.load().empty(), "clear empties the store");
  std::remove(path.c_str());
}

void testSettings() {
  std::cout << "settings\n";

  const std::string path = "/tmp/openbrowse_test_settings.ini";
  std::remove(path.c_str());

  ob::SettingsStore store(path);
  ob::AppSettings defaults = store.load();
  checkEq(defaults.homeUrl, kHome, "default homepage");
  checkEq(defaults.searchUrl, kSearch, "default search template");
  check(defaults.javascriptEnabled, "javascript on by default");
  check(defaults.smoothScrollingEnabled, "smooth scrolling on by default");
  check(defaults.hardwareAccelerationEnabled, "hardware acceleration on by default");

  ob::AppSettings custom;
  custom.homeUrl = "https://example.com";
  custom.searchUrl = "https://www.bing.com/search?q={query}";
  custom.javascriptEnabled = false;
  custom.smoothScrollingEnabled = false;
  custom.hardwareAccelerationEnabled = false;
  store.save(custom);

  ob::AppSettings loaded = store.load();
  checkEq(loaded.homeUrl, "https://example.com", "homepage round trips");
  checkEq(loaded.searchUrl, "https://www.bing.com/search?q={query}", "search template round trips");
  check(!loaded.javascriptEnabled, "javascript off round trips");
  check(!loaded.smoothScrollingEnabled, "smooth scrolling off round trips");
  check(!loaded.hardwareAccelerationEnabled, "hardware acceleration off round trips");

  // A template missing {query} must be rejected, as in the Python build.
  ob::AppSettings broken;
  broken.searchUrl = "https://www.bing.com/search";
  broken.normalizeSearchUrl();
  checkEq(broken.searchUrl, kSearch, "search template without {query} is rejected");

  check(ob::parseBoolText("YES", false), "YES parses as true");
  check(ob::parseBoolText("on", false), "on parses as true");
  check(!ob::parseBoolText("off", true), "off parses as false");
  check(ob::parseBoolText("garbage", true), "unparseable value falls back");

  std::remove(path.c_str());
}

}  // namespace

int main() {
  std::cout << "OpenBrowse portable-layer tests\n\n";
  testUrlHeuristics();
  testJson();
  testHistory();
  testSettings();

  std::cout << "\n" << (gChecks - gFailures) << "/" << gChecks << " checks passed\n";
  if (gFailures > 0) {
    std::cout << gFailures << " FAILURES\n";
    return 1;
  }
  std::cout << "All tests passed.\n";
  return 0;
}