#include "services/voice_lab_service.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "model_path.h"

#define DUPLICATE_GUARD_US 800000LL

typedef struct {
    const char *text;
    /* English graphemes chosen to approximate Brazilian pronunciation.
     * MultiNet6 converts these at runtime; hardware trials will refine them. */
    const char *approximation;
} voice_word_t;

static const voice_word_t words[CHRONVS_VOICE_LAB_WORDS] = {
    // === AÇÕES E VERBOS (Adaptados para vogais longas e 'R' mudo) ===
    {"Comprar", "cone prah"},
    {"Fazer", "fah zay"},
    {"Lembrar", "lem brah"},
    {"Pagar", "pah gah"},
    {"Ligar", "lee gah"},
    {"Mandar", "mun dah"},
    {"Pegar", "pay gah"},
    {"Cancelar", "can say lah"},
    {"Marcar", "mar cah"},
    {"Avisar", "ah vee zah"},
    {"Buscar", "boos cah"},
    {"Levar", "lay vah"},
    {"Pedir", "pay jee"},
    {"Usar", "oo zah"},
    {"Testar", "tes tah"},
    {"Consertar", "cone say tah"},
    {"Limpar", "leem pah"},
    {"Arrumar", "ah hoo mah"},
    {"Guardar", "gwar dah"},
    {"Vender", "ven day"},
    {"Trocar", "troh cah"},
    {"Chamar", "shah mah"},
    {"Encontrar", "in cone trah"},
    {"Sair", "sah ee"},
    {"Chegar", "shay gah"},
    {"Comer", "coh may"},
    {"Beber", "bay bay"},
    {"Ver", "vay"},
    {"Ler", "lay"},
    {"Escrever", "is cray vay"},
    {"Estudar", "is too dah"},
    {"Trabalhar", "trah bah lyah"},
    {"Imprimir", "im pree mee"},
    {"Desenhar", "day zen yah"},
    {"Cortar", "cor tah"},
    {"Medir", "may jee"},
    {"Montar", "mone tah"},
    {"Configurar", "cone fee goo rah"},
    {"Atualizar", "ah twah lee zah"},
    {"Salvar", "sall vah"},

    // === TEMPO E DATAS (Vogais nasais suavizadas) ===
    {"Hoje", "oh jee"},
    {"Amanhã", "ah mah nyah"},
    {"Ontem", "own tayn"},
    {"Agora", "ah goh rah"},
    {"Depois", "day poys"},
    {"Cedo", "say doo"},
    {"Tarde", "tar jee"},
    {"Noite", "noy chee"},
    {"Manhã", "mah nyah"},
    {"Segunda", "say goon dah"},
    {"Terça", "tair sah"},
    {"Quarta", "kwar tah"},
    {"Quinta", "keen tah"},
    {"Sexta", "ses tah"},
    {"Sábado", "sah bah doo"},
    {"Domingo", "doh meen goo"},
    {"Feriado", "fay ree ah doo"},
    {"Semana", "say mah nah"},
    {"Mês", "mays"},
    {"Ano", "ah noo"},
    {"Hora", "oh rah"},
    {"Minuto", "mee noo too"},
    {"Sempre", "sem pree"},
    {"Nunca", "noon cah"},
    {"Logo", "loh goo"},

    // === LOCAIS E NAVEGAÇÃO ===
    {"Casa", "cah zah"},
    {"Trampo", "trump poh"},
    {"Trabalho", "trah bah lyoo"},
    {"Rua", "hoo ah"},
    {"Mercado", "mair cah doo"},
    {"Padaria", "pah dah ree ah"},
    {"Farmácia", "far mah see ah"},
    {"Médico", "may jee coo"},
    {"Dentista", "den tees tah"},
    {"Academia", "ah cah day mee ah"},
    {"Banco", "bun coo"},
    {"Shopping", "show peen"},
    {"Centro", "sen troo"},
    {"Metrô", "may troh"},
    {"Ônibus", "oh nee boos"},
    {"Carro", "cah hoo"},
    {"Posto", "pos too"},
    {"Restaurante", "hes tow run chee"},
    {"Bar", "bah"},
    {"Pizzaria", "peet zah ree ah"},
    {"Parque", "par kee"},
    {"Oficina", "oh fee see nah"},
    {"Loja", "loh zhah"},
    {"Apartamento", "ah par tah men too"},
    {"Prédio", "pray jee oo"},
    {"Portaria", "por tah ree ah"},
    {"Garagem", "gah rah zhen"},
    {"Aeroporto", "ah eh roh por too"},
    {"Rodoviária", "hoh doh vee ah ree ah"},
    {"Fazenda", "fah zen dah"},

    // === ALIMENTAÇÃO E MERCADO ===
    {"Pão", "pow"},
    {"Leite", "lay chee"},
    {"Café", "cah fay"},
    {"Água", "ah gwah"},
    {"Comida", "coh mee dah"},
    {"Almoço", "aw moh soo"},
    {"Janta", "zhun tah"},
    {"Lanche", "lun shee"},
    {"Pizza", "peet sah"},
    {"Hambúrguer", "um boor gay"},
    {"Bacon", "bay cone"},
    {"Remédio", "hay may jee oo"},
    {"Bolo", "boh loo"},
    {"Cerveja", "say vay zhah"},
    {"Vinho", "vee nyoo"},
    {"Carne", "car nee"},
    {"Frango", "frun goo"},
    {"Peixe", "pay shee"},
    {"Fruta", "froo tah"},
    {"Verdura", "vay doo rah"},
    {"Legume", "lay goo mee"},
    {"Arroz", "ah hoz"},
    {"Feijão", "fay zhow"},
    {"Macarrão", "mah cah how"},
    {"Sabonete", "sah boh nay chee"},
    {"Shampoo", "shum poo"},
    {"Papel", "pah pell"},
    {"Lixo", "lee shoo"},
    {"Ração", "hah sow"},
    {"Petisco", "pay chees coo"},

    // === ROTINA, FINANÇAS E OBJETOS ===
    {"Reunião", "hay oh knee ow"},
    {"Festa", "fes tah"},
    {"Evento", "ay ven too"},
    {"Cliente", "clee in chee"},
    {"Conta", "cone tah"},
    {"Boleto", "boh lay too"},
    {"Dinheiro", "jean yay roo"},
    {"Cartão", "car tow"},
    {"Chave", "shah vee"},
    {"Celular", "say loo lah"},
    {"Computador", "com poo tah doh"},
    {"Notebook", "noh chee boo kee"},
    {"Tela", "tay lah"},
    {"Teclado", "tay clah doo"},
    {"Mouse", "mow zee"},
    {"Cabo", "cah boo"},
    {"Carregador", "cah hay gah doh"},
    {"Bateria", "bah tay ree ah"},
    {"Fone", "foh nee"},
    {"Relógio", "hay loh zhee oo"},
    {"Óculos", "oh coo loos"},
    {"Roupa", "hoh pah"},
    {"Tênis", "tay nees"},
    {"Mochila", "moh shee lah"},
    {"Bolsa", "bowl sah"},
    {"Documento", "doh coo men too"},
    {"Ingresso", "in gray soo"},
    {"Passagem", "pah sah zhen"},
    {"Presente", "pray zen chee"},
    {"Corrida", "coh hee dah"},

    // === TECNOLOGIA E PROJETOS PESSOAIS ===
    {"Impressora", "im pray soh rah"},
    {"Creality", "cree ah lee tee"},
    {"Filamento", "fee lah men too"},
    {"Peça", "pay sah"},
    {"Cachepô", "cah shay poe"},
    {"Fidget", "fee jet"},
    {"Código", "coh jee goo"},
    {"Script", "screep chee"},
    {"Placa", "plah cah"},
    {"Terminal", "tay mee now"},
    {"Projeto", "proh zhay too"},
    {"Ideia", "ee day ah"},
    {"Bug", "buh gee"},
    {"Teste", "tes chee"},
    {"Deploy", "day ploy"},
    {"Repositório", "hay poh zee toh ree oo"},
    {"Servidor", "say vee doh"},
    {"Angular", "ung goo lah"},
    {"API", "ah pay ee"},
    {"Docker", "doh cay"},
    {"Linux", "lee nooks"},
    {"Wifi", "wai fai"},
    {"Bluetooth", "bloo toof"},
    {"Rede", "hay jee"},
    {"Obra", "oh brah"},
    {"Vidro", "vee droo"},
    {"Sacada", "sah cah dah"},
    {"Dolphin", "dowl feen"},
    {"Cordeiro", "cor day roo"},
    {"Criativo", "cree ah chee voo"},

    // === FAMÍLIA, PESSOAS E EXTRAS ===
    {"Tamires", "tah mee rees"},
    {"Isabel", "ee zah bell"},
    {"Branquinha", "brung keen yah"},
    {"Matheus", "mah tay oos"},
    {"Leonardo", "lay oh nar doo"},
    {"Helena", "ay lay nah"},
    {"Mãe", "muy"},
    {"Pai", "pie"},
    {"Vó", "voh"},
    {"Família", "fah mee lee ah"},
    {"Cachorro", "cah show hoo"},
    {"Gato", "gah too"},
    {"Zoo", "zoh oo"},
    {"Sim", "seen"},
    {"Não", "now"}
};

