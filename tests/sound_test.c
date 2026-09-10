/* Run the real sound generator with fake I2S and a bounded task iteration. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/services/sound_service.c"

static jmp_buf iteration;
static unsigned allocations, writes, disables;
static int measured_peak;
static bool mute_after_write;
static bool drain_preview;
static unsigned audible_frames;
static unsigned restart_at;
static int audible_peak;
int i2s_new_channel(const i2s_chan_config_t *config, i2s_chan_handle_t *out, void *rx) {
    (void)rx; assert(config->dma_frame_num == 160); ++allocations; *out = (void *)1; return 0;
}
int i2s_channel_init_std_mode(i2s_chan_handle_t h, const i2s_std_config_t *cfg) {
    (void)h; assert(cfg->gpio_cfg.dout == 47); return 0;
}
int i2s_channel_enable(i2s_chan_handle_t h) { (void)h; return 0; }
int i2s_channel_disable(i2s_chan_handle_t h) { (void)h; ++disables; return 0; }
int i2s_del_channel(i2s_chan_handle_t h) { (void)h; return 0; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; return pdPASS;
}
void vTaskDelay(unsigned ticks) { (void)ticks; longjmp(iteration, 1); }
int i2s_channel_write(i2s_chan_handle_t h, const void *data, size_t size, size_t *written, int timeout) {
    (void)h; (void)timeout;
    const int16_t *samples = data; measured_peak = 0; ++writes;
    for (unsigned i = 0; i < size / sizeof(*samples); i += 2) {
        assert(samples[i] == samples[i + 1]);
        int magnitude = abs(samples[i]);
        if (magnitude > measured_peak) measured_peak = magnitude;
    }
    *written = size;
    if (measured_peak) { ++audible_frames; audible_peak = measured_peak; }
    if (restart_at && writes == restart_at) chronvs_sound_preview();
    if (mute_after_write) { chronvs_sound_set_volume(0); return 0; }
    if (drain_preview) { assert(writes < 200); return 0; }
    longjmp(iteration, 1);
}
static void run_iteration(void) { if (!setjmp(iteration)) sound_task(NULL); }
int main(void) {
    assert(chronvs_sound_volume() == 1);
    chronvs_sound_set_volume(0); chronvs_sound_set_ringing(true);
    assert(allocations == 0); run_iteration(); assert(writes == 0);
    int previous = 0;
    for (unsigned level = 1; level <= 5; ++level) {
        chronvs_sound_set_volume(level); run_iteration();
        assert(allocations == 1 && measured_peak > previous && measured_peak <= 32767);
        if (level == 1) assert(measured_peak == 3276);
        previous = measured_peak;
    }
    assert(previous == 32760);
    chronvs_sound_set_volume(255); assert(chronvs_sound_volume() == 5);
    mute_after_write = true; run_iteration(); assert(disables == 1);
    chronvs_sound_set_ringing(false); chronvs_sound_set_volume(2);
    unsigned old_writes = writes; run_iteration(); assert(writes == old_writes);
    mute_after_write = false; drain_preview = true;
    previous = 0;
    for (unsigned level = 1; level <= 5; ++level) {
        chronvs_sound_set_volume(level);
        old_writes = writes; audible_frames = 0;
        chronvs_sound_preview(); chronvs_sound_preview();
        run_iteration();
        assert(writes == old_writes + 16 && audible_frames == 12);
        assert(audible_peak > previous && audible_peak <= 32767);
        previous = audible_peak;
        assert(!atomic_load(&requested));
        old_writes = writes; run_iteration(); assert(writes == old_writes);
    }
    chronvs_sound_preview(); chronvs_sound_set_volume(0);
    chronvs_sound_preview(); run_iteration(); assert(writes == old_writes);
    chronvs_sound_set_volume(3); run_iteration(); assert(writes == old_writes);
    restart_at = writes + 6; audible_frames = 0;
    chronvs_sound_preview(); run_iteration();
    assert(writes == old_writes + 22 && audible_frames == 18);
    restart_at = 0; old_writes = writes;
    mute_after_write = true;
    chronvs_sound_preview(); run_iteration(); assert(writes == old_writes + 1);
    assert(!atomic_load(&requested));
    puts("Sound passed: mute, lazy initialization, five gains, stereo bounds, finite preview, retrigger and live mute.");
}
