#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

// Pinos
#define LED_GPIO 2 // LED vermelho
#define UART2_TX_PIN 17 // Conectar ao RX2 com jumper
#define UART2_RX_PIN 16 // Conectar ao TX2 com jumper

// Configuração UART
#define UART_NUM UART_NUM_2
#define BAUD_RATE 115200
#define BUF_SIZE 256 // Tamanho do buffer de recepção

#define MSG_LIGAR "LIGAR"
#define MSG_DESLIGAR "DESLIGAR"

void app_main(void)
{
    // Configura o LED como saída e começa apagado
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    // Configura a UART2
    uart_config_t uart_cfg = {
        .baud_rate  = BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS, // 8 bits de dados
        .parity     = UART_PARITY_DISABLE, // sem paridade
        .stop_bits  = UART_STOP_BITS_1, // 1 stop bit
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };

    // Aplica a configuração
    uart_param_config(UART_NUM, &uart_cfg);

    // Define os pinos TX e RX da UART2
    uart_set_pin(UART_NUM,
                 UART2_TX_PIN, // TX
                 UART2_RX_PIN, // RX
                 UART_PIN_NO_CHANGE, // RTS (não usado)
                 UART_PIN_NO_CHANGE);// CTS (não usado)

    // Instala o driver com buffer de RX de 256 bytes
    uart_driver_install(UART_NUM, BUF_SIZE, 0, 0, NULL, 0);

    printf("UART2 iniciada: TX=GPIO%d  RX=GPIO%d  %d baud\n",
           UART2_TX_PIN, UART2_RX_PIN, BAUD_RATE);
    printf("Conecte um jumper entre GPIO%d e GPIO%d\n\n",
           UART2_TX_PIN, UART2_RX_PIN);

    uint8_t rx_buf[BUF_SIZE]; // Buffer para os dados recebidos
    int toggle = 0; // Alterna entre LIGAR e DESLIGAR

    while (1) {
        // Escolhe a mensagem desta rodada
        const char *msg = toggle ? MSG_DESLIGAR : MSG_LIGAR;
        toggle = !toggle;

        // Envia pela UART2
        uart_write_bytes(UART_NUM, msg, strlen(msg));
        printf("[TX] Enviado: %s\n", msg);

        // Aguarda o eco chegar pelo loopback (máx. 500 ms)
        int len = uart_read_bytes(UART_NUM, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(500));

        if (len > 0) {
            rx_buf[len] = '\0'; // Termina a string
            printf("[RX] Recebido: %s\n", (char *)rx_buf);

            // Compara e controla o LED 
            if (strcmp((char *)rx_buf, MSG_LIGAR) == 0) {
                gpio_set_level(LED_GPIO, 1); // LED acende
                printf("[LED] LIGADO\n");
            } else if (strcmp((char *)rx_buf, MSG_DESLIGAR) == 0) {
                gpio_set_level(LED_GPIO, 0); // LED apaga
                printf("[LED] DESLIGADO\n");
            }
        } else {
            // Nenhum dado recebido → verifique o jumper
            printf("[RX] Nenhum dado. Verifique o jumper TX-RX!\n");
        }

        printf("---\n");

        // Aguarda 2 segundos antes do próximo envio
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}