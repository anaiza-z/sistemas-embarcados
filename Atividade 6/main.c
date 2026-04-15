#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// pinos
#define LED_GPIO GPIO_NUM_2
#define BOTAO_GPIO GPIO_NUM_4

// tempos
#define DEBOUNCE_TIME_MS 50 // debounce do botão
#define AUTO_OFF_TIME_MS 10000 // desligamento automático (10s)
#define HOLD_OFF_TIME_MS 2000 // desligamento forçado (2s segurado)

// dados enviados da ISR para a task
typedef struct {
    int level;
    int64_t timestamp;
} EventoBotao;

QueueHandle_t fila_eventos_botao = NULL;

// ISR: captura a borda e envia para a fila
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    EventoBotao evt;
    evt.level = gpio_get_level(gpio_num);
    evt.timestamp = esp_timer_get_time() / 1000; // captura tempo em ms
    
    // envia o evento (borda de subida ou descida) para a Fila do FreeRTOS
    xQueueSendFromISR(fila_eventos_botao, &evt, NULL);
}

void controle_iluminacao_task(void* arg) {
    EventoBotao evt;
    int64_t tempo_pressionado = 0;
    int64_t tempo_led_ligado = 0;
    int64_t ultimo_tempo_evt = 0;
    
    bool botao_esta_pressionado = false;
    uint8_t led_estado = 0;

    while(1) {
        TickType_t tempo_espera = portMAX_DELAY;
        int64_t tempo_atual = esp_timer_get_time() / 1000;
        // 1. Calcula o timeout dinâmico da fila
        if (botao_esta_pressionado) {
            // aguarda para acionar o desligamento forçado aos exatos 2 segundos
            int64_t tempo_segurando = tempo_atual - tempo_pressionado;
            if (tempo_segurando < HOLD_OFF_TIME_MS) {
                tempo_espera = pdMS_TO_TICKS(HOLD_OFF_TIME_MS - tempo_segurando);
            } else {
                tempo_espera = 0; // dispara imediatamente o timeout
            }
        } else if (led_estado == 1) {
            // aciona o desligamento automático aos exatos 10s
            int64_t tempo_aceso = tempo_atual - tempo_led_ligado;
            if (tempo_aceso < AUTO_OFF_TIME_MS) {
                tempo_espera = pdMS_TO_TICKS(AUTO_OFF_TIME_MS - tempo_aceso);
            } else {
                tempo_espera = 0;
            }
        }
        // 2. Aguarda evento da ISR ou timeout
        if (xQueueReceive(fila_eventos_botao, &evt, tempo_espera) == pdTRUE) {
            
            // descarta eventos dentro da janela de debounce
            if ((evt.timestamp - ultimo_tempo_evt) < DEBOUNCE_TIME_MS) {
                continue;
            }
            ultimo_tempo_evt = evt.timestamp;

            if (evt.level == 1) { 
                // borda de subida: botão pressionado
                botao_esta_pressionado = true;
                tempo_pressionado = evt.timestamp;
            } else { 
                // borda de descida: botão solto
                botao_esta_pressionado = false;
                int64_t duracao_clique = evt.timestamp - tempo_pressionado;
                
                // clique válido (entre debounce e 2s)
                if (duracao_clique < HOLD_OFF_TIME_MS && duracao_clique > DEBOUNCE_TIME_MS) {
                    if (led_estado == 0) {
                        // primeiro clique: liga o LED e inicia contagem de 10s
                        led_estado = 1;
                        gpio_set_level(LED_GPIO, 1);
                        tempo_led_ligado = evt.timestamp;
                        printf("Primeiro Clique: LED ACESO (10s iniciados).\n");
                    } else {
                        // clique subsequente: renova o timer de 10s
                        tempo_led_ligado = evt.timestamp;
                        printf("Clique Subsequente: Temporizador de 10s reiniciado.\n");
                    }
                }
            }
        } else {
            // 3. Timeout atingido: verifica qual evento de tempo disparou
            tempo_atual = esp_timer_get_time() / 1000;
            
            // desligamento forçado: botão segurado por mais de 2s
            if (botao_esta_pressionado && (tempo_atual - tempo_pressionado >= HOLD_OFF_TIME_MS)) {
                if (led_estado == 1) {
                    led_estado = 0;
                    gpio_set_level(LED_GPIO, 0);
                    printf("Desligamento Forcado: Botao segurado por > 2s. LED APAGADO imediatamente.\n");
                }
                botao_esta_pressionado = false;
            } 
            // desligamento automático: 10s sem interação
            else if (!botao_esta_pressionado && led_estado == 1 && (tempo_atual - tempo_led_ligado >= AUTO_OFF_TIME_MS)) {
                led_estado = 0;
                gpio_set_level(LED_GPIO, 0);
                printf("Temporizador de Seguranca: LED desligado automaticamente apos 10s.\n");
            }
        }
    }
}

void app_main(void) {
    printf("Iniciando Sistema de Iluminacao com Interrupcao (ISR)...\n");

    // configuração do GPIO do LED
    gpio_config_t io_conf_led = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf_led);
    gpio_set_level(LED_GPIO, 0); //garante que inicie apagado

    // configuração do GPIO do botão com interrupção nas duas bordas
    gpio_config_t io_conf_botao = {
        .pin_bit_mask = (1ULL << BOTAO_GPIO),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_ANYEDGE,
        .pull_down_en = 1,              
        .pull_up_en = 0,
    };
    gpio_config(&io_conf_botao);

    // fila para troca de mensagens entre ISR e task
    fila_eventos_botao = xQueueCreate(10, sizeof(EventoBotao));

    // task que processa os eventos e controla o LED
    xTaskCreate(controle_iluminacao_task, "controle_iluminacao_task", 2048, NULL, 10, NULL);

    // habilita o serviço de ISR e registra o handler no pino do botão
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BOTAO_GPIO, gpio_isr_handler, (void*) BOTAO_GPIO);
}