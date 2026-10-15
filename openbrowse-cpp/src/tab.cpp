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

// WebView2 getters that allocate strings return CoTaskMem memory.
std::wstring takeCoTaskString(PWSTR raw) {
  std::wstring out = raw ? std::wstring(raw) : std::wstring();
  if (raw) CoTaskMemFree(raw);
  return out;
}

// The current top-level document URL.
//
// ICoreWebView2 has no get_Source; it was added on ICoreWebView2_2. The
// SourceChanged event arguments only expose get_IsNewDocument.
std::wstring currentSource(ICoreWebView2* webview) {
  ComPtr<ICoreWebView2_2> upgraded;
  if (webview == nullptr) return std::wstring();
  if (FAILED(webview->QueryInterface(IID_PPV_ARGS(&upgraded))) || !upgraded) return std::wstring();
  PWSTR raw = nullptr;
  if (FAILED(upgraded->get_Source(&raw)) || raw == nullptr) return std::wstring();
  return takeCoTaskString(raw);
}

// Reads an IStream fully into memory, for favicon PNG data.
bool readStreamToVector(IStream* stream, std::vector<BYTE>* out) {
  if (stream == nullptr) return false;

  STATSTG stats{};
  if (SUCCEEDED(stream->Stat(&stats, STATFLAG_DEFAULT)) && stats.cbSize.QuadPart > 0 &&
      stats.cbSize.QuadPart < 64 * 1024) {
    out->resize(static_cast<size_t>(stats.cbSize.QuadPart));
  } else {
    out->clear();
  }

  size_t offset = 0;
  for (;;) {
    if (offset == out->size()) {
      if (out->size() >= 64 * 1024) return false;  // refuse implausibly large icons
      out->resize(out->empty() ? 8192 : out->size() * 2);
    }
    const ULONG wanted =
        static_cast<ULONG>(std::min<size_t>(out->size() - offset, 64 * 1024));
    ULONG read = 0;
    if (FAILED(stream->Read(out->data() + offset, wanted, &read))) break;
    if (read == 0) break;
    offset += read;
  }
  out->resize(offset);
  return offset > 0;
}

}  // namespace

Tab::Tab(BrowserWindow* owner, TabKind kind, std::string label)
    : st_(std::make_shared<TabState>()) {
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

  // The background colour is a controller property (ICoreWebView2Controller2),
  // not a per-page setting.
  ComPtr<ICoreWebView2Controller2> controller2;
  if (SUCCEEDED(st_->controller.As(&controller2)) && controller2) {
    controller2->put_DefaultBackgroundColor(RGB(255, 255, 255));
    st_->controller2 = controller2;
  }

  // Append the OpenBrowse marker to the default user agent.
  ComPtr<ICoreWebView2Settings2> webSettings2;
  if (SUCCEEDED(webSettings.As(&webSettings2)) && webSettings2) {
    PWSTR rawAgent = nullptr;
    if (SUCCEEDED(webSettings2->get_UserAgent(&rawAgent)) && rawAgent != nullptr) {
      std::wstring agent = takeCoTaskString(rawAgent);
      if (agent.find(L"OpenBrowse/") == std::wstring::npos) {
        agent += L" OpenBrowse/1.0";
        webSettings2->put_UserAgent(agent.c_str());
      }
    }
  }
}

