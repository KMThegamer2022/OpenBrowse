// Windows-specific filesystem locations for OpenBrowse.
#pragma once

// WIN32_LEAN_AND_MEAN must NOT be defined before including WebView2.h: it
// makes windows.h skip the headers that declare `interface`, and WebView2.h
// only includes rpc.h/rpcndr.h, so the macro never becomes available and the
// header fails to parse. Verified on MSVC 19.4x with SDK 1.0.2903.40.
#include <windows.h>

#include <shlobj.h>
#include <cstdio>
#include <string>

#include "url_utils.hpp"

namespace ob {

inline std::wstring widen(const std::string& text) {
  if (text.empty()) return std::wstring();
  const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
  return out;
}

inline std::string narrow(const std::wstring& text) {
  if (text.empty()) return std::string();
  const int needed =
      WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                          nullptr, nullptr);
  std::string out(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed,
                      nullptr, nullptr);
  return out;
}

inline std::string knownFolder(const GUID& folderId, const std::string& fallback) {
  PWSTR raw = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &raw)) && raw != nullptr) {
    std::string path = narrow(std::wstring(raw));
    CoTaskMemFree(raw);
    while (path.size() > 1 && (path.back() == '\\' || path.back() == '/')) path.pop_back();
    return path;
  }
  return fallback;
}

inline std::string localAppData() {
  return knownFolder(FOLDERID_LocalAppData, ".");
}

inline std::string appDataRoot() { return localAppData() + "\\" + kAppName; }

inline std::string settingsPath() { return appDataRoot() + "\\settings.ini"; }
inline std::string historyPath() { return appDataRoot() + "\\history.json"; }
inline std::string logPath() { return appDataRoot() + "\\startup-error.log"; }

// WebView2 keeps cookies, cache and local storage here.
inline std::string webViewUserDataFolder() { return appDataRoot() + "\\WebView2"; }

inline std::string downloadsDir() {
  const std::string folder = knownFolder(FOLDERID_Downloads, "");
  if (!folder.empty()) return folder;
  PWSTR profileRaw = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profileRaw)) && profileRaw) {
    std::string profile = narrow(std::wstring(profileRaw));
    CoTaskMemFree(profileRaw);
    return profile + "\\Downloads";
  }
  return appDataRoot();
}

inline bool ensureDirectory(const std::string& path) {
  if (path.empty()) return false;
  const DWORD attrs = GetFileAttributesA(path.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return true;
  return CreateDirectoryA(path.c_str(), nullptr) != FALSE ||
         GetLastError() == ERROR_ALREADY_EXISTS;
}

// Creates the data directories the browser expects. Safe to call repeatedly.
inline void ensureRuntimeDirectories() { ensureDirectory(appDataRoot()); }

inline std::string describeHresult(HRESULT hr) {
  if (SUCCEEDED(hr)) return "success";
  char* text = nullptr;
  const DWORD len = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<char*>(&text), 0, nullptr);
  std::string message = len ? std::string(text, len) : std::string("unknown error");
  if (text) LocalFree(text);
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
  char code[16];
  std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
  return message + " (hr=" + code + ")";
}

}  // namespace ob