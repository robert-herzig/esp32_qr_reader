/**********************************************************************
  Filename    : ESP32 S3 QR Code Scanner
  Description : QR code scanner using ESP32 camera and       if (fb->format == PIXFORMAT_GRAYSCALE) {
        Serial.pr            if (fb->format == PIXFORMAT_JPEG) {
  ESP32 S3 QR Code Scanner - quirc library
  Auther      : Modified from www         if (fb->format == PIXFORMAT_JPEG) {
        Serial.println("🔄 Converting JPEG to grayscale for QR processing task...");
        
        // Use task-based QR processing to avoid stack overflow       // Use task-based QR processing to avoid stack overflowFORMAT_JPEG) {
        Serial.println("🔄 Converting JPEG to grayscale for QR processing task...");
        
        // Use task-based QR processing to avoid stack overflownove.com
  Modification: 2024/09/24 - QR scanning with quirc library (Espressif recommended)
**********************************************************************/
#include "esp_camera.h"
#include "quirc.h"
#include "img_converters.h"  // ESP32 built-in image conversion functions
#include "sd_read_write.h"   // SD card functions
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Select camera model - use same as your working video stream
#define CAMERA_MODEL_ESP32S3_EYE // This is what your working video stream used!
#include "camera_pins.h"

// QR Code detection variables
struct quirc *qr_recognizer = NULL;
String lastQRCodeData = "";
unsigned long lastQRCodeTime = 0;
int qrCodeCount = 0;

// Task-based QR processing to avoid stack overflow
TaskHandle_t qrProcessingTaskHandle = NULL;
QueueHandle_t qrImageQueue = NULL;
QueueHandle_t qrResultQueue = NULL;

// Structure for passing image data to QR processing task
struct QRImageData {
  uint8_t* image_data;
  int width;
  int height;
  int attempt_number;
};

// Structure for QR processing results
struct QRResult {
  bool found;
  char content[512];  // Limited size to avoid issues
  int attempt_number;
};

// Function declarations
void qrProcessingTask(void *pvParameters);
void enhanceImageForQR(uint8_t* image, int width, int height);

// Image enhancement function to improve QR code detection
void enhanceImageForQR(uint8_t* image, int width, int height) {
  int total_pixels = width * height;
  
  // Find current min/max for histogram stretching
  uint8_t min_val = 255, max_val = 0;
  for (int i = 0; i < total_pixels; i++) {
    if (image[i] < min_val) min_val = image[i];
    if (image[i] > max_val) max_val = image[i];
  }
  
  // Apply histogram stretching for better contrast
  if (max_val > min_val) {
    float scale = 255.0f / (max_val - min_val);
    for (int i = 0; i < total_pixels; i++) {
      int enhanced = (int)((image[i] - min_val) * scale);
      image[i] = (uint8_t)(enhanced > 255 ? 255 : (enhanced < 0 ? 0 : enhanced));
    }
    Serial.printf("QR Task: Enhanced contrast - stretched %d-%d to 0-255\n", min_val, max_val);
  }
  
  // Apply median filter to reduce noise (simple 3x3 filter on edges)
  // This helps clean up noisy pixels that can cause ECC failures
  for (int y = 1; y < height - 1; y++) {
    for (int x = 1; x < width - 1; x++) {
      int idx = y * width + x;
      
      // Get 3x3 neighborhood
      uint8_t neighbors[9] = {
        image[(y-1)*width + (x-1)], image[(y-1)*width + x], image[(y-1)*width + (x+1)],
        image[y*width + (x-1)],     image[y*width + x],     image[y*width + (x+1)],
        image[(y+1)*width + (x-1)], image[(y+1)*width + x], image[(y+1)*width + (x+1)]
      };
      
      // Simple median (sort and take middle value)
      for (int i = 0; i < 8; i++) {
        for (int j = i + 1; j < 9; j++) {
          if (neighbors[i] > neighbors[j]) {
            uint8_t temp = neighbors[i];
            neighbors[i] = neighbors[j];
            neighbors[j] = temp;
          }
        }
      }
      image[idx] = neighbors[4]; // median value
    }
  }
  Serial.println("QR Task: Applied noise reduction filter");
}