void Tab::wireEvents(const std::shared_ptr<TabState>& state) {
  ICoreWebView2* webview = state->webview.Get();
  if (webview == nullptr) return;

  // ---- Navigation starting: intercept openbrowse://settings ----------------
  webview->add_NavigationStarting(
      Callback<ICoreWebView2NavigationStartingEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args)
              -> HRESULT {
            if (state->abandoned || state->tab == nullptr) return S_OK;
            BrowserWindow* window = state->owner;
            if (window == nullptr) return S_OK;

            PWSTR rawUri = nullptr;
            std::wstring uri;
            if (SUCCEEDED(args->get_Uri(&rawUri))) uri = takeCoTaskString(rawUri);
            const std::string url = narrow(uri);

            if (toLower(url) == kSettingsUrl) {
              // put_Cancel lives on the version 2 arguments.
              ComPtr<ICoreWebView2NavigationStartingEventArgs2> args2;
              if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args2))) && args2) {
                args2->put_Cancel(TRUE);
              }
              state->kind = TabKind::Settings;
              state->tab->navigateToString(window->buildSettingsHtml(), kSettingsUrl);
              return S_OK;
            }

            // Navigating away from the settings page turns it into a normal tab.
            if (state->kind == TabKind::Settings) state->kind = TabKind::Browser;

            if (!url.empty() && url != "about:blank") {
              state->displayUrl = url;
              window->onTabUrlChanged(state->tab, url);
            }
            window->onTabNavigationStarted(state->tab);
            return S_OK;
          })
          .Get(),
      &state->tokNavigationStarting);

  // ---- Navigation completed: history, status, navigation state -------------
  webview->add_NavigationCompleted(
      Callback<ICoreWebView2NavigationCompletedEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args)
              -> HRESULT {
            if (state->abandoned || state->tab == nullptr || state->owner == nullptr) return S_OK;

            BOOL isSuccess = FALSE;
            args->get_IsSuccess(&isSuccess);

            const std::string url = narrow(currentSource(sender));
            state->tab->setDisplayUrl(url);
            state->owner->onTabNavigationCompleted(state->tab, url, isSuccess != FALSE);
            return S_OK;
          })
          .Get(),
      &state->tokNavigationCompleted);

  // ---- Title ---------------------------------------------------------------
  webview->add_DocumentTitleChanged(
      // The arguments are untyped; the title comes from ICoreWebView2_2.
      Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
          [state](ICoreWebView2* sender, IUnknown* /*args*/) -> HRESULT {
            if (state->abandoned || state->tab == nullptr || state->owner == nullptr) return S_OK;

            ComPtr<ICoreWebView2_2> view2;
            if (FAILED(sender->QueryInterface(IID_PPV_ARGS(&view2))) || !view2) return S_OK;
            PWSTR rawTitle = nullptr;
            if (FAILED(view2->get_DocumentTitle(&rawTitle))) return S_OK;
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

  // ---- Content loading: progress ------------------------------------------
  webview->add_ContentLoading(
      Callback<ICoreWebView2ContentLoadingEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2ContentLoadingEventArgs* args) -> HRESULT {
            if (state->abandoned || state->tab == nullptr || state->owner == nullptr) return S_OK;
            const std::string url = narrow(currentSource(sender));
            state->owner->onTabContentLoading(state->tab, url);
            return S_OK;
          })
          .Get(),
      &state->tokContentLoading);

  // ---- Source changed: keep the address bar in step ------------------------
  webview->add_SourceChanged(
      Callback<ICoreWebView2SourceChangedEventHandler>(
          [state](ICoreWebView2* sender, IUnknown* /*args*/) -> HRESULT {
            if (state->abandoned || state->tab == nullptr || state->owner == nullptr) return S_OK;
            const std::string url = narrow(currentSource(sender));
            state->tab->setDisplayUrl(url);
            state->owner->onTabSourceChanged(state->tab, url);
            return S_OK;
          })
          .Get(),
      &state->tokSourceChanged);

  // ---- window.open / target=_blank becomes a new tab ----------------------
  webview->add_NewWindowRequested(
      Callback<ICoreWebView2NewWindowRequestedEventHandler>(
          [state](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args)
              -> HRESULT {
            if (state->abandoned || state->tab == nullptr || state->owner == nullptr) return S_OK;
            // We take over, so suppress the popup WebView2 would create.
            args->put_Handled(TRUE);
            state->owner->onTabNewWindowRequested(state->tab);
            return S_OK;
          })
          .Get(),
      &state->tokNewWindowRequested);

  // ---- Downloads -----------------------------------------------------------
  // add_DownloadStarting arrived on ICoreWebView2_4.
  if (SUCCEEDED(webview->QueryInterface(IID_PPV_ARGS(&state->webview4))) && state->webview4) {
    state->webview4->add_DownloadStarting(
        Callback<ICoreWebView2DownloadStartingEventHandler>(
            [state](ICoreWebView2* sender, ICoreWebView2DownloadStartingEventArgs* args)
                -> HRESULT {
              if (state->abandoned || state->owner == nullptr) return S_OK;
              state->owner->onDownloadStarting(state->tab, args);
              return S_OK;
            })
            .Get(),
        &state->tokDownloadStarting);
  }

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
            if (state->abandoned || state->tab == nullptr || state->owner == nullptr) return S_OK;
            COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
            args->get_ProcessFailedKind(&kind);
            state->owner->onTabProcessFailed(state->tab, static_cast<int>(kind));
            return S_OK;
          })
          .Get(),
      &state->tokProcessFailed);

  // ---- window.close() closes the tab --------------------------------------
  webview->add_WindowCloseRequested(
      Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
          [state](ICoreWebView2* sender, IUnknown* /*args*/) -> HRESULT {
            if (state->abandoned || state->tab == nullptr || state->owner == nullptr) return S_OK;
            state->owner->onTabCloseRequested(state->tab);
            return S_OK;
          })
          .Get(),
      &state->tokWindowCloseRequested);

  // ---- Favicons ------------------------------------------------------------
  // add_FaviconChanged arrived on ICoreWebView2_15, and the icon itself is
  // fetched separately through GetFavicon, which hands back an IStream. An
  // older WebView2 runtime simply means no tab icons.
  ComPtr<ICoreWebView2_15> webview15;
  if (SUCCEEDED(webview->QueryInterface(IID_PPV_ARGS(&webview15))) && webview15) {
    webview15->add_FaviconChanged(
        Callback<ICoreWebView2FaviconChangedEventHandler>(
            // This event carries IUnknown rather than a typed arguments struct.
            [state](ICoreWebView2* sender, IUnknown* /*args*/) -> HRESULT {
              if (state->abandoned || state->tab == nullptr || state->owner == nullptr) return S_OK;

              ComPtr<ICoreWebView2_15> view15;
              if (FAILED(sender->QueryInterface(IID_PPV_ARGS(&view15))) || !view15) return S_OK;

              return view15->GetFavicon(
                  COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG,
                  Callback<ICoreWebView2GetFaviconCompletedHandler>(
                      [state](HRESULT errorCode, IStream* result) -> HRESULT {
                        if (state->abandoned || state->tab == nullptr ||
                            state->owner == nullptr || FAILED(errorCode)) {
                          return S_OK;
                        }
                        std::vector<BYTE> png;
                        if (!readStreamToVector(result, &png)) return S_OK;

                        HICON icon = CreateIconFromResourceEx(
                            png.data(), static_cast<int>(png.size()), TRUE, 0x00030000,
                            IMAGE_ICON, 0);
                        if (icon == nullptr) return S_OK;
                        state->owner->onTabIconChanged(state->tab, icon);
                        return S_OK;
                      })
                      .Get());
            })
            .Get(),
        &state->tokFaviconChanged);
  }
}

