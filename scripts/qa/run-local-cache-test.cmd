@echo off
set SLIME_CACHE_MANIFEST=http://127.0.0.1:8765/manifest.json
start "" "%~dp0..\..\dist\SlimeFinder\SlimeFinder.exe"
