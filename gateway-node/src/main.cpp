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
    uint16_t moisture;
    uint16_t version;
} sensor_node_message;

typedef struct __attribute__((packed)) {
    uint8_t id[16];
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
