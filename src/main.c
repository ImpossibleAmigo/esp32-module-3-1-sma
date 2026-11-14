#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

// Налаштування пінів для ESP32-S3
#define LDR_ADC_CHAN       ADC_CHANNEL_3 // GPIO4
#define LED_GPIO           GPIO_NUM_5

// Налаштування фільтрації (SMA) та гістерезису
#define SMA_WINDOW         10
#define THRESH_DARK        1000 // Нижче цього значення - темно (вмикаємо LED)
#define THRESH_LIGHT       2000 // Вище цього - світло (вимикаємо LED)
// Зверни увагу: пороги можливо доведеться відкалібрувати під твоє освітлення і резистор

static const char *TAG = "SMA_HW";

void app_main(void)
{
    // 1. Налаштування GPIO для світлодіода
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_GPIO, 0); // Початковий стан - вимкнено

    // 2. Налаштування ADC (Oneshot mode)
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, LDR_ADC_CHAN, &config));

    // 3. Змінні для Simple Moving Average
    int sma_buffer[SMA_WINDOW] = {0};
    int sma_sum = 0;
    int sma_idx = 0;
    int count = 0; // Для коректного підрахунку перших SMA_WINDOW ітерацій

    bool led_state = false;

    while (1) {
        int raw_val;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, LDR_ADC_CHAN, &raw_val));

        // Оновлення буфера та суми для SMA
        sma_sum -= sma_buffer[sma_idx];
        sma_buffer[sma_idx] = raw_val;
        sma_sum += raw_val;
        sma_idx = (sma_idx + 1) % SMA_WINDOW;

        if (count < SMA_WINDOW) {
            count++;
        }
        
        // Відфільтроване значення освітленості
        int avg_val = sma_sum / count; 

        ESP_LOGI(TAG, "Raw: %d \t SMA: %d", raw_val, avg_val);

        // 4. Логіка гістерезису
        if (avg_val < THRESH_DARK && !led_state) {
            led_state = true;
            gpio_set_level(LED_GPIO, 1);
            ESP_LOGI(TAG, "-> Стало темно. LED УВІМКНЕНО");
        } else if (avg_val > THRESH_LIGHT && led_state) {
            led_state = false;
            gpio_set_level(LED_GPIO, 0);
            ESP_LOGI(TAG, "-> Стало світло. LED ВИМКНЕНО");
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Зчитуємо 10 разів на секунду
    }
}