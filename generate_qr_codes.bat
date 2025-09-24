@echo off
echo Activating virtual environment...
call venv\Scripts\activate.bat

echo Generating QR codes...
python generate_qr_codes.py

echo.
echo QR codes generated successfully!
echo Check the 'qr_codes' directory for the generated images.
echo.

pause
