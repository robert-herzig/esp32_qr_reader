# Generate QR Codes PowerShell Script
Write-Host "ESP32 S3 QR Code Generator" -ForegroundColor Green
Write-Host "=========================" -ForegroundColor Green
Write-Host ""

Write-Host "Activating virtual environment..." -ForegroundColor Yellow
& .\venv\Scripts\Activate.ps1

Write-Host "Generating QR codes..." -ForegroundColor Yellow
python generate_qr_codes.py

Write-Host ""
Write-Host "QR codes generated successfully!" -ForegroundColor Green
Write-Host "Check the 'qr_codes' directory for the generated images." -ForegroundColor Green
Write-Host ""

Read-Host "Press Enter to continue..."
