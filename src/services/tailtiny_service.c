/* Dedicated runtime for the watch while the display is disconnected.
 * Owns the existing exclusive Wi-Fi session; no NTP/Clima/app workers run. */
#ifdef CHRONVS_TAILTINY
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "tailtiny.h"
#include "services/wifi_session_service.h"
#include "chronvs_tailtiny_secrets.h"

static const char *TAG = "chronvs_tail";

static void memory_status(void) {
    ESP_LOGI(TAG, "internal_free=%u DMA_largest=%u PSRAM_free=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void state_changed(tailtiny_t *client, tailtiny_state_t state, void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "state=%d", (int)state);
    if (state == ML_STATE_CONNECTED) {
        char ip[16];
        tailtiny_ip_to_str(tailtiny_get_vpn_ip(client), ip);
        ESP_LOGI(TAG, "Connected! VPN IP: %s name=chronvs", ip);
        memory_status();
    }
}

/* Echo arbitrary bytes only inside WireGuard; never log received contents. */
static void udp_received(tailtiny_udp_socket_t *socket, uint32_t ip, uint16_t port,
                         const uint8_t *data, size_t len, void *arg) {
    (void)arg;
    if (len > 1394) return;
    char reply[1400];
    memcpy(reply, "ECHO: ", 6);
    memcpy(reply + 6, data, len);
    esp_err_t err = tailtiny_udp_send(socket, ip, port, reply, len + 6);
    ESP_LOGI(TAG, "UDP echo bytes=%u result=%s", (unsigned)len, esp_err_to_name(err));
}

void chronvs_tailtiny_run(void) {
    /* Keep battery power latched without touching the LCD/GNSS/SD/audio pins. */
    gpio_config_t power = {.pin_bit_mask = 1ULL << 7, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&power));
    gpio_set_level(7, 1);
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "BEGIN TailTiny v3.3.0 name=chronvs no_display=1");
    /* Never erase NVS: existing preferences and VPN identity must survive. */
    ESP_ERROR_CHECK(nvs_flash_init());
    if (!chronvs_wifi_session_init() || !chronvs_wifi_session_configured()) {
        ESP_LOGE(TAG, "Wi-Fi credentials unavailable");
        return;
    }
    for (;;) {
        esp_err_t err = chronvs_wifi_session_acquire();
        if (err == ESP_OK) break;
        ESP_LOGW(TAG, "Wi-Fi connection failed: %s; retry in 30 s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
    ESP_LOGI(TAG, "Wi-Fi connected");
    memory_status();
    /* Certificate validation needs UTC. Do not change the hardware RTC here. */
    esp_sntp_config_t ntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&ntp));
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000)) != ESP_OK)
        ESP_LOGW(TAG, "NTP timeout; certificate validation remains enabled");
    tailtiny_config_t config = {
        .auth_key = CHRONVS_TAILSCALE_AUTH_KEY,
        .device_name = "chronvs",
        .enable_derp = true, .enable_stun = true, .enable_disco = true,
        .max_peers = CONFIG_ML_MAX_PEERS,
    };
    tailtiny_t *client = tailtiny_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "TailTiny allocation failed");
        esp_netif_sntp_deinit();
        chronvs_wifi_session_release();
        return;
    }
    tailtiny_set_state_callback(client, state_changed, NULL);
    ESP_ERROR_CHECK(tailtiny_start(client));
    tailtiny_udp_socket_t *socket = NULL;
    unsigned seconds = 0;
    for (;;) {
        /* The session owner alone reconnects Wi-Fi; event handlers never do. */
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            esp_wifi_connect();
        }
        if (!socket && tailtiny_is_connected(client)) {
            socket = tailtiny_udp_create(client, 9000);
            if (socket) {
                ESP_ERROR_CHECK(tailtiny_udp_set_rx_callback(socket, udp_received, NULL));
                ESP_LOGI(TAG, "UDP echo listening on VPN port 9000");
            }
        }
        if (++seconds % 30 == 0) {
            char ip[16];
            tailtiny_ip_to_str(tailtiny_get_vpn_ip(client), ip);
            ESP_LOGI(TAG, "STATUS state=%d VPN=%s peers=%d echo_ready=%u",
                (int)tailtiny_get_state(client), ip,
                tailtiny_get_peer_count(client), socket != NULL);
            memory_status();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif
