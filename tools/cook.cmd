@echo off
rem cook.cmd: the cook (cook.py) for Windows. Double-click it, or drop the game's disc image (.iso) onto it, and it
rem checks the tools and asks for what it needs (cook.py --ask); with options it runs cook.py with them (cook.cmd
rem --help lists them). cook.py runs on Python 3.8 or newer from PATH: the py launcher's, or python.
setlocal
title UC2 cook
set "HERE=%~dp0"
set "FIRST=%~1"
set "OPTIONS="
set "RC=1"
rem The first character is read only when there is an argument: an empty one breaks the line it is on.
if not defined FIRST goto :start
if "%FIRST:~0,1%"=="-" set "OPTIONS=1"
:start

if not exist "%HERE%cook.py" goto :not_unpacked
call :find_python
if not defined PY goto :no_python

if defined OPTIONS goto :options
if not defined FIRST goto :ask
%PY% "%HERE%cook.py" --ask --in "%~1"
set "RC=%errorlevel%"
goto :done
:ask
%PY% "%HERE%cook.py" --ask
set "RC=%errorlevel%"
goto :done
:options
%PY% "%HERE%cook.py" %*
exit /b %errorlevel%

:not_unpacked
echo cook.cmd: cook.py is not beside it. Unpack the whole zip first (right-click it, Extract All...),
echo then start cook.cmd in the folder that makes.
goto :done

:no_python
echo cook.cmd: the cook needs Python 3.8 or newer on PATH, and this window finds none
echo (the Microsoft Store's "python" placeholder is not Python).
echo Install Python from https://www.python.org/downloads/ (in the installer, tick "Add python.exe to PATH"
echo when it offers it), or Python 3 from the Microsoft Store. Then start cook.cmd again from a new window.
goto :done

rem PY: a Python 3.8 or newer on PATH that runs (the Microsoft Store's python placeholder does not), or nothing.
:find_python
set "PY="
py -3 -c "import sys; sys.exit(sys.version_info < (3, 8))" >nul 2>&1 && set "PY=py -3"
if defined PY exit /b
python -c "import sys; sys.exit(sys.version_info < (3, 8))" >nul 2>&1 && set "PY=python"
exit /b

rem From a console with options the window stays; started by a double-click or a drop it waits for a key.
:done
if defined OPTIONS exit /b %RC%
echo.
pause
exit /b %RC%