static const char *TAG = "voice_lab";
static atomic_int state = CHRONVS_VOICE_IDLE;
static atomic_bool stop_requested;
static QueueHandle_t results;
static const char *last_error = "";

const char *chronvs_voice_lab_word(unsigned index) {
    return index < CHRONVS_VOICE_LAB_WORDS ? words[index].text : NULL;
}

chronvs_voice_state_t chronvs_voice_lab_state(void) {
    return (chronvs_voice_state_t)atomic_load(&state);
}

const char *chronvs_voice_lab_error(void) { return last_error; }

bool chronvs_voice_lab_take_result(chronvs_voice_result_t *result) {
    return result && results && xQueueReceive(results, result, 0) == pdTRUE;
}

static void fail(const char *message) {
    last_error = message;
    atomic_store(&state, CHRONVS_VOICE_ERROR);
    ESP_LOGE(TAG, "%s", message);
}

static bool configure_commands(const esp_mn_iface_t *multinet,
                               model_iface_data_t *model) {
    if (esp_mn_commands_alloc(multinet, model) != ESP_OK) return false;
    for (unsigned i = 0; i < CHRONVS_VOICE_LAB_WORDS; ++i) {
        if (esp_mn_commands_add((int)i + 1, words[i].approximation) != ESP_OK) {
            ESP_LOGE(TAG, "Invalid approximation for %s: %s",
                     words[i].text, words[i].approximation);
            return false;
        }
    }
    return esp_mn_commands_update() == NULL;
}

