#include "rg_system.h"
#include "rg_i2c.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM) && defined(RG_GPIO_I2C_SDA) && defined(RG_GPIO_I2C_SCL)
#include <esp_idf_version.h>
#include <esp_err.h>
// The legacy driver cannot be mixed with the new one (used by esp_lcd_touch, esp_codec_dev, ...) so targets that need
// those, and esp-idf 6.0+ (where the legacy driver is deprecated), use the new master driver.
#if defined(RG_I2C_USE_MASTER_DRIVER) || ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#include <driver/i2c_master.h>
#define USE_I2C_MASTER_DRIVER 1
#else
#include <driver/i2c.h>
#endif
#define USE_I2C_DRIVER 1
#endif

static bool i2c_initialized = false;

#ifdef USE_I2C_MASTER_DRIVER

#define I2C_SPEED_HZ 400000
#define I2C_TIMEOUT_MS 500
#define I2C_MAX_DEVICES 8

static i2c_master_bus_handle_t i2c_bus;
static struct {uint8_t addr; i2c_master_dev_handle_t handle;} i2c_devices[I2C_MAX_DEVICES];
static int i2c_devices_count;

// The master driver needs a handle per device, they are created on first use
static i2c_master_dev_handle_t get_device(uint8_t addr)
{
    for (int i = 0; i < i2c_devices_count; ++i)
    {
        if (i2c_devices[i].addr == addr)
            return i2c_devices[i].handle;
    }
    if (i2c_devices_count >= I2C_MAX_DEVICES)
        return NULL;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    i2c_master_dev_handle_t handle;
    if (i2c_master_bus_add_device(i2c_bus, &config, &handle) != ESP_OK)
        return NULL;
    i2c_devices[i2c_devices_count].addr = addr;
    i2c_devices[i2c_devices_count].handle = handle;
    i2c_devices_count++;
    return handle;
}

void *rg_i2c_get_bus_handle(void)
{
    return i2c_initialized ? i2c_bus : NULL;
}

bool rg_i2c_init(void)
{
    if (i2c_initialized)
        return true;
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = RG_GPIO_I2C_SDA,
        .scl_io_num = RG_GPIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&config, &i2c_bus);
    if (err != ESP_OK)
    {
        RG_LOGE("Failed to initialize I2C driver. err=0x%x\n", err);
        return false;
    }
    RG_LOGI("I2C driver ready (SDA:%d SCL:%d).\n", RG_GPIO_I2C_SDA, RG_GPIO_I2C_SCL);
    i2c_initialized = true;
    return true;
}

bool rg_i2c_deinit(void)
{
    if (!i2c_initialized)
        return true;
    for (int i = 0; i < i2c_devices_count; ++i)
        i2c_master_bus_rm_device(i2c_devices[i].handle);
    i2c_devices_count = 0;
    if (i2c_del_master_bus(i2c_bus) == ESP_OK)
        RG_LOGI("I2C driver terminated.\n");
    i2c_initialized = false;
    return true;
}

bool rg_i2c_read(uint8_t addr, int reg, void *read_data, size_t read_len)
{
    i2c_master_dev_handle_t dev = i2c_initialized ? get_device(addr) : NULL;
    esp_err_t err = ESP_FAIL;
    if (dev)
    {
        if (reg >= 0)
        {
            const uint8_t reg8 = (uint8_t)reg;
            err = i2c_master_transmit_receive(dev, &reg8, 1, read_data, read_len, I2C_TIMEOUT_MS);
        }
        else
        {
            err = i2c_master_receive(dev, read_data, read_len, I2C_TIMEOUT_MS);
        }
    }
    if (err != ESP_OK)
        RG_LOGE("Read from 0x%02X failed. reg=0x%02X, err=0x%03X, init=%d\n", addr, reg, err, i2c_initialized);
    return err == ESP_OK;
}

