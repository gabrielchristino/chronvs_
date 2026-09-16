#include "core/Notas_text.h"
#include <string.h>

static const char *const groups[Notas_KEY_GROUPS] = {
    "abáàãâ", "cdç", "eféê",
    "gh1", "ijí2", "kl3", "mn4", "opóôõ5",
    "qr6", "st7", "uvú8", "wx9", "yz0",
    ".,?!", "@#&",
    ".,", "?!", ":;", "\"'", "()", "[]", "{}", "<>",
    "+-=", "*/\\", "_@", "#$%", "&|~^`"
};

static unsigned width(unsigned char c) { return c < 128 ? 1 : 2; }

bool Notas_text_valid(const char *text, size_t capacity) {
    unsigned count = 0;
    for (size_t i = 0; i < capacity;) {
        unsigned char c = text[i++];
        if (!c) return count <= Notas_MAX_CHARS;
        if (++count > Notas_MAX_CHARS) return false;
        if (c < 128) { if (c != '\n' && (c < 32 || c > 126)) return false; }
        else {
            /* The keyboard repertoire is Latin-1, encoded canonically in UTF-8. */
            if (c != 0xC3 || i >= capacity || (unsigned char)text[i] < 0x80 ||
                (unsigned char)text[i] > 0xBF) return false;
            ++i;
        }
    }
    return false;
}
unsigned Notas_text_length(const char *text) {
    unsigned count = 0;
    while (*text) { text += width((unsigned char)*text); ++count; }
    return count;
}
size_t Notas_text_offset(const char *text, unsigned cursor) {
    size_t offset = 0;
    while (cursor-- && text[offset]) offset += width((unsigned char)text[offset]);
    return offset;
}
void Notas_editor_load(Notas_editor_t *e, const char *text) {
    memset(e, 0, sizeof(*e));
    if (Notas_text_valid(text, Notas_TEXT_BYTES)) strcpy(e->text, text);
    e->cursor = Notas_text_length(e->text);
    e->pending_key = -1;
}
void Notas_editor_confirm(Notas_editor_t *e) { e->pending_key = -1; }
void Notas_editor_move(Notas_editor_t *e, unsigned cursor) {
    Notas_editor_confirm(e);
    unsigned length = Notas_text_length(e->text);
    e->cursor = cursor > length ? length : cursor;
}
void Notas_editor_shift(Notas_editor_t *e) {
    bool enabled = e->shift || (e->pending_key >= 0 && e->pending_shift);
    Notas_editor_confirm(e); e->shift = !enabled;
}
bool Notas_editor_insert(Notas_editor_t *e, const char *character) {
    Notas_editor_confirm(e);
    size_t size = strlen(e->text), n = strlen(character);
    if (!Notas_text_valid(character, n + 1) ||
        Notas_text_length(character) != 1 || size + n >= sizeof(e->text) ||
        Notas_text_length(e->text) >= Notas_MAX_CHARS) return false;
    size_t pos = Notas_text_offset(e->text, e->cursor);
    memmove(e->text + pos + n, e->text + pos, size - pos + 1);
    memcpy(e->text + pos, character, n); ++e->cursor;
    return true;
}
bool Notas_editor_backspace(Notas_editor_t *e) {
    Notas_editor_confirm(e);
    if (!e->cursor) return false;
    size_t end = Notas_text_offset(e->text, e->cursor--);
    size_t start = Notas_text_offset(e->text, e->cursor);
    memmove(e->text + start, e->text + end, strlen(e->text + end) + 1);
    return true;
}
bool Notas_editor_expire(Notas_editor_t *e, uint32_t now) {
    if (e->pending_key < 0 || (uint32_t)(now - e->last_tap) < Notas_TAP_MS) return false;
    Notas_editor_confirm(e); return true;
}
bool Notas_editor_key(Notas_editor_t *e, unsigned key, uint32_t now) {
    if (key >= Notas_KEY_GROUPS) return false;
    Notas_editor_expire(e, now);
    bool repeat = e->pending_key == (int)key;
    unsigned cycle = repeat ? (e->cycle + 1) % Notas_text_length(groups[key]) : 0;
    bool shifted = repeat ? e->pending_shift : e->shift;
    const char *source = groups[key] + Notas_text_offset(groups[key], cycle);
    char character[3] = {0};
    memcpy(character, source, width((unsigned char)*source));
    bool letter = key < Notas_LETTER_KEYS;
    if (letter && shifted) {
        if (character[0] >= 'a' && character[0] <= 'z') character[0] -= 32;
        else if ((unsigned char)character[0] == 0xC3 && (unsigned char)character[1] >= 0xA0)
            character[1] = (char)((unsigned char)character[1] - 32);
    }
    if (repeat) Notas_editor_backspace(e);
    if (!Notas_editor_insert(e, character)) return false;
    e->pending_key = (int)key; e->cycle = cycle; e->pending_shift = shifted;
    e->last_tap = now;
    if (letter) e->shift = false;
    return true;
}
