# Simple PowerShell API Testing Script for Hobby Horse Jumping
param(
    [string]$Command = "",
    [string]$ApiUrl = "http://127.0.0.1:3003"
)

function Get-CurrentTimestamp {
    return [int64](([datetime]::UtcNow) - (get-date "1/1/1970")).TotalMilliseconds
}

function Test-ApiConnection {
    Write-Host "Testing API connection..." -ForegroundColor Cyan
    try {
        $response = Invoke-RestMethod -Uri "$ApiUrl/api/competitions/" -Method Get -ErrorAction Stop
        Write-Host "API connected! Found $($response.Count) competitions" -ForegroundColor Green
        return $true
    }
    catch {
        Write-Host "Could not connect to API. Make sure the Django server is running." -ForegroundColor Red
        return $false
    }
}

function Send-TimingSignal {
    param(
        [string]$TimeMark
    )
    
    $SensorTime = Get-CurrentTimestamp
    $payload = @{
        time_mark = $TimeMark
        sensor_time = $SensorTime.ToString()
    } | ConvertTo-Json
    
    Write-Host "Sending $TimeMark signal..." -ForegroundColor Cyan
    
    try {
        $headers = @{
            "Content-Type" = "application/json"
        }
        
        $response = Invoke-RestMethod -Uri "$ApiUrl/timereadings/" -Method Post -Body $payload -Headers $headers -ErrorAction Stop
        Write-Host "$TimeMark signal sent successfully!" -ForegroundColor Green
        return $true
    }
    catch {
        Write-Host "Failed to send $TimeMark signal: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

# Main execution
Write-Host "Hobby Horse Jumping - API Testing Tool" -ForegroundColor Magenta
Write-Host "======================================" -ForegroundColor Magenta

# Test API connection first
if (-not (Test-ApiConnection)) {
    exit 1
}

# Handle command line arguments
if ($Command) {
    switch ($Command.ToUpper()) {
        "START" { Send-TimingSignal -TimeMark "START" }
        "FINISH" { Send-TimingSignal -TimeMark "FINISH" }
        "RUN" { 
            Write-Host "Simulating complete run..." -ForegroundColor Magenta
            Send-TimingSignal -TimeMark "START"
            Start-Sleep -Seconds 3
            Send-TimingSignal -TimeMark "FINISH"
            Write-Host "Complete run finished!" -ForegroundColor Green
        }
        default {
            Write-Host "Unknown command: $Command" -ForegroundColor Red
            Write-Host "Available commands: START, FINISH, RUN" -ForegroundColor Yellow
        }
    }
}
else {
    Write-Host "Usage: .\test-api-simple.ps1 -Command [START|FINISH|RUN]" -ForegroundColor Yellow
    Write-Host "Example: .\test-api-simple.ps1 -Command START" -ForegroundColor Gray
}

Write-Host "Script completed." -ForegroundColor Green
