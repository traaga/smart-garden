#include "nvs_flash.h"
#include "esp_netif.h"
#include "zh_network.h"
#include <M5Unified.h>
#include <map>

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

extern "C" void zh_network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

std::map<std::string, int> message_counts;

typedef struct
{
    int moisture;
} sensor_node_message;

extern "C" void app_main(void)
{
    esp_log_level_set("zh_vector", ESP_LOG_NONE);
    esp_log_level_set("zh_network", ESP_LOG_INFO);
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
    
    auto cfg = M5.config();
    M5.begin(cfg); 
    M5.Display.setRotation(1);
    M5.Display.print("Master node");
}

extern "C" void zh_network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == ZH_NETWORK_ON_RECV_EVENT)
    {
        zh_network_event_on_recv_t *recv_data = (zh_network_event_on_recv_t *)event_data;
        sensor_node_message *recv_message = (sensor_node_message *)recv_data->data;
        M5.Display.clear();
        M5.Display.setCursor(0,0);

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", MAC2STR(recv_data->mac_addr));

        int old_count = message_counts[macStr];
        message_counts[macStr] = old_count + 1;

        for (auto el : message_counts) {
            M5.Display.print(el.first.c_str());
            M5.Display.print(" - ");
            M5.Display.println(el.second);
        }

        heap_caps_free(recv_data->data); // Do not delete to avoid memory leaks!
    }
}
