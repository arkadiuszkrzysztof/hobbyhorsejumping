# Enhanced Hobby Horse Jumping Championship Setup

Write-Host "🐎 Starting Enhanced Hobby Horse Jumping Championship System..." -ForegroundColor Green

# Start backend server in a new window
Write-Host "Starting Django backend server..." -ForegroundColor Yellow
Start-Process powershell -ArgumentList "-NoExit", "-Command", "cd 'c:\Users\chanandler\development\hobbyhorsejumping\hhj-backend'; python manage.py runserver 127.0.0.1:3003"

# Wait for backend to start
Start-Sleep -Seconds 3

# Start frontend development server in a new window  
Write-Host "Starting React frontend development server..." -ForegroundColor Yellow
Start-Process powershell -ArgumentList "-NoExit", "-Command", "cd 'c:\Users\chanandler\development\hobbyhorsejumping\hhj-frontend'; npm start"

# Wait for frontend to start
Start-Sleep -Seconds 5

Write-Host "🎯 System Components Started!" -ForegroundColor Green
Write-Host ""
Write-Host "📋 Available URLs:" -ForegroundColor Cyan
Write-Host "  • Internal Dashboard: http://localhost:3000" -ForegroundColor White
Write-Host "  • Public Display: http://localhost:3000/public" -ForegroundColor White
Write-Host "  • Backend API: http://127.0.0.1:3003" -ForegroundColor White
Write-Host ""
Write-Host "🔧 New Features Added:" -ForegroundColor Magenta
Write-Host "  ✅ Competitor reordering (drag & drop + buttons)" -ForegroundColor Green
Write-Host "  ✅ Real-time state synchronization via WebSocket" -ForegroundColor Green
Write-Host "  ✅ Public display for competition results" -ForegroundColor Green
Write-Host "  ✅ Persistent selection across multiple screens" -ForegroundColor Green
Write-Host "  ✅ Enhanced mock data for testing" -ForegroundColor Green
Write-Host ""
Write-Host "🎮 How to Use:" -ForegroundColor Yellow
Write-Host "  1. Open Internal Dashboard (http://localhost:3000)" -ForegroundColor White
Write-Host "  2. Select a competition from the dropdown" -ForegroundColor White
Write-Host "  3. Click the 🔢 icon to reorder competitors" -ForegroundColor White
Write-Host "  4. Open Public Display (http://localhost:3000/public) on another screen" -ForegroundColor White
Write-Host "  5. Changes will sync automatically between screens" -ForegroundColor White
Write-Host ""
Write-Host "🧪 Testing:" -ForegroundColor Cyan
Write-Host "  • Use the 🐛 icon for manual API testing" -ForegroundColor White
Write-Host "  • Select competitors and test START/FINISH timings" -ForegroundColor White
Write-Host "  • Reorder starting list and see changes on public display" -ForegroundColor White
Write-Host ""

# Open the main dashboard
Write-Host "Opening main dashboard in browser..." -ForegroundColor Green
Start-Process "http://localhost:3000"

Write-Host "Ready! 🚀" -ForegroundColor Green
