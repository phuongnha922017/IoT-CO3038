// #include <Arduino.h>
// #include <WiFi.h>
// #include <PubSubClient.h>
// #include <DHT.h>
// const char* ssid = "...";
// const char* password = "0354453801";

// const char* mqttServer = "app.coreiot.io"; 
// const int mqttPort = 1883;                  
// const char* mqttUser = "nUwypBbRbqsn2crp6u6P";  
// const char* mqttPassword = "";              

// // DHT Sensor Config
// #define DHTPIN 25
// #define DHTTYPE DHT11
// DHT dht(DHTPIN, DHTTYPE);

// // WiFi & MQTT Client
// WiFiClient wifiClient;
// PubSubClient mqttClient(wifiClient);

// TaskHandle_t wifiReconnectTaskHandle;
// TaskHandle_t dhtTaskHandle;
// TaskHandle_t sendDataTaskHandle;
// TaskHandle_t mqttReconnectTaskHandle;

// void wifiReconnectTask(void *pvParameters) {
//     while (1) {
//         if (WiFi.status() != WL_CONNECTED) {
//             Serial.println("[WiFi] Disconnected! Reconnecting...");
//             WiFi.disconnect();
//             WiFi.begin(ssid, password);
            
//             int retryCount = 0;
//             while (WiFi.status() != WL_CONNECTED && retryCount < 10) {
//                 delay(1000);
//                 Serial.print(".");
//                 retryCount++;
//             }

//             if (WiFi.status() == WL_CONNECTED) {
//                 Serial.println("\n[WiFi] Reconnected! IP: " + WiFi.localIP().toString());
//             } else {
//                 Serial.println("\n[WiFi] Failed to reconnect!");
//             }
//         }
//         vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds
//     }
// }

// // MQTT Reconnect Task
// void mqttReconnectTask(void *pvParameters) {
//     while (1) {
//         if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
//             Serial.println("[MQTT] Disconnected! Reconnecting...");
//             if (mqttClient.connect("ESP32Client", mqttUser, mqttPassword)) {
//                 Serial.println("[MQTT] Connected!");
//                 // Subscribe to RPC requests
//                 mqttClient.subscribe("v1/devices/me/rpc/request/+");
//             } else {
//                 Serial.println("[MQTT] Failed to connect, rc=" + String(mqttClient.state()));
//             }
//         }
//         mqttClient.loop();  // Process incoming messages
//         vTaskDelay(pdMS_TO_TICKS(1000));  // Check every 1 second
//     }
// }

// void readDHTTask(void *pvParameters) {
//     while (1) {
//         float temperature = dht.readTemperature();
//         float humidity = dht.readHumidity();

//         if (isnan(temperature) || isnan(humidity)) {
//             Serial.println("[DHT] Failed to read sensor data!");
//         } else {
//             Serial.print("[DHT] Temperature: ");
//             Serial.print(temperature);
//             Serial.println(" °C");
//             Serial.print("[DHT] Humidity: ");
//             Serial.print(humidity);
//             Serial.println(" %");
//         }
//         vTaskDelay(pdMS_TO_TICKS(2000));  // Read every 2 seconds
//     }
// }

// // Send Data Task (Telemetry via MQTT)
// void sendDataTask(void *pvParameters) {
//     while (1) {
//         if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
//             float temperature = dht.readTemperature();
//             float humidity = dht.readHumidity();

//             if (!isnan(temperature) && !isnan(humidity)) {
//                 String telemetry = "{\"temperature\":" + String(temperature) + ", \"humidity\":" + String(humidity) + "}";
//                 mqttClient.publish("v1/devices/me/telemetry", telemetry.c_str());
//                 Serial.println("[MQTT] Telemetry sent: " + telemetry);
//             }
//         } else {
//             Serial.println("[MQTT] Skipped - Not connected");
//         }
//         vTaskDelay(pdMS_TO_TICKS(5000));  // Send every 5 seconds
//     }
// }

// void setup() {
//     Serial.begin(115200);

//     WiFi.begin(ssid, password);
//     Serial.print("Connecting to WiFi");
//     while (WiFi.status() != WL_CONNECTED) {
//         delay(500);
//         Serial.print(".");
//     }
//     Serial.println("\nConnected to WiFi! IP: " + WiFi.localIP().toString());

//     mqttClient.setServer(mqttServer, mqttPort);
//     dht.begin();

//     // Create tasks
//     xTaskCreate(wifiReconnectTask, "WiFi Reconnect Task", 2048, NULL, 1, &wifiReconnectTaskHandle);
//     xTaskCreate(mqttReconnectTask, "MQTT Reconnect Task", 4096, NULL, 1, &mqttReconnectTaskHandle);
//     xTaskCreate(readDHTTask, "DHT11 Task", 2048, NULL, 1, &dhtTaskHandle);
//     xTaskCreate(sendDataTask, "Send Data Task", 4096, NULL, 1, &sendDataTaskHandle);
// }

// void loop() {
   
// }