bool rg_i2c_write(uint8_t addr, int reg, const void *write_data, size_t write_len)
{
    i2c_master_dev_handle_t dev = i2c_initialized ? get_device(addr) : NULL;
    esp_err_t err = ESP_FAIL;
    if (dev)
    {
        // The register is sent in the same transaction, as the first byte
        size_t offset = (reg >= 0) ? 1 : 0;
        uint8_t stack_buffer[32];
        uint8_t *buffer = write_len + offset <= sizeof(stack_buffer) ? stack_buffer : malloc(write_len + offset);
        if (buffer)
        {
            if (offset)
                buffer[0] = (uint8_t)reg;
            if (write_len)
                memcpy(buffer + offset, write_data, write_len);
            err = i2c_master_transmit(dev, buffer, write_len + offset, I2C_TIMEOUT_MS);
            if (buffer != stack_buffer)
                free(buffer);
        }
    }
    if (err != ESP_OK)
        RG_LOGE("Write to 0x%02X failed. reg=0x%02X, err=0x%03X, init=%d\n", addr, reg, err, i2c_initialized);
    return err == ESP_OK;
}

#else // legacy driver

void *rg_i2c_get_bus_handle(void)
{
    return NULL;
}

#define TRY(x)                 \
    if ((err = (x)) != ESP_OK) \
    {                          \
        goto fail;             \
    }


bool rg_i2c_init(void)
{
#if USE_I2C_DRIVER
    const i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = RG_GPIO_I2C_SDA,
        .scl_io_num = RG_GPIO_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t err = ESP_FAIL;

    if (i2c_initialized)
        return true;

    TRY(i2c_param_config(I2C_NUM_0, &i2c_config));
    TRY(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    RG_LOGI("I2C driver ready (SDA:%d SCL:%d).\n", i2c_config.sda_io_num, i2c_config.scl_io_num);
    i2c_initialized = true;
    return true;
fail:
    RG_LOGE("Failed to initialize I2C driver. err=0x%x\n", err);
#else
    RG_LOGE("I2C is not available on this device.\n");
#endif
    i2c_initialized = false;
    return false;
}

bool rg_i2c_deinit(void)
{
#if USE_I2C_DRIVER
    if (i2c_initialized && i2c_driver_delete(I2C_NUM_0) == ESP_OK)
        RG_LOGI("I2C driver terminated.\n");
#endif
    i2c_initialized = false;
    return true;
}

bool rg_i2c_read(uint8_t addr, int reg, void *read_data, size_t read_len)
{
#if USE_I2C_DRIVER
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t err = ESP_FAIL;

    if (!cmd || !i2c_initialized)
        goto fail;

    if (reg >= 0)
    {
        TRY(i2c_master_start(cmd));
        TRY(i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true));
        TRY(i2c_master_write_byte(cmd, (uint8_t)reg, true));
    }
    TRY(i2c_master_start(cmd));
    TRY(i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true));
    TRY(i2c_master_read(cmd, read_data, read_len, I2C_MASTER_LAST_NACK));
    TRY(i2c_master_stop(cmd));
    TRY(i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(500)));
    i2c_cmd_link_delete(cmd);
    return true;
fail:
    i2c_cmd_link_delete(cmd);
    RG_LOGE("Read from 0x%02X failed. reg=0x%02X, err=0x%03X, init=%d\n", addr, reg, err, i2c_initialized);
#endif
    return false;
}

bool rg_i2c_write(uint8_t addr, int reg, const void *write_data, size_t write_len)
{
#if USE_I2C_DRIVER
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t err = ESP_FAIL;

    if (!cmd || !i2c_initialized)
        goto fail;

    TRY(i2c_master_start(cmd));
    TRY(i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true));
    if (reg >= 0)
    {
        TRY(i2c_master_write_byte(cmd, (uint8_t)reg, true));
    }
    TRY(i2c_master_write(cmd, (void *)write_data, write_len, true));
    TRY(i2c_master_stop(cmd));
    TRY(i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(500)));
    i2c_cmd_link_delete(cmd);
    return true;
fail:
    i2c_cmd_link_delete(cmd);
    RG_LOGE("Write to 0x%02X failed. reg=0x%02X, err=0x%03X, init=%d\n", addr, reg, err, i2c_initialized);
#endif
    return false;
}

#endif // USE_I2C_MASTER_DRIVER

