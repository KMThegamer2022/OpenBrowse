#include "tab.h"

#include <windowsx.h>

#include <vector>

#include "browser_window.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace ob {
namespace {

constexpr wchar_t kTabClassName[] = L"OpenBrowseTabHost";

LRESULT CALLBACK tabHostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_NCDESTROY:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool registerTabClassOnce() {
  static bool registered = false;
  if (registered) return true;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = tabHostProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kTabClassName;
  if (RegisterClassExW(&wc) == 0) return false;
  registered = true;
  return true;
}

// Widening helpers for the WebView2 getters that allocate with CoTaskMemAlloc.
std::wstring takeCoTaskString(PWSTR raw) {
  std::wstring out = raw ? std::wstring(raw) : std::wstring();
  if (raw) CoTaskMemFree(raw);
  return out;
}

}  // namespace

Tab::Tab(BrowserWindow* owner, TabKind kind, std::string label) : st_(std::make_shared<TabState>()) {
  st_->owner = owner;
  st_->kind = kind;
  st_->label = std::move(label);
  st_->tab = this;
}

Tab::~Tab() {
  // Leave the state alive: a controller-creation callback may still hold it.
  st_->tab = nullptr;
  st_->abandoned = true;
  st_->owner = nullptr;
  unwireEvents();
  st_->controller.Reset();
  st_->webview.Reset();
  if (st_->hwnd != nullptr) {
    HWND hwnd = st_->hwnd;
    st_->hwnd = nullptr;
    ::DestroyWindow(hwnd);
  }
  st_.reset();
}

void Tab::create(HWND parent, ICoreWebView2Environment* environment) {
  if (environment == nullptr) return;

  if (!registerTabClassOnce()) return;

  st_->hwnd = CreateWindowExW(0, kTabClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
  if (st_->hwnd == nullptr) return;

  std::shared_ptr<TabState> state = st_;
  environment->CreateCoreWebView2Controller(
      st_->hwnd,
      Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
          [state](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (state->abandoned || FAILED(result) || controller == nullptr) {
              if (controller) controller->Release();
              return S_OK;
            }

            state->controller = controller;

            ComPtr<ICoreWebView2> webview;
            if (FAILED(controller->get_CoreWebView2(&webview)) || webview == nullptr) {
              state->controller.Reset();
              return S_OK;
            }
            state->webview = webview;

            if (state->tab) {
              state->tab->applyWebSettings();
              state->tab->wireEvents(state);
            }

            // Flush whatever navigation was requested before we were ready.
            if (state->pendingIsHtml) {
              state->webview->NavigateToString(widen(state->pendingHtml).c_str());
            } else if (!state->pendingUrl.empty()) {
              state->webview->Navigate(widen(state->pendingUrl).c_str());
            }
            state->pendingUrl.clear();
            state->pendingHtml.clear();
            state->pendingIsHtml = false;
            return S_OK;
          })
          .Get());
}