static esp_err_t microphone_open(i2s_chan_handle_t *channel, unsigned frame_samples) {
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 4;
    channel_config.dma_frame_num = frame_samples;
    esp_err_t err = i2s_new_channel(&channel_config, NULL, channel);
    if (err != ESP_OK) return err;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_15, .ws = GPIO_NUM_2,
            .dout = I2S_GPIO_UNUSED, .din = GPIO_NUM_39,
        },
    };
    config.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    err = i2s_channel_init_std_mode(*channel, &config);
    if (err == ESP_OK) err = i2s_channel_enable(*channel);
    if (err != ESP_OK) {
        i2s_del_channel(*channel);
        *channel = NULL;
    }
    return err;
}

static void voice_task(void *argument) {
    (void)argument;
    srmodel_list_t *models = NULL;
    const esp_mn_iface_t *multinet = NULL;
    model_iface_data_t *model = NULL;
    i2s_chan_handle_t microphone = NULL;
    int32_t *raw = NULL;
    int16_t *samples = NULL;

    models = esp_srmodel_init("model");
    char *model_name = models ? esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH) : NULL;
    if (!model_name) { fail("Modelo MultiNet não encontrado"); goto cleanup; }
    multinet = esp_mn_handle_from_name(model_name);
    model = multinet ? multinet->create(model_name, 3000) : NULL;
    if (!model) { fail("Falha ao carregar MultiNet"); goto cleanup; }
    if (!configure_commands(multinet, model)) {
        fail("Vocabulário fonético inválido"); goto cleanup;
    }

    const int chunk = multinet->get_samp_chunksize(model);
    if (chunk <= 0 || microphone_open(&microphone, (unsigned)chunk) != ESP_OK) {
        fail("Microfone indisponível"); goto cleanup;
    }
    raw = malloc((size_t)chunk * sizeof(*raw));
    samples = malloc((size_t)chunk * sizeof(*samples));
    if (!raw || !samples) { fail("Memória insuficiente"); goto cleanup; }

    last_error = "";
    atomic_store(&state, CHRONVS_VOICE_LISTENING);
    int last_id = -1;
    int64_t last_detection = 0;
    while (!atomic_load(&stop_requested)) {
        size_t read = 0;
        esp_err_t err = i2s_channel_read(microphone, raw,
            (size_t)chunk * sizeof(*raw), &read, pdMS_TO_TICKS(100));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK || read != (size_t)chunk * sizeof(*raw)) {
            fail("Falha ao ler microfone"); break;
        }
        /* The board microphone carries its significant signed bits in the
         * upper portion of the 32-bit right slot, as in the vendor example. */
        for (int i = 0; i < chunk; ++i) samples[i] = (int16_t)(raw[i] >> 14);
        esp_mn_state_t detected = multinet->detect(model, samples);
        const int64_t now = esp_timer_get_time();
        if (detected == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *found = multinet->get_results(model);
            if (found && found->num > 0 &&
                (found->command_id[0] != last_id || now - last_detection >= DUPLICATE_GUARD_US)) {
                chronvs_voice_result_t result = {0};
                const char *start = found->string;
                size_t length = strnlen(start, sizeof(found->string));
                while (length && *start == ' ') { ++start; --length; }
                while (length && start[length - 1] == ' ') --length;
                if (length) {
                    snprintf(result.text, sizeof(result.text), "%.*s", (int)length, start);
                    ESP_LOGI(TAG, "VOX: %s", result.text);
                    xQueueSend(results, &result, 0);
                    last_id = found->command_id[0];
                    last_detection = now;
                }
            }
        }
    }

