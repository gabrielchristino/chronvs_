/* Explicit BLE radio bench; no UI, Wi-Fi, pairing, bonding or user data. */
#ifdef CHRONVS_BLUETOOTH_DIAGNOSTIC
#include <stdatomic.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "hw_test";
static atomic_bool params_ready, scan_done, data_ready, adv_ready;
static atomic_int async_failure;
static unsigned packets, responses, unique;
static int strongest = -127, weakest;
static uint8_t seen[64][6];

static void gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (p->scan_param_cmpl.status != ESP_BT_STATUS_SUCCESS) atomic_store(&async_failure, 1);
        atomic_store(&params_ready, true);
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (p->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) atomic_store(&async_failure, 2);
        ESP_LOGI(TAG, "BLE scan_start_status=%u", p->scan_start_cmpl.status);
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (p->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) atomic_store(&scan_done, true);
        if (p->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            ++packets;
            if (p->scan_rst.scan_rsp_len || p->scan_rst.ble_evt_type == ESP_BLE_EVT_SCAN_RSP) ++responses;
            if (p->scan_rst.rssi > strongest) strongest = p->scan_rst.rssi;
            if (p->scan_rst.rssi < weakest) weakest = p->scan_rst.rssi;
            unsigned i;
            for (i = 0; i < unique; ++i) if (!memcmp(seen[i], p->scan_rst.bda, 6)) break;
            if (i == unique && unique < 64) memcpy(seen[unique++], p->scan_rst.bda, 6);
        }
        break;
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        if (p->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) atomic_store(&async_failure, 3);
        atomic_store(&data_ready, true);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (p->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) atomic_store(&async_failure, 4);
        ESP_LOGI(TAG, "BLE advertising_start_status=%u", p->adv_start_cmpl.status);
        atomic_store(&adv_ready, true);
        break;
    default: break;
    }
}
static bool wait_event(atomic_bool *flag, unsigned seconds) {
    int64_t deadline = esp_timer_get_time() + (int64_t)seconds * 1000000;
    while (!atomic_load(flag) && !atomic_load(&async_failure) && esp_timer_get_time() < deadline)
        vTaskDelay(pdMS_TO_TICKS(50));
    return atomic_load(flag) && !atomic_load(&async_failure);
}
void chronvs_bluetooth_diagnostic_run(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));
    gpio_config_t power = {.pin_bit_mask = 1ULL << 7, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&power)); gpio_set_level(7, 1);
    ESP_LOGI(TAG, "BEGIN BLE_only=1 no_pairing=1 no_display=1 addresses_not_logged=1");
    bool controller_initialized = false, controller_enabled = false;
    bool host_initialized = false, host_enabled = false, scanning = false, advertising = false;
    bool scan_pass = false, adv_pass = false;
    esp_err_t result = nvs_flash_init(); /* Never erase NVS on failure. */
    if (result != ESP_OK) goto finish;
    esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    result = esp_bt_controller_init(&config);
    if (result != ESP_OK) goto finish;
    controller_initialized = true;
    result = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (result != ESP_OK) goto finish;
    controller_enabled = true;
    result = esp_bluedroid_init();
    if (result != ESP_OK) goto finish;
    host_initialized = true;
    result = esp_bluedroid_enable();
    if (result != ESP_OK) goto finish;
    host_enabled = true;
    result = esp_ble_gap_register_callback(gap_event);
    if (result != ESP_OK) goto finish;
    ESP_LOGI(TAG, "BLE controller_and_host=OK internal=%u DMA_largest=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    esp_ble_scan_params_t scan = {.scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC, .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50, .scan_window = 0x30, .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE};
    result = esp_ble_gap_set_scan_params(&scan);
    if (result != ESP_OK || !wait_event(&params_ready, 5)) goto finish;
    ESP_LOGI(TAG, "BLE active_scan_seconds=20");
    result = esp_ble_gap_start_scanning(20);
    if (result != ESP_OK) goto finish;
    scanning = true;
    if (!wait_event(&scan_done, 25)) goto finish;
    scanning = false;
    scan_pass = packets > 0;
    ESP_LOGI(TAG, "BLE scan_pass=%u packets=%u scan_responses=%u distinct_addresses_capped64=%u RSSI_min=%d RSSI_max=%d",
        scan_pass, packets, responses, unique, weakest, strongest);
    uint8_t payload[] = {2, 1, 6, 13, 9, 'C','h','r','o','n','v','s','-','T','e','s','t'};
    result = esp_ble_gap_config_adv_data_raw(payload, sizeof(payload));
    if (result != ESP_OK || !wait_event(&data_ready, 5)) goto finish;
    esp_ble_adv_params_t adv = {.adv_int_min = 0xa0, .adv_int_max = 0xf0,
        .adv_type = ADV_TYPE_NONCONN_IND, .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL, .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY};
    result = esp_ble_gap_start_advertising(&adv);
    if (result != ESP_OK) goto finish;
    advertising = true;
    if (!wait_event(&adv_ready, 5)) goto finish;
    adv_pass = true;
    ESP_LOGI(TAG, "BLE advertising_name=Chronvs-Test seconds=20 nonconnectable=1 remote_reception_not_verified=1");
    vTaskDelay(pdMS_TO_TICKS(20000));
finish:
    if (scanning) esp_ble_gap_stop_scanning();
    if (advertising) esp_ble_gap_stop_advertising();
    vTaskDelay(pdMS_TO_TICKS(100));
    if (host_enabled) esp_bluedroid_disable();
    if (host_initialized) esp_bluedroid_deinit();
    if (controller_enabled) esp_bt_controller_disable();
    if (controller_initialized) esp_bt_controller_deinit();
    ESP_LOGI(TAG, "DONE BLE_scan_pass=%u advertising_started=%u result=%s async_error=%d pairing_not_tested=1",
        scan_pass, adv_pass, esp_err_to_name(result), atomic_load(&async_failure));
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif
