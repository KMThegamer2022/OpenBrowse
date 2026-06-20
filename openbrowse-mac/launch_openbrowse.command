#!/bin/bash

set -u

BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$BASE_DIR" || exit 1

pause_on_error() {
  echo
  read -r -p "Press Enter to close this window..." _
}

find_venv_python() {
  if [ -x ".venv/bin/python3" ]; then
    echo ".venv/bin/python3"
    return
  fi
  if [ -x ".venv/bin/python" ]; then
    echo ".venv/bin/python"
    return
  fi
  echo ""
}

echo "Starting OpenBrowse for macOS..."

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 was not found."
  echo "Please install Python 3 and try again."
  pause_on_error
  exit 1
fi

PYTHON_BIN="$(find_venv_python)"
if [ -z "$PYTHON_BIN" ]; then
  echo "Creating virtual environment..."
  if ! python3 -m venv .venv; then
    echo "OpenBrowse could not create the virtual environment."
    pause_on_error
    exit 1
  fi
  PYTHON_BIN="$(find_venv_python)"
fi

if [ -z "$PYTHON_BIN" ]; then
  echo "OpenBrowse could not find the virtual environment Python binary."
  pause_on_error
  exit 1
fi

if ! "$PYTHON_BIN" -c "import PyQt6, PyQt6.QtWebEngineWidgets" >/dev/null 2>&1; then
  echo "Installing or repairing required packages..."
  if ! "$PYTHON_BIN" -m pip install --upgrade pip; then
    echo "OpenBrowse could not upgrade pip."
    pause_on_error
    exit 1
  fi
  if ! "$PYTHON_BIN" -m pip install -r requirements.txt; then
    echo "OpenBrowse could not install the required packages."
    pause_on_error
    exit 1
  fi
fi

exec "$PYTHON_BIN" browser.py
