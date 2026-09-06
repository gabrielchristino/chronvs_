#pragma once
#include <stdbool.h>
/* Main task only. Allocates I2S lazily, when the first alert requests sound. */
void chronvs_sound_set_ringing(bool ringing);
