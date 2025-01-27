/*
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

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

    Serial.println("MQTT MESSAGE RECEIVED\n");

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
    Serial.println("Gateway node setup\n");

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
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "driver/uart.h"
#include "cJSON.h"
#include "secrets.h"

#define MQTT_BROKER_URL "mqtt://192.168.1.47"
#define MQTT_PORT 1883
#define MQTT_TOPIC_PUBLISH "mesh/out"
#define MQTT_TOPIC_SUBSCRIBE "mesh/in"

#define UART_PORT UART_NUM_1
#define UART_TX_PIN 17
#define UART_RX_PIN 16
#define BUF_SIZE 1024

static const char *TAG = "mqtt_gateway";
static esp_mqtt_client_handle_t mqtt_client = NULL;

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

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Connected to WiFi");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_t *event = (esp_mqtt_event_t *)event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC_SUBSCRIBE, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected");
            break;

        case MQTT_EVENT_DATA:
            if (strncmp(event->topic, MQTT_TOPIC_SUBSCRIBE, event->topic_len) == 0) {
                cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
                if (root == NULL) {
                    ESP_LOGE(TAG, "JSON Parse Error");
                    return;
                }

                node_config config;
                memset(&config, 0, sizeof(config));

                cJSON *id = cJSON_GetObjectItem(root, "id");
                if (id && id->valuestring) {
                    for (int i = 0; i < 16; i++) {
                        sscanf(&id->valuestring[i*2], "%2hhx", &config.id[i]);
                    }
                }

                cJSON *name = cJSON_GetObjectItem(root, "name");
                if (name && name->valuestring) {
                    strlcpy(config.name, name->valuestring, sizeof(config.name));
                }

                cJSON *version = cJSON_GetObjectItem(root, "version");
                if (version) config.version = version->valueint;

                cJSON *interval = cJSON_GetObjectItem(root, "interval");
                if (interval) config.interval = interval->valueint;

                cJSON *led_state = cJSON_GetObjectItem(root, "led_state");
                if (led_state) config.led_state = led_state->valueint;

                uart_write_bytes(UART_PORT, (const char*)&config, sizeof(node_config));

                cJSON_Delete(root);
            }
            break;

        default:
            break;
    }
}

static void init_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASSWORD);
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    wifi_config.sta.bssid_set = 0;
    wifi_config.sta.channel = 0;
    wifi_config.sta.listen_interval = 0;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void init_uart() {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, BUF_SIZE, BUF_SIZE, 0, NULL, 0);
}

static void uart_rx_task(void *arg)
{
    uint8_t buffer[BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(UART_PORT, buffer, BUF_SIZE - 1, 50 / portTICK_PERIOD_MS);
        
        if (len == sizeof(sensor_node_message)) {
            sensor_node_message *msg = (sensor_node_message*)buffer;

            char id_hex[33];
            for(int i = 0; i < 16; i++) {
                sprintf(&id_hex[i*2], "%02x", msg->id[i]);
            }

            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "id", id_hex);
            cJSON_AddStringToObject(root, "name", msg->name);
            cJSON_AddNumberToObject(root, "version", msg->version);
            cJSON_AddNumberToObject(root, "moisture", msg->moisture);

            char *json_string = cJSON_PrintUnformatted(root);
            esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_PUBLISH, json_string, 0, 1, 1);

            free(json_string);
            cJSON_Delete(root);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_wifi();
    init_uart();

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URL;
    mqtt_cfg.broker.address.port = MQTT_PORT;

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));

    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 10, NULL);
}
