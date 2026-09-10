#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "services/weather_service.c"

static char fixture[4096];
static const char *response;
static size_t response_length, read_offset;
static int status_code = 200, wifi_error, open_error;
static bool chunked, stalled, truncated, fail_write, fail_commit, fail_alloc, fail_task;
static int64_t clock_us;
static unsigned acquires, releases, requests, cleans, task_count, commits;
static bool radio;
static void (*queued_task)(void *);
static unsigned char disk[128], staged[128];
static size_t disk_size, staged_size;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1,sizeof(int)); }
int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks) {
    (void)ticks; assert(!*(int *)mutex); *(int *)mutex=1; return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t mutex) { assert(*(int *)mutex); *(int *)mutex=0; return pdTRUE; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle) {
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    if (fail_task) return 0;
    assert(!queued_task); queued_task=fn; ++task_count; return pdPASS;
}
void vTaskDelete(void *task) { (void)task; }
void vTaskDelay(unsigned ms) { clock_us+=(int64_t)ms*1000; }
int64_t esp_timer_get_time(void) { return clock_us; }
void *heap_caps_malloc(size_t size, unsigned caps) { (void)caps; return fail_alloc ? NULL : malloc(size); }
int esp_crt_bundle_attach(void *conf) { (void)conf; return ESP_OK; }
bool chronvs_wifi_session_init(void) { return true; }
bool chronvs_wifi_session_configured(void) { return true; }
esp_err_t chronvs_wifi_session_acquire(void) {
    assert(!radio);
    ++acquires; radio=!wifi_error; return wifi_error;
}
void chronvs_wifi_session_release(void) { assert(radio); radio=false; ++releases; }

int nvs_open(const char *space, int mode, nvs_handle_t *handle) {
    (void)mode; assert(!strcmp(space,"weather")); *handle=1; return ESP_OK;
}
int nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    (void)handle; assert(!strcmp(key,"snapshot_v1"));
    if (!disk_size || *size<disk_size) return ESP_FAIL;
    memcpy(out,disk,disk_size); *size=disk_size; return ESP_OK;
}
int nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    (void)handle; assert(!radio && !strcmp(key,"snapshot_v1") && size<=sizeof(staged));
    if (fail_write) return ESP_FAIL;
    memcpy(staged,data,size); staged_size=size; return ESP_OK;
}
int nvs_commit(nvs_handle_t handle) {
    (void)handle; if (fail_commit) return ESP_FAIL;
    memcpy(disk,staged,staged_size); disk_size=staged_size; ++commits; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    assert(radio && config->crt_bundle_attach == esp_crt_bundle_attach && config->disable_auto_redirect);
    assert(!strncmp(config->url,"https://api.open-meteo.com/",27));
    assert(config->timeout_ms>0 && config->timeout_ms<=15000);
    read_offset=0; ++requests; return (void *)1;
}
int esp_http_client_open(esp_http_client_handle_t c, int size) { (void)c; (void)size; return open_error; }
int esp_http_client_set_timeout_ms(esp_http_client_handle_t c, int ms) { (void)c; assert(ms>0 && ms<=15000); return ESP_OK; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c) { (void)c; return chunked ? 0 : response_length; }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return status_code; }
int esp_http_client_read(esp_http_client_handle_t c, char *out, int room) {
    (void)c;
    if (stalled) { clock_us+=1000000; return -ESP_ERR_HTTP_EAGAIN; }
    if (truncated && read_offset>50) return 0;
    size_t count=response_length-read_offset;
    if (count>(size_t)room) count=room;
    if (count>37) count=37; /* Split UTF-8 and JSON across arbitrary TCP chunks. */
    memcpy(out,response+read_offset,count); read_offset+=count; return count;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { (void)c; return read_offset==response_length; }
int esp_http_client_close(esp_http_client_handle_t c) { (void)c; return ESP_OK; }
int esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; ++cleans; return ESP_OK; }

