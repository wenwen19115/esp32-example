/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>

#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "usb/usb_host.h"
#include "usb/uvc_host.h"

#include "web_preview.h"

#define EXAMPLE_USB_HOST_PRIORITY   (15)

// Private function prototypes
static bool frame_callback(const uvc_host_frame_t *frame, void *user_ctx);
static void stream_callback(const uvc_host_stream_event_data_t *event, void *user_ctx);

// Private variables
static QueueHandle_t rx_frames_queue;
static bool dev_connected = false;
static const char *TAG = "UVC example";

/**
 * @brief Configuration of the UVC stream
 *
 * Update the vs_format to match your camera's capabilities.
 * The example will try to open the camera with the specified resolution and FPS.
 */
static const uvc_host_stream_config_t stream_config = {
    .event_cb = stream_callback,
    .frame_cb = frame_callback,
    .user_ctx = &rx_frames_queue,
    .usb = {
        .vid = UVC_HOST_ANY_VID, // Set to 0 to match any VID
        .pid = UVC_HOST_ANY_PID, // Set to 0 to match any PID
        .uvc_stream_index = 0,   /* Index of UVC function you want to use. Set to 0 to use first available UVC function.
                                    Setting this to >= 1 will only work if the camera has multiple UVC functions (eg. multiple image sensors in one USB device) */
    },
    .vs_format = {
        // Note: For Full-Speed targets, only limited set of formats might be supported by the camera.
        // Check the camera's Configuration descriptor for supported formats and resolutions.
#if CONFIG_SPIRAM
        .h_res = 640,
        .v_res = 480,
#else
        .h_res = 320,
        .v_res = 240,
#endif
        .fps = 15,
        .format = UVC_VS_FORMAT_MJPEG,
    },
    .advanced = {
#if CONFIG_SPIRAM
        .frame_size = 0,              /* Use camera-reported max frame size when PSRAM is available */
        .number_of_frame_buffers = 3,
        .number_of_urbs = 3,
        .urb_size = 10 * 1024,
        .frame_heap_caps = MALLOC_CAP_SPIRAM,
#else
        /* Cap buffer size so UVC + Wi-Fi SoftAP fit in internal RAM */
        .frame_size = 25 * 1024,
        .number_of_frame_buffers = 2,
        .number_of_urbs = 2,
        .urb_size = 2 * 1024,
#endif
    },
};

static bool frame_callback(const uvc_host_frame_t *frame, void *user_ctx)
{
    assert(frame);
    assert(user_ctx);
    QueueHandle_t frame_q = *((QueueHandle_t *)user_ctx);

    // Send the received frame to queue for further processing
    ESP_LOGD(TAG, "Frame callback! data len: %d", frame->data_len);
    BaseType_t result = xQueueSendToBack(frame_q, &frame, 0);
    if (pdPASS != result) {
        ESP_LOGW(TAG, "Queue full, losing frame"); // This should never happen
        return true; // We will not process this frame, return it immediately
    }
    return false; // We only passed the frame to Queue, so we must return false and call uvc_host_frame_return() later
}

static void stream_callback(const uvc_host_stream_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
    case UVC_HOST_TRANSFER_ERROR:
        ESP_LOGE(TAG, "USB error has occurred, err_no = %i", event->transfer_error.error);
        break;
    case UVC_HOST_DEVICE_DISCONNECTED:
        ESP_LOGI(TAG, "Device suddenly disconnected");
        dev_connected = false;
        ESP_ERROR_CHECK(uvc_host_stream_close(event->device_disconnected.stream_hdl));
        break;
    case UVC_HOST_FRAME_BUFFER_OVERFLOW:
        // The Frame was discarded because it exceeded the available frame buffer size.
        // To resolve this, increase the `frame_size` parameter in `uvc_host_stream_config_t.advanced` to allocate a larger buffer.
        ESP_LOGW(TAG, "Frame buffer overflow");
        break;
    case UVC_HOST_FRAME_BUFFER_UNDERFLOW:
        // The Frame was discarded because no available buffer was free for storage.
        // To resolve this, either optimize your processing speed or increase the `number_of_frame_buffers` parameter in
        // `uvc_host_stream_config_t.advanced` to allocate additional buffers.
        ESP_LOGW(TAG, "Frame buffer underflow");
        break;
    default:
        ESP_LOGW(TAG, "Unsupported UVC event: %d (possibly suspend/resume)", event->type);
        break;
    }
}

static void usb_lib_task(void *arg)
{
    while (1) {
        // Start handling system events
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "USB: All devices freed");
            // Continue handling USB events to allow device reconnection
        }
    }
}

static void frame_handling_task(void *arg)
{
    const uvc_host_stream_config_t *stream_config = (const uvc_host_stream_config_t *)arg;
    QueueHandle_t frame_q = *((QueueHandle_t *)(stream_config->user_ctx));

    // This is the main program loop, it tries to open the UVC device and start streaming
    while (true) {
        uvc_host_stream_hdl_t uvc_stream = NULL;

        ESP_LOGI(TAG, "Opening UVC device 0x%04X:0x%04X %dx%d@%2.1fFPS...",
                 stream_config->usb.vid, stream_config->usb.pid, stream_config->vs_format.h_res, stream_config->vs_format.v_res, stream_config->vs_format.fps);
        esp_err_t err = uvc_host_stream_open(stream_config, pdMS_TO_TICKS(5000), &uvc_stream);
        if (ESP_OK != err) {
            ESP_LOGI(TAG, "Failed to open device");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        dev_connected = true;
        ESP_LOGI(TAG, "UVC Device OPENED!");
        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_LOGI(TAG, "Stream start (continuous preview)");
        uvc_host_stream_start(uvc_stream);

        while (dev_connected) {
            uvc_host_frame_t *frame;
            if (xQueueReceive(frame_q, &frame, pdMS_TO_TICKS(5000)) == pdPASS) {
                ESP_LOGD(TAG, "New frame! Len: %d", frame->data_len);
                web_preview_push_frame(frame->data, frame->data_len);
                uvc_host_frame_return(uvc_stream, frame);
            } else {
                ESP_LOGW(TAG, "Stream: Frame not received on time");
            }
        }

        ESP_LOGI(TAG, "Device disconnected, reopening...");
    }
}

/**
 * @brief Main application
 */
void app_main(void)
{
    ESP_ERROR_CHECK(web_preview_start());

    rx_frames_queue = xQueueCreate(3, sizeof(uvc_host_frame_t *));
    assert(rx_frames_queue);

    // Install USB Host driver. Should only be called once in entire application
    ESP_LOGI(TAG, "Installing USB Host");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Create a task that will handle USB library events
    BaseType_t task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, EXAMPLE_USB_HOST_PRIORITY, NULL, tskNO_AFFINITY);
    assert(task_created == pdTRUE);

    ESP_LOGI(TAG, "Installing UVC driver");
    const uvc_host_driver_config_t uvc_driver_config = {
        .driver_task_stack_size = 4 * 1024,
        .driver_task_priority = EXAMPLE_USB_HOST_PRIORITY + 1,
        .xCoreID = tskNO_AFFINITY,
        .create_background_task = true,
    };
    ESP_ERROR_CHECK(uvc_host_install(&uvc_driver_config));

    task_created = xTaskCreatePinnedToCore(frame_handling_task, "frame_hdl", 4096, (void *)&stream_config, EXAMPLE_USB_HOST_PRIORITY - 2, NULL, tskNO_AFFINITY);
    assert(task_created == pdTRUE);
}
