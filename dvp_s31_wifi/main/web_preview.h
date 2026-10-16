/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start SoftAP and HTTP server for camera preview.
 *
 * Connect to Wi-Fi SSID "ESP-S31-CAM" (password "12345678"),
 * then open http://192.168.4.1 in a browser.
 */
esp_err_t web_preview_start(void);

/**
 * @brief Push one MJPEG frame for the HTTP stream.
 *
 * Safe to call from the DVP JPEG encoding task.
 */
void web_preview_push_frame(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
