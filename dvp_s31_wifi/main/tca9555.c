#include "tca9555.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "tca9555";

static esp_err_t write_reg(tca9555_t *io, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(io->dev, buf, sizeof(buf), 100);
}

static esp_err_t read_reg(tca9555_t *io, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(io->dev, &reg, 1, val, 1, 100);
}

esp_err_t tca9555_init(tca9555_t *io, i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (io == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(io, 0, sizeof(*io));
    io->bus = bus;
    io->addr = addr;

    esp_err_t err = i2c_master_probe(bus, addr, 200);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "0x%02X 无应答 (%s)：IO 扩展器未焊接或总线异常", addr, esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &io->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "挂载设备失败: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t cfg0 = 0, cfg1 = 0, out0 = 0, out1 = 0;
    if (read_reg(io, TCA9555_REG_CONFIG0, &cfg0) != ESP_OK ||
        read_reg(io, TCA9555_REG_CONFIG1, &cfg1) != ESP_OK ||
        read_reg(io, TCA9555_REG_OUTPUT0, &out0) != ESP_OK ||
        read_reg(io, TCA9555_REG_OUTPUT1, &out1) != ESP_OK) {
        ESP_LOGE(TAG, "读取寄存器失败");
        return ESP_FAIL;
    }
    io->config = (uint16_t)cfg0 | ((uint16_t)cfg1 << 8);
    io->outputs = (uint16_t)out0 | ((uint16_t)out1 << 8);
    io->ready = true;
    ESP_LOGI(TAG, "TCA9555 @0x%02X 就绪: CONFIG=0x%04X OUTPUT=0x%04X", addr, io->config, io->outputs);
    return ESP_OK;
}

esp_err_t tca9555_config_output(tca9555_t *io, int port, int bit)
{
    if (!io->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    io->config &= (uint16_t) ~(1u << (port * 8 + bit));
    esp_err_t err = write_reg(io, port == 0 ? TCA9555_REG_CONFIG0 : TCA9555_REG_CONFIG1,
                              (uint8_t)(io->config >> (port * 8)));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "配置 P%d.%d 为输出失败: %s", port, bit, esp_err_to_name(err));
    }
    return err;
}

esp_err_t tca9555_set_level(tca9555_t *io, int port, int bit, bool level)
{
    if (!io->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const int idx = port * 8 + bit;
    if (level) {
        io->outputs |= (uint16_t)(1u << idx);
    } else {
        io->outputs &= (uint16_t) ~(1u << idx);
    }
    esp_err_t err = write_reg(io, port == 0 ? TCA9555_REG_OUTPUT0 : TCA9555_REG_OUTPUT1,
                              (uint8_t)(io->outputs >> (port * 8)));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写 P%d.%d=%d 失败: %s", port, bit, level, esp_err_to_name(err));
    }
    return err;
}

esp_err_t tca9555_get_level(tca9555_t *io, int port, int bit, bool *level)
{
    if (!io->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t v = 0;
    esp_err_t err = read_reg(io, port == 0 ? TCA9555_REG_OUTPUT0 : TCA9555_REG_OUTPUT1, &v);
    if (err == ESP_OK && level != NULL) {
        *level = (v >> bit) & 0x1;
    }
    return err;
}

esp_err_t tca9555_read_inputs(tca9555_t *io, uint16_t *inputs)
{
    if (!io->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t in0 = 0, in1 = 0;
    esp_err_t err = read_reg(io, TCA9555_REG_INPUT0, &in0);
    if (err == ESP_OK) {
        err = read_reg(io, TCA9555_REG_INPUT1, &in1);
    }
    if (err == ESP_OK && inputs != NULL) {
        *inputs = (uint16_t)in0 | ((uint16_t)in1 << 8);
    }
    return err;
}
