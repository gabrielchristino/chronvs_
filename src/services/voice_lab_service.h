#pragma once

#include <stdbool.h>
#include <stddef.h>

#define CHRONVS_VOICE_LAB_WORDS 200

typedef enum {
    CHRONVS_VOICE_IDLE,
    CHRONVS_VOICE_STARTING,
    CHRONVS_VOICE_LISTENING,
    CHRONVS_VOICE_STOPPING,
    CHRONVS_VOICE_ERROR,
} chronvs_voice_state_t;

typedef struct {
    char text[257];
} chronvs_voice_result_t;

/* Loads I2S and MultiNet only for an explicit laboratory session. */
bool chronvs_voice_lab_start(void);
void chronvs_voice_lab_stop(void);
chronvs_voice_state_t chronvs_voice_lab_state(void);
const char *chronvs_voice_lab_error(void);
bool chronvs_voice_lab_take_result(chronvs_voice_result_t *result);
const char *chronvs_voice_lab_word(unsigned index);
