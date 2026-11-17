#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_network.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_types.h"
#include "esp_rmaker_standard_params.h"

/* DHT11 3-pin module: S -> GPIO4 (D4), V -> 3V3, G -> GND */
#define LDR_PIN 34

#define I2C_PORT I2C_NUM_0
#define OLED_ADDRESS 0x3C
#define OLED_SDA 21
#define OLED_SCL 22

/* DHT11 timing limits and pulse threshold. */
#define DHT_START_LOW_US 20000
#define DHT_BIT_THRESHOLD_US 40
#define DHT_TIMEOUT_US 200
#define DHT_CPU_HZ 160000000U
#define DHT_SAMPLE_INTERVAL_US 3000000LL

static const char *TAG = "sensor";

static esp_rmaker_param_t *temperature_param;
static esp_rmaker_param_t *light_param;
static float startup_temperature;
static bool startup_temperature_valid;
static gpio_num_t dht_pin = GPIO_NUM_4;

static portMUX_TYPE dht_mux = portMUX_INITIALIZER_UNLOCKED;

/* ================= I2C ================= */

static esp_err_t i2c_write_bytes(const uint8_t *data, size_t length)
{
    return i2c_master_write_to_device(
        I2C_PORT,
        OLED_ADDRESS,
        data,
        length,
        pdMS_TO_TICKS(100)
    );
}

static void oled_command(uint8_t command);

/* ================= OLED FONT ================= */

static const uint8_t *oled_glyph(char character)
{
    static const uint8_t blank[] = {0, 0, 0, 0, 0};

    static const uint8_t glyphs[][6] = {
        {' ', 0x00, 0x00, 0x00, 0x00, 0x00},
        {'%', 0x62, 0x64, 0x08, 0x13, 0x23},
        {'.', 0x00, 0x60, 0x60, 0x00, 0x00},
        {':', 0x00, 0x36, 0x36, 0x00, 0x00},
        {'-', 0x08, 0x08, 0x08, 0x08, 0x08},

        {'0', 0x3E, 0x51, 0x49, 0x45, 0x3E},
        {'1', 0x00, 0x42, 0x7F, 0x40, 0x00},
        {'2', 0x42, 0x61, 0x51, 0x49, 0x46},
        {'3', 0x21, 0x41, 0x45, 0x4B, 0x31},
        {'4', 0x18, 0x14, 0x12, 0x7F, 0x10},
        {'5', 0x27, 0x45, 0x45, 0x45, 0x39},
        {'6', 0x3C, 0x4A, 0x49, 0x49, 0x30},
        {'7', 0x01, 0x71, 0x09, 0x05, 0x03},
        {'8', 0x36, 0x49, 0x49, 0x49, 0x36},
        {'9', 0x06, 0x49, 0x49, 0x29, 0x1E},

        {'C', 0x3E, 0x41, 0x41, 0x41, 0x22},
        {'D', 0x7F, 0x41, 0x41, 0x22, 0x1C},
        {'E', 0x7F, 0x49, 0x49, 0x49, 0x41},
        {'F', 0x7F, 0x09, 0x09, 0x09, 0x01},
        {'H', 0x7F, 0x08, 0x08, 0x08, 0x7F},
        {'K', 0x7F, 0x08, 0x14, 0x22, 0x41},
        {'L', 0x7F, 0x40, 0x40, 0x40, 0x40},
        {'O', 0x3E, 0x41, 0x41, 0x41, 0x3E},
        {'R', 0x7F, 0x09, 0x19, 0x29, 0x46},
        {'S', 0x46, 0x49, 0x49, 0x49, 0x31},
        {'T', 0x01, 0x01, 0x7F, 0x01, 0x01},
        {'W', 0x7F, 0x20, 0x18, 0x20, 0x7F},

        {'e', 0x38, 0x54, 0x54, 0x54, 0x18},
        {'g', 0x0C, 0x52, 0x52, 0x52, 0x3E},
        {'h', 0x7F, 0x08, 0x04, 0x04, 0x78},
        {'i', 0x00, 0x44, 0x7D, 0x40, 0x00},
        {'m', 0x7C, 0x04, 0x18, 0x04, 0x78},
        {'p', 0x7C, 0x14, 0x14, 0x14, 0x08},
        {'t', 0x04, 0x3F, 0x44, 0x40, 0x20},
    };

    for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++) {
        if (glyphs[i][0] == (uint8_t)character) {
            return &glyphs[i][1];
        }
    }

    return blank;
}

