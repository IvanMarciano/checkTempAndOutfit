#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

const char* ssid = "WIFI";  
const char* password = "PASS";  

WebServer server(80);

void startCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sscb_sda = 26;
  config.pin_sscb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error iniciando cámara: 0x%x", err);
    return;
  }
}

void handleCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Error al capturar");
    return;
  }

  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleStream() {
  WiFiClient client = server.client();
  String boundary = "frame";
  server.sendContent("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=" + boundary + "\r\n\r\n");

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) continue;
    client.printf("--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", boundary.c_str(), fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);
    delay(80);
  }
}

void setup() {
  Serial.begin(115200);

  // Conectar al wifi (DHCP)
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nCAM IP: " + WiFi.localIP().toString());

  // Iniciar mDNS
  if (!MDNS.begin("esp32-cam")) {
    Serial.println("Error iniciando mDNS");
  } else {
    Serial.println("mDNS iniciado: esp32-cam.local");
  }

  // Iniciar camara
  startCamera();

  // Iniciar web server
  server.on("/capture", handleCapture);
  server.on("/stream", handleStream);
  server.begin();
  Serial.println("Servidor iniciado");
}

void loop() {
  // Reconectar al WiFi si se desconecto
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado, intentando reconectar...");
    WiFi.reconnect();
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nReconectado. CAM IP: " + WiFi.localIP().toString());
  }
  server.handleClient();
}