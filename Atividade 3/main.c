#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define DELAY_MS 10
#define FADE_STEP 64

#define LED1_GPIO 2
#define LED2_GPIO 4
#define LED3_GPIO 5
#define LED4_GPIO 13
#define BUZZER_GPIO 14

#define LED_FREQ_HZ 1000
#define LED_RESOLUTION LEDC_TIMER_13_BIT
#define DUTY_MAX 8191
#define DUTY_MIN 0

#define BUZZER_FREQ_MIN 500
#define BUZZER_FREQ_MAX 2000
#define BUZZER_FREQ_INIT 1000
#define BUZZER_FREQ_STEP 50
#define BUZZER_RESOLUTION LEDC_TIMER_10_BIT
#define BUZZER_DUTY_50PCT 512

#define CH_LED1 LEDC_CHANNEL_0
#define CH_LED2 LEDC_CHANNEL_1
#define CH_LED3 LEDC_CHANNEL_2
#define CH_LED4 LEDC_CHANNEL_3
#define CH_BUZZER LEDC_CHANNEL_4

#define TIMER_LEDS LEDC_TIMER_0
#define TIMER_BUZZER LEDC_TIMER_1

static void ledc_timer_leds_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LED_RESOLUTION,
        .timer_num = TIMER_LEDS,
        .freq_hz = LED_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
}

static void ledc_timer_buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BUZZER_RESOLUTION,
        .timer_num = TIMER_BUZZER,
        .freq_hz = BUZZER_FREQ_INIT,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
}

static void ledc_channel_led_init(ledc_channel_t channel, int gpio)
{
    ledc_channel_config_t ch = {
        .gpio_num = gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel,
        .timer_sel  = TIMER_LEDS,
        .duty = DUTY_MIN,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

static void ledc_channel_buzzer_init(void)
{
    ledc_channel_config_t ch = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CH_BUZZER,
        .timer_sel = TIMER_BUZZER,
        .duty = DUTY_MIN,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

static void pwm_init(void)
{
    ledc_timer_leds_init();
    ledc_timer_buzzer_init();

    ledc_channel_led_init(CH_LED1, LED1_GPIO);
    ledc_channel_led_init(CH_LED2, LED2_GPIO);
    ledc_channel_led_init(CH_LED3, LED3_GPIO);
    ledc_channel_led_init(CH_LED4, LED4_GPIO);
    ledc_channel_buzzer_init();
}

static void led_set_duty(ledc_channel_t channel, uint32_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

static void buzzer_set_freq(uint32_t freq_hz)
{
    ledc_set_freq(LEDC_LOW_SPEED_MODE, TIMER_BUZZER, freq_hz);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CH_BUZZER, BUZZER_DUTY_50PCT);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CH_BUZZER);
}

static void buzzer_off(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, CH_BUZZER, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CH_BUZZER);
}

static void fase1_fading_sincronizado(void)
{
    printf("[Fase 1] Fading Sincronizado\n");

    for (uint32_t duty = DUTY_MIN; duty <= DUTY_MAX; duty += FADE_STEP) {
        led_set_duty(CH_LED1, duty);
        led_set_duty(CH_LED2, duty);
        led_set_duty(CH_LED3, duty);
        led_set_duty(CH_LED4, duty);
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
    }

    for (int32_t duty = DUTY_MAX; duty >= (int32_t)DUTY_MIN; duty -= FADE_STEP) {
        led_set_duty(CH_LED1, (uint32_t)duty);
        led_set_duty(CH_LED2, (uint32_t)duty);
        led_set_duty(CH_LED3, (uint32_t)duty);
        led_set_duty(CH_LED4, (uint32_t)duty);
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
    }

    led_set_duty(CH_LED1, DUTY_MIN);
    led_set_duty(CH_LED2, DUTY_MIN);
    led_set_duty(CH_LED3, DUTY_MIN);
    led_set_duty(CH_LED4, DUTY_MIN);
}

static void fading_led(ledc_channel_t channel)
{
    for (uint32_t duty = DUTY_MIN; duty <= DUTY_MAX; duty += FADE_STEP) {
        led_set_duty(channel, duty);
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
    }
    for (int32_t duty = DUTY_MAX; duty >= (int32_t)DUTY_MIN; duty -= FADE_STEP) {
        led_set_duty(channel, (uint32_t)duty);
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
    }
    led_set_duty(channel, DUTY_MIN);
}

static void fase2_fading_sequencial(void)
{
    printf("[Fase 2] Fading Sequencial\n");

    fading_led(CH_LED1);
    fading_led(CH_LED2);
    fading_led(CH_LED3);
    fading_led(CH_LED4);

    fading_led(CH_LED4);
    fading_led(CH_LED3);
    fading_led(CH_LED2);
    fading_led(CH_LED1);
}

static void fase3_teste_sonoro(void)
{
    printf("[Fase 3] Teste Sonoro\n");

    for (uint32_t f = BUZZER_FREQ_MIN; f <= BUZZER_FREQ_MAX; f += BUZZER_FREQ_STEP) {
        buzzer_set_freq(f);
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS * 5));
    }

    for (int32_t f = BUZZER_FREQ_MAX; f >= (int32_t)BUZZER_FREQ_MIN; f -= BUZZER_FREQ_STEP) {
        buzzer_set_freq((uint32_t)f);
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS * 5));
    }

    buzzer_off();
}

void app_main(void)
{
    printf("=== Atividade 3: PWM com LEDC no ESP32-S3 ===\n");

    pwm_init();

    while (true) {
        fase1_fading_sincronizado();
        fase2_fading_sequencial();
        fase3_teste_sonoro();
    }
}