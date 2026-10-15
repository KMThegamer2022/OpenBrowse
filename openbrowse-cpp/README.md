# OpenBrowse for Windows (native C++)

A C++ rewrite of OpenBrowse built on the **Edge WebView2 Runtime** instead of
Python and Qt. The output is a single native `OpenBrowse.exe` — no interpreter,
no `.bat`/`.vbs` launchers, no script files of any kind.

## Why this exists

The original `browser.py` was 925 lines of Python driving Qt WebEngine. That
build has to be launched by a script and runs a Python interpreter, so it will
not start under a no-script execution policy.

Two alternatives were considered:

| Approach | Result |
| --- | --- |
| Freeze the Python build with PyInstaller | Produces a `.exe`, but it still loads `python312.dll` and `.pyd` modules. Policies that key on interpreters still block it, and it must be built on Windows. |
| **Native C++ on WebView2** | A plain PE binary with no interpreter. Chromium comes from the WebView2 Runtime already present on Windows. |

WebView2 also removes Qt entirely, which is where most of the original's
dependency weight came from.

## Requirements

- Windows 11, or Windows 10 21H2 or later (for the preinstalled WebView2 Runtime)
- To build: Visual Studio 2022 Build Tools with the **Desktop development with
  C++** workload, CMake, and `nuget.exe`

No runtime installer is needed on the target machine. If WebView2 is missing,
OpenBrowse shows a message pointing at <https://developer.microsoft.com/microsoft-edge/webview2/>.

## Build

```powershell
winget install Kitware.CMake
winget install Microsoft.NuGet
git clone https://github.com/KMThegamer2022/OpenBrowse.git
cd OpenBrowse\openbrowse-cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The executable is `build\Release\OpenBrowse.exe`. It links
`WebView2LoaderStatic.lib`, so there is no companion DLL to deploy.

### Building from GitHub Actions

Push to `main` and the workflow compiles the executable on a Windows runner and
attaches `OpenBrowse-win64.zip` to the run's artifacts. Tagging `v*` publishes
`OpenBrowse.exe` onto the release.

## Testing

The JSON, URL heuristics, settings and history layers are free of Windows APIs,
so they are unit tested on any host:

```bash
clang++ -std=c++17 -Isrc src/tests_portable.cpp -o /tmp/ob_tests && /tmp/ob_tests
```

CMake exposes the same suite as `OpenBrowseTests` with `ctest`.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+T` | New tab |
| `Ctrl+W` | Close tab |
| `Ctrl+L` | Focus the address bar |
| `Ctrl+,` | Settings |
| `Ctrl+Shift+K` | Duplicate tab |
| `Alt+Left` / `Alt+Right` | Back / forward |
| `Alt+Home` | Home |
| `F5` | Reload |
| `F11` | Full screen |
| `F12` | DevTools |
| `Alt+F4` | Quit |

Typing `openbrowse://settings` in the address bar also opens settings.

## Feature parity with the Python build

Ported: tabs with favicons and a 24-character label, address bar with URL and
search heuristics, the four search providers, back/forward/reload/stop/home,
`window.open` into a new tab, downloads with a Save As dialog, full screen,
DevTools, persistent cookies and storage, `history.json` in the same format,
the settings page, and the close-confirmation prompt.

Two deliberate differences:

1. **DevTools opens in a separate window.** WebView2 has no supported way to
   host DevTools inside the app as a tab, so `F12` uses
   `OpenDevToolsWindow()`.
2. **JavaScript, smooth scrolling and hardware acceleration need a restart.**
   These are Chromium launch flags in WebView2 rather than live page settings,
   so the settings page saves them and asks you to restart. The rest of the
   preferences apply immediately.

`OPENBROWSE_FORCE_SOFTWARE_RENDERING=1` still disables the GPU for machines with
broken graphics drivers.

## Data locations

| Path | Contents |
| --- | --- |
| `%LOCALAPPDATA%\OpenBrowse\settings.ini` | Preferences |
| `%LOCALAPPDATA%\OpenBrowse\history.json` | History, same shape as the Python build |
| `%LOCALAPPDATA%\OpenBrowse\WebView2\` | Cookies, cache, local storage |
| `%LOCALAPPDATA%\OpenBrowse\startup-error.log` | Written only if startup fails |

`settings.ini` uses the same key names as the QSettings file the Python build
wrote, so preferences carry over.

## How it is put together

| File | Role |
| --- | --- |
| `src/build_config.h` | `UNICODE` and the SDK version macros, set before any Windows header |
| `src/url_utils.hpp` | Address bar heuristics, URL normalisation |
| `src/json.hpp` | Small dependency-free JSON reader and writer |
| `src/settings.hpp` | INI-backed preferences |
| `src/history.hpp` | `history.json` load, save and de-duplication |
| `src/settings_page.hpp` | The `openbrowse://settings` document |
| `src/app_paths.hpp` | Windows known folders and string conversion |
| `src/tab.*` | One WebView2 controller, its events and lifetime |
| `src/browser_window.*` | Native Win32 chrome, tab strip, downloads |
| `src/main.cpp` | WebView2 environment and the message loop |

### Two things that will bite you

**Do not define `WIN32_LEAN_AND_MEAN` before including `WebView2.h`.** It makes
`windows.h` skip the headers that declare the `interface` keyword macro, and
`WebView2.h` only includes `rpc.h`/`rpcndr.h`, so `interface` never becomes
available and every typedef in that header fails to parse. The symptom is a wall
of `C2146: syntax error: missing ';' before identifier 'ICoreWebView2'` pointing
into the SDK header rather than at your own code.

**The tab strip is drawn by hand, not with `WC_TABCONTROL`.** `TCIFIMAGE`,
`TabStrip_*`, `NMTABCTRL`, `NMCDS_*` and `CDRF_*` are absent from the Windows SDK
headers on the build machine, and no combination of `_WIN32_WINNT` and
`NTDDI_VERSION` brings them back. The strip is an ordinary child window;
`BrowserWindow::layoutTabs` computes the geometry shared by the painter and the
hit test. As a consequence the project links no common controls at all, and the
address bar is subclassed by replacing its window procedure rather than with
`SetWindowSubclass`.

### The settings page

Settings render with `NavigateToString`, so the markup exists only in memory —
no `.html` or `.js` file is written to disk. The page talks to the host over
`PostWebMessageAsJson`, which is how Save and Clear actions reach native code.

### Tab lifetime

`CreateCoreWebView2Controller` is asynchronous, so a controller can arrive after
its tab has already been closed. Each tab therefore keeps its COM objects and
event tokens in a separately reference-counted `TabState`; closing a tab clears
the back-pointer and lets any in-flight callback finish with the state still
valid. See `src/tab.h`.