void Tab::unwireEvents() {
  if (st_->webview != nullptr) {
    ICoreWebView2* webview = st_->webview.Get();
    webview->remove_NavigationStarting(st_->tokNavigationStarting);
    webview->remove_NavigationCompleted(st_->tokNavigationCompleted);
    webview->remove_DocumentTitleChanged(st_->tokDocumentTitleChanged);
    webview->remove_ContentLoading(st_->tokContentLoading);
    webview->remove_SourceChanged(st_->tokSourceChanged);
    webview->remove_NewWindowRequested(st_->tokNewWindowRequested);
    if (st_->webview4) st_->webview4->remove_DownloadStarting(st_->tokDownloadStarting);
    webview->remove_WebMessageReceived(st_->tokWebMessageReceived);
    webview->remove_ProcessFailed(st_->tokProcessFailed);
    webview->remove_WindowCloseRequested(st_->tokWindowCloseRequested);

    ComPtr<ICoreWebView2_15> webview15;
    if (SUCCEEDED(webview->QueryInterface(IID_PPV_ARGS(&webview15))) && webview15) {
      webview15->remove_FaviconChanged(st_->tokFaviconChanged);
    }
  }
  // Closing the controller tears down the renderer for this tab.
  if (st_->controller) st_->controller->Close();
  st_->webview4.Reset();
  st_->controller2.Reset();
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
  // TrySuspend arrived on ICoreWebView2_3, and returns false while a navigation
  // is already committed, which is harmless.
  ComPtr<ICoreWebView2_3> webview3;
  if (st_->webview && SUCCEEDED(st_->webview.As(&webview3)) && webview3) {
    webview3->TrySuspend();
  }
}

void Tab::openDevTools() {
  if (st_->webview) st_->webview->OpenDevToolsWindow();
}

void Tab::setVisible(bool visible) {
  if (st_->controller) st_->controller->put_IsVisible(visible ? TRUE : FALSE);
}

void Tab::close() { unwireEvents(); }

}  // namespace ob