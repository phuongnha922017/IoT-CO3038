#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

const char *ssid = "Wokwi-GUEST";
const char *password = "";
const char *mqttServer = "app.coreiot.io";
const int mqttPort = 1883;
const char *mqttUser = "nUwypBbRbqsn2crp6u6P";
const char *mqttPassword = "";
#define DHTPIN 25
#define DHTTYPE DHT11
#define LED1_PIN 13
#define LED2_PIN 23
volatile int blinkInterval = 1000;
DHT dht(DHTPIN, DHTTYPE);

// [Existing WiFi/MQTT clients and task handles unchanged]
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
TaskHandle_t dhtTaskHandle;
TaskHandle_t sendDataTaskHandle;
TaskHandle_t wifiReconnectTaskHandle;
TaskHandle_t mqttReconnectTaskHandle;
volatile bool ledState = false;

// Mutex for blinkInterval
SemaphoreHandle_t blinkIntervalMutex;

// Request ID for attribute requests
int attributeRequestId = 1;

void callback(char *topic, byte *payload, unsigned int length)
{
    String message;
    for (unsigned int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }
    Serial.println("[MQTT] Received on " + String(topic) + ": " + message);

    String topicStr = String(topic);
    if (topicStr.startsWith("v1/devices/me/rpc/request/"))
    {
        String requestId = topicStr.substring(String("v1/devices/me/rpc/request/").length());

        // Parse JSON payload
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error)
        {
            Serial.println("[MQTT] Failed to parse RPC JSON: " + String(error.c_str()));
            return;
        }

        String method = doc["method"];
        String responseTopic = "v1/devices/me/rpc/response/" + requestId;
        String responsePayload;

        if (method == "setLEDValue")
        {
            bool newState = false;
            if (doc["params"].is<bool>())
            {
                newState = doc["params"].as<bool>();
            }
            else if (doc["params"].is<JsonObject>() && doc["params"].containsKey("value"))
            {
                newState = doc["params"]["value"].as<bool>();
            }
            else
            {
                Serial.println("[MQTT] Invalid params format for setLEDValue");
                return;
            }

            ledState = newState;
            digitalWrite(LED1_PIN, ledState ? HIGH : LOW);
            Serial.print("ledState: ");
            Serial.println(ledState);
            Serial.println("[MQTT] LED State: " + String(ledState ? "ON" : "OFF"));

            responsePayload = "{\"value\":" + String(ledState ? 1 : 0) + "}";
            if (!mqttClient.publish(responseTopic.c_str(), responsePayload.c_str()))
            {
                Serial.println("[MQTT] Failed to publish RPC response");
            }
        }
        else if (method == "getLEDValue")
        {
            responsePayload = "{\"value\":" + String(ledState ? 1 : 0) + "}";
            if (!mqttClient.publish(responseTopic.c_str(), responsePayload.c_str()))
            {
                Serial.println("[MQTT] Failed to publish RPC response");
            }
            Serial.println("[MQTT] LED State requested: " + String(ledState ? "ON" : "OFF"));
        }
    }

    // Handle shared attribute updates and responses
    if (topicStr == "v1/devices/me/attributes" || topicStr.startsWith("v1/devices/me/attributes/response/"))
    {
        Serial.println("[MQTT] Processing attributes payload: " + message);
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error)
        {
            Serial.println("[MQTT] Failed to parse attributes JSON: " + String(error.c_str()));
            return;
        }

        int newInterval = -1;
        if (doc.containsKey("blinkInterval"))
        {
            newInterval = doc["blinkInterval"].as<int>();
        }
        else if (doc.containsKey("shared") && doc["shared"].containsKey("blinkInterval"))
        {
            newInterval = doc["shared"]["blinkInterval"].as<int>();
        }

        if (newInterval >= 0)
        {
            if (xSemaphoreTake(blinkIntervalMutex, pdMS_TO_TICKS(100)))
            {
                blinkInterval = newInterval;
                xSemaphoreGive(blinkIntervalMutex);
                Serial.println("[MQTT] Updated blinkInterval: " + String(blinkInterval));
            }
            else
            {
                Serial.println("[MQTT] Failed to take blinkIntervalMutex");
            }
        }
        else
        {
            Serial.println("[MQTT] No blinkInterval key found in payload");
        }
    }
}

