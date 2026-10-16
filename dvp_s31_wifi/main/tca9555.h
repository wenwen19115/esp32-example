/**
 * @file tca9555.h
 * @brief TCA9555 16 位 I2C IO 扩展器最小驱动（本板 U7 @0x20）
 *
 * 本板用途：
 *   P0.5 (IO_P5)  -> 摄像头电源轨使能（高有效，驱动 LU2=1.8V / LU3=2.8V LDO 的 EN）
 *   P1.2 (IO_P12) -> 摄像头复位 DVP_RST（经 0R 电阻 SXTR1，低有效）
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TCA9555 寄存器 */
#define TCA9555_REG_INPUT0      0x00
#define TCA9555_REG_INPUT1      0x01
#define TCA9555_REG_OUTPUT0     0x02
#define TCA9555_REG_OUTPUT1     0x03
#define TCA9555_REG_POLARITY0   0x04
#define TCA9555_REG_POLARITY1   0x05
#define TCA9555_REG_CONFIG0     0x06
#define TCA9555_REG_CONFIG1     0x07

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    uint8_t addr;
    uint16_t outputs;   /* 影子寄存器：当前输出锁存值 */
    uint16_t config;    /* 影子寄存器：0=输出, 1=输入 */
    bool ready;
} tca9555_t;

/**
 * @brief 挂载并探测 TCA9555（不改变任何输出，仅读取影子寄存器）
 */
esp_err_t tca9555_init(tca9555_t *io, i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 把某个位配置为输出（config bit = 0）
 */
esp_err_t tca9555_config_output(tca9555_t *io, int port, int bit);

/**
 * @brief 设置输出电平（读-改-写影子寄存器后整端口下发，幂等）
 */
esp_err_t tca9555_set_level(tca9555_t *io, int port, int bit, bool level);

/**
 * @brief 读取当前输出锁存器（用于确认写入是否生效）
 */
esp_err_t tca9555_get_level(tca9555_t *io, int port, int bit, bool *level);

/**
 * @brief 读取输入端口（TCA9555 输入寄存器反映引脚真实电平）
 */
esp_err_t tca9555_read_inputs(tca9555_t *io, uint16_t *inputs);

#ifdef __cplusplus
}
#endif