void Tab::wireEvents(const std::shared_ptr<TabState>& state) {
  ICoreWebView2* webview = state->webview.Get();
  if (webview == nullptr) return;

  BrowserWindow* owner = state->owner;

  // ---- Navigation starting: intercept openbrowse://settings ----------------
  webview->add_NavigationStarting(
      Callback<ICoreWebView2NavigationStartingEventHandler2>(
          [state](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args)
              -> HRESULT {
            if (state->abandoned || !state->tab) return S_OK;
            BrowserWindow* win = state->owner;
            if (win == nullptr) return S_OK;

            PWSTR rawUri = nullptr;
            std::wstring uri;
            if (SUCCEEDED(args->get_Uri(&rawUri))) uri = takeCoTaskString(rawUri);
            const std::string url = narrow(uri);

            if (toLower(url) == kSettingsUrl) {
              args->put_Cancel(TRUE);
              state->kind = TabKind::Settings;
              state->tab->navigateToString(win->buildSettingsHtml(), kSettingsUrl);
              return S_OK;
            }

            // Navigating away from the settings page turns it into a normal tab.
            if (state->kind == TabKind::Settings) state->kind = TabKind::Browser;

            if (!url.empty() && url != "about:blank") {
              state->displayUrl = url;
              win->onTabUrlChanged(state->tab, url);
            }
            win->onTabNavigationStarted(state->tab);
            return S_OK;
          })
          .Get(),
      &state->tokNavigationStarting);

  // ---- Navigation completed: history + status ------------------------------
  webview->add_NavigationCompleted(
      Callback<ICoreWebView2NavigationCompletedEventHandler2>(
          [state](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args)
              -> HRESULT {
            if (state->abandoned || !state->tab || state->owner == nullptr) return S_OK;

            BOOL isSuccess = FALSE;
            args->get_IsSuccess(&isSuccess);

            ICoreWebView2* view = nullptr;
            sender->get_CoreWebView2(&view);
            std::string url;
            PWSTR rawSource = nullptr;
            if (view && SUCCEEDED(view->get_Source(&rawSource))) {
              url = narrow(takeCoTaskString(rawSource));
            }
            if (view) view->Release();

            state->tab->setDisplayUrl(url);
            state->owner->onTabNavigationCompleted(state->tab, url, isSuccess != FALSE);
            return S_OK;
          })
          .Get(),
      &state->tokNavigationCompleted);

  // ---- Title ---------------------------------------------------------------
  webview->add_DocumentTitleChanged(
      Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2DocumentTitleChangedEventArgs* args)
              -> HRESULT {
            if (state->abandoned || !state->tab || state->owner == nullptr) return S_OK;
            PWSTR rawTitle = nullptr;
            if (FAILED(args->get_Title(&rawTitle))) return S_OK;
            const std::wstring titleW = takeCoTaskString(rawTitle);
            state->title = narrow(titleW);
            // Matches the Python build's 24 character tab label.
            std::string label = state->title.substr(0, 24);
            if (label.empty()) label = "New Tab";
            state->tab->setLabel(label);
            state->owner->onTabTitleChanged(state->tab);
            return S_OK;
          })
          .Get(),
      &state->tokDocumentTitleChanged);

  // ---- Favicon -------------------------------------------------------------
  webview->add_FaviconChanged(
      Callback<ICoreWebView2FaviconChangedEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2FaviconChangedEventArgs* args) -> HRESULT {
            if (state->abandoned || !state->tab || state->owner == nullptr) return S_OK;
            UINT32 length = 0;
            if (FAILED(args->get_IconData(&length, nullptr)) || length == 0) return S_OK;

            std::vector<BYTE> data(length);
            if (FAILED(args->get_IconData(&length, data.data()))) return S_OK;

            HICON icon = CreateIconFromResourceEx(data.data(), static_cast<int>(length), TRUE,
                                                  0x00030000, IMAGE_ICON, 0);
            if (icon == nullptr) return S_OK;
            state->owner->onTabIconChanged(state->tab, icon);
            return S_OK;
          })
          .Get(),
      &state->tokFaviconChanged);

  // ---- Content loading: progress bar ---------------------------------------
  webview->add_ContentLoading(
      Callback<ICoreWebView2ContentLoadingEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2ContentLoadingEventArgs* args) -> HRESULT {
            if (state->abandoned || !state->tab || state->owner == nullptr) return S_OK;
            std::string url;
            PWSTR rawSource = nullptr;
            if (SUCCEEDED(sender->get_Source(&rawSource))) url = narrow(takeCoTaskString(rawSource));
            state->owner->onTabContentLoading(state->tab, url);
            return S_OK;
          })
          .Get(),
      &state->tokContentLoading);

  // ---- Navigation starting for progress + popup handling --------------------
  webview->add_SourceChanged(
      Callback<ICoreWebView2SourceChangedEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
            if (state->abandoned || !state->tab || state->owner == nullptr) return S_OK;
            PWSTR rawSource = nullptr;
            if (FAILED(args->get_Source(&rawSource))) return S_OK;
            const std::string url = narrow(takeCoTaskString(rawSource));
            state->tab->setDisplayUrl(url);
            state->owner->onTabSourceChanged(state->tab, url);
            return S_OK;
          })
          .Get(),
      &state->tokSourceChanged);

  // ---- window.open / target=_blank becomes a new tab -----------------------
  webview->add_NewWindowRequested(
      Callback<ICoreWebView2NewWindowRequestedEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args)
              -> HRESULT {
            if (state->abandoned || !state->tab || state->owner == nullptr) return S_OK;
            args->put_Handled(TRUE);  // we take over; no stray popup window
            if (state->owner) state->owner->onTabNewWindowRequested(state->tab);
            return S_OK;
          })
          .Get(),
      &state->tokNewWindowRequested);

  // ---- Downloads -----------------------------------------------------------
  webview->add_DownloadStarting(
      Callback<ICoreWebView2DownloadStartingEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2DownloadStartingEventArgs* args)
              -> HRESULT {
            if (state->abandoned || state->owner == nullptr) return S_OK;
            state->owner->onDownloadStarting(state->tab, args);
            return S_OK;
          })
          .Get(),
      &state->tokDownloadStarting);

  // ---- Settings page bridge ------------------------------------------------
  webview->add_WebMessageReceived(
      Callback<ICoreWebView2WebMessageReceivedEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args)
              -> HRESULT {
            if (state->abandoned || state->owner == nullptr) return S_OK;
            PWSTR rawJson = nullptr;
            if (FAILED(args->TryGetWebMessageAsString(&rawJson))) return S_OK;
            const std::string message = narrow(takeCoTaskString(rawJson));
            state->owner->onSettingsMessage(state->tab, message);
            return S_OK;
          })
          .Get(),
      &state->tokWebMessageReceived);

  // ---- Renderer crash ------------------------------------------------------
  webview->add_ProcessFailed(
      Callback<ICoreWebView2ProcessFailedEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
            if (state->abandoned || !state->tab || state->owner == nullptr) return S_OK;
            COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXIT;
            args->get_ProcessFailedKind(&kind);
            state->owner->onTabProcessFailed(state->tab, static_cast<int>(kind));
            return S_OK;
          })
          .Get(),
      &state->tokProcessFailed);

  // ---- window.close() closes the tab ---------------------------------------
  webview->add_WindowCloseRequested(
      Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2WindowCloseRequestedEventArgs* args)
              -> HRESULT {
            if (state->abandoned || !state->tab || state->owner == nullptr) return S_OK;
            state->owner->onTabCloseRequested(state->tab);
            return S_OK;
          })
          .Get(),
      &state->tokWindowCloseRequested);
}

