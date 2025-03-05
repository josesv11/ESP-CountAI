
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

//WIFI
const char* ssid = "YourRedWiFi";  
const char* password = "YourPassword";
const char* serverUrl = "http://YourIPDirection:5000/upload";
WebServer server(80);//Este es el puerto para el servidor web

bool isPaused = false; 

//Estado del sistema
void handleLatestImage() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Error al capturar imagen");
        return;
    }

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
}


void handleStatus() {
    String status = isPaused ? "Pausado" : "Activo";  
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", status);
}


//Página principal
void handleRoot() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", "<h1>ESP32-CAM en línea</h1>");
}

//Cambiar a estado pausa
void handleToggle() {
    isPaused = !isPaused;  
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", isPaused ? "Pausado" : "Reanudado");
}


//Configuración inicial
void setup() {
    Serial.begin(115200);
    Serial.println("\n Iniciando el ESP32-CAM...");

    WiFi.begin(ssid, password);
    Serial.print("Conectando a WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConectado a WiFi correctamente");
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());

    //Configuración del servidor web
    server.on("/", handleRoot);
    server.on("/toggle", handleToggle);
    server.on("/status", handleStatus); 
    server.begin();
    Serial.println("Servidor web iniciado");

    server.on("/latest.jpg", handleLatestImage);
    //Configuración de la cámara
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
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Error al inicializar la cámara (0x%x)\n", err);
        return;
    }
    Serial.println("Cámara lista");
}

//Bucle principal
unsigned long ultimoEnvio = 0;
const int intervalo = 10000; 

bool estadoAnterior = false;

void loop() {
    server.handleClient(); //Para manejar las solicitudes de HTTP
    
    //Solo se imprime si el estado cambió
    if (isPaused != estadoAnterior) {
        Serial.print("Estado: ");
        Serial.println(isPaused ? "Pausado" : "Activo");
        estadoAnterior = isPaused; 
    }

    if (!isPaused) {
        if (millis() - ultimoEnvio >= intervalo) {
            sendPhoto();
            ultimoEnvio = millis();
        }
    }

    yield(); 
}

//Enviar la foto al servidor
void sendPhoto() {
    Serial.println("\nCapturando imagen...");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Error al capturar imagen");
        return;
    }

    Serial.println("Imagen capturada.Enviando al servidor...");

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;
        http.begin(client, serverUrl);
        http.addHeader("Content-Type", "application/octet-stream");

        int httpResponseCode = http.POST(fb->buf, fb->len);

        if (httpResponseCode > 0) {
            Serial.print("Respuesta del servidor: ");
            Serial.println(http.getString());
        } else {
            Serial.print("Error en la conexión: ");
            Serial.println(httpResponseCode);
        }

        http.end();
    } else {
        Serial.println("No conectado a WiFi");
    }

    esp_camera_fb_return(fb);
}