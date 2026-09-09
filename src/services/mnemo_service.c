#include "services/mnemo_service.h"
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "esp_heap_caps.h"

/* Notes are not DMA data. Keep their cache out of the internal DMA heap,
 * and allocate it only when Mnemo is first opened, not before board boot. */
static mnemo_note_t *notes;
static nvs_handle_t storage;
static bool initialized;
static uint32_t revision;

bool chronvs_mnemo_init(void) {
    if (initialized) return true;
    if (!notes) notes = heap_caps_calloc(MNEMO_NOTE_LIMIT, sizeof(*notes),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!notes) return false;
    if (nvs_open("mnemo", NVS_READWRITE, &storage) != ESP_OK) return false;
    for (unsigned i = 0; i < MNEMO_NOTE_LIMIT; ++i) {
        char key[16]; snprintf(key, sizeof(key), "note%u_v1", i);
        size_t size = sizeof(notes[i]);
        if (nvs_get_blob(storage, key, &notes[i], &size) != ESP_OK ||
            size != sizeof(notes[i]) || !mnemo_text_valid(notes[i].text, sizeof(notes[i].text)))
            memset(&notes[i], 0, sizeof(notes[i]));
        if (notes[i].revision > revision) revision = notes[i].revision;
    }
    initialized = true;
    return true;
}
const mnemo_note_t *chronvs_mnemo_get(unsigned slot) {
    return initialized && slot < MNEMO_NOTE_LIMIT && notes[slot].text[0] ? &notes[slot] : NULL;
}
int chronvs_mnemo_free_slot(void) {
    if (!initialized) return -1;
    for (unsigned i = 0; i < MNEMO_NOTE_LIMIT; ++i) if (!notes[i].text[0]) return (int)i;
    return -1;
}
bool chronvs_mnemo_save(unsigned slot, const char *text) {
    if (slot >= MNEMO_NOTE_LIMIT || !mnemo_text_valid(text, MNEMO_TEXT_BYTES) ||
        !chronvs_mnemo_init()) return false;
    if (!strcmp(notes[slot].text, text)) return true;
    mnemo_note_t next = { .revision = revision + 1 };
    strcpy(next.text, text);
    char key[16]; snprintf(key, sizeof(key), "note%u_v1", slot);
    if (nvs_set_blob(storage, key, &next, sizeof(next)) != ESP_OK ||
        nvs_commit(storage) != ESP_OK) return false;
    notes[slot] = next; revision = next.revision;
    return true;
}