void Tab::applyWebSettings() {
  ICoreWebView2* webview = st_->webview.Get();
  if (webview == nullptr) return;

  ComPtr<ICoreWebView2Settings> webSettings;
  if (FAILED(webview->get_Settings(&webSettings)) || webSettings == nullptr) return;

  // A right-click menu, DevTools and zoom are all expected in a browser.
  webSettings->put_AreDefaultContextMenusEnabled(TRUE);
  webSettings->put_AreDevToolsEnabled(TRUE);
  webSettings->put_AreDefaultScriptDialogsEnabled(TRUE);
  webSettings->put_AreHostObjectsAllowed(TRUE);
  webSettings->put_IsZoomControlEnabled(TRUE);
  webSettings->put_IsStatusBarEnabled(FALSE);

  // Matches the Python build's white page background.
  webSettings->put_DefaultBackgroundColor(RGB(255, 255, 255));

  // Append the OpenBrowse marker to the default user agent.
  ComPtr<ICoreWebView2Settings2> webSettings2;
  if (SUCCEEDED(webSettings.As(&webSettings2)) && webSettings2) {
    PWSTR rawAgent = nullptr;
    if (SUCCEEDED(webSettings2->get_UserAgent(&rawAgent)) && rawAgent != nullptr) {
      std::wstring agent(rawAgent);
      CoTaskMemFree(rawAgent);
      if (agent.find(L"OpenBrowse/") == std::wstring::npos) {
        agent += L" OpenBrowse/1.0";
        webSettings2->put_UserAgent(agent.c_str());
      }
    }
  }
}

void Tab::unwireEvents() {
  ICoreWebView2* webview = st_->webview.Get();
  if (webview != nullptr) {
    webview->remove_NavigationStarting(st_->tokNavigationStarting);
    webview->remove_NavigationCompleted(st_->tokNavigationCompleted);
    webview->remove_DocumentTitleChanged(st_->tokDocumentTitleChanged);
    webview->remove_FaviconChanged(st_->tokFaviconChanged);
    webview->remove_ContentLoading(st_->tokContentLoading);
    webview->remove_SourceChanged(st_->tokSourceChanged);
    webview->remove_NewWindowRequested(st_->tokNewWindowRequested);
    webview->remove_DownloadStarting(st_->tokDownloadStarting);
    webview->remove_WebMessageReceived(st_->tokWebMessageReceived);
    webview->remove_ProcessFailed(st_->tokProcessFailed);
    webview->remove_WindowCloseRequested(st_->tokWindowCloseRequested);
  }
  // Closing the controller tears down the renderer for this tab.
  if (st_->controller) st_->controller->Close();
  st_->webview.Reset();
  st_->controller.Reset();
}

void Tab::navigate(const std::string& url) {
  if (url.empty()) return;
  if (st_->webview == nullptr) {
    st_->pendingUrl = url;
    st_->pendingIsHtml = false;
    return;
  }
  st_->webview->Navigate(widen(url).c_str());
}

void Tab::navigateToString(const std::string& html, const std::string& displayUrl) {
  st_->displayUrl = displayUrl;
  if (st_->webview == nullptr) {
    st_->pendingHtml = html;
    st_->pendingDisplayUrl = widen(displayUrl);
    st_->pendingIsHtml = true;
    return;
  }
  st_->webview->NavigateToString(widen(html).c_str());
}

void Tab::goBack() {
  if (st_->webview) st_->webview->GoBack();
}

void Tab::goForward() {
  if (st_->webview) st_->webview->GoForward();
}

void Tab::reload() {
  if (st_->webview) st_->webview->Reload();
}

void Tab::stop() {
  // TrySuspend returns false while a navigation is already committed, which is
  // harmless; there is nothing further to stop in that case.
  if (st_->webview) st_->webview->TrySuspend();
}

void Tab::openDevTools() {
  if (st_->webview) st_->webview->OpenDevToolsWindow();
}

void Tab::setVisible(bool visible) {
  if (st_->controller) {
    st_->controller->put_IsVisible(visible ? TRUE : FALSE);
  }
}

void Tab::close() {
  unwireEvents();
}

}  // namespace ob