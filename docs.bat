@echo off
setlocal
rem Resolve all paths from this batch file, including when run from another folder.
set "DOCS_PYTHON=%~dp0docs\.venv\Scripts\python.exe"
if not exist "%DOCS_PYTHON%" (
    where python >nul 2>&1
    if errorlevel 1 (
        py -3 -m venv "%~dp0docs\.venv"
    ) else (
        python -m venv "%~dp0docs\.venv"
    )
    if errorlevel 1 exit /b 1
)
"%DOCS_PYTHON%" -m pip --disable-pip-version-check install -r "%~dp0docs\requirements.txt"
if errorlevel 1 exit /b 1
"%DOCS_PYTHON%" "%~dp0docs\build.py"
exit /b %errorlevel%
