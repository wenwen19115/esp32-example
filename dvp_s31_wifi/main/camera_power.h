/**
 * @file camera_power.h
 * @brief 摄像头电源 / 复位时序控制（TCA9555 P0.5 + P1.2）
 */
#pragma once

#include "esp_err.h"
#include "tca9555.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool power_on;      /* 电源轨是否已使能 */
    bool reset_held;    /* 复位是否处于有效（低）状态 */
} camera_power_state_t;

/**
 * @brief 打开摄像头电源轨（TCA9555 P0.5 = 1 -> IO_P5 -> LU2/LU3 EN）
 *
 * 时序：置位 -> 等待 CAM_PWR_SETTLE_MS -> 可选读回确认。
 * 幂等：板上其它固件可能已经打开该轨，重复调用不会出错。
 */
esp_err_t camera_power_on(tca9555_t *io, camera_power_state_t *st);

/**
 * @brief 关闭摄像头电源轨（P0.5 = 0）
 */
esp_err_t camera_power_off(tca9555_t *io, camera_power_state_t *st);

/**
 * @brief 复位脉冲：拉低 CAM_RST_ASSERT_MS -> 释放 -> 等待 CAM_RST_RELEASE_MS
 *
 * 注意：DVP_RST 由扩展器驱动，与 SCCB 共用 I2C1，因此必须在
 * esp_video / sensor 驱动接管总线之前完成。
 */
esp_err_t camera_reset_pulse(tca9555_t *io, camera_power_state_t *st);

/**
 * @brief 拉低复位（保持）
 */
esp_err_t camera_reset_assert(tca9555_t *io, camera_power_state_t *st);

/**
 * @brief 释放复位
 */
esp_err_t camera_reset_release(tca9555_t *io, camera_power_state_t *st);

#ifdef __cplusplus
}
#endif
