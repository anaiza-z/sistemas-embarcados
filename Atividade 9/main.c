#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/adc.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "RTOS_IMU";

//Pinos
#define PIN_LED 2
#define PIN_POT_GPIO 5
#define PIN_POT_CH ADC1_CHANNEL_4
#define PIN_BUTTON 6
#define PIN_SDA 8
#define PIN_SCL 9

// ADC / PWM 
#define ADC_MAX 4095
#define PWM_FREQ_HZ 5000
#define PWM_RESOLUTION LEDC_TIMER_13_BIT  
#define PWM_MAX 8191

//MPU6050 
#define MPU6050_ADDR 0x68
#define MPU_REG_PWR 0x6B
#define MPU_REG_ACCEL 0x3B
#define ACCEL_SENS 16384.0f   /* LSB/g para ±2g */
#define I2C_FREQ_HZ 100000     /* 100 kHz Standard Mode */

// RTOS handles
static QueueHandle_t xQueuePot  = NULL;
static SemaphoreHandle_t xSemButton = NULL;
static SemaphoreHandle_t xMutexIMU = NULL;

//I2C handles 
static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t mpu_dev = NULL;

// Estado IMU 
typedef struct { float x, y, z; } imu_data_t;
static imu_data_t g_imu = {0.0f, 0.0f, 0.0f};

// Estado HOLD 
static volatile bool g_hold = false;

//PERIFÉRICOS

static void init_adc(void)
{
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
    ESP_ERROR_CHECK(adc1_config_channel_atten(PIN_POT_CH, ADC_ATTEN_DB_11));
}

static void init_pwm(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = PIN_LED,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

static void init_button(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BUTTON),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

static void init_i2c(void)
{
    esp_err_t err;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = PIN_SDA,
        .scl_io_num        = PIN_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_cfg, &i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus ERRO: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Barramento I2C OK (SDA=%d SCL=%d)", PIN_SDA, PIN_SCL);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MPU6050_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &mpu_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add_device ERRO: %s", esp_err_to_name(err));
        mpu_dev = NULL;
        return;
    }
    ESP_LOGI(TAG, "MPU6050 registrado (0x%02X)", MPU6050_ADDR);

    /* Acorda MPU6050: limpa bit SLEEP em PWR_MGMT_1 */
    uint8_t wake[2] = { MPU_REG_PWR, 0x00 };
    err = i2c_master_transmit(mpu_dev, wake, 2, pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wake MPU6050 ERRO: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "MPU6050 acordado OK");
    }
}

// POTENCIÔMETRO  (Produtora → Queue)
static void task_pot(void *pv)
{
    while (1) {
        int raw = adc1_get_raw(PIN_POT_CH);
        uint32_t duty = (uint32_t)raw * PWM_MAX / ADC_MAX;
        xQueueOverwrite(xQueuePot, &duty);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// LED  (Consumidora ← Queue | Semáforo HOLD)
static void task_led(void *pv)
{
    uint32_t duty = 0;

    while (1) {
        // Checa alternância HOLD sem bloquear
        if (xSemaphoreTake(xSemButton, 0) == pdTRUE) {
            g_hold = !g_hold;
            ESP_LOGI(TAG, "Modo: %s", g_hold ? "HOLD" : "LIVE");
        }

        if (!g_hold) {
            xQueuePeek(xQueuePot, &duty, pdMS_TO_TICKS(10));
        }
        //HOLD: duty não muda
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// BOTÃO (Produtora → Semáforo binário)
static void task_button(void *pv)
{
    bool last = true;
    bool bloqueado = false;
    TickType_t t_press = 0;
    const TickType_t DEBOUNCE = pdMS_TO_TICKS(300);

    while (1) {
        bool cur = (bool)gpio_get_level(PIN_BUTTON);

        if (bloqueado && (xTaskGetTickCount() - t_press) >= DEBOUNCE) {
            bloqueado = false;
        }

        if (last && !cur && !bloqueado) {
            xSemaphoreGive(xSemButton);
            bloqueado = true;
            t_press = xTaskGetTickCount();
        }

        last = cur;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// IMU  (Produtora → struct global via Mutex)
static void task_imu(void *pv)
{
    if (mpu_dev == NULL) {
        ESP_LOGE(TAG, "task_imu: mpu_dev NULL — IMU desabilitada");
        vTaskDelete(NULL);
        return;
    }

    const uint8_t reg = MPU_REG_ACCEL;
    uint8_t buf[6];

    while (1) {
        esp_err_t err = i2c_master_transmit_receive(
            mpu_dev, &reg, 1, buf, 6, pdMS_TO_TICKS(100));

        if (err == ESP_OK) {
            int16_t ax = (int16_t)((buf[0] << 8) | buf[1]);
            int16_t ay = (int16_t)((buf[2] << 8) | buf[3]);
            int16_t az = (int16_t)((buf[4] << 8) | buf[5]);

            if (xSemaphoreTake(xMutexIMU, pdMS_TO_TICKS(20)) == pdTRUE) {
                g_imu.x = ax / ACCEL_SENS;
                g_imu.y = ay / ACCEL_SENS;
                g_imu.z = az / ACCEL_SENS;
                xSemaphoreGive(xMutexIMU);
            }
        } else {
            ESP_LOGW(TAG, "IMU read falhou: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// CONSOLE  (Consumidora ← Queue + IMU via Mutex)
static void task_console(void *pv)
{
    uint32_t duty = 0;
    uint32_t duty_frozen = 0;   // valor travado no momento do HOLD
    imu_data_t snap = {0.0f, 0.0f, 0.0f};

    while (1) {
        // Só atualiza duty se estiver em modo LIVE
        if (!g_hold) {
            xQueuePeek(xQueuePot, &duty, 0);
            duty_frozen = duty;  // mantém o último valor LIVE atualizado
        }
        // Em HOLD: usa duty_frozen, que parou no momento do bloqueio 

        if (xSemaphoreTake(xMutexIMU, pdMS_TO_TICKS(20)) == pdTRUE) {
            snap = g_imu;
            xSemaphoreGive(xMutexIMU);
        }

        uint32_t duty_show = g_hold ? duty_frozen : duty;
        int raw = (int)((uint64_t)duty_show * ADC_MAX / PWM_MAX);
        int mv = raw * 3300 / ADC_MAX;
        int pct = (int)((uint64_t)duty_show * 100 / PWM_MAX);

        printf("=====================================================\n");
        printf("STATUS: [%s] | POT: %d (%d mV) | LED: %d%%\n",
               g_hold ? "HOLD" : "LIVE", raw, mv, pct);
        printf("IMU ACCEL (g): X: %.2f | Y: %.2f | Z: %.2f\n",
               snap.x, snap.y, snap.z);
        printf("=====================================================\n\n");

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Iniciando sistema RTOS ===");

    init_adc();
    init_pwm();
    init_button();
    init_i2c();

    xQueuePot = xQueueCreate(1, sizeof(uint32_t));
    xSemButton = xSemaphoreCreateBinary();
    xMutexIMU = xSemaphoreCreateMutex();

    configASSERT(xQueuePot);
    configASSERT(xSemButton);
    configASSERT(xMutexIMU);

    xTaskCreate(task_pot, "POT", 2048, NULL, 2, NULL);
    xTaskCreate(task_led, "LED", 2048, NULL, 2, NULL);
    xTaskCreate(task_button, "BUTTON", 2048, NULL, 3, NULL);
    xTaskCreate(task_imu, "IMU", 4096, NULL, 2, NULL);
    xTaskCreate(task_console, "CONSOLE", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "Todas as tarefas criadas. Sistema rodando.");
}