#pragma once
#include "core/Notas_text.h"

#define Notas_NOTE_LIMIT 12
typedef struct {
    uint32_t revision;
    char text[Notas_TEXT_BYTES];
} Notas_note_t;

bool chronvs_Notas_init(void);
const Notas_note_t *chronvs_Notas_get(unsigned slot);
int chronvs_Notas_free_slot(void);
/* Empty text removes the note. Failed writes preserve the in-memory note. */
bool chronvs_Notas_save(unsigned slot, const char *text);
