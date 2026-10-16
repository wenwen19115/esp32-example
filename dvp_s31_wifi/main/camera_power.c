#include "camera_power.h"

#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cam_power";

esp_err_t camera_power_on(tca9555_t *io, camera_power_state_t *st)
{
    if (io == NULL || !io->ready) {
        ESP_LOGE(TAG, "IO 扩展器不可用，无法打开摄像头电源");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = tca9555_config_output(io, CAM_EXP_PORT_PWR, CAM_EXP_BIT_PWR);
    if (err != ESP_OK) {
        return err;
    }
    err = tca9555_set_level(io, CAM_EXP_PORT_PWR, CAM_EXP_BIT_PWR, true);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(CAM_PWR_SETTLE_MS));

    bool level = false;
    tca9555_get_level(io, CAM_EXP_PORT_PWR, CAM_EXP_BIT_PWR, &level);
    if (!level) {
        ESP_LOGE(TAG, "P0.%d 置高后回读失败", CAM_EXP_BIT_PWR);
        return ESP_FAIL;
    }
    if (st) {
        st->power_on = true;
    }
    ESP_LOGI(TAG, "电源轨已使能: TCA9555 P0.%d -> IO_P5 (DVDD_1V8 / AVDD_2V8)", CAM_EXP_BIT_PWR);
    return ESP_OK;
}

esp_err_t camera_power_off(tca9555_t *io, camera_power_state_t *st)
{
    if (io == NULL || !io->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = tca9555_set_level(io, CAM_EXP_PORT_PWR, CAM_EXP_BIT_PWR, false);
    if (err == ESP_OK && st) {
        st->power_on = false;
    }
    ESP_LOGI(TAG, "电源轨已关闭: TCA9555 P0.%d", CAM_EXP_BIT_PWR);
    return err;
}

esp_err_t camera_reset_assert(tca9555_t *io, camera_power_state_t *st)
{
    if (io == NULL || !io->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = tca9555_config_output(io, CAM_EXP_PORT_RST, CAM_EXP_BIT_RST);
    if (err != ESP_OK) {
        return err;
    }
    /* DVP_RST 低有效 */
    err = tca9555_set_level(io, CAM_EXP_PORT_RST, CAM_EXP_BIT_RST, false);
    if (err == ESP_OK && st) {
        st->reset_held = true;
    }
    return err;
}

esp_err_t camera_reset_release(tca9555_t *io, camera_power_state_t *st)
{
    if (io == NULL || !io->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = tca9555_set_level(io, CAM_EXP_PORT_RST, CAM_EXP_BIT_RST, true);
    if (err == ESP_OK && st) {
        st->reset_held = false;
    }
    return err;
}

esp_err_t camera_reset_pulse(tca9555_t *io, camera_power_state_t *st)
{
    esp_err_t err = camera_reset_assert(io, st);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(CAM_RST_ASSERT_MS));
    err = camera_reset_release(io, st);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(CAM_RST_RELEASE_MS));
    ESP_LOGI(TAG, "复位完成: TCA9555 P1.%d -> IO_P12 -> DVP_RST (低 %dms / 释放后等 %dms)",
             CAM_EXP_BIT_RST, CAM_RST_ASSERT_MS, CAM_RST_RELEASE_MS);
    return ESP_OK;
}
