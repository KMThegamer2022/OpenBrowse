#include "browser_window.h"

#include <algorithm>
#include <commdlg.h>
#include <windowsx.h>

#include "settings_page.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace ob {
namespace {

constexpr wchar_t kWindowClassName[] = L"OpenBrowseMainWindow";
constexpr UINT_PTR kSubclassId = 1;

constexpr int kNavHeight = 30;
constexpr int kTabStripHeight = 30;
constexpr int kStatusHeight = 22;
constexpr int kGap = 4;
constexpr int kButtonWidth = 74;
constexpr int kNewTabWidth = 80;

// Close button hit area in the owner-drawn tab strip, matching the painter.
constexpr int kTabCloseBox = 16;
constexpr int kTabPadLeft = 10;
constexpr int kTabPadRight = 26;  // room for the close box

void enableItem(HMENU menu, UINT command, bool enabled) {
  if (menu != nullptr) {
    EnableMenuItem(menu, command, MF_BYCOMMAND | (enabled ? MFS_ENABLED : MFS_GRAYED));
  }
}

}  // namespace

BrowserWindow::BrowserWindow()
    : settingsStore_(settingsPath()), historyStore_(historyPath()) {
  settings_ = settingsStore_.load();
  history_ = historyStore_.load();
}

BrowserWindow::~BrowserWindow() {
  if (accelerators_) DestroyAcceleratorTable(accelerators_);
  if (menu_) DestroyMenu(menu_);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

bool BrowserWindow::create(HINSTANCE instance, int showCommand,
                           ICoreWebView2Environment* environment) {
  instance_ = instance;
  environment_ = environment;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &BrowserWindow::windowProc;
  wc.hInstance = instance;
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = kWindowClassName;

  static bool registered = false;
  if (!registered) {
    if (RegisterClassExW(&wc) == 0) return false;
    registered = true;
  }

  hwnd_ = CreateWindowExW(0, kWindowClassName, kAppName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                          CW_USEDEFAULT, 1440, 920, nullptr, nullptr, instance, this);
  if (hwnd_ == nullptr) return false;

  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  buildMenu();
  createControls();
  layoutControls();

  ShowWindow(hwnd_, showCommand);
  UpdateWindow(hwnd_);

  addTab(TabKind::Browser, settings_.homeUrl, "Home", true);
  return true;
}

void BrowserWindow::buildMenu() {
  menu_ = CreateMenu();

  fileMenu_ = CreatePopupMenu();
  AppendMenuW(fileMenu_, MF_STRING, kCmdNewTab, L"&New Tab\tCtrl+T");
  AppendMenuW(fileMenu_, MF_STRING, kCmdCloseTab, L"&Close Tab\tCtrl+W");
  AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(fileMenu_, MF_STRING, kCmdSettings, L"&Settings\tCtrl+,");
  AppendMenuW(fileMenu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(fileMenu_, MF_STRING, kCmdQuit, L"&Quit\tAlt+F4");

  viewMenu_ = CreatePopupMenu();
  AppendMenuW(viewMenu_, MF_STRING, kCmdDevTools, L"Open De&vTools\tF12");
  AppendMenuW(viewMenu_, MF_STRING, kCmdFullScreen, L"Toggle &Full Screen\tF11");

  browserMenu_ = CreatePopupMenu();
  AppendMenuW(browserMenu_, MF_STRING, kCmdDuplicateTab, L"&Duplicate Tab\tCtrl+Shift+K");
  AppendMenuW(browserMenu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(browserMenu_, MF_STRING, kCmdGoHome, L"&Home\tAlt+Home");
  AppendMenuW(browserMenu_, MF_STRING, kCmdFocusAddressBar, L"&Go To URL\tCtrl+L");
  AppendMenuW(browserMenu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(browserMenu_, MF_STRING, kCmdSettings, L"Open &Settings");

  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu_), L"&File");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu_), L"&View");
  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(browserMenu_), L"&Browser");

  SetMenu(hwnd_, menu_);

  // Ctrl+T, Ctrl+W, Ctrl+,, Ctrl+Shift+K, Ctrl+L, Alt+Home, Alt+Left/Right,
  // F5, F11, F12, Alt+F4, Escape (leaves full screen).
  static const WORD kAccels[] = {
      FVIRTKEY, static_cast<WORD>(VK_CONTROL), 'T',
      FVIRTKEY, static_cast<WORD>(VK_CONTROL), 'W',
      FVIRTKEY, static_cast<WORD>(VK_CONTROL), VK_OEM_COMMA,
      FVIRTKEY, static_cast<WORD>(VK_CONTROL), VK_SHIFT, 'K',
      FVIRTKEY, static_cast<WORD>(VK_CONTROL), 'L',
      FVIRTKEY, static_cast<WORD>(VK_MENU),    VK_HOME,
      FVIRTKEY, static_cast<WORD>(VK_MENU),    VK_LEFT,
      FVIRTKEY, static_cast<WORD>(VK_MENU),    VK_RIGHT,
      FVIRTKEY, static_cast<WORD>(VK_F5),      0,
      FVIRTKEY, static_cast<WORD>(VK_F11),     0,
      FVIRTKEY, static_cast<WORD>(VK_F12),     0,
      FVIRTKEY, static_cast<WORD>(VK_F4),      0,
      FVIRTKEY, static_cast<WORD>(VK_ESCAPE),  0,
      0};
  accelerators_ = CreateAcceleratorTableW(kAccels, static_cast<int>(std::size(kAccels) - 1));
}

