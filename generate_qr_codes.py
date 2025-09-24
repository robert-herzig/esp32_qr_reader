#!/usr/bin/env python3
"""
QR Code Generator for ESP32 S3 QR Scanner Testing
Generates sample QR codes with different strings for testing the scanner.
"""

import qrcode
import os
from datetime import datetime

def generate_qr_code(text, filename, size=10, border=4):
    """
    Generate a QR code image for the given text.
    
    Args:
        text (str): The text to encode in the QR code
        filename (str): The output filename for the QR code image
        size (int): The size of each box in pixels
        border (int): The border size in boxes
    """
    qr = qrcode.QRCode(
        version=1,  # Controls the size of the QR Code
        error_correction=qrcode.constants.ERROR_CORRECT_L,  # About 7% or less errors can be corrected
        box_size=size,  # Controls how many pixels each "box" of the QR code is
        border=border,  # Controls how many boxes thick the border should be
    )
    
    qr.add_data(text)
    qr.make(fit=True)
    
    # Create QR code image
    img = qr.make_image(fill_color="black", back_color="white")
    
    # Save the image
    img.save(filename)
    print(f"Generated QR code for '{text}' -> {filename}")

def main():
    """Generate sample QR codes for testing."""
    
    # Create output directory if it doesn't exist
    output_dir = "qr_codes"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Sample QR code data
    qr_data = [
        "Hello ESP32!",
        "QR Scanner Test",
        "https://www.espressif.com",
        "Temperature: 25.3°C",
        "Device ID: ESP32S3-001",
        "Status: Online",
        "User: Admin",
        "https://github.com/espressif/esp32-camera",
        "WiFi: Connected",
        "Battery: 85%",
        "Sensor Data: OK",
        "Version: 1.0.0",
        "Date: " + datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "JSON: {\"temp\":23.5,\"humidity\":60}",
        "Action: START_RECORDING"
    ]
    
    print(f"Generating {len(qr_data)} QR codes...")
    
    # Generate QR codes
    for i, text in enumerate(qr_data, 1):
        filename = os.path.join(output_dir, f"qr_code_{i:02d}.png")
        generate_qr_code(text, filename)
    
    # Generate a small QR code for close-up testing
    generate_qr_code("SMALL TEST", os.path.join(output_dir, "qr_small.png"), size=5, border=2)
    
    # Generate a large QR code for distance testing
    generate_qr_code("LARGE TEST", os.path.join(output_dir, "qr_large.png"), size=15, border=6)
    
    print(f"\nAll QR codes generated in '{output_dir}' directory!")
    print("\nQR Code Contents:")
    print("-" * 40)
    for i, text in enumerate(qr_data, 1):
        print(f"{i:2d}. {text}")
    print(f"16. SMALL TEST (small size)")
    print(f"17. LARGE TEST (large size)")
    
    print("\nUsage Instructions:")
    print("1. Print or display these QR codes on your screen")
    print("2. Point your ESP32 S3 camera at the QR codes")
    print("3. Check the web interface for detection results")

if __name__ == "__main__":
    main()
