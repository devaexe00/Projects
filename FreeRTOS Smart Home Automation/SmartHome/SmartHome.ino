#include <WiFi.h>
#include <WebServer.h>
#include <BluetoothSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "DHT.h"

#define DHTPIN 4
#define RELAYPIN 5

// ADD THESE MISSING GLOBAL VARIABLES
String currentIP = "192.168.4.1";  // ← MISSING!

QueueHandle_t sensorQueue;
SemaphoreHandle_t dataMutex;
DHT dht(DHTPIN, DHT11);
WebServer server(80);
BluetoothSerial SerialBT;
volatile float lastTemp = 0, lastHum = 0;
volatile bool relayState = false;

void SensorTask(void *pvParameters) {
  float temp, hum;
  dht.begin();
  while(1) {
    hum = dht.readHumidity();
    temp = dht.readTemperature();
    if (!isnan(temp) && !isnan(hum)) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      lastTemp = temp;
      lastHum = hum;
      xQueueOverwrite(sensorQueue, &temp);
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void RelayTask(void *pvParameters) {
  float temp;
  pinMode(RELAYPIN, OUTPUT);
  digitalWrite(RELAYPIN, LOW);
  while(1) {
    if (xQueueReceive(sensorQueue, &temp, pdMS_TO_TICKS(100)) == pdTRUE) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      if (temp > 30.0) {
        digitalWrite(RELAYPIN, HIGH);
        relayState = true;
      } else if (temp < 20.0) {
        digitalWrite(RELAYPIN, HIGH);
        relayState = true;
      } else {
        digitalWrite(RELAYPIN, LOW);
        relayState = false;
      }
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void WiFiTask(void *pvParameters) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("SmartGateway_AP", "12345678");
  currentIP = WiFi.softAPIP().toString();  // AP IP first
  
  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");  // ← CHANGE THESE
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    vTaskDelay(pdMS_TO_TICKS(500));
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    currentIP = WiFi.localIP().toString();  // Update to STA IP
  }
  
  // Web server - FIXED lambdas using global currentIP
  server.on("/", HTTP_GET, []() {
    xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100));
    String html = "<!DOCTYPE html><html><head><title>Smart Gateway</title>"
                  "<meta http-equiv='refresh' content='5'></head><body>"
                  "<h1>Smart Home Gateway</h1>"
                  "<p><b>Temp:</b> " + String(lastTemp,1) + "°C</p>"
                  "<p><b>Humidity:</b> " + String(lastHum,1) + "%</p>"
                  "<p><b>Relay:</b> " + String(relayState ? "ON" : "OFF") + "</p>"
                  "<p><a href='/relay/on' style='padding:10px;background:#4CAF50;color:white'>TURN ON</a> "
                  "<a href='/relay/off' style='padding:10px;background:#f44336;color:white'>TURN OFF</a></p>"
                  "<hr>IP: <b>http://" + currentIP + "</b></body></html>";  // FIXED: currentIP
    xSemaphoreGive(dataMutex);
    server.send(200, "text/html", html);
  });
  
  server.on("/relay/on", HTTP_GET, []() {
    digitalWrite(RELAYPIN, HIGH);
    xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10));
    relayState = true;
    xSemaphoreGive(dataMutex);
    server.send(200, "text/plain", "Relay ON");
  });
  
  server.on("/relay/off", HTTP_GET, []() {
    digitalWrite(RELAYPIN, LOW);
    xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10));
    relayState = false;
    xSemaphoreGive(dataMutex);
    server.send(200, "text/plain", "Relay OFF");
  });
  
  server.begin();
  while(1) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void BLETask(void *pvParameters) {
  SerialBT.begin("SmartGateway");
  while(1) {
    if (SerialBT.available()) {
      String cmd = SerialBT.readStringUntil('\n');
      cmd.trim();
      xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100));
      if (cmd.indexOf("ON") >= 0) {
        digitalWrite(RELAYPIN, HIGH);
        relayState = true;
      } else if (cmd.indexOf("OFF") >= 0) {
        digitalWrite(RELAYPIN, LOW);
        relayState = false;
      }
      SerialBT.printf("Temp:%.1f°C Hum:%.1f%% Relay:%s IP:%s\n", 
                      lastTemp, lastHum, relayState ? "ON" : "OFF", currentIP.c_str());
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Smart Home Gateway RTOS ===");
  
  pinMode(RELAYPIN, OUTPUT);
  digitalWrite(RELAYPIN, LOW);
  
  sensorQueue = xQueueCreate(1, sizeof(float));
  dataMutex = xSemaphoreCreateMutex();
  if (sensorQueue == NULL || dataMutex == NULL) {
    Serial.println("ERROR: Failed to create queue/mutex!");
    while(1);
  }
  
  xTaskCreatePinnedToCore(SensorTask, "Sensor", 4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(RelayTask, "Relay", 2048, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(WiFiTask, "WiFi", 6144, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(BLETask, "BLE", 4096, NULL, 1, NULL, 1);
  
  Serial.println("Tasks started! Web: http://" + currentIP);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
  Serial.printf("Uptime:%dmin T:%.1f°C H:%.1f%% R:%s IP:%s\n", 
                millis()/60000, lastTemp, lastHum, relayState?"ON":"OFF", currentIP.c_str());
}