void BrowserWindow::createControls() {
  const DWORD navStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;

  backButton_ = CreateWindowExW(0, L"BUTTON", L"Back", navStyle, 0, 0, 0, 0, hwnd_,
                                reinterpret_cast<HMENU>(kCmdBack), instance_, nullptr);
  forwardButton_ = CreateWindowExW(0, L"BUTTON", L"Forward", navStyle, 0, 0, 0, 0, hwnd_,
                                   reinterpret_cast<HMENU>(kCmdForward), instance_, nullptr);
  reloadButton_ = CreateWindowExW(0, L"BUTTON", L"Reload", navStyle, 0, 0, 0, 0, hwnd_,
                                  reinterpret_cast<HMENU>(kCmdReload), instance_, nullptr);
  stopButton_ = CreateWindowExW(0, L"BUTTON", L"Stop", navStyle, 0, 0, 0, 0, hwnd_,
                                reinterpret_cast<HMENU>(kCmdStop), instance_, nullptr);
  homeButton_ = CreateWindowExW(0, L"BUTTON", L"Home", navStyle, 0, 0, 0, 0, hwnd_,
                                reinterpret_cast<HMENU>(kCmdGoHome), instance_, nullptr);
  newTabButton_ = CreateWindowExW(0, L"BUTTON", L"New Tab", navStyle, 0, 0, 0, 0, hwnd_,
                                  reinterpret_cast<HMENU>(kCmdNewTab), instance_, nullptr);

  addressBar_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                hwnd_, reinterpret_cast<HMENU>(kIdAddressBar), instance_, nullptr);
  // An edit control reports no WM_COMMAND, so Enter is caught by a subclass.
  SetWindowSubclass(addressBar_, &BrowserWindow::addressBarProc, kSubclassId,
                     reinterpret_cast<DWORD_PTR>(this));

  tabStrip_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
                                                               TCS_TABS | TCS_OWNERDRAWFIXED,
                              0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kIdTabStrip), instance_,
                              nullptr);
  SetWindowSubclass(tabStrip_, &BrowserWindow::tabStripProc, kSubclassId,
                    reinterpret_cast<DWORD_PTR>(this));

  statusLabel_ = CreateWindowExW(0, L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0,
                                 0, hwnd_, nullptr, instance_, nullptr);

  tabImageList_ = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 8);
  if (tabImageList_ && tabStrip_) {
    TabStrip_SetImageList(tabStrip_, tabImageList_.Get());
  }
}

