/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "web_preview.h"

#define WIFI_AP_SSID      "ESP-UVC"
#define WIFI_AP_PASS      "12345678"
#define WIFI_AP_CHANNEL   1
#define WIFI_AP_MAX_CONN  4

#define PART_BOUNDARY     "frame"
#if CONFIG_SPIRAM
#define FRAME_BUF_MAX     (100 * 1024)
#else
#define FRAME_BUF_MAX     (30 * 1024)
#endif

static const char *TAG = "web_preview";

static httpd_handle_t s_server = NULL;
static SemaphoreHandle_t s_frame_mutex;
static SemaphoreHandle_t s_frame_ready;
static uint8_t *s_frame_buf = NULL;
static size_t s_frame_len = 0;
static size_t s_frame_cap = 0;
static uint32_t s_frame_seq = 0;

static const char INDEX_HTML[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>UVC Preview</title>"
    "<style>"
    "body{margin:0;background:#111;color:#eee;font-family:sans-serif;text-align:center}"
    "h1{font-size:1.2rem;margin:12px 0}"
    "img{max-width:100%;height:auto;background:#000}"
    ".hint{color:#888;font-size:0.85rem;margin:8px}"
    "</style></head><body>"
    "<h1>UVC Camera Preview</h1>"
    "<img src=\"/stream\" alt=\"stream\">"
    "<p class=\"hint\">MJPEG stream from USB camera</p>"
    "</body></html>";

void web_preview_push_frame(const uint8_t *data, size_t len)
{
    if (!data || len == 0 || !s_frame_mutex) {
        return;
    }
    if (len > FRAME_BUF_MAX) {
        ESP_LOGW(TAG, "Frame too large (%u), drop", (unsigned)len);
        return;
    }

    if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    if (len > s_frame_cap) {
#if CONFIG_SPIRAM
        uint8_t *new_buf = heap_caps_realloc(s_frame_buf, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!new_buf) {
            new_buf = realloc(s_frame_buf, len);
        }
#else
        uint8_t *new_buf = realloc(s_frame_buf, len);
#endif
        if (!new_buf) {
            xSemaphoreGive(s_frame_mutex);
            ESP_LOGW(TAG, "No mem for frame (%u)", (unsigned)len);
            return;
        }
        s_frame_buf = new_buf;
        s_frame_cap = len;
    }

    memcpy(s_frame_buf, data, len);
    s_frame_len = len;
    s_frame_seq++;
    xSemaphoreGive(s_frame_mutex);

    /* Wake stream handler (keep only latest notification) */
    xSemaphoreGive(s_frame_ready);
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_get_handler(httpd_req_t *req)
{
    esp_err_t res = ESP_OK;
    char part_hdr[128];
    uint32_t last_seq = 0;

    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" PART_BOUNDARY);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    while (true) {
        /* Wait for a new frame */
        if (xSemaphoreTake(s_frame_ready, pdMS_TO_TICKS(3000)) != pdTRUE) {
            continue;
        }

        if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        if (s_frame_len == 0 || s_frame_seq == last_seq) {
            xSemaphoreGive(s_frame_mutex);
            continue;
        }

        size_t len = s_frame_len;
        uint8_t *tmp = malloc(len);
        if (!tmp) {
            xSemaphoreGive(s_frame_mutex);
            ESP_LOGW(TAG, "Stream alloc fail");
            continue;
        }
        memcpy(tmp, s_frame_buf, len);
        last_seq = s_frame_seq;
        xSemaphoreGive(s_frame_mutex);

        int hdr_len = snprintf(part_hdr, sizeof(part_hdr),
                               "--" PART_BOUNDARY "\r\n"
                               "Content-Type: image/jpeg\r\n"
                               "Content-Length: %u\r\n\r\n",
                               (unsigned)len);

        res = httpd_resp_send_chunk(req, part_hdr, hdr_len);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)tmp, len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n", 2);
        }
        free(tmp);

        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Client disconnected from stream");
            break;
        }
    }

    /* Close chunked response */
    httpd_resp_send_chunk(req, NULL, 0);
    return res;
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;
    config.max_open_sockets = 4;
    config.stack_size = 8192;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd_start failed");

    const httpd_uri_t uri_index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
    };
    const httpd_uri_t uri_stream = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_get_handler,
    };

    httpd_register_uri_handler(s_server, &uri_index);
    httpd_register_uri_handler(s_server, &uri_stream);

    ESP_LOGI(TAG, "HTTP server started: http://192.168.4.1/");
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

static esp_err_t wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started. SSID:%s password:%s", WIFI_AP_SSID, WIFI_AP_PASS);
    ESP_LOGI(TAG, "Open http://192.168.4.1 in browser to preview");
    return ESP_OK;
}

esp_err_t web_preview_start(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs_flash_init failed");

    s_frame_mutex = xSemaphoreCreateMutex();
    s_frame_ready = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_frame_mutex && s_frame_ready, ESP_ERR_NO_MEM, TAG, "sem create failed");

    ESP_RETURN_ON_ERROR(wifi_init_softap(), TAG, "wifi softap failed");
    ESP_RETURN_ON_ERROR(start_http_server(), TAG, "http server failed");
    return ESP_OK;
}