/* ================= OLED DISPLAY ================= */

static void oled_set_cursor(uint8_t column, uint8_t page)
{
    oled_command(0xB0 | page);
    oled_command(0x00 | (column & 0x0F));
    oled_command(0x10 | (column >> 4));
}

static void oled_write_text(uint8_t page, const char *text)
{
    uint8_t data[6];

    oled_set_cursor(0, page);
    data[0] = 0x40;

    while (*text != '\0') {
        const uint8_t *glyph = oled_glyph(*text++);
        memcpy(&data[1], glyph, 5);
        i2c_write_bytes(data, sizeof(data));
    }
}

static void oled_show(
    float temperature,
    bool temperature_valid,
    int light_percent,
    bool wifi_ok
)
{
    /* Padded with spaces so old characters get overwritten */
    char line[24];

    if (temperature_valid) {
        snprintf(line, sizeof(line), "Temp: %.1f C     ", temperature);
    } else {
        snprintf(line, sizeof(line), "Temp: DHT ERR    ");
    }

    oled_write_text(0, line);

    snprintf(line, sizeof(line), "Light: %d     ", light_percent);
    oled_write_text(2, line);

    snprintf(line, sizeof(line), "Wi-Fi: %s     ", wifi_ok ? "OK" : "LOST");
    oled_write_text(4, line);
}

static void oled_command(uint8_t command)
{
    uint8_t data[] = {0x00, command};
    i2c_write_bytes(data, sizeof(data));
}

static void oled_init(void)
{
    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA,
        .scl_io_num = OLED_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, config.mode, 0, 0, 0));

    const uint8_t commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00,
        0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
        0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB,
        0x40, 0xA4, 0xA6, 0xAF
    };

    for (size_t i = 0; i < sizeof(commands); i++) {
        oled_command(commands[i]);
    }

    for (uint8_t page = 0; page < 8; page++) {
        uint8_t data[129] = {0x40};

        oled_set_cursor(0, page);
        i2c_write_bytes(data, sizeof(data));
    }
}

/* ================= DHT11 SENSOR ================= */

static void dht_init(void)
{
    gpio_reset_pin(dht_pin);
    gpio_set_direction(dht_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(dht_pin, GPIO_PULLUP_ONLY);
}

/* Wait for a GPIO transition without adding software microsecond delays. */
static bool dht_wait_level(int level, uint32_t timeout_us)
{
    const uint32_t timeout_cycles =
        (uint32_t)(((uint64_t)DHT_CPU_HZ * timeout_us) / 1000000U);
    const uint32_t deadline = esp_cpu_get_cycle_count() + timeout_cycles;

    while (gpio_get_level(dht_pin) != level) {
        if ((int32_t)(esp_cpu_get_cycle_count() - deadline) >= 0) {
            return false;
        }
    }

    return true;
}

/*
 * Read one complete DHT11 frame.
 *
 * Important timing changes:
 * 1. Host holds the data line LOW for 20 ms.
 * 2. Host drives HIGH for 40 us, then releases the line as INPUT.
 * 3. The sensor response and each data pulse are measured by polling.
 * 4. A data bit is 1 when its HIGH pulse is longer than 40 us.
 */
static bool dht_read_once(float *temperature)
{
    uint8_t data[5] = {0};
    bool ok = true;
    int response_failure = 0;

    /* DHT11 start signal */
    gpio_set_direction(dht_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dht_pin, 0);
    esp_rom_delay_us(DHT_START_LOW_US);

    /* Keep interruptions out of the timing-critical response. */
    portENTER_CRITICAL(&dht_mux);

    /* DHT11 requires a HIGH trigger pulse before the bus is released. */
    gpio_set_direction(dht_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dht_pin, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(dht_pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(dht_pin, GPIO_PULLUP_ONLY);

    /* Sensor response: LOW -> HIGH -> LOW */
    if (!dht_wait_level(0, DHT_TIMEOUT_US)) {
        response_failure = 1;
        ok = false;
    } else if (!dht_wait_level(1, DHT_TIMEOUT_US)) {
        response_failure = 2;
        ok = false;
    } else if (!dht_wait_level(0, DHT_TIMEOUT_US)) {
        response_failure = 3;
        ok = false;
    }

    /* Read 40 data bits (5 bytes). */
    for (int bit = 0; ok && bit < 40; bit++) {
        /* Each bit starts with a LOW period, then a HIGH pulse. */
        if (!dht_wait_level(1, DHT_TIMEOUT_US)) {
            ok = false;
            break;
        }

        uint32_t high_start = esp_cpu_get_cycle_count();

        while (gpio_get_level(dht_pin) == 1) {
            uint32_t high_cycles =
                esp_cpu_get_cycle_count() - high_start;

            if (high_cycles >=
                (uint32_t)(((uint64_t)DHT_CPU_HZ * 100U) / 1000000U)) {
                ok = false;
                break;
            }
        }

        if (!ok) {
            break;
        }

        data[bit / 8] <<= 1;

        uint32_t high_width_us = (uint32_t)(
            ((uint64_t)(esp_cpu_get_cycle_count() - high_start) * 1000000U) /
            DHT_CPU_HZ
        );

        if (high_width_us > DHT_BIT_THRESHOLD_US) {
            data[bit / 8] |= 1;
        }
    }

    portEXIT_CRITICAL(&dht_mux);

    if (!ok) {
        if (response_failure == 1) {
            ESP_LOGW(TAG, "DHT response LOW missing on GPIO%d", dht_pin);
        } else if (response_failure == 2) {
            ESP_LOGW(TAG, "DHT response HIGH missing on GPIO%d", dht_pin);
        } else if (response_failure == 3) {
            ESP_LOGW(TAG, "DHT response LOW end missing on GPIO%d", dht_pin);
        }

        ESP_LOGW(TAG, "DHT response/data timeout on GPIO%d", dht_pin);
        return false;
    }

    /* Validate checksum. */
    uint8_t checksum =
        (uint8_t)(data[0] + data[1] + data[2] + data[3]);

    if (checksum != data[4]) {
        ESP_LOGW(
            TAG,
            "DHT checksum error: %u %u %u %u checksum=%u expected=%u",
            data[0], data[1], data[2], data[3], data[4], checksum
        );
        return false;
    }

    /* Reject an all-zero frame. */
    if (data[0] == 0 && data[1] == 0 &&
        data[2] == 0 && data[3] == 0 && data[4] == 0) {
        ESP_LOGW(TAG, "DHT returned an empty frame");
        return false;
    }

    ESP_LOGI(
        TAG,
        "DHT raw: %u.%u%% RH, %u.%u C",
        data[0], data[1], data[2], data[3]
    );

    /* DHT11 temperature: integer byte + decimal byte. */
    *temperature = (float)data[2] + ((float)data[3] / 10.0f);

    return true;
}

/*
 * One DHT11 read per sensor cycle.
 * The sensor task runs every 3 seconds, allowing slower DHT11 modules
 * enough time between requests.
 */
static bool dht_read(float *temperature)
{
    static bool logged_idle_level = false;

    if (!logged_idle_level) {
        ESP_LOGI(
            TAG,
            "DHT GPIO%d idle level: %d",
            dht_pin,
            gpio_get_level(dht_pin)
        );
        logged_idle_level = true;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        if (dht_read_once(temperature)) {
            if (attempt > 0) {
                ESP_LOGI(TAG, "DHT read succeeded after retry %d", attempt);
            }
            return true;
        }

        if (attempt < 2) {
            /*
             * DHT11 modules need at least one second between requests.
             * Retrying slowly is preferable to publishing stale data.
             */
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /*
     * GPIO15 was the original signal pin in the working Arduino reference.
     * Try it once as a recovery path, then return to the documented GPIO4.
     */
    gpio_num_t original_pin = dht_pin;
    dht_pin = original_pin == GPIO_NUM_4 ? GPIO_NUM_15 : GPIO_NUM_4;
    dht_init();

    if (dht_read_once(temperature)) {
        ESP_LOGW(TAG, "DHT responded on GPIO%d", dht_pin);
        return true;
    }

    dht_pin = original_pin;
    dht_init();
    return false;
}

/* ================= SENSOR TASK ================= */

static void sensor_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(3000));

    int64_t last_update = 0;
    int last_light = -1;
    float last_temperature = -1.0f;

    float last_valid_temperature = startup_temperature;
    bool has_valid_temperature = startup_temperature_valid;

    while (true) {
        if (esp_timer_get_time() - last_update >= DHT_SAMPLE_INTERVAL_US) {
            last_update = esp_timer_get_time();

            float temperature = 0.0f;
            bool temperature_valid = dht_read(&temperature);

            if (temperature_valid) {
                last_valid_temperature = temperature;
                has_valid_temperature = true;
            } else if (has_valid_temperature) {
                /*
                 * Keep the last valid temperature visible during a
                 * temporary failed read. The warning makes clear that
                 * the latest sensor read failed.
                 */
                temperature = last_valid_temperature;
                temperature_valid = true;

                ESP_LOGW(
                    TAG,
                    "DHT read failed; displaying last valid temperature: %.1f C",
                    temperature
                );
            }

            /* Photoresistor remains binary: 0% or 100%. */
            int light_percent =
                gpio_get_level(LDR_PIN) == 0 ? 100 : 0;

            if (light_percent != last_light) {
                esp_rmaker_param_update_and_report(
                    light_param,
                    esp_rmaker_int(light_percent)
                );

                last_light = light_percent;
            }

            /* Report temperature only after a valid reading exists. */
            if (temperature_valid && temperature != last_temperature) {
                esp_rmaker_param_update_and_report(
                    temperature_param,
                    esp_rmaker_float(temperature)
                );

                last_temperature = temperature;
            }

            /* Both sensor values in ONE Serial Monitor line. */
            if (temperature_valid) {
                ESP_LOGI(
                    TAG,
                    "Brightness: %d%% | Temperature: %.1f C",
                    light_percent,
                    temperature
                );
            } else {
                ESP_LOGI(
                    TAG,
                    "Brightness: %d%% | Temperature: DHT ERR",
                    light_percent
                );
            }

            /* Update OLED without removing existing display fields. */
            oled_show(
                temperature,
                temperature_valid,
                light_percent,
                true
            );
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ================= MAIN APPLICATION ================= */

void app_main(void)
{
    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    /* Initialize sensors and display */
    gpio_set_direction(LDR_PIN, GPIO_MODE_INPUT);

    dht_init();
    oled_init();

    /*
     * Check the DHT11 once before starting Wi-Fi/RainMaker.
     * This preserves the startup preflight behavior.
     */
    startup_temperature = 0.0f;
    startup_temperature_valid = false;

    vTaskDelay(pdMS_TO_TICKS(1500));

    if (dht_read(&startup_temperature)) {
        startup_temperature_valid = true;

        ESP_LOGI(
            TAG,
            "Native DHT preflight successful: %.1f C",
            startup_temperature
        );
    } else {
        ESP_LOGW(TAG, "Native DHT preflight failed");
    }

    app_network_init();

    /* Initialize ESP RainMaker */
    esp_rmaker_config_t rainmaker_config = {
        .enable_time_sync = false
    };

    esp_rmaker_node_t *node =
        esp_rmaker_node_init(
            &rainmaker_config,
            "ESP32 Sensor",
            "Sensor"
        );

    if (!node) {
        ESP_LOGE(TAG, "Could not initialize node");
        abort();
    }

    esp_rmaker_device_t *device =
        esp_rmaker_device_create(
            "Sensor",
            "esp.device.temperature-sensor",
            NULL
        );

    temperature_param = esp_rmaker_param_create(
        "Temperature",
        "esp.param.temperature",
        esp_rmaker_float(0.0f),
        PROP_FLAG_READ
    );

    light_param = esp_rmaker_param_create(
        "Light",
        "esp.param.light",
        esp_rmaker_int(0),
        PROP_FLAG_READ
    );

    ESP_ERROR_CHECK(esp_rmaker_param_add_bounds(
        temperature_param,
        esp_rmaker_float(0.0f),
        esp_rmaker_float(50.0f),
        esp_rmaker_float(1.0f)
    ));

    ESP_ERROR_CHECK(esp_rmaker_param_add_ui_type(
        temperature_param,
        ESP_RMAKER_UI_SLIDER
    ));

    ESP_ERROR_CHECK(esp_rmaker_param_add_bounds(
        light_param,
        esp_rmaker_int(0),
        esp_rmaker_int(100),
        esp_rmaker_int(1)
    ));

    ESP_ERROR_CHECK(esp_rmaker_param_add_ui_type(
        light_param,
        ESP_RMAKER_UI_SLIDER
    ));

    esp_rmaker_device_add_param(device, temperature_param);
    esp_rmaker_device_add_param(device, light_param);

    esp_rmaker_node_add_device(node, device);

    ESP_ERROR_CHECK(esp_rmaker_start());

    ESP_ERROR_CHECK(app_network_start(POP_TYPE_RANDOM));

    /* Start the sensor task on core 1. */
    BaseType_t task_result = xTaskCreatePinnedToCore(
        sensor_task,
        "sensor_task",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Could not create sensor task");
        abort();
    }
}
