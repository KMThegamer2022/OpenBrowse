import json
import os
import sys
import traceback
from datetime import datetime
from pathlib import Path
from urllib.parse import quote_plus

from PyQt6.QtCore import QSettings, QStandardPaths, Qt, QUrl
from PyQt6.QtGui import QAction, QCloseEvent, QColor, QIcon, QKeySequence
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QStatusBar,
    QTabWidget,
    QToolBar,
    QVBoxLayout,
    QWidget,
)
from PyQt6.QtWebEngineCore import (
    QWebEngineDownloadRequest,
    QWebEnginePage,
    QWebEngineProfile,
    QWebEngineSettings,
)
from PyQt6.QtWebEngineWidgets import QWebEngineView


APP_NAME = "OpenBrowse"
ORG_NAME = "Independent"
DEFAULT_SEARCH_URL = "https://duckduckgo.com/?q={query}"
DEFAULT_HOME_URL = "https://landpg.kmtg.online"
BASE_DIR = Path(__file__).resolve().parent
HISTORY_LIMIT = 100
SETTINGS_URL = "openbrowse://settings"
SEARCH_PROVIDERS = {
    "DuckDuckGo": "https://duckduckgo.com/?q={query}",
    "Google": "https://www.google.com/search?q={query}",
    "Bing": "https://www.bing.com/search?q={query}",
    "Startpage": "https://www.startpage.com/search?q={query}",
}


def app_settings() -> QSettings:
    return QSettings(ORG_NAME, APP_NAME)


def env_flag_enabled(name: str) -> bool:
    return os.environ.get(name, "").strip().lower() in {"1", "true", "yes", "on"}


def settings_bool(settings: QSettings, key: str, default: bool) -> bool:
    value = settings.value(key, default)
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def runtime_data_dir() -> Path:
    appdata = QStandardPaths.writableLocation(
        QStandardPaths.StandardLocation.AppDataLocation
    )
    data_dir = Path(appdata) if appdata else BASE_DIR
    data_dir.mkdir(parents=True, exist_ok=True)
    return data_dir


def runtime_log_path() -> Path:
    return runtime_data_dir() / "startup-error.log"


def history_store_path() -> Path:
    return runtime_data_dir() / "history.json"


def load_history_entries() -> list[dict[str, str]]:
    path = history_store_path()
    if not path.exists():
        return []

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return []

    if not isinstance(data, list):
        return []

    cleaned: list[dict[str, str]] = []
    for entry in data:
        if not isinstance(entry, dict):
            continue
        url = str(entry.get("url", "")).strip()
        title = str(entry.get("title", "")).strip()
        visited_at = str(entry.get("visited_at", "")).strip()
        if url:
            cleaned.append(
                {
                    "url": url,
                    "title": title or url,
                    "visited_at": visited_at,
                }
            )
    return cleaned[:HISTORY_LIMIT]


def save_history_entries(entries: list[dict[str, str]]) -> None:
    history_store_path().write_text(
        json.dumps(entries[:HISTORY_LIMIT], indent=2),
        encoding="utf-8",
    )


def configured_home_url(settings: QSettings) -> str:
    home_url = str(settings.value("browser/home_url", DEFAULT_HOME_URL)).strip()
    return home_url or DEFAULT_HOME_URL


def configured_search_url(settings: QSettings) -> str:
    search_url = str(settings.value("browser/search_url", DEFAULT_SEARCH_URL)).strip()
    if "{query}" not in search_url:
        return DEFAULT_SEARCH_URL
    return search_url


def hardware_acceleration_enabled() -> bool:
    settings = app_settings()
    return settings_bool(settings, "graphics/hardware_acceleration", True)


def is_home_url(url: QUrl, home_url: str) -> bool:
    normalized = url.toString().rstrip("/")
    return normalized == home_url.rstrip("/")


def ensure_scheme(text: str, home_url: str, search_url: str) -> QUrl:
    text = text.strip()
    if not text:
        return QUrl(home_url)

    if text.lower() == SETTINGS_URL:
        return QUrl(text)

    url = QUrl.fromUserInput(text)
    if url.isValid() and not url.scheme():
        url.setScheme("https")

    has_space = " " in text
    looks_like_host = "." in text and " " not in text
    if has_space or (not looks_like_host and not url.scheme().startswith("http")):
        return QUrl(search_url.format(query=quote_plus(text)))

    return url


