#include "nvs_flash.h"
#include "esp_netif.h"
#include "zh_network.h"
#include <M5Unified.h>
#include <map>

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

extern "C" void zh_network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

uint8_t target[6] = {0x58, 0xBF, 0x25, 0x18, 0xC8, 0x04};

std::map<std::string, int> message_counts;

typedef struct
{
    char char_value[30];
    int int_value;
    float float_value;
    bool bool_value;
} example_message_t;

extern "C" void app_main(void)
{
    esp_log_level_set("zh_vector", ESP_LOG_NONE);
    esp_log_level_set("zh_network", ESP_LOG_INFO); // ESP_LOG_NONE
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
#ifdef CONFIG_IDF_TARGET_ESP8266
    esp_event_handler_register(ZH_NETWORK, ESP_EVENT_ANY_ID, &zh_network_event_handler, NULL);
#else
    esp_event_handler_instance_register(ZH_NETWORK, ESP_EVENT_ANY_ID, &zh_network_event_handler, NULL, NULL);
#endif
    example_message_t send_message = {0};
    strcpy(send_message.char_value, "THIS IS A CHAR");
    send_message.float_value = 1.234;
    send_message.bool_value = false;

    auto cfg = M5.config();
    M5.begin(cfg); 
    M5.Display.setRotation(1);
    M5.Display.print("primary display\n");
    /*for (;;)
    {
        send_message.int_value = esp_random();
        zh_network_send(NULL, (uint8_t *)&send_message, sizeof(send_message));
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        //zh_network_send(target, (uint8_t *)&send_message, sizeof(send_message));
        //vTaskDelay(5000 / portTICK_PERIOD_MS);
    }*/
}

extern "C" void zh_network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    /*switch (event_id)
    {
    case ZH_NETWORK_ON_RECV_EVENT:;
        zh_network_event_on_recv_t *recv_data = event_data;
        printf("Message from MAC %02X:%02X:%02X:%02X:%02X:%02X is received. Data lenght %d bytes.\n", MAC2STR(recv_data->mac_addr), recv_data->data_len);
        example_message_t *recv_message = (example_message_t *)recv_data->data;
        M5.Display.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", MAC2STR(recv_data->mac_addr));
        printf("Char %s\n", recv_message->char_value);
        printf("Int %d\n", recv_message->int_value);
        printf("Float %f\n", recv_message->float_value);
        printf("Bool %d\n", recv_message->bool_value);
        heap_caps_free(recv_data->data); // Do not delete to avoid memory leaks!
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        //M5.Lcd.fillScreen(BLACK);
        break;
    case ZH_NETWORK_ON_SEND_EVENT:;
        printf("123");
        zh_network_event_on_send_t *send_data = event_data;
        if (send_data->status == ZH_NETWORK_SEND_SUCCESS)
        {
            //printf("Message to MAC %02X:%02X:%02X:%02X:%02X:%02X sent success.\n", MAC2STR(send_data->mac_addr));
        }
        else
        {
            //printf("Message to MAC %02X:%02X:%02X:%02X:%02X:%02X sent fail.\n", MAC2STR(send_data->mac_addr));
        }
        break;
    default:
        break;
    }*/

    if (event_id == ZH_NETWORK_ON_RECV_EVENT)
    {
        zh_network_event_on_recv_t *recv_data = (zh_network_event_on_recv_t *)event_data;
        example_message_t *recv_message = (example_message_t *)recv_data->data;
        M5.Display.clear();
        M5.Display.setCursor(0,0);
        //M5.Display.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", MAC2STR(recv_data->mac_addr));

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
