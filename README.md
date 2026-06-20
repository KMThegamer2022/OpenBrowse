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

- Windows or macOS with Python 3.11+ recommended
- `pip`

## Setup

OpenBrowse can install its required Python packages for you automatically when you launch it with one of the platform launchers:

- Windows: `launch_openbrowse.bat` or `OpenBrowse.vbs`
- macOS: `openbrowse-mac/launch_openbrowse.command`

If you prefer to set everything up manually on Windows, use:

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

For manual setup on macOS, use:

```bash
cd openbrowse-mac
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install -r requirements.txt
python3 browser.py
```

## Run

```powershell
python browser.py
```

For normal Windows launching without VS Code, double-click [launch_openbrowse.bat](launch_openbrowse.bat) or [OpenBrowse.vbs](OpenBrowse.vbs). They automatically create the project's `.venv` if needed and install packages from `requirements.txt` when PyQt is missing, so most people do not need to run the manual setup steps.

For macOS, use the files in [openbrowse-mac](openbrowse-mac). Double-click [openbrowse-mac/launch_openbrowse.command](openbrowse-mac/launch_openbrowse.command) to create `.venv`, install any missing packages with `python3`, and start the browser.

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
