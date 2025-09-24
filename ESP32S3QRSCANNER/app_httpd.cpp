// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "driver/ledc.h"

#include "sdkconfig.h"

#include "Arduino.h"
#include "sd_read_write.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define TAG ""
#else
#include "esp_log.h"
static const char *TAG = "camera_httpd";
#endif

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static int button_state = 1;

typedef struct
{
    size_t size;  // number of values used for filtering
    size_t index; // current value index
    size_t count; // value count
    int sum;
    int *values; // array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values)
    {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t *filter, int value)
{
    if (!filter->values)
    {
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size)
    {
        filter->count++;
    }
    return filter->sum / filter->count;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[128];

    static int64_t last_frame = 0;
    if (!last_frame)
    {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            _timestamp.tv_sec = fb->timestamp.tv_sec;
            _timestamp.tv_usec = fb->timestamp.tv_usec;
            if (fb->format != PIXFORMAT_JPEG)
            {
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if (!jpeg_converted)
                {
                    ESP_LOGE(TAG, "JPEG compression failed");
                    res = ESP_FAIL;
                }
            }
            else
            {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            ESP_LOGI(TAG, "res != ESP_OK : %d , break!", res);
            break;
        }
        int64_t fr_end = esp_timer_get_time();

        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
        /*
        ESP_LOGI(TAG, "MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)",
                (uint32_t)(_jpg_buf_len),
                (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
                avg_frame_time, 1000.0 / avg_frame_time);
        */
    }
    ESP_LOGI(TAG, "Stream exit!");
    last_frame = 0;
    return res;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf)
{
    char *buf = NULL;
    size_t buf_len = 0;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            *obuf = buf;
            return ESP_OK;
        }
        free(buf);
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}
const char index_web[]=R"rawliteral(
<html>
  <head>
    <title>ESP32 S3 QR Code Scanner</title>
    <style>
      body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
      .container { max-width: 800px; margin: 0 auto; }
      h1 { color: #333; text-align: center; }
      .video-container { text-align: center; margin: 20px 0; }
      .qr-status { 
        background: #fff; 
        border: 2px solid #ddd; 
        border-radius: 10px; 
        padding: 20px; 
        margin: 20px 0; 
        box-shadow: 0 2px 4px rgba(0,0,0,0.1);
      }
      .qr-detected { border-color: #4CAF50; background-color: #f8fff8; }
      .qr-not-detected { border-color: #ff9800; background-color: #fff8f0; }
      .qr-data { 
        font-family: monospace; 
        background: #f5f5f5; 
        padding: 10px; 
        border-radius: 5px; 
        word-break: break-all;
        margin: 10px 0;
      }
      .status-indicator { 
        display: inline-block; 
        width: 12px; 
        height: 12px; 
        border-radius: 50%; 
        margin-right: 8px; 
      }
      .status-green { background-color: #4CAF50; }
      .status-orange { background-color: #ff9800; }
      .controls { text-align: center; margin: 20px 0; }
      .btn { 
        background: #2196F3; 
        color: white; 
        border: none; 
        padding: 10px 20px; 
        border-radius: 5px; 
        cursor: pointer; 
        font-size: 16px;
      }
      .btn:hover { background: #1976D2; }
    </style>
  </head>
  <body>
    <div class="container">
      <h1>🔍 ESP32 S3 QR Code Scanner</h1>
      
      <div class="qr-status" id="qrStatus">
        <h3><span class="status-indicator status-orange" id="statusIndicator"></span>QR Code Status</h3>
        <div id="qrInfo">
          <p><strong>Status:</strong> <span id="detectionStatus">Scanning...</span></p>
          <p><strong>Last Detected:</strong> <span id="lastDetected">None</span></p>
          <div id="qrDataContainer" style="display: none;">
            <p><strong>Decoded Data:</strong></p>
            <div class="qr-data" id="qrData"></div>
          </div>
        </div>
      </div>
      
      <div class="video-container">
        <img id="stream" src="" style="transform:rotate(180deg); max-width: 100%; border: 2px solid #ddd; border-radius: 10px;"/>
      </div>
      
      <div class="controls">
        <iframe width=0 height=0 frameborder=0 id="myiframe" name="myiframe"></iframe>
        <form action="/button" method="POST" target="myiframe" style="display: inline;">
          <input type="submit" value="📷 Save Screenshot" class="btn">
        </form>
      </div>
    </div>
  </body>
  <script>
  document.addEventListener('DOMContentLoaded', function (event) {
    var baseHost = document.location.origin;
    var streamUrl = baseHost + ':81';
    const view = document.getElementById('stream');
    view.src = `${streamUrl}/stream`;
    
    // Update QR code status every 500ms
    function updateQRStatus() {
      fetch('/qr_status')
        .then(response => response.json())
        .then(data => {
          const statusDiv = document.getElementById('qrStatus');
          const indicator = document.getElementById('statusIndicator');
          const detectionStatus = document.getElementById('detectionStatus');
          const lastDetected = document.getElementById('lastDetected');
          const qrDataContainer = document.getElementById('qrDataContainer');
          const qrData = document.getElementById('qrData');
          
          if (data.detected) {
            statusDiv.className = 'qr-status qr-detected';
            indicator.className = 'status-indicator status-green';
            detectionStatus.textContent = 'QR Code Detected!';
            
            if (data.data && data.data !== '') {
              qrDataContainer.style.display = 'block';
              qrData.textContent = data.data;
              
              // Format timestamp
              const date = new Date(data.timestamp);
              lastDetected.textContent = date.toLocaleTimeString();
            }
          } else {
            statusDiv.className = 'qr-status qr-not-detected';
            indicator.className = 'status-indicator status-orange';
            detectionStatus.textContent = 'Scanning for QR codes...';
            
            if (data.data && data.data !== '') {
              const date = new Date(data.timestamp);
              lastDetected.textContent = date.toLocaleTimeString() + ' (expired)';
              qrDataContainer.style.display = 'block';
              qrData.textContent = data.data;
            } else {
              lastDetected.textContent = 'None';
              qrDataContainer.style.display = 'none';
            }
          }
        })
        .catch(error => {
          console.error('Error fetching QR status:', error);
          document.getElementById('detectionStatus').textContent = 'Connection Error';
        });
    }
    
    // Start updating QR status
    updateQRStatus();
    setInterval(updateQRStatus, 500);
  });
  </script>
</html>)rawliteral";


static esp_err_t index_handler(httpd_req_t *req)
{
  esp_err_t err;
  err = httpd_resp_set_type(req, "text/html");
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL)
  {
      err = httpd_resp_send(req, (const char *)index_web, sizeof(index_web));
  }
  else
  {
      ESP_LOGE(TAG, "Camera sensor not found");
      err = httpd_resp_send_500(req);
  }
  return err;
}

static esp_err_t button_handler(httpd_req_t *req)
{
  esp_err_t err;
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb)
  {
      ESP_LOGE(TAG, "Camera capture failed");
      err = ESP_FAIL;
  }
  else
  {
    String video = "/video";
    int jpgCount=readFileNum(SD_MMC, video.c_str());
    String path = video + "/" + String(jpgCount) +".jpg";
    writejpg(SD_MMC, path.c_str(), fb->buf, fb->len);
    esp_camera_fb_return(fb);
    fb = NULL;
    err=ESP_OK;
  }
  return err;
}

// External function declaration (defined in main .ino file)
extern String getQRCodeStatus();

static esp_err_t qr_status_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  String qr_status = getQRCodeStatus();
  httpd_resp_send(req, qr_status.c_str(), qr_status.length());
  
  return ESP_OK;
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL}; 
    
    httpd_uri_t button_uri = {
        .uri = "/button",
        .method = HTTP_POST,
        .handler = button_handler,
        .user_ctx = NULL}; 

    httpd_uri_t qr_status_uri = {
        .uri = "/qr_status",
        .method = HTTP_GET,
        .handler = qr_status_handler,
        .user_ctx = NULL};

    ra_filter_init(&ra_filter, 20);

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);        
        httpd_register_uri_handler(camera_httpd, &button_uri);
        httpd_register_uri_handler(camera_httpd, &qr_status_uri);
        // httpd_register_uri_handler(camera_httpd, &stream_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}















