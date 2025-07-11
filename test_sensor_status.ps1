# Enhanced test script for Hobby Horse Jumping timing system
# Tests sensor alive signals and status monitoring

Write-Host "=== Hobby Horse Jumping System Test - Enhanced ===" -ForegroundColor Green
Write-Host "Testing sensor alive signals and status monitoring" -ForegroundColor Yellow
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
Write-Host "=== Testing Sensor Registration and Alive Signals ===" -ForegroundColor Green

# Function to send alive signal
function Send-AliveSignal {
    param(
        [string]$SensorId,
        [string]$SensorType
    )
    
    $timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $payload = @{
        time_mark = "ALIVE"
        sensor_time = $timestamp.ToString()
        sensor_id = $SensorId
    } | ConvertTo-Json

    try {
        $response = Invoke-RestMethod -Uri "http://127.0.0.1:3003/timereadings/" -Method POST -Body $payload -ContentType "application/json"
        Write-Host "✅ ALIVE signal sent from $SensorId" -ForegroundColor Green
        return $true
    } catch {
        Write-Host "❌ Failed to send ALIVE signal from $SensorId`: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

# Function to send timing signal
function Send-TimingSignal {
    param(
        [string]$TimeMark,
        [string]$SensorId
    )
    
    $timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $payload = @{
        time_mark = $TimeMark
        sensor_time = $timestamp.ToString()
        sensor_id = $SensorId
    } | ConvertTo-Json

    try {
        $response = Invoke-RestMethod -Uri "http://127.0.0.1:3003/timereadings/" -Method POST -Body $payload -ContentType "application/json"
        Write-Host "✅ $TimeMark signal sent from $SensorId" -ForegroundColor Green
        return $true
    } catch {
        Write-Host "❌ Failed to send $TimeMark signal from $SensorId`: $($_.Exception.Message)" -ForegroundColor Red
        return $false
    }
}

# Register sensors with alive signals
Write-Host "Registering START sensor..." -ForegroundColor Blue
Send-AliveSignal -SensorId "START_SENSOR" -SensorType "START"

Start-Sleep -Seconds 1

Write-Host "Registering FINISH sensor..." -ForegroundColor Blue
Send-AliveSignal -SensorId "FINISH_SENSOR" -SensorType "FINISH"

Start-Sleep -Seconds 2

# Check sensor status
Write-Host ""
Write-Host "Checking sensor status..." -ForegroundColor Blue
try {
    $sensorStatus = Invoke-RestMethod -Uri "http://127.0.0.1:3003/sensor-status/" -Method GET
    Write-Host "📡 Sensor Status:" -ForegroundColor Yellow
    Write-Host "   Total sensors: $($sensorStatus.total_sensors)" -ForegroundColor White
    Write-Host "   Online sensors: $($sensorStatus.online_sensors)" -ForegroundColor White
    
    foreach ($sensor in $sensorStatus.sensors) {
        $status = if ($sensor.is_online) { "✅ Online" } else { "❌ Offline" }
        $lastSignal = if ($sensor.last_alive_signal) { 
            $lastTime = [DateTime]::Parse($sensor.last_alive_signal)
            "$(([DateTime]::Now - $lastTime).TotalSeconds) seconds ago"
        } else { 
            "Never" 
        }
        Write-Host "   $($sensor.sensor_id) ($($sensor.sensor_type)): $status (Last: $lastSignal)" -ForegroundColor White
    }
} catch {
    Write-Host "❌ Failed to fetch sensor status: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Testing Complete Timing Sequence ===" -ForegroundColor Green

# Send START signal
Write-Host "Sending START signal..." -ForegroundColor Blue
Send-TimingSignal -TimeMark "START" -SensorId "START_SENSOR"

Start-Sleep -Seconds 3

# Send FINISH signal
Write-Host "Sending FINISH signal..." -ForegroundColor Blue
Send-TimingSignal -TimeMark "FINISH" -SensorId "FINISH_SENSOR"

Start-Sleep -Seconds 2

Write-Host ""
Write-Host "=== Testing Sensor Alive Signal Simulation ===" -ForegroundColor Green
Write-Host "Simulating ongoing alive signals (like real sensors would send)..." -ForegroundColor Blue

# Send multiple alive signals to simulate real sensor behavior
for ($i = 1; $i -le 3; $i++) {
    Write-Host "Alive signal round $i..." -ForegroundColor Cyan
    Send-AliveSignal -SensorId "START_SENSOR" -SensorType "START"
    Send-AliveSignal -SensorId "FINISH_SENSOR" -SensorType "FINISH"
    
    if ($i -lt 3) {
        Start-Sleep -Seconds 5
    }
}

# Final sensor status check
Write-Host ""
Write-Host "Final sensor status check..." -ForegroundColor Blue
try {
    $finalStatus = Invoke-RestMethod -Uri "http://127.0.0.1:3003/sensor-status/" -Method GET
    Write-Host "📡 Final Sensor Status:" -ForegroundColor Yellow
    
    foreach ($sensor in $finalStatus.sensors) {
        $status = if ($sensor.is_online) { "✅ Online" } else { "❌ Offline" }
        $lastSignal = if ($sensor.last_alive_signal) { 
            $lastTime = [DateTime]::Parse($sensor.last_alive_signal)
            "$(([DateTime]::Now - $lastTime).TotalSeconds) seconds ago"
        } else { 
            "Never" 
        }
        Write-Host "   $($sensor.sensor_id): $status (Last alive: $lastSignal)" -ForegroundColor White
    }
} catch {
    Write-Host "❌ Failed to fetch final sensor status: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Write-Host "✅ Enhanced testing completed!" -ForegroundColor Green
Write-Host ""
Write-Host "Verification steps:" -ForegroundColor Yellow
Write-Host "1. Check the dashboard at http://localhost:3000" -ForegroundColor White
Write-Host "2. Verify the 'System Status' widget shows sensor information" -ForegroundColor White
Write-Host "3. Expand the System Status widget to see detailed sensor status" -ForegroundColor White
Write-Host "4. Confirm START_SENSOR and FINISH_SENSOR show as 'Online'" -ForegroundColor White
Write-Host "5. Check that alive signals are being received and timestamps are recent" -ForegroundColor White
Write-Host "6. Verify WebSocket connection shows as 'Connected'" -ForegroundColor White
Write-Host "7. Confirm connected displays count is shown" -ForegroundColor White
Write-Host ""
Write-Host "Real sensor simulation:" -ForegroundColor Yellow
Write-Host "- Upload the updated hhj-sensor.ino to your ESP32 sensors" -ForegroundColor White
Write-Host "- Sensors will now send ALIVE signals every 10 seconds automatically" -ForegroundColor White
Write-Host "- Timing signals will still have priority over alive signals" -ForegroundColor White
