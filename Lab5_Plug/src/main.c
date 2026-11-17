#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_types.h"
#include "esp_rmaker_standard_params.h"
#include "esp_rmaker_standard_devices.h"
#include "app_network.h"

#define RELAY_PIN  19
#define BUTTON_PIN 4

static const char *TAG = "app_plug";
static esp_rmaker_device_t *plug_device;
static bool current_state = false;

static void app_driver_set_state(bool state) {
    current_state = state;
    gpio_set_level(RELAY_PIN, state ? 0 : 1); // active-low relay
    ESP_LOGI(TAG, "Relay set to %s", state ? "ON" : "OFF");
}

static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
            const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx) {
    if (strcmp(esp_rmaker_param_get_name(param), ESP_RMAKER_DEF_POWER_NAME) == 0) {
        app_driver_set_state(val.val.b);
        esp_rmaker_param_update(param, val);
    }
    return ESP_OK;
}

static void button_task(void *arg) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(BUTTON_PIN) == 0) {
                app_driver_set_state(!current_state);
                if (plug_device) {
                    esp_rmaker_param_update_and_report(
                        esp_rmaker_device_get_param_by_name(plug_device, ESP_RMAKER_DEF_POWER_NAME),
                        esp_rmaker_bool(current_state));
                }
                while (gpio_get_level(BUTTON_PIN) == 0) vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    gpio_reset_pin(RELAY_PIN);
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    app_driver_set_state(false);

    // 1. Initialize network and provisioning (creates rmaker_claim endpoint)
    app_network_init();   

    esp_rmaker_config_t rainmaker_cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP32 Plug", "Switch");
    if (!node) {
        ESP_LOGE(TAG, "Could not initialize node");
        vTaskDelay(pdMS_TO_TICKS(5000));
        abort();
    }

    plug_device = esp_rmaker_switch_device_create("Plug", NULL, false);
    esp_rmaker_device_add_cb(plug_device, write_cb, NULL);
    esp_rmaker_node_add_device(node, plug_device);

    // 2. Start RainMaker
    ESP_ERROR_CHECK(esp_rmaker_start());

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);

    // 3. Start provisioning (generates QR code and handles claiming)
    ESP_ERROR_CHECK(app_network_start(POP_TYPE_RANDOM)); 
}