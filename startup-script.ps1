# Hobby Horse Jumping Championship System - Automated Startup Script
# Run this script as Administrator to set up and start the timing system

param(
    [switch]$InstallOnly,
    [switch]$StartOnly,
    [switch]$SetupAutoStart
)

# Color functions for better output
function Write-ColorOutput($ForegroundColor) {
    $fc = $host.UI.RawUI.ForegroundColor
    $host.UI.RawUI.ForegroundColor = $ForegroundColor
    if ($args) {
        Write-Output $args
    } else {
        $input | Write-Output
    }
    $host.UI.RawUI.ForegroundColor = $fc
}

function Write-Info($message) {
    Write-ColorOutput Cyan "ℹ️ $message"
}

function Write-Success($message) {
    Write-ColorOutput Green "✅ $message"
}

function Write-Warning($message) {
    Write-ColorOutput Yellow "⚠️ $message"
}

function Write-Error($message) {
    Write-ColorOutput Red "❌ $message"
}

function Write-Header($message) {
    Write-Host ""
    Write-ColorOutput Magenta "🐎 $message"
    Write-Host "=" * 60
}

# Global variables
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$backendPath = Join-Path $projectRoot "hhj-backend"
$frontendPath = Join-Path $projectRoot "hhj-frontend"
$logPath = Join-Path $projectRoot "logs"

# Ensure log directory exists
if (!(Test-Path $logPath)) {
    New-Item -ItemType Directory -Path $logPath -Force | Out-Null
}

