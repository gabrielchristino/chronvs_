#pragma once
void scan_test_log(const char *format, ...);
#define ESP_LOGI(tag, ...) scan_test_log(__VA_ARGS__)
#define ESP_LOGE(tag, ...) scan_test_log(__VA_ARGS__)
