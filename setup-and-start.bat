@echo off
title Hobby Horse Jumping Championship System
color 0A

echo.
echo ========================================
echo  Hobby Horse Jumping Championship System
echo ========================================
echo.

REM Check if we're in the right directory
if not exist "hhj-backend" (
    echo ERROR: hhj-backend folder not found!
    echo Please run this script from the project root directory.
    pause
    exit /b 1
)

if not exist "hhj-frontend" (
    echo ERROR: hhj-frontend folder not found!
    echo Please run this script from the project root directory.
    pause
    exit /b 1
)

echo Checking dependencies...
echo.

REM Check Python
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python not found! Please install Python 3.12+ from python.org
    pause
    exit /b 1
) else (
    echo ✓ Python found
)

REM Check Node.js
node --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Node.js not found! Please install Node.js from nodejs.org
    pause
    exit /b 1
) else (
    echo ✓ Node.js found
)

REM Check pipenv
pip show pipenv >nul 2>&1
if errorlevel 1 (
    echo Installing pipenv...
    pip install pipenv
    if errorlevel 1 (
        echo ERROR: Failed to install pipenv
        pause
        exit /b 1
    )
) else (
    echo ✓ Pipenv found
)

echo.
echo Setting up backend...
cd hhj-backend

REM Install Python dependencies
echo Installing Python dependencies...
pipenv install
if errorlevel 1 (
    echo ERROR: Failed to install Python dependencies
    pause
    exit /b 1
)

REM Run migrations
echo Running database migrations...
pipenv run python manage.py migrate
if errorlevel 1 (
    echo ERROR: Failed to run migrations
    pause
    exit /b 1
)

REM Setup initial data
echo Setting up initial test data...
pipenv run python manage.py setup_initial_data

cd ..

echo.
echo Setting up frontend...
cd hhj-frontend

REM Install Node.js dependencies
echo Installing Node.js dependencies...
npm install
if errorlevel 1 (
    echo ERROR: Failed to install Node.js dependencies
    pause
    exit /b 1
)

cd ..

echo.
echo ========================================
echo  Setup completed successfully!
echo ========================================
echo.
echo Starting services...
echo.

REM Start backend in a new window
echo Starting Django backend...
start "HHJ-Backend" cmd /k "cd /d "%~dp0hhj-backend" && echo Backend Server Starting... && echo. && pipenv run python manage.py runserver 127.0.0.1:3003"

REM Wait for backend to start
echo Waiting for backend to start...
timeout /t 10 /nobreak >nul

REM Start frontend in a new window
echo Starting React frontend...
start "HHJ-Frontend" cmd /k "cd /d "%~dp0hhj-frontend" && echo Frontend Server Starting... && echo. && set BROWSER=none && npm start"

echo.
echo ========================================
echo  System Started Successfully!
echo ========================================
echo.
echo Services:
echo   Backend:  http://127.0.0.1:3003
echo   Frontend: http://localhost:3000
echo   Admin:    http://127.0.0.1:3003/admin/
echo.
echo The system is now running in separate windows.
echo You can close this window safely.
echo.
echo To stop the system, close the Backend and Frontend windows.
echo.

REM Create a quick start script for future use
echo @echo off > quick-start.bat
echo title HHJ System - Quick Start >> quick-start.bat
echo echo Starting Hobby Horse Jumping Championship System... >> quick-start.bat
echo start "HHJ-Backend" cmd /k "cd /d "%~dp0hhj-backend" && pipenv run python manage.py runserver 127.0.0.1:3003" >> quick-start.bat
echo timeout /t 5 /nobreak ^>nul >> quick-start.bat
echo start "HHJ-Frontend" cmd /k "cd /d "%~dp0hhj-frontend" && set BROWSER=none && npm start" >> quick-start.bat
echo echo. >> quick-start.bat
echo echo System started! Backend: http://127.0.0.1:3003 ^| Frontend: http://localhost:3000 >> quick-start.bat
echo pause >> quick-start.bat

echo Created 'quick-start.bat' for future use.
echo.
echo Next steps:
echo 1. Configure your ESP32 sensors with WiFi credentials
echo 2. Update sensor server URL to: http://YOUR_PC_IP:3003/timereadings/
echo 3. Create competitions in admin panel: http://127.0.0.1:3003/admin/
echo 4. Open display board: http://localhost:3000
echo.
pause