int rg_i2c_read_byte(uint8_t addr, uint8_t reg)
{
    uint8_t value;
    return rg_i2c_read(addr, reg, &value, 1) ? value : -1;
}

bool rg_i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value)
{
    return rg_i2c_write(addr, reg, &value, 1);
}


#ifdef RG_I2C_GPIO_DRIVER

typedef struct {int input_reg, output_reg, direction_reg, pullup_reg;} _gpio_port;
typedef struct {uint8_t reg, value;} _gpio_sequence;

#if RG_I2C_GPIO_DRIVER == 1 // AW9523

static const _gpio_port gpio_ports[] = {
    {0x00, 0x02, 0x04, -1}, // PORT 0
    {0x01, 0x03, 0x05, -1}, // PORT 1
};
static const _gpio_sequence gpio_init_seq[] = {
    {0x7F, 0x00  }, // Software reset (is it really necessary?)
    {0x11, 1 << 4}, // Push-Pull mode
};
static const _gpio_sequence gpio_deinit_seq[] = {};

#elif RG_I2C_GPIO_DRIVER == 2 // PCF9539

static const _gpio_port gpio_ports[] = {
    {0x00, 0x02, 0x06, -1}, // PORT 0
    {0x01, 0x03, 0x07, -1}, // PORT 1
};
static const _gpio_sequence gpio_init_seq[] = {};
static const _gpio_sequence gpio_deinit_seq[] = {};

#elif RG_I2C_GPIO_DRIVER == 3 // MCP23017

// Mappings when IOCON.BANK = 0 (which should be default on power-on)
static const _gpio_port gpio_ports[] = {
    {0x12, 0x14, 0x00, 0x0C}, // PORT A
    {0x13, 0x15, 0x01, 0x0D}, // PORT B
};
static const _gpio_sequence gpio_init_seq[] = {};
static const _gpio_sequence gpio_deinit_seq[] = {};

#elif RG_I2C_GPIO_DRIVER == 4 // PCF8575

static const _gpio_port gpio_ports[] = {
    {-1, -1, -1, -1}, // PORT 0
    {-1, -1, -1, -1}, // PORT 1
};
static const _gpio_sequence gpio_init_seq[] = {};
static const _gpio_sequence gpio_deinit_seq[] = {};
static rg_gpio_mode_t PCF857x_mode = -1;

#elif RG_I2C_GPIO_DRIVER == 5 // PCF8574

static const _gpio_port gpio_ports[] = {
    {-1, -1, -1, -1}, // PORT 0
};
static const _gpio_sequence gpio_init_seq[] = {};
static const _gpio_sequence gpio_deinit_seq[] = {};
static rg_gpio_mode_t PCF857x_mode = -1;

#else

#error "Unknown I2C GPIO Extender driver type!"

#endif

static const size_t gpio_ports_count = RG_COUNT(gpio_ports);
static uint8_t gpio_output_values[RG_COUNT(gpio_ports)];
static uint8_t gpio_address = RG_I2C_GPIO_ADDR;
static bool gpio_initialized = false;


bool rg_i2c_gpio_init(void)
{
    if (gpio_initialized)
        return true;

    if (!i2c_initialized && !rg_i2c_init())
        return false;

    // Configure extender-specific registers if needed (disable open-drain, interrupts, inversion, etc)
    for (size_t i = 0; (int)i < (int)RG_COUNT(gpio_init_seq); ++i)
    {
        if (!rg_i2c_write_byte(gpio_address, gpio_init_seq[i].reg, gpio_init_seq[i].value))
            goto fail;
    }

    // Set all pins as inputs and clear output latches
    for (size_t i = 0; i < gpio_ports_count; ++i)
    {
        if (!rg_i2c_gpio_configure_port(i, 0xFF, RG_GPIO_INPUT))
            goto fail;
        if (!rg_i2c_gpio_write_port(i, 0x00))
            goto fail;
    }

    RG_LOGI("GPIO Extender ready (driver:%d, addr:0x%02X).", RG_I2C_GPIO_DRIVER, gpio_address);
    gpio_initialized = true;
    return true;
fail:
    RG_LOGE("Failed to initialize extender (driver:%d, addr:0x%02X).", RG_I2C_GPIO_DRIVER, gpio_address);
    gpio_initialized = false;
    return false;
}

