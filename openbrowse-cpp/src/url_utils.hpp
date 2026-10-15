// URL helpers that mirror the address-bar heuristics of the original
// Python build. Free of Windows headers so it can be unit tested anywhere.
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ob {

inline constexpr char kAppName[] = "OpenBrowse";
inline constexpr char kSettingsUrl[] = "openbrowse://settings";
inline constexpr char kDefaultSearchUrl[] = "https://duckduckgo.com/?q={query}";
inline constexpr char kDefaultHomeUrl[] = "https://landpg.kmtg.online";
inline constexpr size_t kHistoryLimit = 100;

struct SearchProvider {
  const char* label;
  const char* url_template;  // must contain "{query}"
};

inline constexpr SearchProvider kSearchProviders[] = {
    {"DuckDuckGo", "https://duckduckgo.com/?q={query}"},
    {"Google", "https://www.google.com/search?q={query}"},
    {"Bing", "https://www.bing.com/search?q={query}"},
    {"Startpage", "https://www.startpage.com/search?q={query}"},
};

inline std::string trim(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
  while (begin < end && isSpace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && isSpace(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(begin, end - begin);
}

inline std::string toLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

// Equivalent of urllib.parse.quote_plus.
inline std::string quotePlus(const std::string& text) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

// Substitutes the single "{query}" placeholder. Returns input unchanged when
// the template lacks the placeholder.
inline std::string applySearchTemplate(const std::string& tmpl, const std::string& query) {
  const std::string token = "{query}";
  size_t at = tmpl.find(token);
  if (at == std::string::npos) return tmpl;
  return tmpl.substr(0, at) + quotePlus(query) + tmpl.substr(at + token.size());
}

inline bool isValidSearchTemplate(const std::string& tmpl) {
  return tmpl.find("{query}") != std::string::npos;
}

// True when the text starts with `scheme:` using RFC 3986 scheme syntax.
inline bool hasSchemePrefix(const std::string& text) {
  size_t i = 0;
  if (i >= text.size() || !std::isalpha(static_cast<unsigned char>(text[0]))) return false;
  ++i;
  while (i < text.size()) {
    const char c = text[i];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.') {
      ++i;
      continue;
    }
    break;
  }
  return i < text.size() && text[i] == ':';
}

// Detects "host:port" text such as "localhost:3000" or "example.com:8080",
// which is syntactically indistinguishable from a scheme but is not one.
inline bool looksLikeHostPort(const std::string& text) {
  const size_t colon = text.find(':');
  if (colon == std::string::npos || colon == 0) return false;
  const std::string authority = text.substr(0, colon);
  return authority == "localhost" || authority.find('.') != std::string::npos;
}

// Schemes that are legitimately not followed by "//".
inline bool isOpaqueScheme(const std::string& scheme) {
  static const char* kOpaque[] = {"mailto", "javascript", "data", "blob",
                                  "about",   "view-source", "openbrowse"};
  for (const char* candidate : kOpaque) {
    if (scheme == candidate) return true;
  }
  return false;
}

// True when `text` genuinely begins with a URL scheme, as opposed to a host
// and port such as "example.com:8080" or "localhost:3000".
inline bool startsWithUrlScheme(const std::string& text) {
  if (!hasSchemePrefix(text)) return false;
  const size_t colon = text.find(':');
  const std::string scheme = toLower(text.substr(0, colon));
  const std::string rest = text.substr(colon + 1);
  if (rest.compare(0, 2, "//") == 0) return true;
  return isOpaqueScheme(scheme);
}

// True when the text should be treated as a location rather than a search.
inline bool looksLikeUrl(const std::string& text) {
  if (text.empty()) return false;
  if (text.find(' ') != std::string::npos || text.find('\t') != std::string::npos) return false;
  if (startsWithUrlScheme(text)) return true;
  if (text.compare(0, 2, "//") == 0) return true;
  if (!hasSchemePrefix(text) && text.find('.') != std::string::npos) return true;
  if (hasSchemePrefix(text) && looksLikeHostPort(text)) return true;
  return false;
}

// Turns navigable text into something WebView2 will accept, adding a scheme
// when the user omitted one.
inline std::string toNavigableUrl(const std::string& text) {
  if (text.compare(0, 2, "//") == 0) return "https:" + text;
  if (startsWithUrlScheme(text)) return text;
  // "example.com:8080" and "localhost:3000" are host:port pairs.
  if (hasSchemePrefix(text)) return "http://" + text;
  return "https://" + text;
}

// Ports SettingsPage.ensure_scheme from the Python implementation.
inline std::string ensureScheme(const std::string& rawText, const std::string& homeUrl,
                                const std::string& searchUrl) {
  const std::string text = trim(rawText);
  if (text.empty()) return homeUrl;
  if (toLower(text) == kSettingsUrl) return text;

  if (looksLikeUrl(text)) return toNavigableUrl(text);
  return applySearchTemplate(searchUrl, text);
}

// Ports SettingsPage.is_home_url / trailing-slash normalisation.
inline bool isHomeUrl(const std::string& url, const std::string& homeUrl) {
  auto strip = [](std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
  };
  return strip(trim(url)) == strip(trim(homeUrl));
}

// Normalises a homepage typed into settings so it is always navigable.
inline std::string normalizeHomeUrl(const std::string& rawHomeUrl) {
  std::string text = trim(rawHomeUrl);
  if (text.empty()) return kDefaultHomeUrl;
  if (toLower(text) == kSettingsUrl) return text;
  if (looksLikeUrl(text)) return toNavigableUrl(text);
  return kDefaultHomeUrl;
}

inline bool isHttpOrHttps(const std::string& url) {
  const std::string lower = toLower(url);
  return lower.compare(0, 7, "http://") == 0 || lower.compare(0, 8, "https://") == 0;
}

// File name safe for the "Downloaded: x" status line.
inline std::string baseName(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

inline std::string dirName(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace ob