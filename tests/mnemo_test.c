#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "core/mnemo_text.h"
#include "services/mnemo_service.c"

static mnemo_note_t flash_notes[MNEMO_NOTE_LIMIT];
static bool fail_write, fail_commit;
static unsigned writes;
static bool fail_alloc;
static unsigned allocations;
void *heap_caps_calloc(size_t count, size_t size, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++allocations;
    return fail_alloc ? NULL : calloc(count, size);
}
int nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    (void)mode; assert(!strcmp(name, "mnemo")); *handle = 1; return ESP_OK;
}
int nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *size) {
    (void)handle; unsigned i; assert(sscanf(key, "note%u_v1", &i) == 1);
    assert(i < MNEMO_NOTE_LIMIT && *size >= sizeof(mnemo_note_t));
    memcpy(data, &flash_notes[i], sizeof(mnemo_note_t)); *size = sizeof(mnemo_note_t); return ESP_OK;
}
int nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    (void)handle; unsigned i; assert(sscanf(key, "note%u_v1", &i) == 1);
    assert(i < MNEMO_NOTE_LIMIT && size == sizeof(mnemo_note_t)); ++writes;
    if (fail_write || fail_commit) return fail_write ? -1 : ESP_OK;
    memcpy(&flash_notes[i], data, size); return ESP_OK;
}
int nvs_commit(nvs_handle_t handle) { (void)handle; return fail_commit ? -1 : ESP_OK; }

