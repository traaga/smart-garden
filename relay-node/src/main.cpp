#include "nvs_flash.h"
#include "esp_netif.h"
#include "zh_network.h"

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
}
