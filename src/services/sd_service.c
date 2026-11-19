#include "services/sd_service.h"
#include <errno.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static bool mounted;
static bool directory(const char *path) {
    struct stat info;
    if (!stat(path, &info)) return S_ISDIR(info.st_mode);
    return errno == ENOENT && mkdir(path, 0775) == 0;
}

bool chronvs_sd_mount(void) {
    if (!mounted) {
        ESP_LOGI("sd", "Before mount: internal=%u, largest DMA=%u",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
        slot.width = 1;
        slot.clk = GPIO_NUM_14; slot.cmd = GPIO_NUM_17; slot.d0 = GPIO_NUM_16;
        slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
        slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        esp_vfs_fat_sdmmc_mount_config_t config = {
            .format_if_mount_failed = false, .max_files = 4, .allocation_unit_size = 16384,
        };
        sdmmc_card_t *card;
        if (esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &config, &card) != ESP_OK) return false;
        mounted = true;
        ESP_LOGI("sd", "Mounted: internal=%u, largest DMA=%u",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    }
    return directory("/sdcard/chronvs") && directory(CHRONVS_RUN_DIRECTORY);
}
