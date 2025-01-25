#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

#define MQTT_HOST IPAddress(192, 168, 1, 47)
#define MQTT_PORT 1883
#define MQTT_TOPIC_PUBLISH "mesh/out"
#define MQTT_TOPIC_SUBSCRIBE "mesh/in"

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

typedef struct __attribute__((packed)) {
    uint8_t id[16];
    char name[32]; 
    uint16_t moisture;
    uint16_t version;
} sensor_node_message;

typedef struct __attribute__((packed)) {
    uint8_t id[16];
    char name[32];
    uint16_t version;
    uint16_t interval;
    bool led_state;
} node_config;

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt()
{
    Serial.println("Connecting to MQTT...");
    mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP &event)
{
    Serial.println("Connected to Wi-Fi.");
    connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event)
{
    Serial.println("Disconnected from Wi-Fi.");
    mqttReconnectTimer.detach();
    wifiReconnectTimer.once(2, connectToWifi);
}

void onMqttConnect(bool sessionPresent)
{
    Serial.println("Connected to MQTT.");
    Serial.print("Session present: ");
    Serial.println(sessionPresent);

    uint16_t packetIdSub = mqttClient.subscribe(MQTT_TOPIC_SUBSCRIBE, 1);
    Serial.printf("Subscribing to %s with QoS 1, packetId: %i\n", MQTT_TOPIC_SUBSCRIBE, packetIdSub);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    Serial.println("Disconnected from MQTT.");

    if (WiFi.isConnected())
    {
        mqttReconnectTimer.once(2, connectToMqtt);
    }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload, len);

    if (error) {
        Serial.printf("JSON parsing failed: %s\n", error.c_str());
        return;
    }

    node_config config;
    
    // Convert hex string ID to bytes
    const char* id_str = doc["id"].as<const char*>();
    for (int i = 0; i < 16; i++) {
        sscanf(&id_str[i*2], "%2hhx", &config.id[i]);
    }
    
    strlcpy(config.name, doc["name"], sizeof(config.name));
    config.version = doc["version"];
    config.interval = doc["interval"];
    config.led_state = doc["led_state"];

    Serial.write((uint8_t*)&config, sizeof(node_config));
}

void setup()
{
    Serial.begin(115200); // UART on ESP8266: TX=GPIO1, RX=GPIO3
    Serial.println("Gateway node setup");

    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    // Uncomment and set your MQTT username/password if required
    // mqttClient.setCredentials("REPLACE_WITH_YOUR_USER", "REPLACE_WITH_YOUR_PASSWORD");

    connectToWifi();
}

void loop()
{
    if (Serial.available() >= sizeof(sensor_node_message))
    {
        uint8_t buffer[sizeof(sensor_node_message)];

        Serial.readBytes(buffer, sizeof(sensor_node_message));

        sensor_node_message received_message;
        memcpy(&received_message, buffer, sizeof(sensor_node_message));

        // Create hex string for ID bytes
        char id_hex[33];  // 16 bytes = 32 hex chars + null terminator
        for(int i = 0; i < 16; i++) {
            sprintf(&id_hex[i*2], "%02x", received_message.id[i]);
        }
        
        // Ensure name is null-terminated
        char name_buffer[33];  // 32 chars + null terminator
        strncpy(name_buffer, received_message.name, 32);
        name_buffer[32] = '\0';

        // Increased JSON buffer size to accommodate larger message
        char json_buffer[128];  
        snprintf(json_buffer, sizeof(json_buffer), 
            "{\"id\":\"%s\",\"name\":\"%s\",\"version\":%d,\"moisture\":%d}", 
            id_hex,
            name_buffer,
            received_message.version,
            received_message.moisture
        );

        Serial.printf("Received Moisture Value: %d\n", received_message.moisture);

        uint16_t packetId = mqttClient.publish(MQTT_TOPIC_PUBLISH, 1, true, json_buffer);
        Serial.printf("Published to topic %s with packetId %i\n", MQTT_TOPIC_PUBLISH, packetId);
    }

    delay(10);
}