// QR Processing Task - runs with large stack to handle quirc library
void qrProcessingTask(void *pvParameters) {
  QRImageData imageData;
  QRResult result;
  
  while (true) {
    // Wait for image data from main loop
    if (xQueueReceive(qrImageQueue, &imageData, portMAX_DELAY) == pdTRUE) {
      Serial.printf("QR Task: Processing image %d (%dx%d)\n", 
                   imageData.attempt_number, imageData.width, imageData.height);
      Serial.printf("QR Task Stack: %d bytes free\n", 
                   uxTaskGetStackHighWaterMark(NULL));
      
      // Initialize result
      result.found = false;
      result.attempt_number = imageData.attempt_number;
      strcpy(result.content, "");
      
      // Create temporary quirc instance for this task (heap allocation)
      struct quirc *task_qr_recognizer = quirc_new();
      if (task_qr_recognizer && 
          quirc_resize(task_qr_recognizer, imageData.width, imageData.height) >= 0) {
        
        // Copy image data to quirc buffer
        uint8_t *qr_image = quirc_begin(task_qr_recognizer, NULL, NULL);
        if (qr_image) {
          memcpy(qr_image, imageData.image_data, imageData.width * imageData.height);
          
          // Enhance image quality for better QR detection
          enhanceImageForQR(qr_image, imageData.width, imageData.height);
          
          quirc_end(task_qr_recognizer);
          
          // Look for QR codes
          int qr_count = quirc_count(task_qr_recognizer);
          Serial.printf("QR Task: Found %d potential QR codes\n", qr_count);
          
          if (qr_count > 0) {
            // Allocate QR structures on heap to minimize stack usage
            quirc_code *qr_code = (quirc_code*)heap_caps_malloc(sizeof(quirc_code), MALLOC_CAP_8BIT);
            quirc_data *qr_data = (quirc_data*)heap_caps_malloc(sizeof(quirc_data), MALLOC_CAP_8BIT);
            
            if (qr_code && qr_data) {
              Serial.printf("QR Task Stack after heap alloc: %d bytes free\n", 
                           uxTaskGetStackHighWaterMark(NULL));
              
              quirc_extract(task_qr_recognizer, 0, qr_code);
              quirc_decode_error_t err = quirc_decode(qr_code, qr_data);
              
              if (err == QUIRC_SUCCESS) {
                result.found = true;
                strncpy(result.content, (char*)qr_data->payload, sizeof(result.content) - 1);
                result.content[sizeof(result.content) - 1] = '\0';  // Ensure null termination
                Serial.printf("QR Task: Successfully decoded QR code\n");
              } else {
                Serial.printf("QR Task: Decode error - %s\n", quirc_strerror(err));
              }
            } else {
              Serial.println("QR Task: Failed to allocate QR structures");
            }
            
            // Clean up heap allocations
            if (qr_code) free(qr_code);
            if (qr_data) free(qr_data);
          } else {
            Serial.println("QR Task: No QR codes detected");
          }
        }
        quirc_destroy(task_qr_recognizer);
      }
      
      // Free the image data
      if (imageData.image_data) {
        free(imageData.image_data);
      }
      
      // Send result back
      xQueueSend(qrResultQueue, &result, 0);
    }
  }
}

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
    // Optimized sensor settings for QR code detection
    s->set_vflip(s, 1);          // flip it back
    s->set_brightness(s, 1);     // Slightly higher brightness for QR codes
    s->set_contrast(s, 2);       // Increase contrast for better black/white distinction
    s->set_saturation(s, 0);     // Not relevant for QR detection
    s->set_gainceiling(s, (gainceiling_t)2);  // Higher gain ceiling for better low-light performance
    s->set_quality(s, 10);       // Higher quality JPEG (lower compression)
    s->set_colorbar(s, 0);       // Disable color bar
    s->set_whitebal(s, 1);       // Enable auto white balance
    s->set_gain_ctrl(s, 1);      // Enable auto gain control for varying lighting
    s->set_exposure_ctrl(s, 1);  // Enable auto exposure for optimal brightness
    s->set_hmirror(s, 0);        // No horizontal mirror
    s->set_wpc(s, 1);            // Enable white pixel correction
    s->set_bpc(s, 1);            // Enable black pixel correction for cleaner blacks
    s->set_lenc(s, 1);           // Enable lens correction for sharper edges
    s->set_dcw(s, 1);            // Enable DCW (downsize)
    
    Serial.println("✓ Camera settings optimized for QR code detection");
    Serial.println("  - Higher contrast and brightness for better black/white distinction");
    Serial.println("  - Higher quality JPEG compression");
    Serial.println("  - Enhanced pixel correction for cleaner QR patterns");
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

  // Increase stack size for main loop task to prevent overflow
  Serial.println("Configuring task stack sizes...");
  
  // Check PSRAM
  if (psramFound()) {
    Serial.println("✓ PSRAM found and available");
    Serial.printf("PSRAM size: %d bytes\n", ESP.getPsramSize());
  } else {
    Serial.println("⚠️ WARNING: PSRAM not found - may affect performance");
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
  
  // Create queues for task communication
  qrImageQueue = xQueueCreate(1, sizeof(QRImageData));
  qrResultQueue = xQueueCreate(1, sizeof(QRResult));
  
  if (!qrImageQueue || !qrResultQueue) {
    Serial.println("ERROR: Failed to create communication queues!");
    return;
  }
  
  // Create QR processing task with very large stack (48KB - quirc needs a lot of stack)
  BaseType_t taskResult = xTaskCreatePinnedToCore(
    qrProcessingTask,      // Task function
    "QRProcessing",        // Task name
    49152,                 // Stack size (48KB - quirc library needs substantial stack space)
    NULL,                  // Task parameters
    1,                     // Task priority
    &qrProcessingTaskHandle, // Task handle
    0                      // Core 0 (camera usually runs on core 1)
  );
  
  if (taskResult != pdPASS) {
    Serial.println("ERROR: Failed to create QR processing task!");
    return;
  }
  
  Serial.println("✓ QR processing task created with 48KB stack");
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
        
        // OLD QUIRC CODE REMOVED - Now using task-based processing
        
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
        
        // Allocate grayscale buffer for task processing
        size_t grayscale_len = fb->width * fb->height;
        uint8_t *grayscale_buffer = (uint8_t*)malloc(grayscale_len);
        if (!grayscale_buffer) {
          Serial.println("ERROR: Failed to allocate grayscale buffer for task");
          free(rgb_buf);
          esp_camera_fb_return(fb);
          lastAttempt = millis();
          return;
        }
        
        // Convert RGB to grayscale for task processing
        for (int i = 0; i < fb->width * fb->height; i++) {
          uint8_t r = rgb_buf[i * 3];
          uint8_t g = rgb_buf[i * 3 + 1];
          uint8_t b = rgb_buf[i * 3 + 2];
          grayscale_buffer[i] = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b);
        }
        
        free(rgb_buf);  // Clean up RGB buffer
        Serial.println("✅ RGB to grayscale conversion successful!");
        
        // Analyze grayscale image quality for debugging  
        uint32_t pixel_sum = 0;
        uint8_t min_pixel = 255, max_pixel = 0;
        size_t pixel_count = fb->width * fb->height;
        
        for (size_t i = 0; i < pixel_count; i++) {
          uint8_t pixel = grayscale_buffer[i];
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
        
        // Send image data to QR processing task (task-based approach to avoid stack overflow)
        QRImageData imageData;
        imageData.image_data = grayscale_buffer;
        imageData.width = fb->width;
        imageData.height = fb->height;
        imageData.attempt_number = testCount;
        
        if (xQueueSend(qrImageQueue, &imageData, 0) == pdTRUE) {
          Serial.println("📤 Image sent to QR processing task");
          
          // Check for result (non-blocking with short timeout)
          QRResult result;
          if (xQueueReceive(qrResultQueue, &result, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (result.found) {
              Serial.printf("\n🎯 QR CODE DETECTED (Attempt #%d):\n", result.attempt_number);
              Serial.printf("Content: %s\n", result.content);
              Serial.printf("Length: %d chars\n", strlen(result.content));
              Serial.println("====================");
            } else {
              Serial.println("No QR codes detected by processing task");
            }
          } else {
            Serial.println("⏳ QR processing task is still working...");
          }
        } else {
          Serial.println("⚠️ Failed to send image to QR processing task (queue full)");
          free(grayscale_buffer); // Clean up if we couldn't send
        }
        
        esp_camera_fb_return(fb);
        
      } else {
        Serial.printf("ERROR: Expected grayscale format (2), got format %d\n", fb->format);
        esp_camera_fb_return(fb);
      }
      // Memory and stack monitoring
      Serial.printf("Free heap: %d bytes, Free PSRAM: %d bytes\n", 
                   ESP.getFreeHeap(), ESP.getFreePsram());
      Serial.printf("Stack high water mark: %d bytes\n", 
                   uxTaskGetStackHighWaterMark(NULL));
    }
    
    lastAttempt = millis();
  }
  
  delay(500);  // Increased delay to reduce processing pressure
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



