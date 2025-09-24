# ESP32 S3 QR Code Scanner

This project converts an ESP32 S3 camera module into a real-time QR code scanner with a web interface. The camera stream displays live video while continuously scanning for QR codes and showing the decoded results.

## Features

- 🔍 Real-time QR code detection and decoding
- 📹 Live camera stream via web interface
- 🌐 Web-based control panel with QR status updates
- 📱 Responsive design for mobile and desktop
- 💾 Screenshot capture to SD card
- 🔄 Automatic QR code detection timeout (5 seconds)

## Hardware Requirements

- ESP32-S3-EYE development board (or compatible ESP32 S3 with camera)
- MicroSD card (optional, for screenshot storage)
- WiFi network connection

## Software Requirements

### ESP32 Arduino IDE Setup

1. **Install ESP32 Board Package**:
   - Open Arduino IDE
   - Go to File → Preferences
   - Add this URL to "Additional Board Manager URLs": 
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Go to Tools → Board → Boards Manager
   - Search for "esp32" and install "esp32 by Espressif Systems"

2. **Install Required Libraries**:
   - **ESP32QRCodeReader**: 
     - Go to Tools → Manage Libraries
     - Search for "ESP32QRCodeReader" and install it
     - Or download from: https://github.com/alvarowolfx/ESP32QRCodeReader

### Python QR Code Generator Setup

1. **Install Python Requirements**:
   ```bash
   pip install -r requirements.txt
   ```

2. **Generate Test QR Codes**:
   ```bash
   python generate_qr_codes.py
   ```

## Configuration

1. **WiFi Settings**:
   - Open `ESP32S3QRSCANNER.ino`
   - Update these lines with your WiFi credentials:
   ```cpp
   const char* ssid     = "YOUR_WIFI_NAME";
   const char* password = "YOUR_WIFI_PASSWORD";  
   ```

2. **Camera Model**:
   - The code is configured for ESP32S3_EYE by default
   - If using a different camera module, update the camera model in `ESP32S3QRSCANNER.ino`:
   ```cpp
   #define CAMERA_MODEL_ESP32S3_EYE // Change this if needed
   ```

## Installation

1. **Upload Code to ESP32**:
   - Open `ESP32S3QRSCANNER.ino` in Arduino IDE
   - Select your ESP32 S3 board under Tools → Board
   - Select the correct COM port under Tools → Port
   - Click Upload

2. **Generate QR Codes for Testing**:
   ```bash
   cd /path/to/project
   python generate_qr_codes.py
   ```
   This creates a `qr_codes/` directory with sample QR codes for testing.

## Usage

1. **Connect to ESP32**:
   - After uploading, open Serial Monitor (115200 baud)
   - Wait for WiFi connection
   - Note the IP address displayed (e.g., `192.168.1.100`)

2. **Access Web Interface**:
   - Open browser and go to: `http://YOUR_ESP32_IP/`
   - You should see the QR Code Scanner interface

3. **Test QR Code Detection**:
   - Print or display the generated QR codes on screen
   - Point the ESP32 camera at the QR codes
   - Watch the web interface for real-time detection results

## Web Interface Features

- **Live Video Stream**: Real-time camera feed with 180° rotation
- **QR Status Panel**: Shows detection status with color-coded indicators
  - 🟢 Green: QR code currently detected  
  - 🟠 Orange: Scanning for QR codes
- **Decoded Data Display**: Shows the actual content of detected QR codes
- **Timestamp**: When the QR code was last detected
- **Screenshot Button**: Saves current frame to SD card

## API Endpoints

- `GET /` - Main web interface
- `GET /stream` - MJPEG video stream (port 81)
- `GET /qr_status` - JSON API for QR code status
- `POST /button` - Capture screenshot to SD card

### QR Status JSON Format
```json
{
  "detected": true,
  "data": "Hello ESP32!",
  "timestamp": 1234567890,
  "uptime": 123456
}
```

## Troubleshooting

### Common Issues

1. **Camera Init Failed**:
   - Check camera module connections
   - Verify camera model selection in code
   - Ensure sufficient power supply

2. **WiFi Connection Issues**:
   - Verify SSID and password in code
   - Check WiFi network availability
   - Ensure ESP32 is within range

3. **QR Codes Not Detected**:
   - Ensure good lighting conditions
   - Try different QR code sizes (use generated test codes)
   - Check camera focus and positioning
   - Verify ESP32QRCodeReader library is installed

4. **Web Interface Not Loading**:
   - Check Serial Monitor for IP address
   - Ensure you're on the same network as ESP32
   - Try refreshing the page
   - Check firewall settings

### Performance Tips

- Use high-contrast QR codes (black on white)
- Ensure adequate lighting
- Position QR codes 10-30cm from camera
- Use larger QR codes for better detection
- Avoid reflective surfaces

## Code Structure

```
ESP32S3QRSCANNER/
├── ESP32S3QRSCANNER.ino    # Main Arduino sketch
├── app_httpd.cpp           # Web server and HTTP handlers  
├── camera_pins.h           # Camera pin definitions
├── sd_read_write.cpp       # SD card functions
└── sd_read_write.h         # SD card header

Root Directory/
├── generate_qr_codes.py    # Python QR code generator
├── requirements.txt        # Python dependencies
└── README.md              # This file
```

## Customization

### Adding New QR Code Types
Edit `generate_qr_codes.py` to add custom QR code content:
```python
qr_data = [
    "Your custom text here",
    "https://your-website.com", 
    '{"sensor": "temperature", "value": 25.3}',
    # Add more as needed
]
```

### Modifying Detection Timeout
Change the timeout period in `ESP32S3QRSCANNER.ino`:
```cpp
// Clear QR code detection flag after X milliseconds  
if (qrCodeDetected && (millis() - lastQRCodeTime > 5000)) { // 5000 = 5 seconds
```

### Styling the Web Interface
Modify the CSS in the `index_web` string in `app_httpd.cpp` to customize appearance.

## License

This project is based on the Freenove ESP32 camera examples and incorporates the ESP32QRCodeReader library. Please check individual component licenses for specific terms.

## Contributing

Feel free to submit issues, feature requests, or pull requests to improve this QR code scanner project.
