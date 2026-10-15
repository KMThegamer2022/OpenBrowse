// A single WebView2-backed browser tab.
#pragma once

// WIN32_LEAN_AND_MEAN must NOT be defined before including WebView2.h: it
// makes windows.h skip the headers that declare `interface`, and WebView2.h
// only includes rpc.h/rpcndr.h, so the macro never becomes available and the
// header fails to parse. Verified on MSVC 19.4x with SDK 1.0.2903.40.
#include <windows.h>
#include <wrl.h>

#include <memory>
#include <string>

#include "WebView2.h"

#include "app_paths.hpp"
#include "url_utils.hpp"

namespace ob {

class BrowserWindow;
class Tab;

enum class TabKind { Browser, Settings };

// Holds the COM objects and event registrations for one tab.
//
// CreateCoreWebView2Controller is asynchronous, so the controller can arrive
// after the tab has already been closed. This state is therefore reference
// counted independently of the Tab: closing a tab clears `tab` and sets
// `abandoned`, and the state survives until any in-flight callback has
// finished using it. Event handlers capture a shared_ptr to this struct, which
// would form a reference cycle through the webview, so unwireEvents() always
// removes them.
struct TabState {
  BrowserWindow* owner = nullptr;
  TabKind kind = TabKind::Browser;
  std::string label = "New Tab";
  std::string title;
  std::string displayUrl;

  HWND hwnd = nullptr;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview;

  // Later WebView2 interfaces, kept so their events can be unwired.
  Microsoft::WRL::ComPtr<ICoreWebView2_4> webview4;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;

  // Navigation queued while the controller was still being created.
  std::string pendingUrl;
  std::string pendingHtml;
  std::wstring pendingDisplayUrl;
  bool pendingIsHtml = false;

  Tab* tab = nullptr;
  bool abandoned = false;

  EventRegistrationToken tokNavigationStarting{};
  EventRegistrationToken tokNavigationCompleted{};
  EventRegistrationToken tokDocumentTitleChanged{};
  EventRegistrationToken tokFaviconChanged{};
  EventRegistrationToken tokContentLoading{};
  EventRegistrationToken tokSourceChanged{};
  EventRegistrationToken tokNewWindowRequested{};
  EventRegistrationToken tokDownloadStarting{};
  EventRegistrationToken tokWebMessageReceived{};
  EventRegistrationToken tokProcessFailed{};
  EventRegistrationToken tokWindowCloseRequested{};
};

class Tab {
 public:
  Tab(BrowserWindow* owner, TabKind kind, std::string label);
  ~Tab();

  Tab(const Tab&) = delete;
  Tab& operator=(const Tab&) = delete;

  // Creates the child window and starts controller creation.
  void create(HWND parent, ICoreWebView2Environment* environment);

  void navigate(const std::string& url);
  void navigateToString(const std::string& html, const std::string& displayUrl);

  void goBack();
  void goForward();
  void reload();
  void stop();
  void openDevTools();

  void setVisible(bool visible);
  void close();

  bool isReady() const { return st_->webview != nullptr; }
  TabKind kind() const { return st_->kind; }
  void setKind(TabKind kind) { st_->kind = kind; }

  HWND hwnd() const { return st_->hwnd; }
  const std::string& label() const { return st_->label; }
  void setLabel(std::string label) { st_->label = std::move(label); }
  const std::string& title() const { return st_->title; }
  const std::string& displayUrl() const { return st_->displayUrl; }
  void setDisplayUrl(std::string url) { st_->displayUrl = std::move(url); }

  ICoreWebView2* webview() const { return st_->webview.Get(); }
  ICoreWebView2Controller* controller() const { return st_->controller.Get(); }

  const std::shared_ptr<TabState>& state() const { return st_; }

 private:
  void wireEvents(const std::shared_ptr<TabState>& state);
  void applyWebSettings();
  void unwireEvents();

  std::shared_ptr<TabState> st_;
};

}  // namespace ob