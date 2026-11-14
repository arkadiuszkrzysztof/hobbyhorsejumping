@echo off
rem ===========================================================================
rem  Hobby Horse Jumping - starts the server and the operator console (Windows)
rem
rem  Double-click it, or have Windows run it at logon:
rem      press Win+R, type shell:startup, put a SHORTCUT to this file there
rem      (a shortcut, not a copy: the file must stay next to hhj-backend).
rem
rem  It opens two windows and then the console in the browser:
rem      "HHJ backend"   HTTP 3003, gates and display on UDP 3004
rem      "HHJ console"   http://localhost:3000
rem  Closing a window stops that part. Running this file again starts
rem  whatever is not running.
rem
rem  On a fresh start (nothing running yet) it first fetches the newest
rem  version with git pull. Without internet, or if that fails for any other
rem  reason, it starts the version that is here.
rem
rem  Needs on the PATH: Python 3.12 with pipenv, Node.js 20.19+ or 22.12+.
rem  Packages are installed on the first run and after a lock file changed -
rem  that needs internet. Every other start works offline.
rem  If Windows Firewall asks about Python or Node.js: allow, or the gates
rem  and the display cannot reach the server.
rem ===========================================================================
setlocal
set "ROOT=%~dp0"
title HHJ start
if /i "%~1"=="backend"  goto backend
if /i "%~1"=="frontend" goto frontend
if /i "%~1"=="updated"  goto start

rem  Update only when nothing is running: with one part up, new code would reach
rem  the console at once but the server only after its restart.
call :answers 3003/api/competitions/
if not errorlevel 1 goto start
call :answers 3000/
if not errorlevel 1 goto start
cd /d "%ROOT%"
rem  Never wait for a login window, and give up on a connection that stalls.
set "GIT_TERMINAL_PROMPT=0"
set "GCM_INTERACTIVE=never"
echo Looking for a newer version ...
rem  The pull may replace this very file, and cmd reads a batch file line by line
rem  as it goes: so the pull and the fresh start of this file are ONE line.
git -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=15 pull --ff-only || echo Could not update - starting the version that is here. & "%~f0" updated

:start
call :answers 3003/api/competitions/
if errorlevel 1 start "HHJ backend" cmd /k ""%~f0" backend"
call :answers 3000/
if errorlevel 1 start "HHJ console" cmd /k ""%~f0" frontend"

echo Waiting for the server and the console. The first start installs packages ...
set /a tries=0
:wait
call :answers 3003/api/competitions/
if not errorlevel 1 call :answers 3000/
if not errorlevel 1 goto open
set /a tries+=1
if %tries% geq 600 goto late
timeout /t 1 /nobreak >nul
goto wait

:open
start "" "http://localhost:3000/"
exit /b 0

:late
echo.
echo Still not up after 10 minutes. Look at the "HHJ backend" and "HHJ console" windows.
pause
exit /b 1

rem ---------------------------------------------------------------- backend
:backend
title HHJ backend
cd /d "%ROOT%hhj-backend" || goto failed
set "PY=.venv\Scripts\python.exe"
rem  The virtualenv lives in hhj-backend\.venv, the same as with "make backend".
set "PIPENV_VENV_IN_PROJECT=1"
set "PIPENV_IGNORE_VIRTUALENVS=1"
set "PIPENV_NOSPIN=1"
rem  Install the Python packages on the first run and after Pipfile.lock changed.
if exist "%PY%" fc /b Pipfile.lock .venv\.hhj-lock >nul 2>&1 && goto backend_run
echo Installing the Python packages ...
where pipenv >nul 2>&1 || (echo pipenv was not found. Install it with:  pip install pipenv & goto failed)
call pipenv sync || goto failed
copy /y Pipfile.lock .venv\.hhj-lock >nul
:backend_run
rem  A migration can drop tables and columns, so a copy of the database is kept from before one.
if exist db.sqlite3 "%PY%" manage.py migrate --check >nul 2>&1 || "%PY%" -c "import shutil, time; shutil.copy2('db.sqlite3', time.strftime('db.before-migration-%%Y%%m%%d-%%H%%M%%S.sqlite3'))"
"%PY%" manage.py migrate || goto failed
echo.
echo HHJ backend on http://localhost:3003 - gates and display on UDP 3004.
echo Close this window to stop it.
echo.
rem  --noreload: one process, and no restart when a file changes mid-competition.
"%PY%" manage.py runserver 0.0.0.0:3003 --noreload
goto failed

rem --------------------------------------------------------------- frontend
:frontend
title HHJ console
cd /d "%ROOT%hhj-frontend" || goto failed
where node >nul 2>&1 || (echo Node.js was not found. Install the LTS version from https://nodejs.org & goto failed)
node -e "const [a,b]=process.versions.node.split('.').map(Number);process.exit((a==20&&b>=19)||(a==22&&b>=12)||a>22?0:1)"
if errorlevel 1 (echo The console needs Node.js 20.19+ or 22.12+. This one is: & node --version & goto failed)
rem  Install the npm packages on the first run and after package-lock.json changed.
fc /b package-lock.json node_modules\.hhj-lock >nul 2>&1 && goto frontend_run
echo Installing the npm packages ...
call npm ci || goto failed
copy /y package-lock.json node_modules\.hhj-lock >nul
:frontend_run
call npm start
goto failed

rem ------------------------------------------------------------------ helpers
:failed
echo.
echo *** This part of HHJ is NOT running. See the messages above. ***
exit /b 1

rem  call :answers PORT/PATH   sets errorlevel 0 when that local address answers
:answers
curl -s -o nul --max-time 2 "http://127.0.0.1:%~1" >nul 2>&1
exit /b %errorlevel%
