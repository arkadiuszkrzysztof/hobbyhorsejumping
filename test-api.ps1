# PowerShell API Testing Script for Hobby Horse Jumping Timing System
# This script allows you to test the API endpoints directly from PowerShell

param(
    [string]$Command = "",
    [string]$ApiUrl = "http://127.0.0.1:3003"
)

function Get-CurrentTimestamp {
    return [int64](([datetime]::UtcNow) - (get-date "1/1/1970")).TotalMilliseconds
}

function Test-ApiConnection {
    Write-Host "🔍 Testing API connection..." -ForegroundColor Cyan
    try {
        $response = Invoke-RestMethod -Uri "$ApiUrl/api/competitions/" -Method Get -ErrorAction Stop
        Write-Host "✅ API connected! Found $($response.Count) competitions" -ForegroundColor Green
        return $true
    }
    catch {
        if ($_.Exception.Message -like "*connection*") {
            Write-Host "❌ Could not connect to API. Make sure the Django server is running." -ForegroundColor Red
        }
        else {
            Write-Host "❌ API error: $($_.Exception.Message)" -ForegroundColor Red
        }
        return $false
    }
}
}

function Get-ActiveCompetitor {
    try {
        $response = Invoke-RestMethod -Uri "$ApiUrl/api/active-competitor/" -Method Get -ErrorAction Stop
        if ($response.active_competitor) {
            $competitor = $response.active_competitor
            Write-Host "✅ Active competitor: #$($competitor.starting_order) $($competitor.participant.name)" -ForegroundColor Green
            return $competitor
        }
        else {
            Write-Host "⚠️ No active competitor set" -ForegroundColor Yellow
            return $null
        }
    }
    catch {
        Write-Host "❌ Failed to get active competitor: $($_.Exception.Message)" -ForegroundColor Red
        return $null
    }
}

function Send-TimingSignal {
    param(
        [string]$TimeMark,
        [int64]$SensorTime = 0
    )
    
    if ($SensorTime -eq 0) {
        $SensorTime = Get-CurrentTimestamp
    }
    
    $payload = @{
        time_mark = $TimeMark
        sensor_time = $SensorTime.ToString()
    } | ConvertTo-Json
    
    Write-Host "📡 Sending $TimeMark signal (timestamp: $SensorTime)..." -ForegroundColor Cyan
    
    try {
        $headers = @{
            "Content-Type" = "application/json"
        }
        
        $response = Invoke-RestMethod -Uri "$ApiUrl/timereadings/" -Method Post -Body $payload -Headers $headers -ErrorAction Stop
        Write-Host "✅ $TimeMark signal sent successfully!" -ForegroundColor Green
        Write-Host "   Server response: $($response | ConvertTo-Json -Depth 3)" -ForegroundColor Gray
        return $true
    }
    catch {
        Write-Host "❌ Failed to send $TimeMark signal: $($_.Exception.Message)" -ForegroundColor Red
        if ($_.ErrorDetails.Message) {
            Write-Host "   Details: $($_.ErrorDetails.Message)" -ForegroundColor Red
        }
        return $false
    }
}

function Start-CompleteRun {
    Write-Host "🏁 Simulating complete timing run..." -ForegroundColor Magenta
    
    # Send START signal
    if (-not (Send-TimingSignal -TimeMark "START")) {
        Write-Host "❌ Failed to send START signal, aborting run" -ForegroundColor Red
        return $false
    }
    
    Write-Host "⏳ Waiting 3 seconds..." -ForegroundColor Yellow
    Start-Sleep -Seconds 3
    
    # Send FINISH signal
    if (-not (Send-TimingSignal -TimeMark "FINISH")) {
        Write-Host "❌ Failed to send FINISH signal" -ForegroundColor Red
        return $false
    }
    
    Write-Host "✅ Complete run simulation finished!" -ForegroundColor Green
    return $true
}

function Start-InteractiveMode {
    Write-Host "`n🧪 Interactive Testing Mode" -ForegroundColor Cyan
    Write-Host "Available commands:" -ForegroundColor White
    Write-Host "  1 - Send START signal" -ForegroundColor Gray
    Write-Host "  2 - Send FINISH signal" -ForegroundColor Gray
    Write-Host "  3 - Simulate complete run" -ForegroundColor Gray
    Write-Host "  4 - Check active competitor" -ForegroundColor Gray
    Write-Host "  5 - Test API connection" -ForegroundColor Gray
    Write-Host "  q - Quit" -ForegroundColor Gray
    
    while ($true) {
        $choice = Read-Host "`nEnter command"
        
        switch ($choice.ToLower()) {
            'q' { 
                Write-Host "👋 Goodbye!" -ForegroundColor Green
                break 
            }
            '1' { Send-TimingSignal -TimeMark "START" }
            '2' { Send-TimingSignal -TimeMark "FINISH" }
            '3' { Start-CompleteRun }
            '4' { Get-ActiveCompetitor }
            '5' { Test-ApiConnection }
            default { 
                Write-Host "❌ Invalid command. Please try again." -ForegroundColor Red 
            }
        }
        
        if ($choice.ToLower() -eq 'q') { break }
    }
}

# Main execution
Write-Host "🐎 Hobby Horse Jumping - API Testing Tool" -ForegroundColor Magenta
Write-Host "=========================================" -ForegroundColor Magenta

# Test API connection first
if (-not (Test-ApiConnection)) {
    exit 1
}

# Check for active competitor
Get-ActiveCompetitor | Out-Null

# Handle command line arguments
if ($Command) {
    switch ($Command.ToUpper()) {
        "START" { Send-TimingSignal -TimeMark "START" }
        "FINISH" { Send-TimingSignal -TimeMark "FINISH" }
        "RUN" { Start-CompleteRun }
        default {
            Write-Host "❌ Unknown command: $Command" -ForegroundColor Red
            Write-Host "Available commands: START, FINISH, RUN" -ForegroundColor Yellow
        }
    }
}
else {
    # Enter interactive mode
    Start-InteractiveMode
}

Write-Host "`nScript completed." -ForegroundColor Green
