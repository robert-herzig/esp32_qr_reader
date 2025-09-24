/**********************************************************************
  Filename    : ESP32 S3 QR Code Scanner
  Description : QR code scanner using ESP32 camera and       if (fb->format == PIXFORMAT_GRAYSCALE) {
        Serial.pr            if (fb->format == PIXFORMAT_JPEG) {
        Serial.println("🔄 Converting JPEG to grayscale using ESP32 built-in conversion...");f (fb->format == PIXFORMAT_JPEG) {
        Serial.println("🔄 Converting JPEG to grayscale for QR detection...");
        
        // Use a smaller resolution for QR detection (quirc works better with smaller images)
        int qr_width = 160;   // Half the camera resolution
        int qr_height = 120;  // Half the camera resolution if (fb->format == PIXFORMAT_JPEG) {
        Serial.println("✅ Got JPEG image - will skip QR detection for now (testing camera stability)");tln("✅ Processing grayscale image for QR detection...");irc library
  Auther      : Modified from www.freenove.com
  Modification: 2024/09/24 - QR scanning with quirc library (Espressif recommended)
**********************************************************************/
#include "esp_camera.h"
#include "quirc.h"
#include "img_converters.h"  // ESP32 built-in image conversion functions
#include "sd_read_write.h"   // SD card functions
#include "img_converters.h"  // ESP32 built-in image conversion functions

// Select camera model - use same as your working video stream
#define CAMERA_MODEL_ESP32S3_EYE // This is what your working video stream used!
#include "camera_pins.h"

// QR Code detection variables
struct quirc *qr_recognizer = NULL;
String lastQRCodeData = "";
unsigned long lastQRCodeTime = 0;
int qrCodeCount = 0;

// Function declarations

