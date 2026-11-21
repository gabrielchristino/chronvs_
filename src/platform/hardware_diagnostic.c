/* Isolated, headless bench. Never initializes LCD/LVGL or changes GNSS config. */
#ifdef CHRONVS_HARDWARE_DIAGNOSTIC
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/uart.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "services/battery_service.h"
#include "services/gps_data.h"
#include "services/rtc_service.h"
#include "services/sd_service.h"

static const char *TAG = "hw_test";
static uint8_t imu_address, saved_imu[7];
static bool imu_ready, gps_ready;
static chronvs_gps_data_t gps;
static unsigned gps_baud, gps_bytes, gps_length;
static char gps_line[256];

static esp_err_t read_reg(uint8_t addr, uint8_t reg, void *data, size_t count) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, addr, &reg, 1,
        data, count, pdMS_TO_TICKS(100));
}
static esp_err_t write_reg(uint8_t addr, uint8_t reg, uint8_t value) {
    const uint8_t bytes[] = {reg, value};
    return i2c_master_write_to_device(I2C_MASTER_NUM, addr, bytes, 2, pdMS_TO_TICKS(100));
}
static void memory_test(void) {
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0, flash_id = 0;
    esp_err_t result = esp_flash_get_size(NULL, &flash_size);
    esp_err_t id_result = esp_flash_read_id(NULL, &flash_id);
    ESP_LOGI(TAG, "CHIP cores=%u revision=%u FLASH result=%s id_result=%s bytes=%lu id=%lx",
        chip.cores, chip.revision, esp_err_to_name(result), esp_err_to_name(id_result),
        (unsigned long)flash_size, (unsigned long)flash_id);
    const size_t count = 64 * 1024 / sizeof(uint32_t);
    uint32_t *memory = heap_caps_malloc(count * sizeof(*memory), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool pass = memory != NULL;
    for (unsigned pattern = 0; memory && pattern < 3; ++pattern) {
        for (size_t i = 0; i < count; ++i) memory[i] = (uint32_t)i ^ (0x55555555U * pattern);
        for (size_t i = 0; i < count; ++i)
            if (memory[i] != ((uint32_t)i ^ (0x55555555U * pattern))) pass = false;
    }
    free(memory);
    ESP_LOGI(TAG, "PSRAM pass=%u tested_bytes=65536 total=%u free=%u internal=%u DMA_largest=%u",
        pass, (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}
static void i2c_test(void) {
    const uint8_t addresses[] = {0x20, 0x51, 0x53, 0x6a, 0x6b};
    for (unsigned i = 0; i < sizeof(addresses); ++i) {
        uint8_t value = 0;
        esp_err_t result = read_reg(addresses[i], 0, &value, 1);
        ESP_LOGI(TAG, "I2C addr=0x%02x result=%s reg0=0x%02x", addresses[i], esp_err_to_name(result), value);
        if ((addresses[i] == 0x6a || addresses[i] == 0x6b) && result == ESP_OK && value == 0x05)
            imu_address = addresses[i];
    }
    if (!imu_address) return;
    /* QMI8658: preserve volatile controls, use 4g/256dps at ~120 Hz. */
    if (read_reg(imu_address, 2, saved_imu, sizeof(saved_imu)) != ESP_OK) return;
    imu_ready = write_reg(imu_address, 8, 0) == ESP_OK &&
        write_reg(imu_address, 2, 0x40) == ESP_OK &&
        write_reg(imu_address, 3, 0x16) == ESP_OK &&
        write_reg(imu_address, 4, 0x46) == ESP_OK &&
        write_reg(imu_address, 7, 0) == ESP_OK &&
        write_reg(imu_address, 8, 0x03) == ESP_OK;
    ESP_LOGI(TAG, "IMU configured=%u address=0x%02x", imu_ready, imu_address);
}
static void sd_test(void) {
    if (!chronvs_sd_mount()) { ESP_LOGE(TAG, "SD mount=FAIL formatting=disabled"); return; }
    uint64_t total = 0, available = 0;
    esp_err_t info = esp_vfs_fat_info("/sdcard", &total, &available);
    char path[96];
    snprintf(path, sizeof(path), "/sdcard/chronvs/hw-%08lx.tmp", (unsigned long)esp_random());
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) { ESP_LOGE(TAG, "SD exclusive create=FAIL errno=%d", errno); return; }
    uint8_t bytes[512], actual[512];
    bool pass = true;
    for (unsigned block = 0; block < 32; ++block) {
        for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)(i ^ block ^ 0xa5);
        if (write(fd, bytes, sizeof(bytes)) != sizeof(bytes)) { pass = false; break; }
    }
    if (fsync(fd)) pass = false;
    if (close(fd)) pass = false;
    fd = open(path, O_RDONLY);
    if (fd < 0) pass = false;
    for (unsigned block = 0; fd >= 0 && block < 32; ++block) {
        for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)(i ^ block ^ 0xa5);
        if (read(fd, actual, sizeof(actual)) != sizeof(actual) || memcmp(bytes, actual, sizeof(bytes))) pass = false;
    }
    if (fd >= 0) close(fd);
    /* Only the exclusively-created test file is removed, never user data. */
    int removed = unlink(path);
    ESP_LOGI(TAG, "SD pass=%u verified_bytes=16384 cleanup=%u info=%s total_MB=%llu free_MB=%llu",
        pass, removed == 0, esp_err_to_name(info), total / 1048576, available / 1048576);
}
static void wifi_test(void) {
    esp_err_t result = nvs_flash_init();
    /* Never erase existing NVS on an initialization error. */
    if (result != ESP_OK) { ESP_LOGE(TAG, "NVS result=%s no_erase=1", esp_err_to_name(result)); return; }
    result = esp_netif_init();
    if (result == ESP_OK) result = esp_event_loop_create_default();
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (result == ESP_OK) result = esp_wifi_init(&config);
    if (result != ESP_OK) { ESP_LOGE(TAG, "WIFI init=%s", esp_err_to_name(result)); return; }
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result == ESP_OK) result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result == ESP_OK) result = esp_wifi_start();
    wifi_scan_config_t scan = {.scan_type = WIFI_SCAN_TYPE_PASSIVE, .show_hidden = true};
    if (result == ESP_OK) result = esp_wifi_scan_start(&scan, true);
    uint16_t count = 0;
    if (result == ESP_OK) result = esp_wifi_scan_get_ap_num(&count);
    ESP_LOGI(TAG, "WIFI passive_scan=%s networks=%u names_not_logged=1", esp_err_to_name(result), count);
    esp_wifi_clear_ap_list(); esp_wifi_stop(); esp_wifi_deinit();
    ESP_LOGI(TAG, "BLUETOOTH not_tested=1 current_firmware_has_no_BLE_test");
}
static void gps_poll(void) {
    if (!gps_ready) return;
    uint8_t bytes[512];
    int count = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes), 0);
    for (int i = 0; i < count; ++i) {
        ++gps_bytes;
        char c = (char)bytes[i];
        if (c == '$') gps_length = 0;
        if (c == '\n') {
            gps_line[gps_length] = 0;
            if (gps_length) chronvs_gps_parse(&gps, gps_line, esp_timer_get_time() / 1000);
            gps_length = 0;
        } else if (c != '\r' && gps_length < sizeof(gps_line) - 1) gps_line[gps_length++] = c;
    }
}
static void gps_start(void) {
    const unsigned rates[] = {9600, 38400, 115200, 4800, 19200, 57600};
    uart_config_t config = {.baud_rate = rates[0], .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT};
    esp_err_t result = uart_param_config(UART_NUM_1, &config);
    /* RX only: leave receiver TX/configuration and GPIO43 untouched. */
    if (result == ESP_OK) result = uart_set_pin(UART_NUM_1, UART_PIN_NO_CHANGE, 44, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (result == ESP_OK) result = uart_driver_install(UART_NUM_1, 8192, 0, 0, NULL, 0);
    gps_ready = result == ESP_OK;
    if (!gps_ready) { ESP_LOGE(TAG, "GPS init=%s", esp_err_to_name(result)); return; }
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        gps_baud = rates[i];
        uart_set_baudrate(UART_NUM_1, gps_baud); uart_flush_input(UART_NUM_1); gps_length = 0;
        int64_t until = esp_timer_get_time() + 2200000;
        while (esp_timer_get_time() < until) { gps_poll(); vTaskDelay(pdMS_TO_TICKS(20)); }
        if (gps.sentences >= 2) break;
    }
    ESP_LOGI(TAG, "GPS detected=%u baud=%u bytes=%u valid_NMEA=%lu", gps.sentences >= 2,
        gps_baud, gps_bytes, (unsigned long)gps.sentences);
}
static void audio_test(void) {
    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 4; chan.dma_frame_num = 320; chan.auto_clear = true;
    esp_err_t speaker = i2s_new_channel(&chan, &tx, NULL);
    i2s_std_config_t out = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = 48, .ws = 38, .dout = 47, .din = I2S_GPIO_UNUSED},
    };
    if (speaker == ESP_OK) speaker = i2s_channel_init_std_mode(tx, &out);
    if (speaker == ESP_OK) speaker = i2s_channel_enable(tx);
    chan.id = I2S_NUM_1;
    esp_err_t microphone = i2s_new_channel(&chan, NULL, &rx);
    i2s_std_config_t in = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = 15, .ws = 2, .dout = I2S_GPIO_UNUSED, .din = 39},
    };
    in.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    if (microphone == ESP_OK) microphone = i2s_channel_init_std_mode(rx, &in);
    if (microphone == ESP_OK) microphone = i2s_channel_enable(rx);
    ESP_LOGI(TAG, "AUDIO speaker_init=%s microphone_init=%s three_1kHz_tones_next=1",
        esp_err_to_name(speaker), esp_err_to_name(microphone));
    int16_t pcm[640]; int32_t raw[320];
    unsigned tx_ok = 0, rx_ok = 0, varied = 0;
    for (unsigned phase = 0; phase < 6; ++phase) {
        double sum = 0, squares = 0; unsigned samples = 0, peak = 0;
        for (unsigned frame = 0; frame < 50; ++frame) {
            for (unsigned i = 0; i < 320; ++i)
                pcm[2*i] = pcm[2*i+1] = phase % 2 ? (int16_t)(3276 * sinf(i * 6.2831853f / 16)) : 0;
            size_t count = 0;
            if (speaker == ESP_OK && i2s_channel_write(tx, pcm, sizeof(pcm), &count, 100) == ESP_OK && count == sizeof(pcm)) ++tx_ok;
            count = 0;
            if (microphone == ESP_OK && i2s_channel_read(rx, raw, sizeof(raw), &count, 100) == ESP_OK) {
                ++rx_ok;
                for (unsigned i = 0; i < count / sizeof(raw[0]); ++i) {
                    int value = raw[i] >> 14;
                    unsigned magnitude = abs(value);
                    if (magnitude > peak) peak = magnitude;
                    sum += value; squares += (double)value * value; ++samples;
                }
            }
            if (microphone != ESP_OK && speaker != ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
            gps_poll();
        }
        double variance = samples ? squares / samples - (sum / samples) * (sum / samples) : 0;
        if (variance > 1) ++varied;
        ESP_LOGI(TAG, "MIC phase=%u tone=%u samples=%u rms_ac=%.1f peak=%u", phase, phase % 2, samples, sqrt(fmax(variance, 0)), peak);
    }
    if (tx) { if (speaker == ESP_OK) i2s_channel_disable(tx); i2s_del_channel(tx); }
    if (rx) { if (microphone == ESP_OK) i2s_channel_disable(rx); i2s_del_channel(rx); }
    ESP_LOGI(TAG, "AUDIO tx_frames=%u/300 rx_frames=%u/300 varied_phases=%u audible_confirmation_required=1", tx_ok, rx_ok, varied);
}
static void sample_sensors(unsigned second) {
    chronvs_battery_status_t battery = chronvs_battery_read();
    ESP_LOGI(TAG, "BAT second=%u valid=%u volts=%.3f estimated_percent=%u USB_connected_not_capacity_test=1",
        second, battery.valid, battery.voltage, battery.percent);
    if (imu_ready) {
        uint8_t raw[12];
        esp_err_t result = read_reg(imu_address, 0x35, raw, sizeof(raw));
        float v[6] = {0};
        if (result == ESP_OK) for (unsigned i = 0; i < 6; ++i)
            v[i] = (int16_t)(raw[2*i] | raw[2*i+1] << 8) / (i < 3 ? 8192.f : 128.f);
        ESP_LOGI(TAG, "IMU second=%u result=%s accel_g=%.3f,%.3f,%.3f gyro_dps=%.2f,%.2f,%.2f norm_g=%.3f",
            second, esp_err_to_name(result), v[0],v[1],v[2],v[3],v[4],v[5], sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]));
    }
    chronvs_time_t rtc = chronvs_rtc_read();
    ESP_LOGI(TAG, "RTC second=%u valid=%u date=%02u-%02u-%02u time=%02u:%02u:%02u",
        second, rtc.valid, rtc.year,rtc.month,rtc.day,rtc.hour,rtc.minute,rtc.second);
    chronvs_gps_freshen(&gps, esp_timer_get_time() / 1000);
    ESP_LOGI(TAG, "GPS second=%u bytes=%u valid_NMEA=%lu checksum_errors=%lu fix=%u used=%d HDOP=%.1f",
        second, gps_bytes, (unsigned long)gps.sentences, (unsigned long)gps.errors,
        gps.fix, gps.satellites_valid ? (int)gps.satellites : -1, gps.hdop_valid ? gps.hdop : -1);
    for (unsigned i = 0; i < CHRONVS_GPS_GSV_SOURCES; ++i) if (gps.sky[i].valid)
        ESP_LOGI(TAG, "SKY talker=%s visible=%u CNO=%d", gps.sky[i].talker,
            gps.sky[i].visible, gps.sky[i].cno_valid ? (int)gps.sky[i].cno : -1);
    ESP_LOGI(TAG, "BUTTONS power=%d boot=%d touch_irq=%d", gpio_get_level(6),gpio_get_level(0),gpio_get_level(4));
}
void chronvs_hardware_diagnostic_run(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));
    ESP_LOGI(TAG, "BEGIN headless=1 LCD_not_initialized=1 GNSS_RX_only=1");
    gpio_config_t power = {.pin_bit_mask = 1ULL << 7, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&power)); gpio_set_level(7, 1);
    gpio_config_t inputs = {.pin_bit_mask = (1ULL << 6) | (1ULL << 0) | (1ULL << 4),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&inputs));
    I2C_Init(); EXIO_Init(); memory_test(); i2c_test(); chronvs_battery_init();
    sd_test(); wifi_test(); gps_start(); audio_test();
    ESP_LOGI(TAG, "OBSERVE seconds=90 move_board_and_speak_near_microphone=1");
    for (unsigned i = 0; i <= 90; ++i) {
        if (!(i % 5)) sample_sensors(i);
        int64_t until = esp_timer_get_time() + 1000000;
        while (esp_timer_get_time() < until) { gps_poll(); vTaskDelay(pdMS_TO_TICKS(20)); }
    }
    if (imu_ready) {
        write_reg(imu_address, 8, 0);
        for (unsigned i = 0; i < sizeof(saved_imu); ++i) write_reg(imu_address, 2+i, saved_imu[i]);
    }
    if (gps_ready) uart_driver_delete(UART_NUM_1);
    gps_ready = false;
    ESP_LOGI(TAG, "DONE display_and_touch_functional_test=SKIPPED bluetooth=SKIPPED battery_autonomy=SKIPPED");
    /* Serial-triggered audio repeat lets the observer be ready before tones. */
    char command[16];
    for (;;) {
        ESP_LOGI(TAG, "COMMAND_READY send_a_for_audio=1");
        if (fgets(command, sizeof(command), stdin)) {
            if (command[0] == 'a') audio_test();
        } else { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
}
#endif