bool rg_i2c_gpio_deinit(void)
{
    if (gpio_initialized)
    {
        for (size_t i = 0; (int)i < (int)RG_COUNT(gpio_deinit_seq); ++i)
            rg_i2c_write_byte(gpio_address, gpio_deinit_seq[i].reg, gpio_deinit_seq[i].value);
        // Should we reset all pins to be high impedance?
        gpio_initialized = false;
    }
    return true;
}

static bool update_register(int reg, uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t value;
    return rg_i2c_read(gpio_address, reg, &value, 1) &&
           rg_i2c_write_byte(gpio_address, reg, (value & ~clear_mask) | set_mask);
}

bool rg_i2c_gpio_configure_port(int port, uint8_t mask, rg_gpio_mode_t mode)
{
    if (port < 0 || port >= gpio_ports_count)
        return false;
#if RG_I2C_GPIO_DRIVER == 4 || RG_I2C_GPIO_DRIVER == 5 // PCF8575/PCF8574
    uint32_t temp = 0xFFFFFFFF;
    if (mask != 0xFF && mode != PCF857x_mode && (mode == RG_GPIO_OUTPUT || PCF857x_mode == RG_GPIO_OUTPUT))
        RG_LOGW("PCF857x mode cannot be set by pin. (mask is 0x%02X, expected 0xFF)", mask);
    PCF857x_mode = mode;
    if (mode != RG_GPIO_OUTPUT)
        return rg_i2c_write(gpio_address, -1, &temp, gpio_ports_count)
            && rg_i2c_read(gpio_address, -1, &temp, gpio_ports_count);
    return rg_i2c_write(gpio_address, -1, &gpio_output_values, gpio_ports_count);
#else
    int direction_reg = gpio_ports[port].direction_reg;
    int pullup_reg = gpio_ports[port].pullup_reg;
    if (pullup_reg != -1 && !update_register(pullup_reg, mask, mode == RG_GPIO_INPUT_PULLUP ? mask : 0))
        return false;
    return update_register(direction_reg, mask, mode != RG_GPIO_OUTPUT ? mask : 0);
#endif
}

int rg_i2c_gpio_read_port(int port)
{
    if (port < 0 || port >= gpio_ports_count)
        return -1;
#if RG_I2C_GPIO_DRIVER == 4 || RG_I2C_GPIO_DRIVER == 5 // PCF8575/PCF8574
    uint8_t values[gpio_ports_count];
    return rg_i2c_read(gpio_address, -1, &values, gpio_ports_count) ? values[port] : -1;
#else
    return rg_i2c_read_byte(gpio_address, gpio_ports[port].input_reg);
#endif
}

bool rg_i2c_gpio_write_port(int port, uint8_t value)
{
    if (port < 0 || port >= gpio_ports_count)
        return false;
    gpio_output_values[port] = value;
#if RG_I2C_GPIO_DRIVER == 4 || RG_I2C_GPIO_DRIVER == 5 // PCF8575/PCF8574
    if (PCF857x_mode != RG_GPIO_OUTPUT)
        return true; // This is consistent with other extenders, where the output latch is updated even in input mode
    return rg_i2c_write(gpio_address, -1, &gpio_output_values, gpio_ports_count);
#else
    return rg_i2c_write_byte(gpio_address, gpio_ports[port].output_reg, value);
#endif
}

bool rg_i2c_gpio_set_direction(int pin, rg_gpio_mode_t mode)
{
    return rg_i2c_gpio_configure_port(pin >> 3, 1 << (pin & 7), mode);
}

int rg_i2c_gpio_get_level(int pin)
{
    return (rg_i2c_gpio_read_port(pin >> 3) >> (pin & 7)) & 1;
}

bool rg_i2c_gpio_set_level(int pin, int level)
{
    uint8_t port = (pin >> 3) % gpio_ports_count, mask = 1 << (pin & 7);
    uint8_t value = (gpio_output_values[port] & ~mask) | (level ? mask : 0);
    return rg_i2c_gpio_write_port(port, value);
}
#endif
