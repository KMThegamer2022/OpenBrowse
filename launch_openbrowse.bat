@echo off
setlocal
cd /d "%~dp0"

set "VENV_PYTHON=.venv\Scripts\python.exe"
set "VENV_PYTHONW=.venv\Scripts\pythonw.exe"

if not exist "%VENV_PYTHON%" (
    echo Creating virtual environment...
    py -3 -m venv .venv >nul 2>&1
    if errorlevel 1 (
        python -m venv .venv >nul 2>&1
    )
)

if not exist "%VENV_PYTHON%" (
    echo OpenBrowse could not create the virtual environment.
    echo.
    echo Please install Python 3 and try again.
    echo.
    pause
    exit /b 1
)

call :ensure_requirements
if errorlevel 1 (
    echo.
    echo OpenBrowse could not install the required packages.
    echo.
    pause
    exit /b 1
)

start "" "%VENV_PYTHONW%" "browser.py"
exit /b 0

:ensure_requirements
"%VENV_PYTHON%" -c "import PyQt6, PyQt6.QtWebEngineWidgets" >nul 2>&1
if not errorlevel 1 (
    exit /b 0
)

echo Installing or repairing required packages...
"%VENV_PYTHON%" -m pip install --upgrade pip
if errorlevel 1 exit /b 1

"%VENV_PYTHON%" -m pip install -r requirements.txt
if errorlevel 1 exit /b 1

"%VENV_PYTHON%" -c "import PyQt6, PyQt6.QtWebEngineWidgets" >nul 2>&1
if errorlevel 1 exit /b 1

exit /b 0