void BrowserWindow::layoutControls() {
  if (hwnd_ == nullptr || addressBar_ == nullptr) return;

  RECT client{};
  GetClientRect(hwnd_, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;

  int y = menu_ ? GetSystemMetrics(SM_CYMENU) : 0;

  // Navigation row: five buttons, address bar, new tab button.
  int x = kGap;
  for (HWND button : {backButton_, forwardButton_, reloadButton_, stopButton_, homeButton_}) {
    if (button == nullptr) continue;
    MoveWindow(button, x, y + 2, kButtonWidth, kNavHeight - 4, TRUE);
    x += kButtonWidth + kGap;
  }

  const int addressLeft = x;
  const int newTabLeft = width - kNewTabWidth - kGap;
  const int addressWidth = std::max(40, newTabLeft - kGap - addressLeft);
  MoveWindow(addressBar_, addressLeft, y + 2, addressWidth, kNavHeight - 4, TRUE);
  MoveWindow(newTabButton_, newTabLeft, y + 2, kNewTabWidth, kNavHeight - 4, TRUE);

  y += kNavHeight + kGap;

  MoveWindow(tabStrip_, 0, y, width, kTabStripHeight, TRUE);
  y += kTabStripHeight + kGap;

  MoveWindow(statusLabel_, kGap, y, std::max(10, width - 170), kStatusHeight, TRUE);
  y += kStatusHeight;

  const int contentHeight = height - y;
  for (const auto& tab : tabs_) {
    if (tab->hwnd() == nullptr) continue;
    MoveWindow(tab->hwnd(), 0, y, width, std::max(0, contentHeight), TRUE);
  }

  InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Tab management
// ---------------------------------------------------------------------------

Tab* BrowserWindow::addTab(TabKind kind, const std::string& url, const std::string& label,
                           bool makeCurrent) {
  if (hwnd_ == nullptr) return nullptr;

  auto tab = std::make_unique<Tab>(this, kind, label);
  Tab* raw = tab.get();
  tabs_.push_back(std::move(tab));
  tabIcons_.push_back(nullptr);

  raw->create(hwnd_, environment_.Get());

  if (kind == TabKind::Settings) {
    raw->navigateToString(buildSettingsHtml(), kSettingsUrl);
  } else if (url.empty()) {
    raw->navigate(settings_.homeUrl);
  } else {
    raw->navigate(url);
  }

  updateTabStrip();
  if (makeCurrent) setCurrentIndex(static_cast<int>(tabs_.size()) - 1);
  return raw;
}

void BrowserWindow::closeTabAt(int index) {
  if (index < 0 || index >= static_cast<int>(tabs_.size())) return;

  // Closing the last tab closes the window, matching the Python build.
  if (tabs_.size() == 1) {
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    return;
  }

  tabs_[index]->close();
  tabs_.erase(tabs_.begin() + index);
  if (index < static_cast<int>(tabIcons_.size())) tabIcons_.erase(tabIcons_.begin() + index);

  updateTabStrip();

  const int count = static_cast<int>(tabs_.size());
  setCurrentIndex(index >= count ? count - 1 : index);
  layoutControls();
}

void BrowserWindow::closeTabObject(Tab* tab) {
  const int index = indexOfTab(tab);
  if (index >= 0) closeTabAt(index);
}

int BrowserWindow::indexOfTab(Tab* tab) const {
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (tabs_[i].get() == tab) return static_cast<int>(i);
  }
  return -1;
}

int BrowserWindow::currentIndex() const {
  return tabStrip_ ? TabStrip_GetCurSel(tabStrip_) : -1;
}

Tab* BrowserWindow::currentTab() const {
  const int index = currentIndex();
  if (index < 0 || index >= static_cast<int>(tabs_.size())) return nullptr;
  return tabs_[index].get();
}

void BrowserWindow::setCurrentIndex(int index) {
  if (tabStrip_ == nullptr) return;
  if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
  TabStrip_SetCurSel(tabStrip_, index);
  showCurrentTab();
}

void BrowserWindow::showCurrentTab() {
  const int selected = currentIndex();
  for (size_t i = 0; i < tabs_.size(); ++i) {
    tabs_[i]->setVisible(static_cast<int>(i) == selected);
  }
  layoutControls();
  updateAddressBar();
  updateNavigationButtons();
  updateWindowTitle();
}

// ---------------------------------------------------------------------------
// Chrome state
// ---------------------------------------------------------------------------

void BrowserWindow::updateAddressBar() {
  if (addressBar_ == nullptr) return;

  Tab* tab = currentTab();
  std::wstring text;
  if (tab != nullptr) {
    if (tab->kind() == TabKind::Settings) {
      text = widen(std::string(kSettingsUrl));
    } else {
      const std::string url = tab->displayUrl();
      text = widen(isHomeUrl(url, settings_.homeUrl) ? std::string() : url);
    }
  }

  suppressAddressBarEvents_ = true;
  SetWindowTextW(addressBar_, text.c_str());
  suppressAddressBarEvents_ = false;
}

void BrowserWindow::updateNavigationButtons() {
  Tab* tab = currentTab();
  const bool isBrowser = tab != nullptr && tab->kind() == TabKind::Browser;
  ICoreWebView2* webview = isBrowser ? tab->webview() : nullptr;

  BOOL canBack = FALSE;
  BOOL canForward = FALSE;
  if (webview != nullptr) {
    webview->get_CanGoBack(&canBack);
    webview->get_CanGoForward(&canForward);
  }

  const bool haveWebview = webview != nullptr;
  EnableWindow(backButton_, haveWebview && canBack != FALSE);
  EnableWindow(forwardButton_, haveWebview && canForward != FALSE);
  EnableWindow(reloadButton_, haveWebview);
  EnableWindow(stopButton_, haveWebview);
  EnableWindow(homeButton_, isBrowser);

  enableItem(browserMenu_, kCmdBack, haveWebview && canBack != FALSE);
  enableItem(browserMenu_, kCmdForward, haveWebview && canForward != FALSE);
  enableItem(browserMenu_, kCmdReload, haveWebview);
  enableItem(browserMenu_, kCmdStop, haveWebview);
  enableItem(browserMenu_, kCmdGoHome, isBrowser);
  enableItem(browserMenu_, kCmdDuplicateTab, isBrowser);
  enableItem(viewMenu_, kCmdDevTools, haveWebview);
}

void BrowserWindow::updateTabStrip() {
  if (tabStrip_ == nullptr) return;

  const int previous = currentIndex();

  TabStrip_DeleteAllItems(tabStrip_);
  if (tabIcons_.size() != tabs_.size()) tabIcons_.assign(tabs_.size(), nullptr);

  for (size_t i = 0; i < tabs_.size(); ++i) {
    const Tab* tab = tabs_[i].get();
    const std::string text =
        tab->kind() == TabKind::Settings ? std::string("Settings") : tab->label();

    TCITEMW item{};
    item.mask = TCIF_TEXT | TCIFIMAGE;
    item.iImage = -1;
    item.pszText = widen(text.empty() ? std::string("New Tab") : text).c_str();
    TabStrip_InsertItem(tabStrip_, static_cast<int>(i), &item);
  }

  const int count = static_cast<int>(tabs_.size());
  if (count > 0) {
    TabStrip_SetCurSel(tabStrip_, (previous >= 0 && previous < count) ? previous : count - 1);
  }

  // Re-inserting the items clears their image indices, so restore the icons.
  rebuildTabIcons();
  updateNavigationButtons();
}

void BrowserWindow::rebuildTabIcons() {
  if (tabImageList_ == nullptr) return;
  ImageList_RemoveAll(tabImageList_);
  for (size_t i = 0; i < tabIcons_.size(); ++i) {
    int index = -1;
    if (tabIcons_[i] != nullptr) {
      index = ImageList_Add(tabImageList_.Get(), tabIcons_[i]);
    }
    TCITEMW item{};
    item.mask = TCIFIMAGE;
    item.iImage = index;
    TabStrip_SetItem(tabStrip_, static_cast<int>(i), &item);
  }
}

void BrowserWindow::updateWindowTitle() {
  Tab* tab = currentTab();
  if (tab == nullptr) {
    SetWindowTextW(hwnd_, kAppName);
    return;
  }
  if (tab->kind() == TabKind::Settings) {
    SetWindowTextW(hwnd_, L"Settings - OpenBrowse");
    return;
  }
  const std::wstring title = widen(tab->title().empty() ? std::string(kAppName) : tab->title());
  SetWindowTextW(hwnd_, (title + L" - " + widen(std::string(kAppName))).c_str());
}

void BrowserWindow::setStatus(const std::string& text) {
  statusText_ = text;
  if (statusLabel_ != nullptr) SetWindowTextW(statusLabel_, widen(text).c_str());
}

void BrowserWindow::setProgress(int percent) {
  if (progress_ == percent) return;
  progress_ = percent;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void BrowserWindow::executeCommand(UINT command) {
  switch (command) {
    case kCmdNewTab:
      addTab(TabKind::Browser, "", "New Tab", true);
      executeCommand(kCmdFocusAddressBar);
      break;

    case kCmdCloseTab:
      closeTabAt(currentIndex());
      break;

    case kCmdSettings:
      openSettingsTab();
      break;

    case kCmdQuit:
      PostMessageW(hwnd_, WM_CLOSE, 0, 0);
      break;

    case kCmdDevTools:
      openDevTools();
      break;

    case kCmdFullScreen:
      toggleFullScreen();
      break;

    case kCmdDuplicateTab:
      duplicateCurrentTab();
      break;

    case kCmdGoHome:
      goHome();
      break;

    case kCmdFocusAddressBar:
      if (addressBar_ != nullptr) {
        SetFocus(addressBar_);
        SendMessageW(addressBar_, EM_SETSEL, 0, -1);
      }
      break;

    case kCmdBack:
    case kCmdForward:
    case kCmdReload:
    case kCmdStop: {
      Tab* tab = currentTab();
      if (tab == nullptr) break;
      if (tab->kind() == TabKind::Settings) {
        if (command == kCmdReload) {
          refreshSettingsTab();
          setStatus("Settings reloaded");
        }
        break;
      }
      switch (command) {
        case kCmdBack: tab->goBack(); break;
        case kCmdForward: tab->goForward(); break;
        case kCmdReload: tab->reload(); break;
        case kCmdStop: tab->stop(); break;
        default: break;
      }
      break;
    }

    default:
      break;
  }
}

void BrowserWindow::loadFromAddressBar() {
  const int length = GetWindowTextLengthW(addressBar_);
  std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
  if (length > 0) GetWindowTextW(addressBar_, buffer.data(), length + 1);
  buffer.resize(static_cast<size_t>(length));

  const std::string text = narrow(buffer);

  if (toLower(trim(text)) == kSettingsUrl) {
    openSettingsTab();
    return;
  }

  Tab* tab = currentTab();
  if (tab == nullptr || tab->kind() == TabKind::Settings) {
    tab = addTab(TabKind::Browser, "", "New Tab", true);
  }
  if (tab == nullptr) return;

  const std::string target = ensureScheme(text, settings_.homeUrl, settings_.searchUrl);
  tab->setDisplayUrl(target);
  tab->navigate(target);
  SetFocus(addressBar_);
}

void BrowserWindow::goHome() {
  Tab* tab = currentTab();
  if (tab != nullptr && tab->kind() == TabKind::Browser) {
    tab->setDisplayUrl(settings_.homeUrl);
    tab->navigate(settings_.homeUrl);
    return;
  }
  addTab(TabKind::Browser, settings_.homeUrl, "Home", true);
}

void BrowserWindow::duplicateCurrentTab() {
  Tab* tab = currentTab();
  if (tab == nullptr || tab->kind() != TabKind::Browser) return;
  addTab(TabKind::Browser, tab->displayUrl(), "Duplicate", true);
}

void BrowserWindow::openDevTools() {
  Tab* tab = currentTab();
  if (tab == nullptr || tab->kind() != TabKind::Browser) {
    setStatus("DevTools only works on web tabs.");
    return;
  }
  tab->openDevTools();
}

void BrowserWindow::toggleFullScreen() {
  if (!fullScreen_) {
    savedPlacement_.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd_, &savedPlacement_);

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
      SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
      SetWindowPos(hwnd_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                   monitor.rcMonitor.right - monitor.rcMonitor.left,
                   monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                   SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullScreen_ = true;
  } else {
    SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    if (savedPlacement_.length == sizeof(WINDOWPLACEMENT)) {
      SetWindowPlacement(hwnd_, &savedPlacement_);
    }
    fullScreen_ = false;
  }
  layoutControls();
}

Tab* BrowserWindow::settingsTab() const {
  for (const auto& tab : tabs_) {
    if (tab->kind() == TabKind::Settings) return tab.get();
  }
  return nullptr;
}

void BrowserWindow::openSettingsTab() {
  Tab* existing = settingsTab();
  if (existing != nullptr) {
    refreshSettingsTab();
    setCurrentIndex(indexOfTab(existing));
    return;
  }
  addTab(TabKind::Settings, "", "Settings", true);
}

void BrowserWindow::refreshSettingsTab() {
  Tab* tab = settingsTab();
  if (tab == nullptr) return;
  tab->navigateToString(buildSettingsHtml(), kSettingsUrl);
}

std::string BrowserWindow::buildSettingsHtml() { return settingsHtml(settings_, history_); }

void BrowserWindow::postToSettingsPage(Tab* tab, const std::string& json) {
  if (tab == nullptr || tab->webview() == nullptr) return;
  const std::wstring wide = widen(json);
  tab->webview()->PostWebMessageAsJson(wide.c_str());
}

void BrowserWindow::sendStatusTo(Tab* tab, const std::string& text, const std::string& level) {
  postToSettingsPage(tab, "{\"kind\":\"status\",\"text\":" +
                              json::serialize(json::Value(text)) + ",\"level\":" +
                              json::serialize(json::Value(level)) + "}");
}

void BrowserWindow::sendHistoryTo(Tab* tab) {
  postToSettingsPage(tab, "{\"kind\":\"history\",\"rows\":" +
                              json::serialize(json::Value(historyRows(history_))) + "}");
}

// ---------------------------------------------------------------------------
// Tab callbacks
// ---------------------------------------------------------------------------

void BrowserWindow::onTabUrlChanged(Tab* tab, const std::string& url) {
  if (tab != currentTab()) return;
  suppressAddressBarEvents_ = true;
  SetWindowTextW(addressBar_,
                 widen(isHomeUrl(url, settings_.homeUrl) ? std::string() : url).c_str());
  suppressAddressBarEvents_ = false;
}

void BrowserWindow::onTabSourceChanged(Tab* tab, const std::string& url) {
  if (tab != currentTab()) return;
  onTabUrlChanged(tab, url);
  updateNavigationButtons();
}

void BrowserWindow::onTabTitleChanged(Tab* tab) {
  updateTabStrip();
  if (tab == currentTab()) updateWindowTitle();
}

void BrowserWindow::onTabIconChanged(Tab* tab, HICON icon) {
  const int index = indexOfTab(tab);
  if (index < 0) {
    if (icon) DestroyIcon(icon);
    return;
  }
  if (tabIcons_[index] != nullptr) DestroyIcon(tabIcons_[index]);
  tabIcons_[index] = icon;
  rebuildTabIcons();
}

void BrowserWindow::onTabNavigationStarted(Tab* tab) {
  if (tab != currentTab()) return;
  setStatus("Loading...");
  setProgress(10);
}

void BrowserWindow::onTabNavigationCompleted(Tab* tab, const std::string& url, bool success) {
  if (tab != currentTab()) return;
  setStatus(success ? "Ready" : "Page failed to load");
  setProgress(-1);
  updateNavigationButtons();
  if (success && tab->kind() == TabKind::Browser) recordHistory(tab, url);
}

void BrowserWindow::onTabContentLoading(Tab* tab, const std::string& url) {
  if (tab != currentTab()) return;
  setProgress(url.empty() ? 60 : 90);
}

void BrowserWindow::onTabNewWindowRequested(Tab* tab) {
  addTab(TabKind::Browser, "", "New Tab", true);
  setStatus("Opened link in a new tab");
}

void BrowserWindow::onTabCloseRequested(Tab* tab) { closeTabObject(tab); }

void BrowserWindow::onTabProcessFailed(Tab* tab, int failedKind) {
  static const char* kNames[] = {
      "Browser process exited",   "Render process exited",
      "Frame render process exited", "Utility process exited",
      "Sandbox helper process exited", "GPU process exited",
      "PPAPI plugin process exited", "PPAPI broker process exited",
      "Unknown process failed"};
  const char* name = (failedKind >= 0 && failedKind < static_cast<int>(std::size(kNames)))
                         ? kNames[failedKind]
                         : "Process failed";
  if (tab == currentTab()) {
    setStatus(std::string(name) + " - try reloading this tab");
  }
}

// ---------------------------------------------------------------------------
// History, preferences and browsing data
// ---------------------------------------------------------------------------

void BrowserWindow::recordHistory(Tab* tab, const std::string& url) {
  HistoryStore::record(&history_, url, tab->title());
  historyStore_.save(history_);

  Tab* page = settingsTab();
  if (page != nullptr && page != tab) sendHistoryTo(page);
}

void BrowserWindow::clearBrowsingData(bool history, bool cookies, bool cache) {
  if (history) {
    HistoryStore::clear(&history_);
    historyStore_.save(history_);
  }

  if (!cookies && !cache) return;

  Tab* tab = currentTab();
  if (tab == nullptr || tab->controller() == nullptr) return;

  ComPtr<ICoreWebView2Profile> profile;
  if (FAILED(tab->controller()->get_Profile(&profile)) || profile == nullptr) return;

  // ClearBrowsingData lives on ICoreWebView2Profile2, not ICoreWebView2Profile.
  ComPtr<ICoreWebView2Profile2> profile2;
  if (FAILED(profile.As(&profile2)) || profile2 == nullptr) {
    setStatus("Clearing cookies and cache needs a newer WebView2 runtime");
    return;
  }

  COREWEBVIEW2_BROWSING_DATA_KINDS kinds = COREWEBVIEW2_BROWSING_DATA_KINDS(0);
  if (cookies) kinds |= COREWEBVIEW2_BROWSING_DATA_KINDS_COOKIES;
  if (cache) kinds |= COREWEBVIEW2_BROWSING_DATA_KINDS_DISK_CACHE;
  if (kinds == COREWEBVIEW2_BROWSING_DATA_KINDS(0)) return;

  profile2->ClearBrowsingData(kinds, nullptr);
}

void BrowserWindow::applyPreferences(const AppSettings& updated, bool restartNeeded) {
  const std::string previousHome = settings_.homeUrl;

  settings_ = updated;
  settings_.normalizeHomeUrl();
  settings_.normalizeSearchUrl();
  settingsStore_.save(settings_);

  // A tab sitting on the old home page follows the new one.
  Tab* tab = currentTab();
  if (tab != nullptr && tab->kind() == TabKind::Browser &&
      isHomeUrl(tab->displayUrl(), previousHome)) {
    tab->setDisplayUrl(settings_.homeUrl);
    tab->navigate(settings_.homeUrl);
  }

  updateAddressBar();
  updateWindowTitle();
  (void)restartNeeded;
}

// ---------------------------------------------------------------------------
// Settings page protocol
// ---------------------------------------------------------------------------

void BrowserWindow::onSettingsMessage(Tab* tab, const std::string& message) {
  handleSettingsAction(tab, message);
}

void BrowserWindow::handleSettingsAction(Tab* tab, const std::string& message) {
  json::Value payload;
  try {
    payload = json::parse(message);
  } catch (const std::exception&) {
    return;
  }
  if (!payload.isObject()) return;

  const std::string action = payload.str("action");

  if (action == msg::kSave) {
    AppSettings updated = settings_;
    updated.homeUrl = normalizeHomeUrl(payload.str("home_url", settings_.homeUrl));
    updated.searchUrl = payload.str("search_url", settings_.searchUrl);
    updated.javascriptEnabled = payload.boolean("javascript", settings_.javascriptEnabled);
    updated.smoothScrollingEnabled =
        payload.boolean("smooth_scrolling", settings_.smoothScrollingEnabled);
    updated.hardwareAccelerationEnabled =
        payload.boolean("hardware_acceleration", settings_.hardwareAccelerationEnabled);

    // These three are Chromium launch flags, so they need a restart.
    const bool restartNeeded = updated.javascriptEnabled != settings_.javascriptEnabled ||
                               updated.smoothScrollingEnabled != settings_.smoothScrollingEnabled ||
                               updated.hardwareAccelerationEnabled !=
                                   settings_.hardwareAccelerationEnabled;

    applyPreferences(updated, restartNeeded);
    sendStatusTo(tab, restartNeeded ? "Settings saved. Restart OpenBrowse for JavaScript, "
                                     "scrolling, and graphics changes to apply."
                                   : "Settings saved.",
                 "ok");
    return;
  }

  if (action == msg::kClearHistory) {
    clearBrowsingData(true, false, false);
    sendStatusTo(tab, "Saved history was cleared.", "ok");
    sendHistoryTo(tab);
    return;
  }

  if (action == msg::kClearCookies) {
    clearBrowsingData(false, true, false);
    sendStatusTo(tab, "Cookies were cleared.", "ok");
    return;
  }

  if (action == msg::kClearCache) {
    clearBrowsingData(false, false, true);
    sendStatusTo(tab, "Cache was cleared.", "ok");
    return;
  }

  if (action == msg::kClearAll) {
    clearBrowsingData(true, true, true);
    sendStatusTo(tab, "History, cookies, and cache were cleared.", "ok");
    sendHistoryTo(tab);
    return;
  }

  if (action == msg::kRefreshHistory) {
    refreshingSettings_ = true;
    refreshSettingsTab();
    refreshingSettings_ = false;
    sendStatusTo(tab, "History refreshed", "ok");
    return;
  }

  if (action == msg::kOpenHistory) {
    const std::string url = payload.str("url");
    if (!url.empty()) addTab(TabKind::Browser, url, "History", true);
    return;
  }
}

// ---------------------------------------------------------------------------
// Downloads
// ---------------------------------------------------------------------------

void BrowserWindow::onDownloadStarting(Tab* tab, ICoreWebView2DownloadStartingEventArgs* args) {
  if (args == nullptr) return;

  PWSTR rawPath = nullptr;
  if (FAILED(args->get_ResultFilePath(&rawPath))) return;
  std::wstring suggestedW = rawPath ? std::wstring(rawPath) : std::wstring();
  if (rawPath) CoTaskMemFree(rawPath);

  const std::string suggested = narrow(suggestedW);
  const std::string fileName = baseName(suggested);
  const std::string initial = suggested.empty() ? downloadsDir() + "\\download" : suggested;

  std::wstring fileBuffer(MAX_PATH * 4, L'\0');
  std::wstring initialW = widen(initial);
  if (initialW.size() >= fileBuffer.size()) initialW = L"download";
  std::copy(initialW.begin(), initialW.end(), fileBuffer.begin());

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd_;
  ofn.lpstrFilter = L"All Files (*.*)\0\0";
  ofn.lpstrFile = fileBuffer.data();
  ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
  ofn.lpstrTitle = L"Save Download";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

  if (!GetSaveFileNameW(&ofn)) {
    args->put_Cancel(TRUE);
    setStatus("Download cancelled");
    return;
  }

  args->put_ResultFilePath(widen(narrow(fileBuffer)).c_str());
  // We choose the destination and report progress ourselves.
  args->put_Handled(TRUE);

  ActiveDownload record;
  record.fileName = fileName;
  if (FAILED(args->get_DownloadOperation(&record.operation)) || !record.operation) {
    setStatus("Download could not be started");
    return;
  }

  // There is no Completed event on the starting arguments, so completion is
  // tracked through the operation's state instead.
  ComPtr<ICoreWebView2DownloadOperation> operation = record.operation;
  BrowserWindow* self = this;
  operation->add_StateChanged(
      Callback<ICoreWebView2StateChangedEventHandler>(
          [self, operation](ICoreWebView2DownloadOperation* sender, IUnknown* /*args*/)
              -> HRESULT {
            COREWEBVIEW2_DOWNLOAD_STATE state = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
            if (FAILED(sender->get_State(&state))) return S_OK;
            if (state == COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS) return S_OK;
            self->onDownloadCompleted(operation.Get(), state);
            return S_OK;
          })
          .Get(),
      nullptr);

  downloads_.push_back(record);
  setStatus("Downloading: " + fileName);
}

void BrowserWindow::onDownloadCompleted(ICoreWebView2DownloadOperation* operation,
                                        COREWEBVIEW2_DOWNLOAD_STATE state) {
  for (size_t i = 0; i < downloads_.size(); ++i) {
    if (downloads_[i].operation.Get() == operation) {
      const bool completed = state == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED;
      setStatus(completed ? "Downloaded: " + downloads_[i].fileName
                          : "Download interrupted: " + downloads_[i].fileName);
      downloads_.erase(downloads_.begin() + static_cast<ptrdiff_t>(i));
      return;
    }
  }
}

// Tab strip subclass: owner draw, click to select, click the box to close
// ---------------------------------------------------------------------------

LRESULT CALLBACK BrowserWindow::tabStripProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR id, DWORD_PTR refData) {
  BrowserWindow* self = reinterpret_cast<BrowserWindow*>(refData);
  if (self == nullptr) return DefSubclassProc(hwnd, message, wParam, lParam);

  switch (message) {
    case WM_LBUTTONDOWN: {
      POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const int count = TabStrip_GetItemCount(hwnd);

      // Topmost visible item first, since tabs can overlap in compact layouts.
      for (int i = 0; i < count; ++i) {
        RECT itemRect{};
        if (!TabStrip_GetItemRect(hwnd, i, &itemRect)) continue;
        POINT local = point;
        ScreenToClient(hwnd, &local);
        if (!PtInRect(&itemRect, point)) continue;

        const int closeLeft = itemRect.right - kTabCloseBox - kGap;
        RECT closeRect{closeLeft, itemRect.top, itemRect.right - kGap, itemRect.bottom};
        if (PtInRect(&closeRect, point)) {
          self->closeTabAt(i);
          return 0;
        }
        self->setCurrentIndex(i);
        return 0;
      }
      return 0;
    }

    case WM_LBUTTONDBLCLK: {
      POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const int count = TabStrip_GetItemCount(hwnd);
      for (int i = 0; i < count; ++i) {
        RECT itemRect{};
        if (!TabStrip_GetItemRect(hwnd, i, &itemRect)) continue;
        if (PtInRect(&itemRect, point)) {
          self->setCurrentIndex(i);
          self->executeCommand(kCmdNewTab);
          return 0;
        }
      }
      return 0;
    }

    case WM_MOUSEMOVE: {
      TRACKMOUSEEVENT track{};
      track.cbSize = sizeof(track);
      track.dwFlags = TME_LEAVE;
      track.hwndTrack = hwnd;
      TrackMouseEvent(&track);

      POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const int count = TabStrip_GetItemCount(hwnd);
      bool overClose = false;
      for (int i = 0; i < count && !overClose; ++i) {
        RECT itemRect{};
        if (!TabStrip_GetItemRect(hwnd, i, &itemRect)) continue;
        if (!PtInRect(&itemRect, point)) continue;
        RECT closeRect{itemRect.right - kTabCloseBox - kGap, itemRect.top, itemRect.right - kGap,
                       itemRect.bottom};
        overClose = PtInRect(&closeRect, point);
      }
      SetCursor(LoadCursorW(nullptr, overClose ? IDC_HAND : IDC_ARROW));
      return 0;
    }

    default:
      break;
  }

  return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK BrowserWindow::addressBarProc(HWND hwnd, UINT message, WPARAM wParam,
                                                LPARAM lParam, UINT_PTR id, DWORD_PTR refData) {
  BrowserWindow* self = reinterpret_cast<BrowserWindow*>(refData);
  if (self == nullptr) return DefSubclassProc(hwnd, message, wParam, lParam);

  if (message == WM_KEYDOWN && wParam == VK_RETURN) {
    self->loadFromAddressBar();
    return 0;
  }
  return DefSubclassProc(hwnd, message, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK BrowserWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  BrowserWindow* self = reinterpret_cast<BrowserWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) return DefWindowProcW(hwnd, message, wParam, lParam);
  return self->handleMessage(message, wParam, lParam);
}

LRESULT BrowserWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      return 0;

    case WM_SIZE:
      layoutControls();
      return 0;

    case WM_GETMINMAXINFO: {
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
      info->ptMinTrackSize.x = 640;
      info->ptMinTrackSize.y = 420;
      return 0;
    }

    case WM_COMMAND: {
      if (HIWORD(wParam) == BN_CLICKED) executeCommand(LOWORD(wParam));
      return 0;
    }

    case WM_NOTIFY: {
      // NM_CUSTOMDRAW is delivered to the parent, so the owner-drawn tab strip
      // is painted from here rather than from the control subclass.
      NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
      if (header->idFrom != kIdTabStrip || header->code != NM_CUSTOMDRAW) return 0;

      NMTABCTRL* draw = reinterpret_cast<NMTABCTRL*>(lParam);
      if (draw->nmcd.dwDrawStage == NMCDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
      if (draw->nmcd.dwDrawStage != NMCDS_ITEMPREPAINT) return CDRF_DODEFAULT;

      const int index = static_cast<int>(draw->nmcd.dwItemSpec);
      const bool selected = (draw->nmcd.uItemState & CDIS_SELECTED) != 0;
      const bool active = index == currentIndex();

      RECT rect = draw->nmcd.rc;
      HDC dc = draw->nmcd.hdc;

      const HBRUSH background =
          CreateSolidBrush(active ? RGB(255, 255, 255) : RGB(226, 226, 226));
      if (background) {
        FillRect(dc, &rect, background);
        DeleteObject(background);
      }

      if (selected) {
        RECT accent = rect;
        accent.bottom = accent.top + 2;
        const HBRUSH bar = CreateSolidBrush(RGB(31, 136, 61));
        if (bar) {
          FillRect(dc, &accent, bar);
          DeleteObject(bar);
        }
      }

      // Label, clipped so it never runs under the close box.
      Tab* tab =
          (index >= 0 && index < static_cast<int>(tabs_.size())) ? tabs_[index].get() : nullptr;
      std::wstring label = tab != nullptr ? widen(tab->label()) : std::wstring();
      if (label.empty()) label = L"New Tab";

      RECT textRect = rect;
      textRect.left += kTabPadLeft;
      textRect.right -= kTabPadRight;
      DrawTextW(dc, label.c_str(), -1, &textRect,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

      // Close box: an X on the active tab, a dash otherwise.
      const RECT closeRect{rect.right - kTabCloseBox - kGap, rect.top, rect.right - kGap,
                           rect.bottom};
      const HPEN pen = CreatePen(PS_SOLID, 1, RGB(96, 96, 96));
      const HPEN oldPen = SelectObject(dc, pen);
      if (active) {
        const int midX = (closeRect.left + closeRect.right) / 2;
        const int midY = (closeRect.top + closeRect.bottom) / 2;
        MoveToEx(dc, midX - 3, midY - 3, nullptr);
        LineTo(dc, midX + 3, midY + 3);
        MoveToEx(dc, midX + 3, midY - 3, nullptr);
        LineTo(dc, midX - 3, midY + 3);
      } else {
        const int midY = (closeRect.top + closeRect.bottom) / 2;
        MoveToEx(dc, closeRect.left + 2, midY, nullptr);
        LineTo(dc, closeRect.right - 2, midY);
      }
      SelectObject(dc, oldPen);
      DeleteObject(pen);

      return CDRF_SKIPDEFAULTDRAW;
    }

    case WM_CTLCOLORSTATIC:
      SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
      SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_BTNTEXT));
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));

    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC dc = BeginPaint(hwnd_, &ps);
      RECT client{};
      GetClientRect(hwnd_, &client);

      if (progress_ >= 0) {
        RECT track{};
        track.left = client.right - 150;
        track.top = client.bottom - kStatusHeight + 5;
        track.right = client.right - 8;
        track.bottom = client.bottom - 5;

        FillRect(dc, &track, reinterpret_cast<HBRUSH>(GetStockObject(COLOR_BTNFACE)));
        FrameRect(dc, &track, static_cast<HBRUSH>(GetStockObject(COLOR_GRAYTEXT)));

        RECT fill = track;
        fill.left += 1;
        fill.top += 1;
        fill.bottom -= 1;
        const int usable = track.right - track.left - 2;
        fill.right = fill.left + (usable * std::clamp(progress_, 0, 100)) / 100;

        const HBRUSH accent = CreateSolidBrush(RGB(31, 136, 61));
        if (accent) {
          FillRect(dc, &fill, accent);
          DeleteObject(accent);
        }
      }
      EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_CLOSE: {
      if (tabs_.size() > 1) {
        const std::wstring prompt = L"Close " + std::to_wstring(tabs_.size()) + L" open tabs?";
        const int answer = MessageBoxW(hwnd_, prompt.c_str(), L"Close Browser",
                                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer != IDYES) return 0;
      }
      for (auto& tab : tabs_) tab->close();
      tabs_.clear();
      DestroyWindow(hwnd_);
      return 0;
    }

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }

  return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace ob