function Test-Administrator {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-PythonInstallation {
    try {
        $pythonVersion = python --version 2>&1
        if ($pythonVersion -match "Python 3\.1[2-9]") {
            Write-Success "Python found: $pythonVersion"
            return $true
        } else {
            Write-Warning "Python 3.12+ required. Found: $pythonVersion"
            return $false
        }
    } catch {
        Write-Error "Python not found in PATH"
        return $false
    }
}

function Test-NodeInstallation {
    try {
        $nodeVersion = node --version 2>&1
        if ($nodeVersion -match "v1[6-9]|v[2-9]\d") {
            Write-Success "Node.js found: $nodeVersion"
            return $true
        } else {
            Write-Warning "Node.js 16+ required. Found: $nodeVersion"
            return $false
        }
    } catch {
        Write-Error "Node.js not found in PATH"
        return $false
    }
}

function Install-Dependencies {
    Write-Header "Installing Dependencies"
    
    # Check Python
    if (!(Test-PythonInstallation)) {
        Write-Info "Installing Python..."
        try {
            winget install Python.Python.3.12
            Write-Success "Python installed successfully"
        } catch {
            Write-Error "Failed to install Python. Please install manually from python.org"
            return $false
        }
    }
    
    # Check Node.js
    if (!(Test-NodeInstallation)) {
        Write-Info "Installing Node.js..."
        try {
            winget install OpenJS.NodeJS
            Write-Success "Node.js installed successfully"
        } catch {
            Write-Error "Failed to install Node.js. Please install manually from nodejs.org"
            return $false
        }
    }
    
    # Install pipenv if not present
    try {
        pip show pipenv | Out-Null
        Write-Success "Pipenv already installed"
    } catch {
        Write-Info "Installing pipenv..."
        pip install pipenv
        Write-Success "Pipenv installed successfully"
    }
    
    return $true
}

function Setup-Backend {
    Write-Header "Setting up Django Backend"
    
    Set-Location $backendPath
    
    # Install Python dependencies
    Write-Info "Installing Python dependencies..."
    pipenv install
    
    # Run migrations
    Write-Info "Running database migrations..."
    pipenv run python manage.py migrate
    
    # Setup initial data
    Write-Info "Setting up initial test data..."
    pipenv run python manage.py setup_initial_data
    
    Write-Success "Backend setup completed"
}

function Setup-Frontend {
    Write-Header "Setting up React Frontend"
    
    Set-Location $frontendPath
    
    # Install Node.js dependencies
    Write-Info "Installing Node.js dependencies..."
    npm install
    
    Write-Success "Frontend setup completed"
}

function Start-Backend {
    Write-Info "Starting Django backend server..."
    Set-Location $backendPath
    
    $backendJob = Start-Job -ScriptBlock {
        param($path)
        Set-Location $path
        pipenv run python manage.py runserver 127.0.0.1:3003
    } -ArgumentList $backendPath
    
    # Wait a moment for server to start
    Start-Sleep 5
    
    # Test if backend is running
    try {
        $response = Invoke-WebRequest -Uri "http://127.0.0.1:3003/admin/" -Method HEAD -TimeoutSec 5
        Write-Success "Backend server started successfully on http://127.0.0.1:3003"
        return $backendJob
    } catch {
        Write-Error "Failed to start backend server"
        Stop-Job $backendJob -Force
        Remove-Job $backendJob -Force
        return $null
    }
}

function Start-Frontend {
    Write-Info "Starting React frontend server..."
    Set-Location $frontendPath
    
    # Set environment variable to prevent browser auto-opening
    $env:BROWSER = "none"
    
    $frontendJob = Start-Job -ScriptBlock {
        param($path)
        Set-Location $path
        $env:BROWSER = "none"
        npm start
    } -ArgumentList $frontendPath
    
    # Wait for frontend to start
    Start-Sleep 10
    
    # Test if frontend is running
    try {
        $response = Invoke-WebRequest -Uri "http://localhost:3000" -Method HEAD -TimeoutSec 10
        Write-Success "Frontend server started successfully on http://localhost:3000"
        return $frontendJob
    } catch {
        Write-Warning "Frontend server may still be starting..."
        return $frontendJob
    }
}

function Create-StartupBatch {
    Write-Header "Creating Startup Scripts"
    
    $startupScript = @"
@echo off
echo Starting Hobby Horse Jumping Championship System...

REM Start backend
echo Starting Django backend...
start "HHJ-Backend" cmd /k "cd /d `"$backendPath`" && pipenv run python manage.py runserver 127.0.0.1:3003"

REM Wait for backend to start
timeout /t 10 /nobreak

REM Start frontend
echo Starting React frontend...
start "HHJ-Frontend" cmd /k "cd /d `"$frontendPath`" && set BROWSER=none && npm start"

echo.
echo System started successfully!
echo Backend: http://127.0.0.1:3003
echo Frontend: http://localhost:3000
echo.
echo Press any key to exit...
pause
"@
    
    $startupBatchPath = Join-Path $projectRoot "start-hhj-system.bat"
    $startupScript | Out-File -FilePath $startupBatchPath -Encoding ASCII
    
    Write-Success "Startup script created: $startupBatchPath"
    
    if ($SetupAutoStart) {
        Setup-WindowsStartup $startupBatchPath
    }
}

function Setup-WindowsStartup($batchPath) {
    Write-Info "Setting up Windows startup..."
    
    # Create a VBS script to run the batch file without showing command prompt
    $vbsScript = @"
Set objShell = WScript.CreateObject("WScript.Shell")
objShell.Run "cmd /c `"$batchPath`"", 0, False
"@
    
    $vbsPath = Join-Path $projectRoot "start-hhj-system.vbs"
    $vbsScript | Out-File -FilePath $vbsPath -Encoding ASCII
    
    # Add to Windows startup folder
    $startupFolder = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup"
    $shortcutPath = Join-Path $startupFolder "HHJ-System.lnk"
    
    $WshShell = New-Object -ComObject WScript.Shell
    $Shortcut = $WshShell.CreateShortcut($shortcutPath)
    $Shortcut.TargetPath = $vbsPath
    $Shortcut.WorkingDirectory = $projectRoot
    $Shortcut.Description = "Hobby Horse Jumping Championship System"
    $Shortcut.Save()
    
    Write-Success "System configured to start automatically on Windows startup"
}

function Show-SystemStatus {
    Write-Header "System Status"
    
    # Check backend
    try {
        $response = Invoke-WebRequest -Uri "http://127.0.0.1:3003/admin/" -Method HEAD -TimeoutSec 3
        Write-Success "Backend: Running on http://127.0.0.1:3003"
    } catch {
        Write-Warning "Backend: Not responding"
    }
    
    # Check frontend
    try {
        $response = Invoke-WebRequest -Uri "http://localhost:3000" -Method HEAD -TimeoutSec 3
        Write-Success "Frontend: Running on http://localhost:3000"
    } catch {
        Write-Warning "Frontend: Not responding"
    }
    
    Write-Host ""
    Write-Info "Admin Panel: http://127.0.0.1:3003/admin/"
    Write-Info "API Endpoint: http://127.0.0.1:3003/timereadings/"
    Write-Info "Display Board: http://localhost:3000"
    Write-Info "WebSocket: ws://127.0.0.1:3003/ws/timings/"
}

function Show-Help {
    Write-Host "Hobby Horse Jumping Championship System - Startup Script"
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  .\startup-script.ps1                 - Full setup and start"
    Write-Host "  .\startup-script.ps1 -InstallOnly    - Only install dependencies"
    Write-Host "  .\startup-script.ps1 -StartOnly      - Only start services"
    Write-Host "  .\startup-script.ps1 -SetupAutoStart - Setup automatic Windows startup"
    Write-Host ""
    Write-Host "This script will:"
    Write-Host "  1. Check and install Python 3.12+ and Node.js 16+"
    Write-Host "  2. Install Python and Node.js dependencies"
    Write-Host "  3. Run Django migrations"
    Write-Host "  4. Start backend server on http://127.0.0.1:3003"
    Write-Host "  5. Start frontend server on http://localhost:3000"
    Write-Host "  6. Create startup scripts for future use"
    Write-Host ""
}

# Main execution
try {
    if ($args -contains "-help" -or $args -contains "--help" -or $args -contains "-h") {
        Show-Help
        exit 0
    }
    
    Write-Header "Hobby Horse Jumping Championship System Setup"
    Write-Info "Project root: $projectRoot"
    
    if (!(Test-Administrator)) {
        Write-Warning "This script should be run as Administrator for best results"
    }
    
    # Validate project structure
    if (!(Test-Path $backendPath) -or !(Test-Path $frontendPath)) {
        Write-Error "Invalid project structure. Make sure you're running from the project root."
        exit 1
    }
    
    if ($InstallOnly) {
        if (Install-Dependencies) {
            Setup-Backend
            Setup-Frontend
            Write-Success "Installation completed successfully!"
        }
        exit 0
    }
    
    if ($StartOnly) {
        Write-Header "Starting Services"
        $backendJob = Start-Backend
        $frontendJob = Start-Frontend
        
        if ($backendJob -and $frontendJob) {
            Show-SystemStatus
            Write-Success "Services started successfully!"
            Write-Info "Press Ctrl+C to stop services"
            
            try {
                while ($true) {
                    Start-Sleep 1
                }
            } finally {
                Write-Info "Stopping services..."
                if ($backendJob) { Stop-Job $backendJob -Force; Remove-Job $backendJob -Force }
                if ($frontendJob) { Stop-Job $frontendJob -Force; Remove-Job $frontendJob -Force }
            }
        }
        exit 0
    }
    
    # Full setup and start
    if (Install-Dependencies) {
        Setup-Backend
        Setup-Frontend
        Create-StartupBatch
        
        Write-Header "Starting System"
        $backendJob = Start-Backend
        $frontendJob = Start-Frontend
        
        if ($backendJob -and $frontendJob) {
            Show-SystemStatus
            Write-Success "🎉 System is ready!"
            Write-Host ""
            Write-Info "Next steps:"
            Write-Info "1. Configure your ESP32 sensors with your WiFi credentials"
            Write-Info "2. Update sensor server URL to: http://YOUR_PC_IP:3003/timereadings/"
            Write-Info "3. Create competitions and participants in the admin panel"
            Write-Info "4. Place sensors at START and FINISH positions"
            Write-Info "5. Open display board on a monitor: http://localhost:3000"
            Write-Host ""
            Write-Info "Press Ctrl+C to stop services or close window to keep running"
            
            try {
                while ($true) {
                    Start-Sleep 1
                }
            } finally {
                Write-Info "Stopping services..."
                if ($backendJob) { Stop-Job $backendJob -Force; Remove-Job $backendJob -Force }
                if ($frontendJob) { Stop-Job $frontendJob -Force; Remove-Job $frontendJob -Force }
            }
        } else {
            Write-Error "Failed to start some services. Check the logs for details."
            exit 1
        }
    } else {
        Write-Error "Dependency installation failed. Please resolve issues and try again."
        exit 1
    }
    
} catch {
    Write-Error "An unexpected error occurred: $_"
    exit 1
} finally {
    Set-Location $projectRoot
}
