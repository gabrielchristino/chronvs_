#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CHRONVS_SOUND_MAX_VOLUME 5
/* Five audible levels plus mute. Level 1 preserves the original alert gain. */
uint8_t chronvs_sound_volume(void);
void chronvs_sound_set_volume(uint8_t level);
/* Main task only. One 120 ms beep at the current volume; mute is silent.
 * Repeated requests restart the preview; active alerts take priority. */
void chronvs_sound_preview(void);
/* Main task only. Allocates I2S lazily, when the first sound is requested. */
void chronvs_sound_set_ringing(bool ringing);
