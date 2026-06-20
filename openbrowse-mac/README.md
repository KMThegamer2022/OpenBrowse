# OpenBrowse for macOS

This folder contains the macOS version of OpenBrowse.

## Quick start

Double-click [launch_openbrowse.command](launch_openbrowse.command).

The launcher will:

- create `.venv` if it does not exist
- install `requirements.txt` automatically when PyQt is missing
- start OpenBrowse with the virtual environment's Python

## Manual setup

If you want to install everything yourself:

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install -r requirements.txt
python3 browser.py
```

## If macOS blocks the launcher

If the launcher is not executable after extracting a zip, run:

```bash
chmod +x launch_openbrowse.command
```

Then run:

```bash
./launch_openbrowse.command
```
