/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_sleep.h"

#define BOOT_BUTTON_GPIO    GPIO_NUM_0

static void example_register_boot_wakeup(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    /* BOOT button is active-low */
    ESP_ERROR_CHECK(gpio_wakeup_enable(BOOT_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
}

void app_main(void)
{
    example_register_boot_wakeup();

    printf("Entering light sleep, press BOOT (GPIO0) to wake\n");
    uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);

    esp_light_sleep_start();

    printf("Woke up from light sleep\n");

    while (true) {
        printf("HI\n");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
