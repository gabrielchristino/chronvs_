#include "core/mnemo_text.h"
#include <string.h>

static const char *const groups[MNEMO_KEY_GROUPS] = {
    "abáàãâ", "cdç", "eféê",
    "gh1", "ijí2", "kl3", "mn4", "opóôõ5",
    "qr6", "st7", "uvú8", "wx9", "yz0",
    ".,?!", "@#&",
    ".,", "?!", ":;", "\"'", "()", "[]", "{}", "<>",
    "+-=", "*/\\", "_@", "#$%", "&|~^`"
};

static unsigned width(unsigned char c) { return c < 128 ? 1 : 2; }

bool mnemo_text_valid(const char *text, size_t capacity) {
    unsigned count = 0;
    for (size_t i = 0; i < capacity;) {
        unsigned char c = text[i++];
        if (!c) return count <= MNEMO_MAX_CHARS;
        if (++count > MNEMO_MAX_CHARS) return false;
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
unsigned mnemo_text_length(const char *text) {
    unsigned count = 0;
    while (*text) { text += width((unsigned char)*text); ++count; }
    return count;
}
size_t mnemo_text_offset(const char *text, unsigned cursor) {
    size_t offset = 0;
    while (cursor-- && text[offset]) offset += width((unsigned char)text[offset]);
    return offset;
}
void mnemo_editor_load(mnemo_editor_t *e, const char *text) {
    memset(e, 0, sizeof(*e));
    if (mnemo_text_valid(text, MNEMO_TEXT_BYTES)) strcpy(e->text, text);
    e->cursor = mnemo_text_length(e->text);
    e->pending_key = -1;
}
void mnemo_editor_confirm(mnemo_editor_t *e) { e->pending_key = -1; }
void mnemo_editor_move(mnemo_editor_t *e, unsigned cursor) {
    mnemo_editor_confirm(e);
    unsigned length = mnemo_text_length(e->text);
    e->cursor = cursor > length ? length : cursor;
}
void mnemo_editor_shift(mnemo_editor_t *e) {
    bool enabled = e->shift || (e->pending_key >= 0 && e->pending_shift);
    mnemo_editor_confirm(e); e->shift = !enabled;
}
bool mnemo_editor_insert(mnemo_editor_t *e, const char *character) {
    mnemo_editor_confirm(e);
    size_t size = strlen(e->text), n = strlen(character);
    if (!mnemo_text_valid(character, n + 1) ||
        mnemo_text_length(character) != 1 || size + n >= sizeof(e->text) ||
        mnemo_text_length(e->text) >= MNEMO_MAX_CHARS) return false;
    size_t pos = mnemo_text_offset(e->text, e->cursor);
    memmove(e->text + pos + n, e->text + pos, size - pos + 1);
    memcpy(e->text + pos, character, n); ++e->cursor;
    return true;
}
bool mnemo_editor_backspace(mnemo_editor_t *e) {
    mnemo_editor_confirm(e);
    if (!e->cursor) return false;
    size_t end = mnemo_text_offset(e->text, e->cursor--);
    size_t start = mnemo_text_offset(e->text, e->cursor);
    memmove(e->text + start, e->text + end, strlen(e->text + end) + 1);
    return true;
}
bool mnemo_editor_expire(mnemo_editor_t *e, uint32_t now) {
    if (e->pending_key < 0 || (uint32_t)(now - e->last_tap) < MNEMO_TAP_MS) return false;
    mnemo_editor_confirm(e); return true;
}
bool mnemo_editor_key(mnemo_editor_t *e, unsigned key, uint32_t now) {
    if (key >= MNEMO_KEY_GROUPS) return false;
    mnemo_editor_expire(e, now);
    bool repeat = e->pending_key == (int)key;
    unsigned cycle = repeat ? (e->cycle + 1) % mnemo_text_length(groups[key]) : 0;
    bool shifted = repeat ? e->pending_shift : e->shift;
    const char *source = groups[key] + mnemo_text_offset(groups[key], cycle);
    char character[3] = {0};
    memcpy(character, source, width((unsigned char)*source));
    bool letter = key < MNEMO_LETTER_KEYS;
    if (letter && shifted) {
        if (character[0] >= 'a' && character[0] <= 'z') character[0] -= 32;
        else if ((unsigned char)character[0] == 0xC3 && (unsigned char)character[1] >= 0xA0)
            character[1] = (char)((unsigned char)character[1] - 32);
    }
    if (repeat) mnemo_editor_backspace(e);
    if (!mnemo_editor_insert(e, character)) return false;
    e->pending_key = (int)key; e->cycle = cycle; e->pending_shift = shifted;
    e->last_tap = now;
    if (letter) e->shift = false;
    return true;
}
