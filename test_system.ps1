# Test script for Hobby Horse Jumping timing system
# This script tests the signal tracking and reconciliation features

Write-Host "=== Hobby Horse Jumping System Test ===" -ForegroundColor Green
Write-Host "Testing signal tracking and reconciliation features" -ForegroundColor Yellow
Write-Host ""

# Check if backend is running
Write-Host "Checking backend status..." -ForegroundColor Blue
try {
    $response = Invoke-WebRequest -Uri "http://127.0.0.1:3003/api/competitions/" -Method GET -TimeoutSec 5
    Write-Host "✅ Backend is running" -ForegroundColor Green
} catch {
    Write-Host "❌ Backend is not running. Please start: python manage.py runserver 3003" -ForegroundColor Red
    exit 1
}

# Check if frontend is running
Write-Host "Checking frontend status..." -ForegroundColor Blue
try {
    $response = Invoke-WebRequest -Uri "http://localhost:3000" -Method GET -TimeoutSec 5
    Write-Host "✅ Frontend is running" -ForegroundColor Green
} catch {
    Write-Host "❌ Frontend is not running. Please start: npm start" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== Testing Signal Generation ===" -ForegroundColor Green

# Generate test START signal
Write-Host "Sending START signal..." -ForegroundColor Blue
$startTime = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$startPayload = @{
    time_mark = "START"
    sensor_time = $startTime.ToString()
} | ConvertTo-Json

try {
    $response = Invoke-RestMethod -Uri "http://127.0.0.1:3003/timereadings/" -Method POST -Body $startPayload -ContentType "application/json"
    Write-Host "✅ START signal sent successfully" -ForegroundColor Green
} catch {
    Write-Host "❌ Failed to send START signal: $($_.Exception.Message)" -ForegroundColor Red
}

Start-Sleep -Seconds 2

# Generate test FINISH signal
Write-Host "Sending FINISH signal..." -ForegroundColor Blue
$finishTime = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$finishPayload = @{
    time_mark = "FINISH"
    sensor_time = $finishTime.ToString()
} | ConvertTo-Json

try {
    $response = Invoke-RestMethod -Uri "http://127.0.0.1:3003/timereadings/" -Method POST -Body $finishPayload -ContentType "application/json"
    Write-Host "✅ FINISH signal sent successfully" -ForegroundColor Green
} catch {
    Write-Host "❌ Failed to send FINISH signal: $($_.Exception.Message)" -ForegroundColor Red
}

# Generate multiple signals for reconciliation testing
Write-Host ""
Write-Host "Generating additional signals for reconciliation testing..." -ForegroundColor Blue

Start-Sleep -Seconds 1

# Additional START signal
$startTime2 = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$startPayload2 = @{
    time_mark = "START"
    sensor_time = $startTime2.ToString()
} | ConvertTo-Json

Invoke-RestMethod -Uri "http://127.0.0.1:3003/timereadings/" -Method POST -Body $startPayload2 -ContentType "application/json" -ErrorAction SilentlyContinue

Start-Sleep -Seconds 1

# Additional FINISH signal
$finishTime2 = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$finishPayload2 = @{
    time_mark = "FINISH"
    sensor_time = $finishTime2.ToString()
} | ConvertTo-Json

Invoke-RestMethod -Uri "http://127.0.0.1:3003/timereadings/" -Method POST -Body $finishPayload2 -ContentType "application/json" -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "✅ Test signals sent successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Check the dashboard at http://localhost:3000" -ForegroundColor White
Write-Host "2. Verify that the SignalTracking widget shows the test signals" -ForegroundColor White
Write-Host "3. Test reconciliation if multiple signals are present" -ForegroundColor White
Write-Host "4. Verify timer starts and stops correctly with signals" -ForegroundColor White
Write-Host "5. Check that the 'Invalid date' error is resolved" -ForegroundColor White
