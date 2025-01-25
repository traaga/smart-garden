/*
#include "nvs_flash.h"
#include "esp_netif.h"
#include "zh_network.h"
#include "driver/i2c_master.h"
#include "ssd1306.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define LED_GPIO 4

typedef struct __attribute__((packed)) {
    char id[32];
    int moisture;
} sensor_node_message;

static esp_adc_cal_characteristics_t adc1_chars;

void i2c_master_init()
{
    i2c_master_bus_config_t i2c_mst_config = {};
    i2c_mst_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_mst_config.i2c_port = I2C_MASTER_NUM;
    i2c_mst_config.scl_io_num = GPIO_NUM_22;
    i2c_mst_config.sda_io_num = GPIO_NUM_21;
    i2c_mst_config.glitch_ignore_cnt = 7;
    i2c_mst_config.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x3C,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        .scl_wait_us = 0,
        .flags = {}
    };

    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
}

extern "C" void app_main(void)
{
    esp_log_level_set("zh_vector", ESP_LOG_NONE);
    esp_log_level_set("zh_network", ESP_LOG_NONE); //ESP_LOG_INFO
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

    SSD1306_t dev;
    i2c_master_init(&dev, GPIO_NUM_21, GPIO_NUM_22, -1);  // Use the existing function
    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);

    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 0, &adc1_chars);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_12);
    
    sensor_node_message message = {
        //id: "sensor-node-1",
        id: "test",
        moisture: 0,
    };

    bool led_state = false;

    for (;;)
    {
        message.moisture = adc1_get_raw(ADC1_CHANNEL_4);
        printf("%d\n", message.moisture);

        // Update OLED display
        char str[32];
        snprintf(str, sizeof(str), "Moisture: %d", message.moisture);
        ssd1306_clear_screen(&dev, false);
        ssd1306_display_text(&dev, 0, str, strlen(str), false);

        gpio_set_level((gpio_num_t)LED_GPIO, led_state);
        led_state = !led_state;

        //zh_network_send(NULL, (uint8_t *)&message, sizeof(message));
        // 5000 -> 5s
        // 600000 -> 10min
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}*/

// ############################################################################################################

#include "nvs_flash.h"
#include "esp_netif.h"
#include "zh_network.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#define LED_GPIO GPIO_NUM_2

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

int64_t start;
bool is_processing_api_response = false;

// --- SET DEFAULT VALUES ---
node_config config = {
    id: "",
    name: "default",
    //name: "sensor-node-1",
    version: 0,
    interval: 600,
    led_state: 0,
};

static esp_adc_cal_characteristics_t adc1_chars;
extern "C" void zh_network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void write_config(node_config new_config);
void uuid_generate(uint8_t out[16]);

extern "C" void app_main(void)
{
    //esp_log_level_set("zh_vector", ESP_LOG_NONE);
    //esp_log_level_set("zh_network", ESP_LOG_NONE); //ESP_LOG_INFO
    esp_log_level_set("*", ESP_LOG_ERROR);
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

    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 0, &adc1_chars);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_12);

    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    // --- READ CONFIG FROM ONBOARD MEMORY ---
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        uint8_t stored_id[16];
        char stored_name;
        uint16_t stored_version;
        uint32_t stored_interval;
        uint8_t stored_led_state;

        size_t required_id_size = sizeof(config.id);
        size_t required_name_size = sizeof(config.name);

        if (nvs_get_blob(nvs_handle, "id", &stored_id, &required_id_size) == ESP_OK &&
            nvs_get_str(nvs_handle, "name", &stored_name, &required_name_size) == ESP_OK &&
            nvs_get_u16(nvs_handle, "version", &stored_version) == ESP_OK &&
            nvs_get_u32(nvs_handle, "interval", &stored_interval) == ESP_OK &&
            nvs_get_u8(nvs_handle, "led_state", &stored_led_state) == ESP_OK) {

            memcpy(config.id, &stored_id, sizeof(stored_id));
            config.version = stored_version;
            config.interval = stored_interval;
            config.led_state = stored_led_state;
        }
        nvs_close(nvs_handle);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        printf("Config not found, creating default config...\n");

        uint8_t device_uuid[16];
        uuid_generate(device_uuid);
        memcpy(config.id, &device_uuid, sizeof(device_uuid));

        write_config(config);
    } else {
        printf("ERROR: nvs_open FAILED\n");
    }

    sensor_node_message message = {
        id: "",
        name: "default",
        //name: "sensor-node-1",
        moisture: 0,
        version: 0
    };

    // Assign message parameters from config
    memcpy(message.id, &config.id, sizeof(config.id));
    memcpy(message.name, &config.name, sizeof(config.name));
    message.version = config.version;

    printf("NAME: \t\t%s\n", config.name);
    printf("VERSION: \t%d\n", config.version);
    printf("INTERVAL: \t%d\n", config.interval);

    message.moisture = adc1_get_raw(ADC1_CHANNEL_4);
    printf("MOISTURE: \t%d\n", message.moisture);

    gpio_set_level(LED_GPIO, config.led_state);
    printf("LED: \t\t%d\n", config.led_state);

    start = esp_timer_get_time();
    zh_network_send(NULL, (uint8_t *)&message, sizeof(message));

    // TODO: Add config response wait time to config (default 500ms)
    vTaskDelay(500 / portTICK_PERIOD_MS); // 500 (ms)

    printf("------------------------------------\n");
    if(!is_processing_api_response) {
        esp_sleep_enable_timer_wakeup(config.interval * 1000000);
        esp_deep_sleep_start();
    }
}