cleanup:
    if (chronvs_voice_lab_state() != CHRONVS_VOICE_ERROR)
        atomic_store(&state, CHRONVS_VOICE_STOPPING);
    if (microphone) {
        i2s_channel_disable(microphone);
        i2s_del_channel(microphone);
    }
    free(samples);
    free(raw);
    if (model && multinet) multinet->destroy(model);
    if (models) esp_srmodel_deinit(models);
    atomic_store(&stop_requested, false);
    if (chronvs_voice_lab_state() != CHRONVS_VOICE_ERROR)
        atomic_store(&state, CHRONVS_VOICE_IDLE);
    vTaskDelete(NULL);
}

bool chronvs_voice_lab_start(void) {
    chronvs_voice_state_t current = chronvs_voice_lab_state();
    if (current == CHRONVS_VOICE_STARTING || current == CHRONVS_VOICE_LISTENING ||
        current == CHRONVS_VOICE_STOPPING) return false;
    if (!results) results = xQueueCreate(8, sizeof(chronvs_voice_result_t));
    if (!results) { fail("Memória insuficiente"); return false; }
    xQueueReset(results);
    last_error = "";
    atomic_store(&stop_requested, false);
    atomic_store(&state, CHRONVS_VOICE_STARTING);
    if (xTaskCreatePinnedToCore(voice_task, "vox_multinet", 6144, NULL, 4, NULL, 0) == pdPASS)
        return true;
    fail("Memória insuficiente");
    return false;
}

void chronvs_voice_lab_stop(void) {
    chronvs_voice_state_t current = chronvs_voice_lab_state();
    if (current == CHRONVS_VOICE_STARTING || current == CHRONVS_VOICE_LISTENING) {
        atomic_store(&state, CHRONVS_VOICE_STOPPING);
        atomic_store(&stop_requested, true);
    } else if (current == CHRONVS_VOICE_ERROR) {
        atomic_store(&state, CHRONVS_VOICE_IDLE);
    }
}