static void test_text(void) {
    mnemo_editor_t e; mnemo_editor_load(&e, "");
    for (unsigned i = 0; i < 3; ++i) assert(mnemo_editor_key(&e, 0, i*100));
    assert(!strcmp(e.text, "á") && e.cursor == 1);
    assert(!mnemo_editor_expire(&e, 999)); assert(mnemo_editor_expire(&e, 1000));
    mnemo_editor_key(&e, 0, 1100); assert(!strcmp(e.text, "áa"));
    mnemo_editor_key(&e, 1, 1101); mnemo_editor_key(&e, 1, 1102); assert(!strcmp(e.text, "áad"));
    mnemo_editor_move(&e, 1); mnemo_editor_insert(&e, "ç");
    assert(!strcmp(e.text, "áçad")); mnemo_editor_backspace(&e);
    assert(!strcmp(e.text, "áad")); mnemo_editor_backspace(&e); assert(!strcmp(e.text, "ad"));
    assert(!mnemo_editor_backspace(&e));
    mnemo_editor_load(&e, ""); mnemo_editor_shift(&e);
    mnemo_editor_key(&e, 13, 0); assert(e.shift);
    mnemo_editor_key(&e, 0, 100); assert(!e.shift && !strcmp(e.text, ".A"));
    for (int i = 0; i < 2; ++i) mnemo_editor_key(&e, 0, 200+i*100);
    assert(!strcmp(e.text, ".Á"));
    mnemo_editor_key(&e, 1, 500); mnemo_editor_key(&e, 1, 600); assert(!strcmp(e.text, ".Ád"));
    static const char *expected[MNEMO_KEY_GROUPS] = {
        "abáàãâ", "cdç", "eféê", "gh1", "ijí2", "kl3", "mn4", "opóôõ5",
        "qr6", "st7", "uvú8", "wx9", "yz0", ".,?!", "@#&",
        ".,", "?!", ":;", "\"'", "()", "[]", "{}", "<>", "+-=", "*/\\", "_@", "#$%", "&|~^`"};
    for (unsigned key = 0; key < MNEMO_KEY_GROUPS; ++key) {
        mnemo_editor_load(&e, ""); unsigned count = mnemo_text_length(expected[key]);
        for (unsigned i = 0; i <= count; ++i) {
            mnemo_editor_key(&e, key, i*50);
            size_t start = mnemo_text_offset(expected[key], i%count);
            size_t end = mnemo_text_offset(expected[key], i%count+1);
            assert(strlen(e.text) == end-start && !memcmp(e.text, expected[key]+start, end-start));
        }
    }
    /* Shift is the only path to capitals, including every accented variant. */
    static const char *capital[MNEMO_LETTER_KEYS] = {
        "ABÁÀÃÂ", "CDÇ", "EFÉÊ", "GH1", "IJÍ2", "KL3", "MN4", "OPÓÔÕ5",
        "QR6", "ST7", "UVÚ8", "WX9", "YZ0"};
    for (unsigned key = 0; key < MNEMO_LETTER_KEYS; ++key) {
        mnemo_editor_load(&e, ""); mnemo_editor_shift(&e);
        unsigned count = mnemo_text_length(capital[key]);
        for (unsigned i = 0; i < count; ++i) {
            assert(mnemo_editor_key(&e, key, i*50));
            size_t start = mnemo_text_offset(capital[key], i);
            size_t end = mnemo_text_offset(capital[key], i+1);
            assert(strlen(e.text) == end-start && !memcmp(e.text, capital[key]+start, end-start));
        }
        mnemo_editor_confirm(&e); assert(!e.shift);
    }
    mnemo_editor_load(&e, ""); mnemo_editor_shift(&e); mnemo_editor_key(&e, 0, 0);
    mnemo_editor_shift(&e); assert(!e.shift && e.pending_key == -1);
    mnemo_editor_key(&e, 0, 100); assert(!strcmp(e.text, "Aa"));
    assert(!mnemo_editor_key(&e, MNEMO_KEY_GROUPS, 200));
    mnemo_editor_load(&e, "");
    for (unsigned i = 0; i < MNEMO_MAX_CHARS; ++i) assert(mnemo_editor_insert(&e, "ã"));
    assert(strlen(e.text) == 1024 && !mnemo_editor_insert(&e, "a"));
    mnemo_editor_backspace(&e); mnemo_editor_key(&e, 0, 100); mnemo_editor_key(&e, 0, 200);
    assert(mnemo_text_length(e.text) == 512 && e.text[1022] == 'b');
    mnemo_editor_load(&e, ""); mnemo_editor_key(&e, 1, UINT32_MAX-400);
    assert(!mnemo_editor_expire(&e, 100)); assert(mnemo_editor_expire(&e, 400));
    assert(!mnemo_text_valid("\xC3", 2)); assert(!mnemo_text_valid("\x80", 2));
    assert(!mnemo_text_valid("\xC0\xAF", 3));
}
static void test_storage(void) {
    assert(!notes && !initialized && allocations == 0);
    fail_alloc = true; assert(!chronvs_mnemo_init());
    assert(!chronvs_mnemo_get(0) && chronvs_mnemo_free_slot() == -1);
    fail_alloc = false;
    assert(chronvs_mnemo_init()); assert(chronvs_mnemo_free_slot() == 0);
    assert(allocations == 2); assert(chronvs_mnemo_init()); assert(allocations == 2);
    assert(chronvs_mnemo_save(0, "ação\nAmanhã"));
    unsigned before = writes; assert(chronvs_mnemo_save(0, "ação\nAmanhã")); assert(writes == before);
    fail_write = true; assert(!chronvs_mnemo_save(0, "perdida")); fail_write = false;
    fail_commit = true; assert(!chronvs_mnemo_save(0, "perdida")); fail_commit = false;
    assert(!strcmp(chronvs_mnemo_get(0)->text, "ação\nAmanhã"));
    initialized = false; memset(notes, 0, MNEMO_NOTE_LIMIT * sizeof(*notes)); assert(chronvs_mnemo_init());
    assert(!strcmp(chronvs_mnemo_get(0)->text, "ação\nAmanhã"));
    for (unsigned i = 1; i < MNEMO_NOTE_LIMIT; ++i) assert(chronvs_mnemo_save(i, "nota"));
    assert(chronvs_mnemo_free_slot() == -1); assert(!chronvs_mnemo_save(MNEMO_NOTE_LIMIT, "x"));
    fail_write = true; assert(!chronvs_mnemo_save(0, "")); assert(chronvs_mnemo_get(0)); fail_write = false;
    assert(chronvs_mnemo_save(0, "")); assert(!chronvs_mnemo_get(0)); assert(chronvs_mnemo_free_slot() == 0);
    memset(flash_notes[1].text, 'x', sizeof(flash_notes[1].text)); initialized = false;
    assert(chronvs_mnemo_init()); assert(!chronvs_mnemo_get(1));
}
int main(void) { test_text(); test_storage(); puts("Mnemo text and persistence passed."); return 0; }
