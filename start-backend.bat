@echo off
echo Starting Django Backend Server with CORS Support...
echo.

cd /d "%~dp0hhj-backend"

echo Checking if Python is available...
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Python is not installed or not in PATH
    pause
    exit /b 1
)

echo.
echo Installing/Updating django-cors-headers...
pip install django-cors-headers

echo.
echo Starting Django server on port 3003...
echo Backend will be available at: http://127.0.0.1:3003
echo.
echo Press Ctrl+C to stop the server
echo.

python manage.py runserver 127.0.0.1:3003
