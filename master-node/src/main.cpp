#include "nvs_flash.h"
#include "esp_netif.h"
#include "zh_network.h"
#include <map>
#include <string>
#include "driver/uart.h"

#define UART_NUM UART_NUM_1
#define TX_PIN 17
#define RX_PIN 16
#define BUF_SIZE 1024

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

extern "C" void zh_network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

void init_uart();
void send_struct(const sensor_node_message *data);

std::map<std::string, int> message_counts;

extern "C" void app_main(void)
{
    esp_log_level_set("zh_vector", ESP_LOG_NONE);
    esp_log_level_set("zh_network", ESP_LOG_NONE); // ESP_LOG_INFO
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_max_tx_power(8); // Power reduction is for example and testing purposes only. Do not use in your own programs!
    zh_network_init_config_t network_init_config = ZH_NETWORK_INIT_CONFIG_DEFAULT();
    zh_network_init(&network_init_config);
    esp_event_handler_instance_register(ZH_NETWORK, ESP_EVENT_ANY_ID, &zh_network_event_handler, NULL, NULL);

    init_uart();

    while (1) {
        uint8_t buffer[BUF_SIZE];
        int len = uart_read_bytes(UART_NUM, buffer, BUF_SIZE - 1, 50 / portTICK_PERIOD_MS);

        if (len && len == sizeof(node_config)) {
            node_config received;
            memcpy(&received, buffer, sizeof(node_config));
            printf("CONFIG RECEIVED - Version: %d, Interval: %d\n", received.version, received.interval);
            zh_network_send(NULL, (uint8_t *)&received, sizeof(received));
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

extern "C" void zh_network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == ZH_NETWORK_ON_RECV_EVENT)
    {
        zh_network_event_on_recv_t *recv_data = (zh_network_event_on_recv_t *)event_data;
        sensor_node_message *recv_message = (sensor_node_message *)recv_data->data;
        printf("NODE MESSAGE RECEIVED - Version: %d, Moisture: %d, Name: %s\n", recv_message->version, recv_message->moisture, recv_message->name);
        send_struct(recv_message);
        heap_caps_free(recv_data->data); // Do not delete to avoid memory leaks!
    }
}

void init_uart() {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, BUF_SIZE, BUF_SIZE, 0, NULL, 0);
}

void send_struct(const sensor_node_message *data) {
    uint8_t buffer[sizeof(sensor_node_message)];
    memcpy(buffer, data, sizeof(sensor_node_message));
    printf("SENDING %d BYTES VIA UART\n", sizeof(buffer));
    uart_write_bytes(UART_NUM, (const char *)buffer, sizeof(buffer));
}
