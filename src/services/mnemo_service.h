#pragma once
#include "core/mnemo_text.h"

#define MNEMO_NOTE_LIMIT 12
typedef struct {
    uint32_t revision;
    char text[MNEMO_TEXT_BYTES];
} mnemo_note_t;

bool chronvs_mnemo_init(void);
const mnemo_note_t *chronvs_mnemo_get(unsigned slot);
int chronvs_mnemo_free_slot(void);
/* Empty text removes the note. Failed writes preserve the in-memory note. */
bool chronvs_mnemo_save(unsigned slot, const char *text);
