#!/bin/bash

# Test script for Hobby Horse Jumping timing system
# This script tests the signal tracking and reconciliation features

echo "=== Hobby Horse Jumping System Test ==="
echo "Testing signal tracking and reconciliation features"
echo ""

# Check if backend is running
echo "Checking backend status..."
curl -s http://127.0.0.1:3003/api/competitions/ > /dev/null
if [ $? -eq 0 ]; then
    echo "✅ Backend is running"
else
    echo "❌ Backend is not running. Please start: python manage.py runserver 3003"
    exit 1
fi

# Check if frontend is running
echo "Checking frontend status..."
curl -s http://localhost:3000 > /dev/null
if [ $? -eq 0 ]; then
    echo "✅ Frontend is running"
else
    echo "❌ Frontend is not running. Please start: npm start"
    exit 1
fi

echo ""
echo "=== Testing Signal Generation ==="

# Generate test START signal
echo "Sending START signal..."
curl -X POST http://127.0.0.1:3003/timereadings/ \
  -H "Content-Type: application/json" \
  -d '{
    "time_mark": "START",
    "sensor_time": "'$(date +%s%3N)'"
  }'

sleep 2

# Generate test FINISH signal
echo "Sending FINISH signal..."
curl -X POST http://127.0.0.1:3003/timereadings/ \
  -H "Content-Type: application/json" \
  -d '{
    "time_mark": "FINISH",
    "sensor_time": "'$(( $(date +%s%3N) + 5000 ))'"
  }'

echo ""
echo "✅ Test signals sent successfully!"
echo ""
echo "Next steps:"
echo "1. Check the dashboard at http://localhost:3000"
echo "2. Verify that the SignalTracking widget shows the test signals"
echo "3. Test reconciliation if multiple signals are present"
echo "4. Verify timer starts and stops correctly with signals"
