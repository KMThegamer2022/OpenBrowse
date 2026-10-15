// Main OpenBrowse window: native Win32 chrome around WebView2 tabs.
#pragma once

// WIN32_LEAN_AND_MEAN must NOT be defined before including WebView2.h: it
// makes windows.h skip the headers that declare `interface`, and WebView2.h
// only includes rpc.h/rpcndr.h, so the macro never becomes available and the
// header fails to parse. Verified on MSVC 19.4x with SDK 1.0.2903.40.
#include <commctrl.h>
#include <windows.h>
#include <wrl.h>

#include <memory>
#include <string>
#include <vector>

#include "WebView2.h"

#include "app_paths.hpp"
#include "history.hpp"
#include "settings.hpp"
#include "tab.h"
#include "url_utils.hpp"

namespace ob {

// Menu and accelerator commands. Toolbar buttons reuse these same identifiers.
enum : UINT {
  kCmdNewTab = 100,
  kCmdCloseTab,
  kCmdSettings,
  kCmdQuit,
  kCmdDevTools,
  kCmdFullScreen,
  kCmdDuplicateTab,
  kCmdGoHome,
  kCmdBack,
  kCmdForward,
  kCmdReload,
  kCmdStop,
  kCmdFocusAddressBar,
};

// Child window identifiers that are not commands.
enum : UINT { kIdAddressBar = 1000, kIdTabStrip = 1001 };

// One in-flight download. The operation handle must stay alive until it
// completes, so completed downloads are pruned from this list on completion.
struct ActiveDownload {
  Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation> operation;
  std::string fileName;
};

class BrowserWindow {
 public:
  BrowserWindow();
  ~BrowserWindow();

  BrowserWindow(const BrowserWindow&) = delete;
  BrowserWindow& operator=(const BrowserWindow&) = delete;

  bool create(HINSTANCE instance, int showCommand, ICoreWebView2Environment* environment);
  HWND hwnd() const { return hwnd_; }
  HACCEL accelerators() const { return accelerators_; }

  const AppSettings& settings() const { return settings_; }
  std::string buildSettingsHtml();

  // Callbacks driven by Tab.
  void onTabUrlChanged(Tab* tab, const std::string& url);
  void onTabSourceChanged(Tab* tab, const std::string& url);
  void onTabTitleChanged(Tab* tab);
  void onTabIconChanged(Tab* tab, HICON icon);
  void onTabNavigationStarted(Tab* tab);
  void onTabNavigationCompleted(Tab* tab, const std::string& url, bool success);
  void onTabContentLoading(Tab* tab, const std::string& url);
  void onTabNewWindowRequested(Tab* tab);
  void onTabCloseRequested(Tab* tab);
  void onTabProcessFailed(Tab* tab, int failedKind);
  void onDownloadStarting(Tab* tab, ICoreWebView2DownloadStartingEventArgs* args);
  void onSettingsMessage(Tab* tab, const std::string& message);

 private:
  static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK tabStripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR id, DWORD_PTR refData);
  static LRESULT CALLBACK addressBarProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR id, DWORD_PTR refData);

  LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

  void buildMenu();
  void createControls();
  void layoutControls();

  Tab* addTab(TabKind kind, const std::string& url, const std::string& label, bool makeCurrent);
  void closeTabAt(int index);
  void closeTabObject(Tab* tab);
  int indexOfTab(Tab* tab) const;
  int currentIndex() const;
  Tab* currentTab() const;
  void setCurrentIndex(int index);
  void showCurrentTab();

  void updateAddressBar();
  void updateNavigationButtons();
  void updateTabStrip();
  void rebuildTabIcons();
  void updateWindowTitle();
  void setStatus(const std::string& text);
  void setProgress(int percent);  // negative hides the bar

  void executeCommand(UINT command);
  void loadFromAddressBar();
  void goHome();
  void duplicateCurrentTab();
  void toggleFullScreen();
  void openDevTools();

  void openSettingsTab();
  Tab* settingsTab() const;
  void refreshSettingsTab();

  void recordHistory(Tab* tab, const std::string& url);
  void clearBrowsingData(bool history, bool cookies, bool cache);
  void applyPreferences(const AppSettings& updated, bool restartNeeded);

  void postToSettingsPage(Tab* tab, const std::string& json);
  void sendHistoryTo(Tab* tab);
  void sendStatusTo(Tab* tab, const std::string& text, const std::string& level);
  void handleSettingsAction(Tab* tab, const std::string& message);

  void onDownloadCompleted(ICoreWebView2DownloadOperation* operation,
                           COREWEBVIEW2_DOWNLOAD_STATE state);

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND addressBar_ = nullptr;
  HWND tabStrip_ = nullptr;
  HWND statusLabel_ = nullptr;
  HWND backButton_ = nullptr;
  HWND forwardButton_ = nullptr;
  HWND reloadButton_ = nullptr;
  HWND stopButton_ = nullptr;
  HWND homeButton_ = nullptr;
  HWND newTabButton_ = nullptr;

  HMENU menu_ = nullptr;
  HMENU fileMenu_ = nullptr;
  HMENU viewMenu_ = nullptr;
  HMENU browserMenu_ = nullptr;
  HACCEL accelerators_ = nullptr;

  Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;

  std::vector<std::unique_ptr<Tab>> tabs_;
  std::vector<HICON> tabIcons_;
  AppSettings settings_;
  SettingsStore settingsStore_;
  HistoryStore historyStore_;
  std::vector<HistoryEntry> history_;
  std::vector<ActiveDownload> downloads_;
  Microsoft::WRL::ComPtr<IImageList> tabImageList_;

  int progress_ = -1;
  bool suppressAddressBarEvents_ = false;
  bool refreshingSettings_ = false;
  bool fullScreen_ = false;
  WINDOWPLACEMENT savedPlacement_{};
  std::string statusText_ = "Ready";
};

}  // namespace ob