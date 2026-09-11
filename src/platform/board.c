#include "platform/board.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Display_SPD2010.h"
#include "I2C_Driver.h"
#include "LVGL_Driver.h"
#include "TCA9554PWR.h"

static const char *TAG = "board";

#define PWR_KEY_Input_PIN   GPIO_NUM_6
#define PWR_Control_PIN     GPIO_NUM_7

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

    // 4. Inicia a task do botão em background
    xTaskCreate(power_button_task, "power_button_task", 2048, NULL, 5, NULL);
}