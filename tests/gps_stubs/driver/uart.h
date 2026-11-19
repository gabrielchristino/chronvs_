#pragma once
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 0x107
typedef int esp_err_t;
#define UART_NUM_1 1
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
int uart_param_config(int port, const uart_config_t *config);
int uart_set_pin(int port, int tx, int rx, int rts, int cts);
int uart_driver_install(int port, int rx, int tx, int queue, void *handle, int flags);
int uart_driver_delete(int port);
int uart_read_bytes(int port, void *buffer, uint32_t size, uint32_t timeout);
int uart_set_baudrate(int port, uint32_t baud);
int uart_flush_input(int port);
int uart_write_bytes(int port, const void *data, size_t size);
int uart_wait_tx_done(int port, uint32_t ticks);
