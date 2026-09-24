#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
int i2c_master_write_to_device(int, uint8_t, const uint8_t *, size_t, unsigned);