def configure_runtime() -> None:
    flags = os.environ.get("QTWEBENGINE_CHROMIUM_FLAGS", "").split()
    if "--disable-features=Translate" not in flags:
        flags.append("--disable-features=Translate")

    acceleration_enabled = hardware_acceleration_enabled()
    if env_flag_enabled("OPENBROWSE_FORCE_SOFTWARE_RENDERING"):
        acceleration_enabled = False

    QApplication.setAttribute(Qt.ApplicationAttribute.AA_ShareOpenGLContexts, True)

    if acceleration_enabled:
        # Let Qt WebEngine / Chromium choose the platform's preferred GPU backend.
        # On Windows this is typically ANGLE, which is often more stable than
        # forcing the desktop OpenGL driver path.
        pass
    else:
        for flag in ("--disable-gpu", "--disable-gpu-compositing"):
            if flag not in flags:
                flags.append(flag)
        QApplication.setAttribute(Qt.ApplicationAttribute.AA_UseSoftwareOpenGL, True)

    os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = " ".join(flags).strip()


def show_startup_error(error: BaseException) -> None:
    message = "".join(traceback.format_exception(error)).strip()
    log_path = runtime_log_path()
    log_path.write_text(message, encoding="utf-8")

    app = QApplication.instance()
    owns_app = app is None
    if owns_app:
        app = QApplication(sys.argv)

    QMessageBox.critical(
        None,
        f"{APP_NAME} failed to start",
        (
            "OpenBrowse couldn't start.\n\n"
            f"{error}\n\n"
            f"Details were saved to:\n{log_path}"
        ),
    )

    if owns_app and app is not None:
        app.quit()


class BrowserPage(QWebEnginePage):
    def __init__(self, profile: QWebEngineProfile, window: "BrowserWindow") -> None:
        super().__init__(profile, window)
        self.window = window

    def createWindow(self, window_type: QWebEnginePage.WebWindowType) -> QWebEnginePage:
        if window_type == QWebEnginePage.WebWindowType.WebBrowserTab:
            return self.window.add_tab(make_current=True).page()
        return self.window.add_tab(make_current=True).page()


class BrowserView(QWebEngineView):
    def __init__(self, window: "BrowserWindow") -> None:
        super().__init__()
        self.window = window

    def createWindow(self, window_type: QWebEnginePage.WebWindowType) -> QWebEngineView:
        if window_type == QWebEnginePage.WebWindowType.WebBrowserTab:
            return self.window.add_tab(make_current=True)
        return self.window.add_tab(make_current=True)


class SettingsPage(QWidget):
    def __init__(self, window: "BrowserWindow") -> None:
        super().__init__()
        self.window = window
        self.setProperty("kind", "settings")

        layout = QVBoxLayout(self)

        intro = QLabel("Manage browser behavior, browsing data, and performance.")
        intro.setWordWrap(True)
        layout.addWidget(intro)

        preferences_group = QGroupBox("Preferences")
        preferences_layout = QFormLayout(preferences_group)

        self.homepage_input = QLineEdit()
        self.search_engine_combo = QComboBox()
        for label, template in SEARCH_PROVIDERS.items():
            self.search_engine_combo.addItem(label, template)

        self.javascript_checkbox = QCheckBox("Enable JavaScript")
        self.smooth_scrolling_checkbox = QCheckBox("Enable smooth scrolling")
        self.hardware_acceleration_checkbox = QCheckBox(
            "Use hardware acceleration when available"
        )

        preferences_layout.addRow("Homepage", self.homepage_input)
        preferences_layout.addRow("Search engine", self.search_engine_combo)
        preferences_layout.addRow("", self.javascript_checkbox)
        preferences_layout.addRow("", self.smooth_scrolling_checkbox)
        preferences_layout.addRow("", self.hardware_acceleration_checkbox)

        restart_note = QLabel(
            "Graphics acceleration changes apply the next time OpenBrowse starts."
        )
        restart_note.setWordWrap(True)
        preferences_layout.addRow("", restart_note)

        save_button = QPushButton("Save Settings")
        save_button.clicked.connect(self.save_preferences)
        preferences_layout.addRow("", save_button)
        layout.addWidget(preferences_group)

        data_group = QGroupBox("Browsing Data")
        data_layout = QHBoxLayout(data_group)

        clear_history_button = QPushButton("Clear History")
        clear_history_button.clicked.connect(self.clear_history)
        data_layout.addWidget(clear_history_button)

        clear_cookies_button = QPushButton("Clear Cookies")
        clear_cookies_button.clicked.connect(self.clear_cookies)
        data_layout.addWidget(clear_cookies_button)

        clear_cache_button = QPushButton("Clear Cache")
        clear_cache_button.clicked.connect(self.clear_cache)
        data_layout.addWidget(clear_cache_button)

        clear_all_button = QPushButton("Clear All")
        clear_all_button.clicked.connect(self.clear_all)
        data_layout.addWidget(clear_all_button)
        layout.addWidget(data_group)

        history_group = QGroupBox("Recent History")
        history_layout = QVBoxLayout(history_group)

        self.history_list = QListWidget()
        self.history_list.itemDoubleClicked.connect(self.open_history_item)
        history_layout.addWidget(self.history_list)

        refresh_button = QPushButton("Refresh History")
        refresh_button.clicked.connect(self.refresh_history)
        history_layout.addWidget(refresh_button)
        layout.addWidget(history_group)

        self.status_label = QLabel("")
        self.status_label.setWordWrap(True)
        layout.addWidget(self.status_label)
        layout.addStretch(1)

        self.reload_preferences()
        self.refresh_history()

    def reload_preferences(self) -> None:
        self.homepage_input.setText(self.window.home_url)
        index = self.search_engine_combo.findData(self.window.search_url)
        if index >= 0:
            self.search_engine_combo.setCurrentIndex(index)
        else:
            self.search_engine_combo.setCurrentIndex(0)
        self.javascript_checkbox.setChecked(self.window.javascript_enabled)
        self.smooth_scrolling_checkbox.setChecked(
            self.window.smooth_scrolling_enabled
        )
        self.hardware_acceleration_checkbox.setChecked(
            self.window.hardware_acceleration_enabled
        )

    def refresh_history(self) -> None:
        self.history_list.clear()
        if not self.window.history_entries:
            self.history_list.addItem("No saved history yet.")
            return

        for entry in self.window.history_entries:
            title = entry.get("title", entry["url"])
            timestamp = entry.get("visited_at", "")
            label = f"{title}\n{entry['url']}"
            if timestamp:
                label += f"\nVisited: {timestamp}"
            item = QListWidgetItem(label)
            item.setData(Qt.ItemDataRole.UserRole, entry["url"])
            self.history_list.addItem(item)

    def save_preferences(self) -> None:
        home_url = self.homepage_input.text().strip() or DEFAULT_HOME_URL
        search_url = self.search_engine_combo.currentData()
        if not isinstance(search_url, str) or "{query}" not in search_url:
            search_url = DEFAULT_SEARCH_URL

        restart_needed = self.window.apply_preferences(
            home_url=home_url,
            search_url=search_url,
            javascript_enabled=self.javascript_checkbox.isChecked(),
            smooth_scrolling_enabled=self.smooth_scrolling_checkbox.isChecked(),
            hardware_acceleration_enabled=self.hardware_acceleration_checkbox.isChecked(),
        )

        if restart_needed:
            self.status_label.setText(
                "Settings saved. Restart OpenBrowse for the graphics change to apply."
            )
        else:
            self.status_label.setText("Settings saved.")

    def clear_history(self) -> None:
        self.window.clear_history()
        self.refresh_history()
        self.status_label.setText("Saved history was cleared.")

    def clear_cookies(self) -> None:
        self.window.clear_cookies()
        self.status_label.setText("Cookies were cleared.")

    def clear_cache(self) -> None:
        self.window.clear_cache()
        self.status_label.setText("Cache was cleared.")

    def clear_all(self) -> None:
        self.window.clear_browsing_data()
        self.refresh_history()
        self.status_label.setText("History, cookies, and cache were cleared.")

    def open_history_item(self, item: QListWidgetItem) -> None:
        url = item.data(Qt.ItemDataRole.UserRole)
        if isinstance(url, str) and url:
            self.window.add_tab(QUrl(url), "History", make_current=True)


class BrowserWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.settings = app_settings()
        self.home_url = configured_home_url(self.settings)
        self.search_url = configured_search_url(self.settings)
        self.javascript_enabled = settings_bool(
            self.settings, "browser/javascript_enabled", True
        )
        self.smooth_scrolling_enabled = settings_bool(
            self.settings, "browser/smooth_scrolling_enabled", True
        )
        self.hardware_acceleration_enabled = settings_bool(
            self.settings, "graphics/hardware_acceleration", True
        )
        self.history_entries = load_history_entries()
        self.settings_page: SettingsPage | None = None

        self.setWindowTitle(APP_NAME)
        self.resize(1440, 920)
        self.profile = self._build_profile()
        self.downloads: list[QWebEngineDownloadRequest] = []

        self.tabs = QTabWidget()
        self.tabs.setDocumentMode(True)
        self.tabs.setTabsClosable(True)
        self.tabs.setMovable(True)
        self.tabs.setElideMode(Qt.TextElideMode.ElideRight)
        self.tabs.tabCloseRequested.connect(self.close_tab)
        self.tabs.currentChanged.connect(self.on_current_tab_changed)
        self.setCentralWidget(self.tabs)

        self.address_bar = QLineEdit()
        self.address_bar.returnPressed.connect(self.load_from_address_bar)
        self.address_bar.setClearButtonEnabled(True)
        self.address_bar.setPlaceholderText("Enter a URL, search, or openbrowse://settings")

        self.progress_bar = QProgressBar()
        self.progress_bar.setMaximumWidth(140)
        self.progress_bar.setMaximum(100)
        self.progress_bar.hide()

        self.status_label = QLabel("Ready")
        self._build_navigation()
        self._build_status_bar()
        self._build_menu()

        self.profile.downloadRequested.connect(self.handle_download)
        self.add_tab(QUrl(self.home_url), "Home", make_current=True)

    def _build_profile(self) -> QWebEngineProfile:
        data_root = runtime_data_dir()
        cache_path = data_root / "cache"
        storage_path = data_root / "storage"
        downloads_path = Path(
            QStandardPaths.writableLocation(
                QStandardPaths.StandardLocation.DownloadLocation
            )
        )

        profile = QWebEngineProfile(APP_NAME, self)
        profile.setPersistentStoragePath(str(storage_path))
        profile.setCachePath(str(cache_path))
        profile.setDownloadPath(str(downloads_path))
        profile.setPersistentCookiesPolicy(
            QWebEngineProfile.PersistentCookiesPolicy.ForcePersistentCookies
        )
        profile.setHttpUserAgent(profile.httpUserAgent() + f" {APP_NAME}/1.0")
        return profile

    def _build_navigation(self) -> None:
        nav_bar = QToolBar("Navigation")
        nav_bar.setMovable(False)
        self.addToolBar(nav_bar)

        back_action = QAction("Back", self)
        back_action.setShortcut(QKeySequence.StandardKey.Back)
        back_action.triggered.connect(self.go_back)
        nav_bar.addAction(back_action)

        forward_action = QAction("Forward", self)
        forward_action.setShortcut(QKeySequence.StandardKey.Forward)
        forward_action.triggered.connect(self.go_forward)
        nav_bar.addAction(forward_action)

        reload_action = QAction("Reload", self)
        reload_action.setShortcut(QKeySequence.StandardKey.Refresh)
        reload_action.triggered.connect(self.reload_current_tab)
        nav_bar.addAction(reload_action)

        stop_action = QAction("Stop", self)
        stop_action.triggered.connect(self.stop_loading)
        nav_bar.addAction(stop_action)

        home_action = QAction("Home", self)
        home_action.triggered.connect(self.go_home)
        nav_bar.addAction(home_action)

        nav_bar.addSeparator()
        nav_bar.addWidget(self.address_bar)

        new_tab_action = QAction("New Tab", self)
        new_tab_action.setShortcut(QKeySequence("Ctrl+T"))
        new_tab_action.triggered.connect(lambda: self.add_tab(make_current=True))
        nav_bar.addAction(new_tab_action)

    def _build_status_bar(self) -> None:
        status_bar = QStatusBar()
        status_bar.addWidget(self.status_label, 1)
        status_bar.addPermanentWidget(self.progress_bar)
        self.setStatusBar(status_bar)

    def _build_menu(self) -> None:
        menu = self.menuBar()

        file_menu = menu.addMenu("File")
        new_tab = QAction("New Tab", self)
        new_tab.setShortcut(QKeySequence("Ctrl+T"))
        new_tab.triggered.connect(lambda: self.add_tab(make_current=True))
        file_menu.addAction(new_tab)

        close_tab = QAction("Close Tab", self)
        close_tab.setShortcut(QKeySequence("Ctrl+W"))
        close_tab.triggered.connect(lambda: self.close_tab(self.tabs.currentIndex()))
        file_menu.addAction(close_tab)

        file_menu.addSeparator()
        settings_action = QAction("Settings", self)
        settings_action.setShortcut(QKeySequence("Ctrl+,"))
        settings_action.triggered.connect(self.open_settings_tab)
        file_menu.addAction(settings_action)

        file_menu.addSeparator()
        quit_action = QAction("Quit", self)
        quit_action.setShortcut(QKeySequence.StandardKey.Quit)
        quit_action.triggered.connect(self.close)
        file_menu.addAction(quit_action)

        view_menu = menu.addMenu("View")
        devtools_action = QAction("Open DevTools", self)
        devtools_action.setShortcut(QKeySequence("F12"))
        devtools_action.triggered.connect(self.open_devtools)
        view_menu.addAction(devtools_action)

        fullscreen_action = QAction("Toggle Full Screen", self)
        fullscreen_action.setShortcut(QKeySequence("F11"))
        fullscreen_action.triggered.connect(self.toggle_fullscreen)
        view_menu.addAction(fullscreen_action)

        browser_menu = menu.addMenu("Browser")
        duplicate_tab_action = QAction("Duplicate Tab", self)
        duplicate_tab_action.setShortcut(QKeySequence("Ctrl+Shift+K"))
        duplicate_tab_action.triggered.connect(self.duplicate_current_tab)
        browser_menu.addAction(duplicate_tab_action)

        browser_menu.addSeparator()
        go_to_action = QAction("Go To URL", self)
        go_to_action.setShortcut(QKeySequence("Ctrl+L"))
        go_to_action.triggered.connect(self.focus_address_bar)
        browser_menu.addAction(go_to_action)

        open_settings_action = QAction("Open Settings", self)
        open_settings_action.triggered.connect(self.open_settings_tab)
        browser_menu.addAction(open_settings_action)

    def configure_view(self, view: BrowserView) -> None:
        page = BrowserPage(self.profile, self)
        view.setPage(page)
        page.setBackgroundColor(QColor("#ffffff"))
        self.apply_view_preferences(view)

        view.urlChanged.connect(lambda url, v=view: self.sync_url(url, v))
        view.titleChanged.connect(lambda title, v=view: self.sync_title(title, v))
        view.iconChanged.connect(lambda icon, v=view: self.sync_icon(icon, v))
        view.loadProgress.connect(self.on_load_progress)
        view.loadStarted.connect(lambda: self.status_label.setText("Loading..."))
        view.loadFinished.connect(lambda ok, v=view: self.on_load_finished(v, ok))

    def apply_view_preferences(self, view: BrowserView) -> None:
        settings = view.settings()
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.JavascriptEnabled,
            self.javascript_enabled,
        )
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.JavascriptCanOpenWindows,
            self.javascript_enabled,
        )
        settings.setAttribute(QWebEngineSettings.WebAttribute.LocalStorageEnabled, True)
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.FullScreenSupportEnabled, True
        )
        settings.setAttribute(
            QWebEngineSettings.WebAttribute.ScrollAnimatorEnabled,
            self.smooth_scrolling_enabled,
        )

    def add_tab(
        self,
        url: QUrl | None = None,
        label: str = "New Tab",
        make_current: bool = False,
    ) -> BrowserView:
        container = QWidget()
        layout = QVBoxLayout(container)
        layout.setContentsMargins(0, 0, 0, 0)

        view = BrowserView(self)
        self.configure_view(view)
        layout.addWidget(view)
        container.setProperty("kind", "browser")
        container.setProperty("view", view)
        index = self.tabs.addTab(container, label)

        if make_current:
            self.tabs.setCurrentIndex(index)

        if url is None:
            url = QUrl(self.home_url)
        view.setUrl(url)
        return view

    def open_settings_tab(self) -> None:
        if self.settings_page is None:
            self.settings_page = SettingsPage(self)
            index = self.tabs.addTab(self.settings_page, "Settings")
        else:
            self.settings_page.reload_preferences()
            self.settings_page.refresh_history()
            index = self.tabs.indexOf(self.settings_page)

        self.tabs.setCurrentIndex(index)

    def current_browser_view(self) -> BrowserView | None:
        return self.view_for_index(self.tabs.currentIndex())

    def view_for_index(self, index: int) -> BrowserView | None:
        if index < 0:
            return None
        widget = self.tabs.widget(index)
        if widget is None or widget.property("kind") != "browser":
            return None
        return widget.property("view")

    def current_page_kind(self) -> str:
        widget = self.tabs.currentWidget()
        if widget is None:
            return "browser"
        kind = widget.property("kind")
        return kind if isinstance(kind, str) else "browser"

    def go_back(self) -> None:
        view = self.current_browser_view()
        if view is not None:
            view.back()

    def go_forward(self) -> None:
        view = self.current_browser_view()
        if view is not None:
            view.forward()

    def reload_current_tab(self) -> None:
        view = self.current_browser_view()
        if view is not None:
            view.reload()
        elif self.current_page_kind() == "settings" and self.settings_page is not None:
            self.settings_page.reload_preferences()
            self.settings_page.refresh_history()

    def stop_loading(self) -> None:
        view = self.current_browser_view()
        if view is not None:
            view.stop()

    def go_home(self) -> None:
        view = self.current_browser_view()
        if view is not None:
            view.setUrl(QUrl(self.home_url))
        else:
            self.add_tab(QUrl(self.home_url), "Home", make_current=True)

    def close_tab(self, index: int) -> None:
        widget = self.tabs.widget(index)
        if widget is None:
            return

        if self.tabs.count() == 1:
            self.close()
            return

        self.tabs.removeTab(index)
        if widget is self.settings_page:
            self.settings_page = None
        widget.deleteLater()

    def on_current_tab_changed(self, index: int) -> None:
        if index < 0:
            return

        view = self.view_for_index(index)
        if view is not None:
            self.address_bar.setText(
                "" if is_home_url(view.url(), self.home_url) else view.url().toString()
            )
            self.setWindowTitle(f"{view.title() or APP_NAME} - {APP_NAME}")
            return

        widget = self.tabs.widget(index)
        if widget is self.settings_page:
            self.address_bar.setText(SETTINGS_URL)
            self.setWindowTitle(f"Settings - {APP_NAME}")
            self.settings_page.reload_preferences()
            self.settings_page.refresh_history()

    def sync_url(self, url: QUrl, view: BrowserView) -> None:
        if view == self.current_browser_view():
            self.address_bar.setText("" if is_home_url(url, self.home_url) else url.toString())
            self.address_bar.setCursorPosition(0)

    def sync_title(self, title: str, view: BrowserView) -> None:
        index = self._index_for_view(view)
        if index >= 0:
            self.tabs.setTabText(index, title[:24] if title else "New Tab")
        if view == self.current_browser_view():
            self.setWindowTitle(f"{title or APP_NAME} - {APP_NAME}")

    def sync_icon(self, icon: QIcon, view: BrowserView) -> None:
        index = self._index_for_view(view)
        if index >= 0:
            self.tabs.setTabIcon(index, icon)

    def _index_for_view(self, view: BrowserView) -> int:
        for index in range(self.tabs.count()):
            widget = self.tabs.widget(index)
            if widget and widget.property("view") == view:
                return index
        return -1

    def iter_views(self) -> list[BrowserView]:
        views: list[BrowserView] = []
        for index in range(self.tabs.count()):
            view = self.view_for_index(index)
            if view is not None:
                views.append(view)
        return views

    def load_from_address_bar(self) -> None:
        text = self.address_bar.text().strip()
        if text.lower() == SETTINGS_URL:
            self.open_settings_tab()
            return

        view = self.current_browser_view()
        if view is None:
            view = self.add_tab(make_current=True)
        view.setUrl(ensure_scheme(text, self.home_url, self.search_url))

    def focus_address_bar(self) -> None:
        self.address_bar.setFocus()
        self.address_bar.selectAll()

    def duplicate_current_tab(self) -> None:
        view = self.current_browser_view()
        if view is not None:
            self.add_tab(view.url(), "Duplicate", make_current=True)

    def toggle_fullscreen(self) -> None:
        if self.isFullScreen():
            self.showNormal()
        else:
            self.showFullScreen()

    def open_devtools(self) -> None:
        inspected_view = self.current_browser_view()
        if inspected_view is None:
            self.status_label.setText("DevTools only works on web tabs.")
            return
        devtools_view = self.add_tab(label="DevTools", make_current=True)
        inspected_view.page().setDevToolsPage(devtools_view.page())

    def handle_download(self, download: QWebEngineDownloadRequest) -> None:
        suggested_path = download.downloadFileName() or "download"
        destination, _ = QFileDialog.getSaveFileName(
            self,
            "Save Download",
            str(Path(self.profile.downloadPath()) / suggested_path),
        )
        if not destination:
            download.cancel()
            self.status_label.setText("Download cancelled")
            return

        download.setDownloadDirectory(str(Path(destination).parent))
        download.setDownloadFileName(Path(destination).name)
        download.finished.connect(
            lambda: self.status_label.setText(f"Downloaded: {Path(destination).name}")
        )
        self.downloads.append(download)
        download.accept()
        self.status_label.setText(f"Downloading: {Path(destination).name}")

    def record_history(self, view: BrowserView) -> None:
        url = view.url().toString().strip()
        if not url or url == SETTINGS_URL:
            return

        scheme = view.url().scheme().lower()
        if scheme not in {"http", "https"}:
            return

        title = (view.title() or url).strip()
        visited_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        if self.history_entries and self.history_entries[0]["url"] == url:
            self.history_entries[0]["title"] = title
            self.history_entries[0]["visited_at"] = visited_at
        else:
            self.history_entries.insert(
                0,
                {
                    "title": title,
                    "url": url,
                    "visited_at": visited_at,
                },
            )

        self.history_entries = self.history_entries[:HISTORY_LIMIT]
        save_history_entries(self.history_entries)

        if self.settings_page is not None:
            self.settings_page.refresh_history()

    def clear_history(self) -> None:
        self.history_entries = []
        save_history_entries(self.history_entries)
        self.profile.clearAllVisitedLinks()

    def clear_cookies(self) -> None:
        self.profile.cookieStore().deleteAllCookies()

    def clear_cache(self) -> None:
        self.profile.clearHttpCache()

    def clear_browsing_data(self) -> None:
        self.clear_history()
        self.clear_cookies()
        self.clear_cache()

    def apply_preferences(
        self,
        *,
        home_url: str,
        search_url: str,
        javascript_enabled: bool,
        smooth_scrolling_enabled: bool,
        hardware_acceleration_enabled: bool,
    ) -> bool:
        restart_needed = hardware_acceleration_enabled != self.hardware_acceleration_enabled
        previous_home_url = self.home_url

        self.home_url = QUrl.fromUserInput(home_url).toString() or DEFAULT_HOME_URL
        self.search_url = search_url if "{query}" in search_url else DEFAULT_SEARCH_URL
        self.javascript_enabled = javascript_enabled
        self.smooth_scrolling_enabled = smooth_scrolling_enabled
        self.hardware_acceleration_enabled = hardware_acceleration_enabled

        self.settings.setValue("browser/home_url", self.home_url)
        self.settings.setValue("browser/search_url", self.search_url)
        self.settings.setValue("browser/javascript_enabled", self.javascript_enabled)
        self.settings.setValue(
            "browser/smooth_scrolling_enabled", self.smooth_scrolling_enabled
        )
        self.settings.setValue(
            "graphics/hardware_acceleration", self.hardware_acceleration_enabled
        )

        for view in self.iter_views():
            self.apply_view_preferences(view)

        current_view = self.current_browser_view()
        if current_view is not None and is_home_url(current_view.url(), previous_home_url):
            current_view.setUrl(QUrl(self.home_url))

        return restart_needed

    def on_load_progress(self, progress: int) -> None:
        self.progress_bar.show()
        self.progress_bar.setValue(progress)
        if progress >= 100:
            self.progress_bar.hide()

    def on_load_finished(self, view: BrowserView, ok: bool) -> None:
        self.status_label.setText("Ready" if ok else "Page failed to load")
        if ok:
            self.record_history(view)

    def closeEvent(self, event: QCloseEvent) -> None:
        if self.tabs.count() > 1:
            answer = QMessageBox.question(
                self,
                "Close Browser",
                f"Close {self.tabs.count()} open tabs?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No,
            )
            if answer != QMessageBox.StandardButton.Yes:
                event.ignore()
                return
        event.accept()


def main() -> int:
    configure_runtime()
    QApplication.setApplicationName(APP_NAME)
    QApplication.setOrganizationName(ORG_NAME)
    app = QApplication(sys.argv)
    window = BrowserWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        show_startup_error(error)
        raise SystemExit(1)