static void run_worker(void) { assert(queued_task); void (*fn)(void *)=queued_task; queued_task=NULL; fn(NULL); assert(!radio); }
static void reset_transport(void) {
    response=fixture; response_length=strlen(fixture); status_code=200; wifi_error=open_error=0;
    stalled=truncated=chunked=fail_write=fail_commit=fail_alloc=fail_task=false;
}
static void reboot(void) {
    assert(!queued_task && !radio);
    free(lock); lock=NULL; initialized=pending=false; state=CHRONVS_WEATHER_IDLE;
    cached=(chronvs_weather_snapshot_t){0}; last_error="";
    assert(chronvs_weather_init());
}
static void expect_failure(const char *message) {
    chronvs_weather_snapshot_t before,after;
    chronvs_weather_get_snapshot(&before);
    unsigned saved=commits, r=releases, a=acquires;
    assert(chronvs_weather_request_update());
    assert(chronvs_weather_state()==CHRONVS_WEATHER_FETCHING);
    unsigned tasks=task_count;
    assert(!chronvs_weather_request_update() && task_count==tasks);
    run_worker();
    assert(acquires==a+1 && releases==r+(wifi_error?0:1));
    assert(commits==saved && chronvs_weather_state()==CHRONVS_WEATHER_ERROR);
    char error[80]; assert(chronvs_weather_take_result(&after,error,sizeof(error)));
    assert(!strcmp(message,error) && after.valid==before.valid && after.updated_epoch==before.updated_epoch);
    assert(after.temperature_c==before.temperature_c);
    assert(!chronvs_weather_take_result(NULL,NULL,0));
    reset_transport();
}
static void parser_tests(void) {
    chronvs_weather_snapshot_t value={0};
    assert(chronvs_weather_parse(fixture,strlen(fixture),&value));
    assert(fabsf(value.temperature_c-23.4f)<0.01f && value.humidity_percent==65);
    const char *bad[] = {"{}","null","[]","{\"current\":null}","[[[[[[[[[0]]]]]]]]]"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) assert(!chronvs_weather_parse(bad[i],strlen(bad[i]),&value));
    const char *fields[]={"temperature_2m","relative_humidity_2m","apparent_temperature","weather_code","time"};
    for (unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
        cJSON *doc=cJSON_Parse(fixture), *current=cJSON_GetObjectItem(doc,"current");
        cJSON_DeleteItemFromObject(current,fields[i]);
        char *json=cJSON_PrintUnformatted(doc);
        assert(!chronvs_weather_parse(json,strlen(json),&value)); free(json); cJSON_Delete(doc);
    }
    const char *invalid[]={"null","\"23\"","1e999","-91","71"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        cJSON *doc=cJSON_Parse(fixture), *current=cJSON_GetObjectItem(doc,"current");
        cJSON_ReplaceItemInObject(current,"temperature_2m",cJSON_Parse(invalid[i]));
        char *json=cJSON_PrintUnformatted(doc);
        assert(!chronvs_weather_parse(json,strlen(json),&value)); free(json); cJSON_Delete(doc);
    }
    const char *dates[]={"2026-02-30T12:00","2026-09-10T24:00","2026-09-10T12:60","2026-09-10X12:00"};
    for (unsigned i=0;i<sizeof(dates)/sizeof(*dates);++i) {
        cJSON *doc=cJSON_Parse(fixture), *current=cJSON_GetObjectItem(doc,"current");
        cJSON_ReplaceItemInObject(current,"time",cJSON_CreateString(dates[i]));
        char *json=cJSON_PrintUnformatted(doc);
        assert(!chronvs_weather_parse(json,strlen(json),&value)); free(json); cJSON_Delete(doc);
    }
    const struct { const char *object, *key, *replacement; } invalid_fields[] = {
        {"current","relative_humidity_2m","101"}, {"current","relative_humidity_2m","65.5"},
        {"current","weather_code","4"}, {"current","weather_code","2.5"},
        {"daily","temperature_2m_min","[30]"}, {"daily","temperature_2m_max","[]"},
        {"daily","temperature_2m_min","17"}, {"daily","time","[\"2026-09-09\"]"},
        {"current_units","temperature_2m","\"°F\""}, {"daily_units","temperature_2m_min","null"},
        {NULL,"timezone","\"UTC\""}, {NULL,"utc_offset_seconds","0"},
    };
    for (unsigned i=0;i<sizeof(invalid_fields)/sizeof(*invalid_fields);++i) {
        cJSON *doc=cJSON_Parse(fixture);
        cJSON *object=invalid_fields[i].object?cJSON_GetObjectItem(doc,invalid_fields[i].object):doc;
        cJSON_ReplaceItemInObject(object,invalid_fields[i].key,cJSON_Parse(invalid_fields[i].replacement));
        char *json=cJSON_PrintUnformatted(doc);
        assert(!chronvs_weather_parse(json,strlen(json),&value)); free(json); cJSON_Delete(doc);
    }
    char text[80];
    chronvs_weather_age(&value,value.updated_epoch,text,sizeof(text)); assert(!strcmp(text,"Atualizado agora"));
    chronvs_weather_age(&value,value.updated_epoch+10800,text,sizeof(text)); assert(!strcmp(text,"Atualizado há 3 h"));
    chronvs_weather_age(&value,-1,text,sizeof(text)); assert(!strcmp(text,"Horário indisponível"));
    chronvs_time_t t={.year=26,.month=9,.day=10,.hour=12,.valid=true};
    assert(chronvs_weather_local_epoch(&t)==value.updated_epoch);
    t=(chronvs_time_t){.year=0,.month=1,.day=1,.valid=true}; assert(chronvs_weather_local_epoch(&t)==946684800);
    t=(chronvs_time_t){.year=24,.month=2,.day=29,.valid=true}; assert(chronvs_weather_local_epoch(&t)>0);
    t.year=25; assert(chronvs_weather_local_epoch(&t)==-1);
    const int codes[]={0,1,2,3,45,48,51,53,55,56,57,61,63,65,66,67,71,73,75,77,80,81,82,85,86,95,96,99};
    for (unsigned i=0;i<sizeof(codes)/sizeof(*codes);++i) assert(chronvs_weather_condition(codes[i]));
    assert(!chronvs_weather_condition(4));
}