void cameraInit() {
  Serial.println("Using EXACT same camera config as your working video stream...");
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  // Use JPEG mode since that's what worked in your video streaming!
  config.xclk_freq_hz = 10000000;       // Same as your working video stream
  config.frame_size = FRAMESIZE_QVGA;    // 320x240 - reasonable for QR detection
  config.pixel_format = PIXFORMAT_JPEG;  // Same as your working setup!
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;  // Conservative mode
  config.fb_location = CAMERA_FB_IN_DRAM;     // DRAM only
  config.jpeg_quality = 12;             // Same as your working config
  config.fb_count = 1;                  // Single buffer only
  
  Serial.println("Using JPEG mode like your working video stream: 320x240 JPEG @ 10MHz");

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    Serial.println("Common error codes:");
    Serial.println("0x101 = ESP_ERR_INVALID_ARG");
    Serial.println("0x102 = ESP_ERR_INVALID_STATE"); 
    Serial.println("0x103 = ESP_ERR_INVALID_SIZE");
    Serial.println("0x105 = ESP_ERR_NOT_FOUND");
    return;
  }
  
  Serial.println("✓ Camera hardware initialized successfully!");
  
  // Check if sensor is detected
  sensor_t * sensor_check = esp_camera_sensor_get();
  if (sensor_check) {
    Serial.printf("✓ Camera sensor detected: ID = 0x%x\n", sensor_check->id.PID);
  } else {
    Serial.println("✗ Camera sensor not detected!");
  }

  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    // Conservative sensor settings to prevent DMA overflow
    s->set_vflip(s, 1);          // flip it back
    s->set_brightness(s, 0);     // Normal brightness
    s->set_contrast(s, 0);       // Normal contrast  
    s->set_saturation(s, 0);     // Not relevant for grayscale, but set anyway
    s->set_gainceiling(s, (gainceiling_t)0);  // Lowest gain ceiling
    s->set_quality(s, 12);       // Not used for grayscale
    s->set_colorbar(s, 0);       // Disable color bar
    s->set_whitebal(s, 1);       // Enable auto white balance
    s->set_gain_ctrl(s, 1);      // Enable auto gain control
    s->set_exposure_ctrl(s, 1);  // Enable auto exposure
    s->set_hmirror(s, 0);        // No horizontal mirror
    s->set_wpc(s, 1);            // Enable white pixel correction
    s->set_bpc(s, 0);            // Disable black pixel correction (can cause issues)
    s->set_lenc(s, 1);           // Enable lens correction
    s->set_dcw(s, 1);            // Enable DCW (downsize)
    Serial.println("✓ Conservative sensor settings applied for stability");
  } else {
    Serial.println("WARNING: Could not configure sensor settings");
  }
  
  Serial.println("Camera configured for stable QR detection");
  
  // Give camera extra time to fully initialize
  Serial.println("Waiting for camera to stabilize...");
  delay(2000);
  
  // Test capture to verify camera works before main loop
  Serial.println("Testing camera capture...");
  
  // Try multiple capture attempts with delays
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("Capture attempt %d...\n", attempt);
    
    camera_fb_t * test_fb = esp_camera_fb_get();
    if (test_fb) {
      Serial.printf("✓ Test capture SUCCESS: %dx%d, %d bytes, format: %d\n", 
                    test_fb->width, test_fb->height, test_fb->len, test_fb->format);
      esp_camera_fb_return(test_fb);
      Serial.println("🎉 Camera is working properly!");
      break;
    } else {
      Serial.printf("✗ Capture attempt %d failed\n", attempt);
      if (attempt < 3) {
        Serial.println("Waiting before retry...");
        delay(1000);
        
        // Try adjusting sensor settings for Freenove board
        sensor_t * s = esp_camera_sensor_get();
        if (s) {
          Serial.println("Trying Freenove-specific sensor adjustments...");
          s->set_vflip(s, 0);        // Try no vflip for Freenove
          s->set_hmirror(s, 1);      // Try hmirror for Freenove
          s->set_brightness(s, 2);   // Higher brightness
          s->set_contrast(s, 2);     // Higher contrast
          delay(500);
        }
      }
    }
  }
  
  Serial.println("Proceeding to main loop...");
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println();
  Serial.println("===============================");
  Serial.println("ESP32 S3 QR Code Scanner v3.0");
  Serial.println("Using quirc library (Espressif recommended)");
  Serial.println("===============================");
  Serial.println();

  // Check PSRAM
  if (psramFound()) {
    Serial.println("PSRAM found and available");
    Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("WARNING: PSRAM not found - may affect performance");
  }

  // Initialize SD Card
  Serial.println("Initializing SD card...");
  sdmmcInit();  // Use the correct function name from sd_read_write.h
  
  // Test if SD card is working
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("⚠️ SD card initialization failed - will skip image saving");
  } else {
    Serial.println("✓ SD card initialized successfully!");
    Serial.printf("SD Card Type: %s\n", 
                  SD_MMC.cardType() == CARD_MMC ? "MMC" : 
                  SD_MMC.cardType() == CARD_SD ? "SDSC" : 
                  SD_MMC.cardType() == CARD_SDHC ? "SDHC" : "UNKNOWN");
    Serial.printf("SD Card Size: %lluMB\n", SD_MMC.cardSize() / (1024 * 1024));
  }

  // Initialize Camera
  Serial.println("Initializing camera...");
  cameraInit();
  
  // Initialize quirc QR code recognizer
  Serial.println("Initializing QR code recognizer...");
  qr_recognizer = quirc_new();
  if (!qr_recognizer) {
    Serial.println("ERROR: Failed to create QR recognizer!");
    return;
  }
  Serial.println("✓ QR recognizer created successfully");
  
  // Note: We'll resize quirc dynamically in the loop to match our needs
  // No need to resize here since we'll do it when we start QR detection
  
  Serial.println("🚀 Setup complete! Ready to test QR detection...");
  Serial.println("📋 Place a QR code in front of the camera!");
  Serial.println();
  Serial.println("- Use good lighting");
  Serial.println("- Hold QR code 10-30cm from camera");
  Serial.println("- Keep QR code flat and centered");
  Serial.println("Decoded QR codes will appear below:");
  Serial.println("----------------------------------");
}

void loop() {
  static unsigned long lastAttempt = 0;
  static int testCount = 0;
  static bool testPhase = true;  // Start with camera testing
  
  if (millis() - lastAttempt > 3000) {  // Try every 3 seconds
    testCount++;
    
    if (testPhase && testCount <= 5) {
      // Test phase: Just verify camera capture works with your config
      Serial.printf("\n=== Camera Test #%d (Using Your Working Config) ===\n", testCount);
      
      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("ERROR: Camera capture failed!");
        lastAttempt = millis();
        return;
      }
      
      Serial.printf("✓ Camera capture SUCCESS: %dx%d, %d bytes, format: %d\n", 
                    fb->width, fb->height, fb->len, fb->format);
      Serial.printf("✓ Free heap: %d bytes\n", ESP.getFreeHeap());
      
      esp_camera_fb_return(fb);
      
      if (testCount >= 5) {
        testPhase = false;
        testCount = 0;
        Serial.println("\n🎉 Camera test PASSED with your working configuration!");
        Serial.println("📸 Camera is stable and working!");
        Serial.println("🔄 Next: Will implement QR detection on this stable foundation\n");
      }
      
    } else {
      // QR detection phase - now that camera is stable!
      testCount++;
      Serial.printf("\n=== QR Detection Attempt #%d ===\n", testCount);
      
      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("ERROR: Camera capture failed!");
        lastAttempt = millis();
        return;
      }
      
      Serial.printf("📸 Camera: %dx%d, %d bytes, format: %d\n", 
                    fb->width, fb->height, fb->len, fb->format);
      
      if (fb->format == PIXFORMAT_JPEG) {
        Serial.println("� Converting JPEG to grayscale for QR detection...");
        
        // Resize quirc to match camera frame size
        static bool quirc_resized = false;
        if (!quirc_resized) {
          if (quirc_resize(qr_recognizer, fb->width, fb->height) < 0) {
            Serial.println("ERROR: Failed to resize quirc");
            esp_camera_fb_return(fb);
            lastAttempt = millis();
            return;
          }
          quirc_resized = true;
          Serial.printf("✓ Quirc resized to %dx%d\n", fb->width, fb->height);
        }
        
        // Get quirc image buffer
        uint8_t *qr_image = quirc_begin(qr_recognizer, NULL, NULL);
        if (!qr_image) {
          Serial.println("ERROR: Failed to begin quirc");
          esp_camera_fb_return(fb);
          lastAttempt = millis();
          return;
        }
        
        // Use ESP32 built-in JPEG to RGB conversion, then convert to grayscale
        size_t rgb_len = fb->width * fb->height * 3;  // RGB = 3 bytes per pixel
        Serial.printf("Allocating %d bytes for RGB buffer...\n", rgb_len);
        
        // Try PSRAM first if available, then regular heap
        uint8_t *rgb_buf = NULL;
        if (psramFound()) {
          rgb_buf = (uint8_t*)ps_malloc(rgb_len);  // Use PSRAM
          if (rgb_buf) {
            Serial.println("✓ RGB buffer allocated in PSRAM");
          }
        }
        
        if (!rgb_buf) {
          rgb_buf = (uint8_t*)malloc(rgb_len);  // Fallback to regular heap
          if (rgb_buf) {
            Serial.println("✓ RGB buffer allocated in regular heap");
          }
        }
        
        if (!rgb_buf) {
          Serial.printf("ERROR: Failed to allocate %d bytes for RGB buffer!\n", rgb_len);
          Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
          if (psramFound()) {
            Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
          }
          esp_camera_fb_return(fb);
          lastAttempt = millis();
          return;
        }
        
        // Convert JPEG to RGB using ESP32 built-in function
        bool jpeg_ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buf);
        
        if (!jpeg_ok) {
          Serial.println("ERROR: JPEG to RGB conversion failed!");
          free(rgb_buf);
          esp_camera_fb_return(fb);
          lastAttempt = millis();
          return;
        }
        
        Serial.println("✅ JPEG to RGB conversion successful!");
        
        // Convert RGB to grayscale manually (this is fast)
        for (int i = 0; i < fb->width * fb->height; i++) {
          // Standard grayscale formula: 0.299*R + 0.587*G + 0.114*B
          uint8_t r = rgb_buf[i * 3];
          uint8_t g = rgb_buf[i * 3 + 1];
          uint8_t b = rgb_buf[i * 3 + 2];
          qr_image[i] = (r * 77 + g * 150 + b * 29) >> 8;  // Fast integer math
        }
        
        free(rgb_buf);  // Clean up RGB buffer
        Serial.println("✅ RGB to grayscale conversion successful!");
        
        // Save grayscale image to SD card for debugging
        static int image_count = 0;
        if (image_count < 5) {  // Save first 5 images for debugging
          image_count++;
          char filename[50];
          sprintf(filename, "/debug_gray_%d.raw", image_count);
          
          if (SD_MMC.cardType() != CARD_NONE) {
            writejpg(SD_MMC, filename, qr_image, fb->width * fb->height);
            Serial.printf("✓ Saved grayscale image %d to SD: %s\n", image_count, filename);
            Serial.printf("📏 Image size: %dx%d pixels (%d bytes)\n", 
                         fb->width, fb->height, fb->width * fb->height);
            Serial.println("💡 You can view this raw grayscale file on PC to debug QR visibility");
          } else {
            Serial.printf("✗ Failed to save debug image %d\n", image_count);
          }
        }
        
        quirc_end(qr_recognizer);
        
        // Analyze grayscale image quality for debugging
        uint32_t pixel_sum = 0;
        uint8_t min_pixel = 255, max_pixel = 0;
        size_t pixel_count = fb->width * fb->height;
        
        for (size_t i = 0; i < pixel_count; i++) {
          uint8_t pixel = qr_image[i];
          pixel_sum += pixel;
          if (pixel < min_pixel) min_pixel = pixel;
          if (pixel > max_pixel) max_pixel = pixel;
        }
        
        uint8_t avg_pixel = pixel_sum / pixel_count;
        Serial.printf("📊 Grayscale stats: avg=%d, min=%d, max=%d, range=%d\n", 
                     avg_pixel, min_pixel, max_pixel, max_pixel - min_pixel);
        
        if (max_pixel - min_pixel < 50) {
          Serial.println("⚠️ Low contrast detected - QR codes may not be visible!");
        }
        
        // Look for QR codes
        int qr_count = quirc_count(qr_recognizer);
        Serial.printf("🔍 Found %d QR code(s)\n", qr_count);
        
        if (qr_count > 0) {
          for (int i = 0; i < qr_count; i++) {
            quirc_code qr_code;
            quirc_data qr_data;
            
            quirc_extract(qr_recognizer, i, &qr_code);
            quirc_decode_error_t err = quirc_decode(&qr_code, &qr_data);
            
            if (err == QUIRC_SUCCESS) {
              Serial.printf("\n🎯 QR CODE DETECTED #%d:\n", i+1);
              Serial.printf("Content: %s\n", qr_data.payload);
              Serial.printf("Length: %d chars\n", strlen((char*)qr_data.payload));
              Serial.println("====================");
            } else {
              Serial.printf("QR decode error: %s\n", quirc_strerror(err));
            }
          }
        } else {
          Serial.println("No QR codes detected in grayscale image");
        }
        
        esp_camera_fb_return(fb);
        
      } else {
        Serial.printf("ERROR: Expected grayscale format (2), got format %d\n", fb->format);
        esp_camera_fb_return(fb);
      }
      Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    }
    
    lastAttempt = millis();
  }
  
  delay(100);
}

