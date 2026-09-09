#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MNEMO_MAX_CHARS 512
#define MNEMO_TEXT_BYTES (MNEMO_MAX_CHARS * 2 + 1)
#define MNEMO_TAP_MS 800
#define MNEMO_LETTER_KEYS 13
#define MNEMO_SYMBOL_FIRST 15
#define MNEMO_KEY_GROUPS 28

typedef struct {
    char text[MNEMO_TEXT_BYTES];
    unsigned cursor;
    int pending_key;
    unsigned cycle;
    uint32_t last_tap;
    bool shift, pending_shift;
} mnemo_editor_t;

/* Offsets/cursors count Unicode characters, never UTF-8 bytes. */
bool mnemo_text_valid(const char *text, size_t capacity);
unsigned mnemo_text_length(const char *text);
size_t mnemo_text_offset(const char *text, unsigned cursor);
void mnemo_editor_load(mnemo_editor_t *editor, const char *text);
void mnemo_editor_confirm(mnemo_editor_t *editor);
void mnemo_editor_move(mnemo_editor_t *editor, unsigned cursor);
void mnemo_editor_shift(mnemo_editor_t *editor);
bool mnemo_editor_key(mnemo_editor_t *editor, unsigned key, uint32_t now);
bool mnemo_editor_insert(mnemo_editor_t *editor, const char *character);
bool mnemo_editor_backspace(mnemo_editor_t *editor);
bool mnemo_editor_expire(mnemo_editor_t *editor, uint32_t now);
