@echo off
setlocal
cd /d "%~dp0"
rem PYTHON may point to a preferred Python 3.11+ installation.
if defined PYTHON (
 "%PYTHON%" tools\empacotar_release.py %*
) else (
 py -3 tools\empacotar_release.py %*
)
exit /b %errorlevel%
