#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <TinyGPS++.h>
#include <ArduinoJson.h>

const char* ssid = "Nombre del wi fi";
const char* password = "Contrasena";
const char* cam_ip = "esp32-cam.local";  // Use mDNS hostname

WebServer server(80);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

#define RXD2 16
#define TXD2 17

String lastLat = "N/A";
String lastLon = "N/A";
bool gpsValido = false;
String apiRespuesta = "No consultado.";

void fetchAndSaveImage() {
  HTTPClient http;
  http.setTimeout(10000);  // Set 10s timeout for HTTP request
  String url = "http://" + String(cam_ip) + "/capture";
  Serial.println("[INFO] Solicitando imagen desde: " + url);
  http.begin(url);
  int res = http.GET();

  if (res == 200) {
    WiFiClient* stream = http.getStreamPtr();
    File img = SPIFFS.open("/img.jpg", FILE_WRITE);
    if (!img) {
      Serial.println("[ERROR] No se pudo abrir /img.jpg en SPIFFS.");
      http.end();
      return;
    }
    uint8_t buff[128];
    int len = http.getSize();
    Serial.println("[INFO] Tamaño de la imagen: " + String(len) + " bytes");
    while (http.connected() && (len > 0 || len == -1)) {
      size_t r = stream->readBytes(buff, sizeof(buff));
      if (r == 0) break;
      img.write(buff, r);
      if (len > 0) len -= r;
    }
    img.close();
    Serial.println("[OK] Imagen recibida y guardada.");
  } else {
    Serial.println("[ERROR] Falló la solicitud de imagen. Código HTTP: " + String(res));
    if (res == 0) {
      Serial.println("[ERROR] No se pudo conectar al servidor de la cámara.");
    } else if (res < 0) {
      Serial.println("[ERROR] Error de conexión: " + String(http.errorToString(res)));
    }
  }

  http.end();
}

void readGPS() {
  Serial.println("[INFO] Leyendo GPS...");
  unsigned long start = millis();
  while (millis() - start < 2000) {
    while (gpsSerial.available()) {
      char c = gpsSerial.read();
      gps.encode(c);
      Serial.print(c);
    }
  }

  if (gps.location.isValid()) {
    gpsValido = true;
    lastLat = String(gps.location.lat(), 6);
    lastLon = String(gps.location.lng(), 6);
    Serial.println("[GPS OK] Lat: " + lastLat + " | Lon: " + lastLon);
  } else {
    gpsValido = false;
    lastLat = "N/A";
    lastLon = "N/A";
    Serial.println("[GPS ❌] Coordenadas no válidas.");
  }
}