void mqttReconnectTask(void *pvParameters)
{
    while (1)
    {
        if (WiFi.status() == WL_CONNECTED && !mqttClient.connected())
        {
            Serial.println("[MQTT] Disconnected! Reconnecting...");
            if (mqttClient.connect("ESP32Client", mqttUser, mqttPassword))
            {
                Serial.println("[MQTT] Connected!");
                // Subscribe to RPC and attributes
                if (mqttClient.subscribe("v1/devices/me/rpc/request/+"))
                {
                    Serial.println("[MQTT] Subscribed to RPC requests");
                }
                else
                {
                    Serial.println("[MQTT] Failed to subscribe to RPC requests");
                }
                if (mqttClient.subscribe("v1/devices/me/attributes"))
                {
                    Serial.println("[MQTT] Subscribed to attributes");
                }
                else
                {
                    Serial.println("[MQTT] Failed to subscribe to attributes");
                }
              
                if (mqttClient.subscribe("v1/devices/me/attributes/response/+"))
                {
                    Serial.println("[MQTT] Subscribed to attribute responses");
                }
                else
                {
                    Serial.println("[MQTT] Failed to subscribe to attribute responses");
                }
            
                String requestTopic = "v1/devices/me/attributes/request/" + String(attributeRequestId);
                String requestPayload = "{\"sharedKeys\":[\"blinkInterval\"]}";
                if (mqttClient.publish(requestTopic.c_str(), requestPayload.c_str()))
                {
                    Serial.println("[MQTT] Requested shared attributes: " + requestPayload);
                    attributeRequestId++;
                }
                else
                {
                    Serial.println("[MQTT] Failed to request shared attributes");
                }
            }
            else
            {
                Serial.println("[MQTT] Failed to connect, rc=" + String(mqttClient.state()));
            }
        }
        mqttClient.loop();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void blinkLedTask(void *pvParameters)
{
    while (1)
    {
        int currentInterval;
        if (xSemaphoreTake(blinkIntervalMutex, pdMS_TO_TICKS(100)))
        {
            currentInterval = blinkInterval;
            xSemaphoreGive(blinkIntervalMutex);
        }
        else
        {
            currentInterval = 1000; // Fallback to default
            Serial.println("[Blink] Failed to take blinkIntervalMutex");
        }
        Serial.println("[Blink] Current blinkInterval: " + String(currentInterval));
        digitalWrite(LED2_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(currentInterval));
        digitalWrite(LED2_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(currentInterval));
    }
}

// Read DHT Task
void readDHTTask(void *pvParameters)
{
    while (1)
    {
        float temperature = dht.readTemperature();
        float humidity = dht.readHumidity();

        if (isnan(temperature) || isnan(humidity))
        {
            Serial.println("[DHT] Failed to read sensor data!");
        }
        else
        {
            Serial.print("[DHT] Temperature: ");
            Serial.print(temperature);
            Serial.println(" °C");
            Serial.print("[DHT] Humidity: ");
            Serial.print(humidity);
            Serial.println(" %");
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // Read every 2 seconds
    }
}

// Send Data Task (Telemetry via MQTT)
void sendDataTask(void *pvParameters)
{
    while (1)
    {
        if (WiFi.status() == WL_CONNECTED && mqttClient.connected())
        {
            float temperature = dht.readTemperature();
            float humidity = dht.readHumidity();

            if (!isnan(temperature) && !isnan(humidity))
            {
                String telemetry = "{\"temperature\":" + String(temperature) + ", \"humidity\":" + String(humidity) + "}";
                mqttClient.publish("v1/devices/me/telemetry", telemetry.c_str());
                Serial.println("[MQTT] Telemetry sent: " + telemetry);
            }
        }
        else
        {
            Serial.println("[MQTT] Skipped - Not connected");
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Send every 5 seconds
    }
}

void wifiReconnectTask(void *pvParameters)
{
    while (1)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[WiFi] Disconnected! Reconnecting...");
            WiFi.disconnect();
            WiFi.begin(ssid, password);

            int retryCount = 0;
            while (WiFi.status() != WL_CONNECTED && retryCount < 10)
            {
                delay(1000);
                Serial.print(".");
                retryCount++;
            }

            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println("\n[WiFi] Reconnected! IP: " + WiFi.localIP().toString());
            }
            else
            {
                Serial.println("\n[WiFi] Failed to reconnect!");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED1_PIN, OUTPUT);
    pinMode(LED2_PIN, OUTPUT);
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);

    // Initialize mutex
    blinkIntervalMutex = xSemaphoreCreateMutex();
    if (blinkIntervalMutex == NULL)
    {
        Serial.println("[ERROR] Failed to create blinkIntervalMutex");
    }

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi! IP: " + WiFi.localIP().toString());

    mqttClient.setServer(mqttServer, mqttPort);
    mqttClient.setCallback(callback);
    mqttClient.setBufferSize(512); // Increase buffer for larger payloads
    dht.begin();

    // Create tasks
    xTaskCreate(wifiReconnectTask, "WiFi Reconnect Task", 2048, NULL, 1, &wifiReconnectTaskHandle);
    xTaskCreate(mqttReconnectTask, "MQTT Reconnect Task", 4096, NULL, 1, &mqttReconnectTaskHandle);
    xTaskCreate(readDHTTask, "DHT11 Task", 2048, NULL, 1, &dhtTaskHandle);
    xTaskCreate(sendDataTask, "Send Data Task", 4096, NULL, 1, &sendDataTaskHandle);
    xTaskCreate(blinkLedTask, "Blink LED2 Task", 2048, NULL, 1, NULL);
}

void loop()
{
}