// Process camera frame for QR codes using quirc
void processQRCode(camera_fb_t * fb) {
  if (!fb || !qr_recognizer) return;
  
  // Check if we need to resize quirc to match actual camera frame
  static bool quirc_resized = false;
  if (!quirc_resized && fb->width > 0 && fb->height > 0) {
    Serial.printf("Resizing quirc to match camera: %dx%d\n", fb->width, fb->height);
    if (quirc_resize(qr_recognizer, fb->width, fb->height) < 0) {
      Serial.println("Failed to resize quirc to camera dimensions");
      return;
    }
    quirc_resized = true;
  }
  
  // Get image buffer from quirc
  int width, height;
  uint8_t *image = quirc_begin(qr_recognizer, &width, &height);
  if (!image) {
    Serial.println("Failed to begin quirc processing");
    return;
  }
  
  // Ensure we don't copy more data than available
  size_t expected_len = width * height;
  size_t copy_len = (fb->len < expected_len) ? fb->len : expected_len;
  
  // Debug info for first few frames
  static int debug_count = 0;
  if (debug_count < 3) {
    Serial.printf("Frame %d: camera=%dx%d (%d bytes), quirc=%dx%d, copying %d bytes\n", 
                  debug_count, fb->width, fb->height, fb->len, width, height, copy_len);
    debug_count++;
  }
  
  if (fb->format == PIXFORMAT_GRAYSCALE) {
    // Direct copy for grayscale
    memcpy(image, fb->buf, copy_len);
  } else {
    // Convert other formats to grayscale (basic conversion)
    for (size_t i = 0; i < copy_len; i++) {
      image[i] = fb->buf[i]; // Simple copy, assuming reasonable conversion
    }
  }
  
  // End quirc processing
  quirc_end(qr_recognizer);
  
  // Get number of QR codes found
  int count = quirc_count(qr_recognizer);
  
  if (count > 0) {
    for (int i = 0; i < count; i++) {
      struct quirc_code code;
      struct quirc_data data;
      
      // Extract QR code
      quirc_extract(qr_recognizer, i, &code);
      
      // Decode QR code
      quirc_decode_error_t err = quirc_decode(&code, &data);
      
      if (err == QUIRC_SUCCESS) {
        String qrString = String((char*)data.payload);
        unsigned long currentTime = millis();
        
        // Only process if it's new or enough time has passed
        if (qrString != lastQRCodeData || (currentTime - lastQRCodeTime > 2000)) {
          qrCodeCount++;
          lastQRCodeData = qrString;
          lastQRCodeTime = currentTime;
          
          // Print QR code information
          Serial.println();
          Serial.print("[QR #");
          Serial.print(qrCodeCount);
          Serial.print("] Detected at ");
          Serial.print(currentTime / 1000.0, 1);
          Serial.println("s");
          
          Serial.print("Version: ");
          Serial.println(data.version);
          Serial.print("ECC Level: ");
          Serial.println(data.ecc_level);
          Serial.print("Mask: ");
          Serial.println(data.mask);
          Serial.print("Data Type: ");
          Serial.println(data.data_type);
          Serial.println("Content: " + qrString);
          Serial.print("Length: ");
          Serial.print(qrString.length());
          Serial.println(" characters");
          
          // Analyze content type
          if (qrString.startsWith("http://") || qrString.startsWith("https://")) {
            Serial.println("Type: URL/Website");
          }
          else if (qrString.startsWith("wifi:") || qrString.startsWith("WIFI:")) {
            Serial.println("Type: WiFi Configuration");
          }
          else if (qrString.startsWith("{") && qrString.endsWith("}")) {
            Serial.println("Type: JSON Data");
          }
          else if (qrString.startsWith("mailto:")) {
            Serial.println("Type: Email Address");
          }
          else if (qrString.startsWith("tel:")) {
            Serial.println("Type: Phone Number");
          }
          else if (qrString.indexOf(":") > 0) {
            Serial.println("Type: Key-Value Data");
          }
          else {
            Serial.println("Type: Plain Text");
          }
          
          Serial.println("----------------------------------");
        }
      } else {
        Serial.print("QR decode error: ");
        Serial.println(quirc_strerror(err));
      }
    }
  }
}