extern "C" void zh_network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == ZH_NETWORK_ON_RECV_EVENT)
    {
        zh_network_event_on_recv_t *recv_data = (zh_network_event_on_recv_t *)event_data;

        if (recv_data->data_len != sizeof(node_config)) {
            printf("Invalid data size: expected %zu bytes, got %zu bytes\n", 
                sizeof(node_config), recv_data->data_len);
            heap_caps_free(recv_data->data);
            return;
        }

        is_processing_api_response = true;

        node_config *recv_message = (node_config *)recv_data->data;
        printf("NEW CONFIG RECEIVED - Version: %d, Interval: %d\n", recv_message->version, recv_message->interval);
        int64_t end = esp_timer_get_time();
        int64_t duration = end - start;
        printf("Api config response came in %lld microseconds\n", duration);

        printf("OLD ID: \t%s\n", config.id);
        printf("NEW ID: \t%s\n", recv_message->id);

        if (memcmp(config.id, recv_message->id, 16) == 0) {
            printf("IDs are identical\n");
            if(config.version != recv_message->version) {
                write_config(*recv_message);
            } else {
                printf("Config versions are the same\n");
            }
        } else {
            printf("IDs are different\n");
        }

        // TODO: Send acknowledgement response to api of success or error

        esp_sleep_enable_timer_wakeup(recv_message->interval * 1000000);
        heap_caps_free(recv_data->data); // Do not delete to avoid memory leaks!
        esp_deep_sleep_start();
    }
}

void write_config(node_config new_config) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &nvs_handle);

    if (err == ESP_OK) {
        esp_err_t err_write = 
            nvs_set_blob(nvs_handle, "id", new_config.id, sizeof(new_config.id)) |
            nvs_set_str(nvs_handle, "name", new_config.name) |
            nvs_set_u16(nvs_handle, "version", new_config.version) |
            nvs_set_u32(nvs_handle, "interval", new_config.interval) |
            nvs_set_u8(nvs_handle, "led_state", new_config.led_state);

        if (err_write == ESP_OK) {
            // Commit written value
            err = nvs_commit(nvs_handle);
            if (err == ESP_OK) {
                printf("Config committed successfully\n");
            } else {
                printf("Failed to commit config\n");
            }
        } else {
            printf("Failed to write config\n");
        }
        nvs_close(nvs_handle);
    } else {
        printf("Failed to open NVS in write mode\n");
    }
}

// https://github.com/typester/esp32-uuid/blob/master/uuid.c
void uuid_generate(uint8_t out[16])
{
    esp_fill_random(out, sizeof(uint8_t));

    /* uuid version */
    out[6] = 0x40 | (out[6] & 0xF);

    /* uuid variant */
    out[8] = (0x80 | out[8]) & ~0x40;
}