int main(void) {
    FILE *file=fopen("tests/fixtures/weather.json","rb"); assert(file);
    size_t size=fread(fixture,1,sizeof(fixture)-1,file); fclose(file); fixture[size]=0;
    reset_transport(); parser_tests(); reboot();
    chronvs_weather_snapshot_t snapshot;
    assert(!chronvs_weather_get_snapshot(&snapshot));
    wifi_error=ESP_FAIL; expect_failure("Sem Wi-Fi");
    assert(chronvs_weather_request_update()); run_worker();
    assert(chronvs_weather_state()==CHRONVS_WEATHER_SUCCESS && commits==1);
    reboot(); assert(chronvs_weather_get_snapshot(&snapshot) && snapshot.humidity_percent==65);
    wifi_error=ESP_ERR_TIMEOUT; expect_failure("Tempo esgotado");
    wifi_error=ESP_FAIL; expect_failure("Sem Wi-Fi");
    stalled=true; expect_failure("Tempo esgotado");
    status_code=404; expect_failure("Resposta inválida");
    unsigned before=requests; status_code=503; expect_failure("Resposta inválida"); assert(requests==before+3);
    response="{}"; response_length=2; expect_failure("Resposta inválida");
    truncated=true; expect_failure("Resposta inválida");
    fail_write=true; expect_failure("Falha ao salvar");
    fail_commit=true; expect_failure("Falha ao salvar");
    fail_alloc=true; expect_failure("Memória insuficiente");
    fail_task=true; assert(!chronvs_weather_request_update()); assert(!strcmp(chronvs_weather_error(),"Memória insuficiente")); reset_transport();
    char huge[CHRONVS_WEATHER_RESPONSE_LIMIT+2]; memset(huge,' ',sizeof(huge));
    response=huge; response_length=sizeof(huge); expect_failure("Resposta inválida");
    response=huge; response_length=sizeof(huge); chunked=true; expect_failure("Resposta inválida");
    chunked=true; assert(chronvs_weather_request_update()); run_worker(); assert(chronvs_weather_state()==CHRONVS_WEATHER_SUCCESS);
    assert(requests==cleans); reboot(); assert(chronvs_weather_get_snapshot(&snapshot));
    ((weather_record_t *)disk)->humidity=255; reboot(); assert(!chronvs_weather_get_snapshot(&snapshot));
    puts("Weather: parser, cache/reboot, failures, single request, HTTPS limits and release passed");
    return 0;
}