void llamarAPI() {
  if (!SPIFFS.exists("/img.jpg")) {
    apiRespuesta = "No hay imagen disponible.";
    Serial.println("[DEBUG] Imagen no encontrada en SPIFFS.");
    return;
  }

  File img = SPIFFS.open("/img.jpg", FILE_READ);
  if (!img) {
    apiRespuesta = "No se pudo abrir la imagen.";
    Serial.println("[DEBUG] Fallo al abrir /img.jpg.");
    return;
  }

  Serial.println("[DEBUG] Imagen cargada para envío. Tamaño: " + String(img.size()) + " bytes");
  Serial.println("[DEBUG] Lat: " + lastLat + " | Lon: " + lastLon);

  int intentos = 0;
  bool exito = false;

  while (intentos < 3 && !exito) {
    Serial.println("[INFO] Llamando a API. Intento " + String(intentos + 1));

    WiFiClientSecure client;
    client.setInsecure();

    if (!client.connect("aiclothing-chdac2cjcthrfhh4.chilecentral-01.azurewebsites.net", 443)) {
      Serial.println("[ERROR] No se pudo conectar al servidor.");
      apiRespuesta = "No se pudo conectar con la API.";
      break;
    }

    String boundary = "----1234";
    String startReq = "--" + boundary + "\r\n";
    startReq += "Content-Disposition: form-data; name=\"lat\"\r\n\r\n" + lastLat + "\r\n";
    startReq += "--" + boundary + "\r\n";
    startReq += "Content-Disposition: form-data; name=\"lon\"\r\n\r\n" + lastLon + "\r\n";
    startReq += "--" + boundary + "\r\n";
    startReq += "Content-Disposition: form-data; name=\"imagen\"; filename=\"img.jpg\"\r\n";
    startReq += "Content-Type: image/jpeg\r\n\r\n";

    String endReq = "\r\n--" + boundary + "--\r\n";
    int contentLength = startReq.length() + img.size() + endReq.length();

    client.println("POST /evaluar_abrigado HTTP/1.1");
    client.println("Host: aiclothing-chdac2cjcthrfhh4.chilecentral-01.azurewebsites.net");
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Content-Length: " + String(contentLength));
    client.println("Connection: close");
    client.println();

    client.print(startReq);
    while (img.available()) {
      uint8_t buff[128];
      size_t len = img.read(buff, sizeof(buff));
      client.write(buff, len);
    }
    client.print(endReq);
    img.close();

    String body = "";
    bool headersEnded = false;
    unsigned long timeout = millis();
    while (client.connected() && millis() - timeout < 5000) {
      while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
          headersEnded = true;
          break;
        }
      }
      if (headersEnded) break;
    }

    while (client.available()) {
      body += client.readStringUntil('\n');
    }

    Serial.println("[DEBUG] Respuesta bruta de la API:\n" + body);

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error || doc.containsKey("error")) {
      Serial.println("[WARN] Error al analizar JSON o respuesta inválida. Reintentando...");
      apiRespuesta = "API falló (schema inválido), reintentando...";
      intentos++;
      delay(1000);
    } else {
      String clima = doc["clima"]["descripcion"].as<String>();
      String temp = String(doc["clima"]["temperatura"].as<float>()) + "°C";
      String sensacion = String(doc["clima"]["sensacion_termica"].as<float>()) + "°C";
      String comentario = doc["comentario"].as<String>();
      String abrigado = doc["esta_abrigado"] ? "Sí" : "No";
      String ropa = "";
      for (JsonVariant item : doc["ropa_detectada"].as<JsonArray>()) {
        ropa += item.as<String>() + " ";
      }

      apiRespuesta = "Clima: " + clima + " (" + temp + ", sensación " + sensacion + ")<br>Abrigado: " + abrigado +
                     "<br>Prendas: " + ropa + "<br><i>" + comentario + "</i>";
      Serial.println("[DEBUG] JSON parseado correctamente. Clima: " + clima + " | Abrigado: " + abrigado);
      exito = true;
    }

    client.stop();
  }

  if (!exito) {
    apiRespuesta = "❌ Fallaron todos los intentos con la API.";
  }
}

void handleRoot() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head><meta charset='utf-8'><title>ESP32 DevKit</title></head>
    <body style='text-align:center;font-family:sans-serif;'>
      <h2>Control ESP32-CAM + API</h2>
      <form action="/capture" method="get">
        <button style='font-size:1.2em;padding:1em;'>Tomar Foto</button>
      </form><br>
      <form action="/evaluar" method="get">
        <button style='font-size:1.2em;padding:1em;'>Evaluar Abrigo</button>
      </form><br>
  )rawliteral";

  if (SPIFFS.exists("/img.jpg")) {
    html += "<img src='/img.jpg?t=" + String(millis()) + "' width='320'><br><br>";
  }

  html += "<h3>Coordenadas GPS</h3>";
  html += "<p>Latitud: " + lastLat + "<br>Longitud: " + lastLon + "</p>";

  html += "<h3>Resultado de la API</h3><p>" + apiRespuesta + "</p>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // Connect to WiFi (DHCP)
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado. IP: " + WiFi.localIP().toString());

  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
    return;
  }

  server.on("/", handleRoot);
  server.on("/capture", HTTP_GET, []() {
    fetchAndSaveImage();
    readGPS();
    server.sendHeader("Location", "/");
    server.send(303);
  });
  server.on("/evaluar", HTTP_GET, []() {
    llamarAPI();
    server.sendHeader("Location", "/");
    server.send(303);
  });
  server.on("/img.jpg", HTTP_GET, []() {
    File img = SPIFFS.open("/img.jpg", FILE_READ);
    if (!img) {
      server.send(404, "text/plain", "Imagen no encontrada");
      return;
    }
    server.streamFile(img, "image/jpeg");
    img.close();
  });
  server.begin();
}

void loop() {
  // Reconnect to WiFi if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado, intentando reconectar...");
    WiFi.reconnect();
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nReconectado. IP: " + WiFi.localIP().toString());
  }
  server.handleClient();
}