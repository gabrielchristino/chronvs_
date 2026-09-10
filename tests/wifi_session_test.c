#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include "services/wifi_session_service.c"

const char *WIFI_EVENT="wifi", *IP_EVENT="ip";
static void (*handler)(void *,esp_event_base_t,int32_t,void *);
static volatile LONG radio, starts, stops, connects, completed;
static int mode; /* 0 success, 1 timeout, 2 disconnected, 3 start failure */
static int64_t mock_us;
static HANDLE owner_ready, owner_release;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return CreateSemaphore(NULL,1,1,NULL); }
int xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t timeout) { return WaitForSingleObject(mutex,timeout)==WAIT_OBJECT_0; }
int xSemaphoreGive(SemaphoreHandle_t mutex) { return ReleaseSemaphore(mutex,1,NULL); }
EventGroupHandle_t xEventGroupCreate(void) { return calloc(1,sizeof(EventBits_t)); }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group,EventBits_t bits) { return *(EventBits_t *)group|=bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t group,EventBits_t bits) {
    EventBits_t previous=*(EventBits_t *)group; *(EventBits_t *)group&=~bits; return previous;
}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group,EventBits_t bits,int clear,int all,TickType_t timeout) {
    (void)all; EventBits_t current=*(EventBits_t *)group;
    if (!(current&bits)) mock_us+=(int64_t)timeout*1000;
    if (clear) *(EventBits_t *)group&=~bits;
    return current;
}
int64_t esp_timer_get_time(void) { return mock_us; }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { return ESP_ERR_INVALID_STATE; }
void *esp_netif_create_default_wifi_sta(void) { return (void *)1; }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int32_t id,
    void (*fn)(void *,esp_event_base_t,int32_t,void *),void *arg,void *instance) {
    (void)base; (void)id; (void)arg; (void)instance; handler=fn; return ESP_OK;
}
esp_err_t esp_wifi_init(wifi_init_config_t *config) { (void)config; return ESP_OK; }
esp_err_t esp_wifi_set_storage(int storage) { assert(storage==WIFI_STORAGE_RAM); return ESP_OK; }
esp_err_t esp_wifi_set_mode(int value) { (void)value; return ESP_OK; }
esp_err_t esp_wifi_set_config(int iface,wifi_config_t *config) {
    (void)iface; assert(!strcmp((char *)config->sta.ssid,"host-test")); return ESP_OK;
}
esp_err_t esp_wifi_start(void) {
    if (mode==3) return ESP_FAIL;
    assert(InterlockedIncrement(&radio)==1); InterlockedIncrement(&starts); return ESP_OK;
}
esp_err_t esp_wifi_stop(void) {
    assert(InterlockedDecrement(&radio)==0); InterlockedIncrement(&stops);
    handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL); return ESP_OK;
}
esp_err_t esp_wifi_connect(void) {
    InterlockedIncrement(&connects);
    if (mode==0) handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);
    if (mode==2) handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    return ESP_OK;
}
esp_err_t esp_wifi_disconnect(void) { handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL); return ESP_OK; }

static DWORD WINAPI ntp_owner(void *arg) {
    (void)arg; assert(chronvs_wifi_session_acquire()==ESP_OK);
    SetEvent(owner_ready); WaitForSingleObject(owner_release,INFINITE);
    chronvs_wifi_session_release(); return 0;
}
static DWORD WINAPI weather_waiter(void *arg) {
    (void)arg; assert(chronvs_wifi_session_acquire()==ESP_OK);
    InterlockedIncrement(&completed); chronvs_wifi_session_release(); return 0;
}

int main(void) {
    assert(chronvs_wifi_session_init() && chronvs_wifi_session_configured());
    owner_ready=CreateEvent(NULL,TRUE,FALSE,NULL); owner_release=CreateEvent(NULL,TRUE,FALSE,NULL);
    HANDLE ntp=CreateThread(NULL,0,ntp_owner,NULL,0,NULL);
    assert(WaitForSingleObject(owner_ready,2000)==WAIT_OBJECT_0);
    HANDLE weather=CreateThread(NULL,0,weather_waiter,NULL,0,NULL);
    assert(WaitForSingleObject(weather,50)==WAIT_TIMEOUT && completed==0 && starts==1 && stops==0);
    SetEvent(owner_release);
    assert(WaitForSingleObject(ntp,2000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(weather,2000)==WAIT_OBJECT_0);
    assert(completed==1 && starts==2 && stops==2 && !radio);
    /* A release's disconnect event does not initiate reconnects. */
    assert(connects==2);
    mode=1; int64_t before=mock_us;
    assert(chronvs_wifi_session_acquire()==ESP_ERR_TIMEOUT);
    assert(mock_us-before==20000000 && !radio && starts==stops);
    mode=2; LONG count=connects;
    assert(chronvs_wifi_session_acquire()==ESP_FAIL && connects-count==3);
    assert(!radio && starts==stops);
    mode=3; assert(chronvs_wifi_session_acquire()==ESP_FAIL && !radio);
    mode=0; assert(chronvs_wifi_session_acquire()==ESP_OK); chronvs_wifi_session_release();
    assert(!radio && starts==stops);
    puts("Wi-Fi sessions: exclusive NTP/weather ownership, bounded retries, timeout and cleanup passed");
    return 0;
}
