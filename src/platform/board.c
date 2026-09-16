#include "platform/board.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Display_SPD2010.h"
#include "I2C_Driver.h"
#include "LVGL_Driver.h"
#include "TCA9554PWR.h"

static const char *TAG = "board";

#define PWR_KEY_Input_PIN   GPIO_NUM_6
#define PWR_Control_PIN     GPIO_NUM_7
#define TOUCH_INT_PIN       GPIO_NUM_4

static void scan_onboard_i2c(void) {
    static const uint8_t addresses[] = {0x20, 0x51, 0x53, 0x6A, 0x6B};
    static const char *names[] = {
        "TCA9554", "PCF85063 RTC", "SPD2010 touch",
        "QMI8658 IMU", "QMI8658 IMU"
    };
    const uint8_t probe = 0;

    for (size_t index = 0; index < sizeof(addresses); ++index) {
        const esp_err_t result = i2c_master_write_to_device(
            I2C_MASTER_NUM, addresses[index], &probe, 0, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "I2C 0x%02X %-14s %s", addresses[index], names[index],
                 result == ESP_OK ? "OK" : "no response");
    }
}

static void power_button_task(void *arg) {
    int press_counter = 0;
    bool button_released_after_boot = false;

    while (1) {
        int btn_state = gpio_get_level(PWR_KEY_Input_PIN); 

        if (btn_state == 1) {
            button_released_after_boot = true;
            press_counter = 0;
        } 
        else if (btn_state == 0 && button_released_after_boot) {
            press_counter += 100;
            
            if (press_counter >= 2000) {
                ESP_LOGI(TAG, "Desligando dispositivo...");
                
                // Corta a energia da bateria
                gpio_set_level(PWR_Control_PIN, 0); 
                
                // Pequeno delay para permitir que o circuito de hardware caia
                vTaskDelay(pdMS_TO_TICKS(100));
                
                // Fallback: Se estiver no cabo USB, a placa não vai perder energia. 
                // Colocamos o ESP32 em Deep Sleep para não travar num while(1).
                esp_deep_sleep_start(); 
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

void chronvs_board_init(void) {
    I2C_Init();
    EXIO_Init();

    // 1. Configuração do pino de controle (Mantém a placa ligada)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PWR_Control_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Ativa a energia incondicionalmente no boot para a placa não desligar sozinha
    gpio_set_level(PWR_Control_PIN, 1);

    // 2. Configuração do botão de energia (Entrada)
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << PWR_KEY_Input_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_conf);

    // 3. Inicializações normais da placa
    scan_onboard_i2c();
    LCD_Init();
    LVGL_Init();

    // The SPD2010 interrupt is wired internally to GPIO4 and is active low.
    gpio_config_t touch_int_conf = {
        .pin_bit_mask = (1ULL << TOUCH_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&touch_int_conf));
    ESP_ERROR_CHECK(gpio_wakeup_enable(TOUCH_INT_PIN, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(gpio_wakeup_enable(PWR_KEY_Input_PIN, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    ESP_LOGI(TAG, "Wake inputs ready: touch GPIO%d=%d, power GPIO%d=%d",
             TOUCH_INT_PIN, gpio_get_level(TOUCH_INT_PIN),
             PWR_KEY_Input_PIN, gpio_get_level(PWR_KEY_Input_PIN));

    // 4. Inicia a task do botão em background
    xTaskCreate(power_button_task, "power_button_task", 2048, NULL, 5, NULL);
}

bool chronvs_board_light_sleep(uint32_t timeout_ms) {
    if (!timeout_ms || gpio_get_level(TOUCH_INT_PIN) == 0 ||
        gpio_get_level(PWR_KEY_Input_PIN) == 0) return false;

    // Battery power depends on GPIO7 staying high while the GPIO domain sleeps.
    ESP_ERROR_CHECK(gpio_set_level(PWR_Control_PIN, 1));
    ESP_ERROR_CHECK(gpio_hold_en(PWR_Control_PIN));
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)timeout_ms * 1000));
    LVGL_Tick_Suspend();
    const int64_t sleep_started_us = esp_timer_get_time();
    const esp_err_t result = esp_light_sleep_start();
    const uint32_t slept_ms = (uint32_t)((esp_timer_get_time() - sleep_started_us) / 1000);
    LVGL_Tick_Resume();
    ESP_ERROR_CHECK(gpio_set_level(PWR_Control_PIN, 1));
    ESP_ERROR_CHECK(gpio_hold_dis(PWR_Control_PIN));
    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER));
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Light sleep failed: %s", esp_err_to_name(result));
        return false;
    }

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        const int touch_level = gpio_get_level(TOUCH_INT_PIN);
        const int power_level = gpio_get_level(PWR_KEY_Input_PIN);
        if (touch_level == 0)
            ESP_LOGI(TAG, "Light sleep wake after %" PRIu32 " ms: touch GPIO%d",
                     slept_ms, TOUCH_INT_PIN);
        else if (power_level == 0)
            ESP_LOGI(TAG, "Light sleep wake after %" PRIu32 " ms: power GPIO%d",
                     slept_ms, PWR_KEY_Input_PIN);
        else
            ESP_LOGI(TAG, "Light sleep wake after %" PRIu32
                     " ms: GPIO released before sampling",
                     slept_ms);
    }
    return true;
}
