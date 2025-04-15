#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <Update.h>

const char* firmwareURL = "https://github.com/phuongnha922017/IoT-CO3038/blob/main/firmware/firmware.json";
#define firmwareVersion "1.0"

const char* ssid = "...";
const char* password = "";

// ThingsBoard MQTT Server & Token
const char* mqttServer = "app.coreiot.io"; 
const int mqttPort = 1883;                  
const char* mqttUser = "nUwypBbRbqsn2crp6u6P";  
const char* mqttPassword = "";              

// DHT Sensor Config
#define DHTPIN 25
#define DHTTYPE DHT11
#define LED_PIN 13 
DHT dht(DHTPIN, DHTTYPE);

// WiFi & MQTT Client
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Task Handles
TaskHandle_t dhtTaskHandle;
TaskHandle_t sendDataTaskHandle;
TaskHandle_t wifiReconnectTaskHandle;
TaskHandle_t mqttReconnectTaskHandle;
TaskHandle_t otaHandle



volatile bool ledState = false;
void callback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println("[MQTT] Received on " + String(topic) + ": " + message);

    // Check if the topic is an RPC request
    String topicStr = String(topic);
    if (topicStr.startsWith("v1/devices/me/rpc/request/")) {
        // Extract the request ID (e.g., "22" from "v1/devices/me/rpc/request/22")
        String requestId = topicStr.substring(String("v1/devices/me/rpc/request/").length());

        // Handle RPC methods
        if (message.indexOf("setLEDValue") != -1) {
            int valueStart = message.indexOf("\"params\":") + 8;
            int valueEnd = message.indexOf("}", valueStart);

            String valueStr = message.substring(valueStart+1, valueEnd);
            Serial.print("ValueStr:  "); Serial.println(valueStr);
        
            
            ledState = (valueStr == String("true"));
            Serial.print("ledState: "); Serial.println(ledState);
            digitalWrite(LED_PIN, ledState ? HIGH : LOW);
            Serial.println("[MQTT] LED State: " + String(ledState ? "ON" : "OFF"));

            // Publish response to the correct response topic
            String responseTopic = "v1/devices/me/rpc/response/" + requestId;
            String responsePayload = "{\"value\":" + String(ledState ? 1 : 0) + "}";
            mqttClient.publish(responseTopic.c_str(), responsePayload.c_str());
        }
        
        // Handle getLEDvalue request
        if (message.indexOf("getLEDValue") != -1) {
            String responseTopic = "v1/devices/me/rpc/response/" + requestId;
            String responsePayload = "{\"value\":" + String(ledState ? 1 : 0) + "}";
            mqttClient.publish(responseTopic.c_str(), responsePayload.c_str());
            Serial.println("[MQTT] LED State requested: " + String(ledState ? "ON" : "OFF"));
        }
    }
}

// WiFi Reconnect Task
void wifiReconnectTask(void *pvParameters) {
    while (1) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Disconnected! Reconnecting...");
            WiFi.disconnect();
            WiFi.begin(ssid, password);
            
            int retryCount = 0;
            while (WiFi.status() != WL_CONNECTED && retryCount < 10) {
                delay(1000);
                Serial.print(".");
                retryCount++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\n[WiFi] Reconnected! IP: " + WiFi.localIP().toString());
            } else {
                Serial.println("\n[WiFi] Failed to reconnect!");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds
    }
}

// MQTT Reconnect Task
void mqttReconnectTask(void *pvParameters) {
    while (1) {
        if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
            Serial.println("[MQTT] Disconnected! Reconnecting...");
            if (mqttClient.connect("ESP32Client", mqttUser, mqttPassword)) {
                Serial.println("[MQTT] Connected!");
                // Subscribe to RPC requests
                mqttClient.subscribe("v1/devices/me/rpc/request/+");
            } else {
                Serial.println("[MQTT] Failed to connect, rc=" + String(mqttClient.state()));
            }
        }
        mqttClient.loop();  // Process incoming messages
        vTaskDelay(pdMS_TO_TICKS(1000));  // Check every 1 second
    }
}

// Read DHT Task
void readDHTTask(void *pvParameters) {
    while (1) {
        float temperature = dht.readTemperature();
        float humidity = dht.readHumidity();

        if (isnan(temperature) || isnan(humidity)) {
            Serial.println("[DHT] Failed to read sensor data!");
        } else {
            Serial.print("[DHT] Temperature: ");
            Serial.print(temperature);
            Serial.println(" °C");
            Serial.print("[DHT] Humidity: ");
            Serial.print(humidity);
            Serial.println(" %");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));  // Read every 2 seconds
    }
}

// Send Data Task (Telemetry via MQTT)
void sendDataTask(void *pvParameters) {
    while (1) {
        if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
            float temperature = dht.readTemperature();
            float humidity = dht.readHumidity();

            if (!isnan(temperature) && !isnan(humidity)) {
                String telemetry = "{\"temperature\":" + String(temperature) + ", \"humidity\":" + String(humidity) + "}";
                mqttClient.publish("v1/devices/me/telemetry", telemetry.c_str());
                Serial.println("[MQTT] Telemetry sent: " + telemetry);
            }
        } else {
            Serial.println("[MQTT] Skipped - Not connected");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));  // Send every 5 seconds
    }
}

void checkAndUpdateFirmware(void *pvParameter) {
    while (1) {
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            http.begin(firmwareURL);
    
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                Serial.println("Version info: " + payload);
            
                int verIndex = payload.indexOf("\"version\":\"") + 11;
                int verEndIndex = payload.indexOf("\"", verIndex);
                String newVersion = payload.substring(verIndex, verEndIndex);
                Serial.println("Latest version: " + newVersion);
    
                if (newVersion != firmwareVersion) {
                    Serial.println("New firmware detected. Starting OTA...");
                    performOTA(firmwareURL);
                } else {
                    Serial.println("Firmware is up to date.");
                }
            } else {
                Serial.println("Failed to fetch version info, HTTP code: " + String(httpCode));
            }
            http.end();
        }
        vTaskDelay(60000)
    }
}
  
void performOTA(const char* url) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, url);
  
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        bool canBegin = Update.begin(contentLength);
  
        if (canBegin) {
            Serial.println("Begin OTA update...");
            size_t written = Update.writeStream(http.getStream());
  
            if (written == contentLength) {
            Serial.println("OTA written successfully!");
            if (Update.end()) {
                Serial.println("Update complete.");
                ESP.restart();
            } else {
                Serial.println("Error ending update.");
            }
            } else {
            Serial.println("Mismatch in written bytes.");
            }
        } else {
            Serial.println("Not enough space for OTA.");
        }
        } else {
        Serial.println("Failed to download firmware. HTTP code: " + String(httpCode));
        }
        http.end();
    }
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi! IP: " + WiFi.localIP().toString());

    mqttClient.setServer(mqttServer, mqttPort);
    mqttClient.setCallback(callback);
    dht.begin();

    // Create tasks
    xTaskCreate(wifiReconnectTask, "WiFi Reconnect Task", 2048, NULL, 1, &wifiReconnectTaskHandle);
    xTaskCreate(mqttReconnectTask, "MQTT Reconnect Task", 4096, NULL, 1, &mqttReconnectTaskHandle);
    xTaskCreate(readDHTTask, "DHT11 Task", 2048, NULL, 1, &dhtTaskHandle);
    xTaskCreate(sendDataTask, "Send Data Task", 4096, NULL, 1, &sendDataTaskHandle);
    xTaskCreate(checkAndUpdateFirmware, "Check and update firmware", 2048, NULL, 1, &otaHandle)
}


void loop() {
   
}