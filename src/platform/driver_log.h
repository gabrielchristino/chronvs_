#pragma once

#include "sdkconfig.h"
#include <stdio.h>

/* Unlike a runtime sink, this also removes argument evaluation. */
#if CONFIG_LOG_DEFAULT_LEVEL > 0
#define CHRONVS_DRIVER_PRINTF(...) printf(__VA_ARGS__)
#else
#define CHRONVS_DRIVER_PRINTF(...) do { if (0) printf(__VA_ARGS__); } while (0)
#endif
