# OpenBrowse

OpenBrowse is a desktop web browser written in Python with Qt WebEngine. It gives you a real browsing engine with JavaScript, cookies, local storage, downloads, and multiple tabs without building on Google services.

## Features

- Real page rendering through Qt WebEngine
- JavaScript enabled
- Persistent cookies and local storage
- Multi-tab browsing
- Built-in settings page with history and data controls
- Address bar with URL or search support
- Back, forward, reload, stop, and home controls
- Download handling
- Full-screen mode
- Built-in DevTools tab

## Requirements

- Windows with Python 3.11+ recommended
- `pip`

## Setup

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

## Run

```powershell
python browser.py
```

For normal Windows launching without VS Code, double-click [launch_openbrowse.bat] or [OpenBrowse.vbs]. They automatically create the project's `.venv` if needed and install packages from `requirements.txt` when PyQt is missing.

## Keyboard shortcuts

- `Ctrl+T` new tab
- `Ctrl+W` close tab
- `Ctrl+L` focus address bar
- `Ctrl+,` open settings
- `Ctrl+R` reload
- `F11` full screen
- `F12` DevTools

## Notes

- Cookies, cache, and site storage are persisted under Qt's app data directory.
- Search queries are sent to DuckDuckGo by default.
- Hardware acceleration is enabled by default using Qt WebEngine's normal backend selection. If a specific PC has graphics-driver issues, you can force the old software path by launching with `OPENBROWSE_FORCE_SOFTWARE_RENDERING=1`.
- Type `openbrowse://settings` in the address bar or press `Ctrl+,` to open the settings page.
