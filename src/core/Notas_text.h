#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define Notas_MAX_CHARS 512
#define Notas_TEXT_BYTES (Notas_MAX_CHARS * 2 + 1)
#define Notas_TAP_MS 800
#define Notas_LETTER_KEYS 13
#define Notas_SYMBOL_FIRST 15
#define Notas_KEY_GROUPS 28

typedef struct {
    char text[Notas_TEXT_BYTES];
    unsigned cursor;
    int pending_key;
    unsigned cycle;
    uint32_t last_tap;
    bool shift, pending_shift;
} Notas_editor_t;

/* Offsets/cursors count Unicode characters, never UTF-8 bytes. */
bool Notas_text_valid(const char *text, size_t capacity);
unsigned Notas_text_length(const char *text);
size_t Notas_text_offset(const char *text, unsigned cursor);
void Notas_editor_load(Notas_editor_t *editor, const char *text);
void Notas_editor_confirm(Notas_editor_t *editor);
void Notas_editor_move(Notas_editor_t *editor, unsigned cursor);
void Notas_editor_shift(Notas_editor_t *editor);
bool Notas_editor_key(Notas_editor_t *editor, unsigned key, uint32_t now);
bool Notas_editor_insert(Notas_editor_t *editor, const char *character);
bool Notas_editor_backspace(Notas_editor_t *editor);
bool Notas_editor_expire(Notas_editor_t *editor, uint32_t now);
