#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// pinos
#define LED_GPIO GPIO_NUM_2
#define BOTAO_GPIO GPIO_NUM_4

// tempo
#define DEBOUNCE_TIME_MS 50 // Tempo de debounce para o botão
#define AUTO_OFF_TIME_MS 10000 // 10 segundos para o desligamento automático

uint8_t led_estado = 0; // 0 = Apagado, 1 = Aceso

// lê o estado do botão usando polling e controla o LED.
void controle_iluminacao() {
    static bool botao_processado = false;
    static int64_t tempo_ultimo_acionamento = 0;
    static int64_t tempo_led_ligado = 0; // quando o LED foi aceso
    
    int64_t tempo_atual = esp_timer_get_time() / 1000; // tempo atual em ms

    // lógica de Leitura e Toggle (com Debounce)
    int estado_botao = gpio_get_level(BOTAO_GPIO);

    // Detecta o pressionamento (nível alto)
    if (estado_botao == 1 && !botao_processado) {
        if (tempo_atual - tempo_ultimo_acionamento > DEBOUNCE_TIME_MS) {
            botao_processado = true;
            tempo_ultimo_acionamento = tempo_atual;

            // Inverte o estado do LED
            led_estado = !led_estado;
            gpio_set_level(LED_GPIO, led_estado);
            
            if (led_estado == 1) {
                printf("Botão pressionado. LED ACESO.\n");
                tempo_led_ligado = tempo_atual; // Registra o momento em que ligou
            } else {
                printf("Botão pressionado. LED APAGADO.\n");
            }
        }
    }

    // Rearma a flag do botão quando ele for solto
    if (botao_processado && estado_botao == 0 && (tempo_atual - tempo_ultimo_acionamento > DEBOUNCE_TIME_MS)) {
        botao_processado = false; 
    }

    // --- Lógica do Temporizador de Segurança ---
    // Se o LED estiver aceso E já se passaram 10 segundos desde que foi ligado...
    if (led_estado == 1 && (tempo_atual - tempo_led_ligado > AUTO_OFF_TIME_MS)) {
        led_estado = 0; // Apaga o LED
        gpio_set_level(LED_GPIO, led_estado);
        printf("Temporizador de segurança: LED desligado automaticamente após 10s.\n");
    }
}

void app_main(void) {
    printf("Iniciando Sistema de Iluminacao com Temporizador...\n");

    // --- Configuração do GPIO do LED ---
    gpio_config_t io_conf_led = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf_led);
    gpio_set_level(LED_GPIO, 0); // Garante que inicie apagado

    // --- Configuração do GPIO do Botão ---
    gpio_config_t io_conf_botao = {
        .pin_bit_mask = (1ULL << BOTAO_GPIO),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE, // sem interrupções
        .pull_down_en = 1, // Pull-down interno ativado
        .pull_up_en = 0,
    };
    gpio_config(&io_conf_botao);

    // polling
    while (1) {
        controle_iluminacao(); 
        vTaskDelay(pdMS_TO_TICKS(10)); // Pausa de 10ms para liberar o processador
    }
}