#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Initialize on the main task before starting network workers; no radio activity. */
bool chronvs_wifi_session_init(void);
bool chronvs_wifi_session_configured(void);
/* Blocking, worker-only, exclusive session. Success must be paired with release
 * on the same task. Failure already stops the radio and releases ownership.
 * Waiting for another owner is separate from the 20 s connection budget. */
esp_err_t chronvs_wifi_session_acquire(void);
void chronvs_wifi_session_release(void);
