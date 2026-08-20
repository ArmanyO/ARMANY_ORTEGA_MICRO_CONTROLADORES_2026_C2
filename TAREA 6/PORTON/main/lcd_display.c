#include "lcd_display.h"

#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "lcd";
static uint8_t s_addr = 0x3C;

#define I2C_PORT I2C_NUM_0
#define LCD_WIDTH 128
#define LCD_PAGES 8

const char *porton_state_name(porton_state_t state)
{
    switch (state) {
    case PORTON_INIT: return "INIT";
    case PORTON_CALIBRATION: return "CALIBRACION";
    case PORTON_CLOSED: return "CERRADO";
    case PORTON_OPENING: return "ABRIENDO";
    case PORTON_OPEN: return "ABIERTO";
    case PORTON_CLOSING: return "CERRANDO";
    case PORTON_STOPPED: return "DETENIDO";
    case PORTON_OBSTRUCTED: return "OBSTRUIDO";
    case PORTON_FAULT: return "FALLA";
    default: return "DESCONOCIDO";
    }
}

static esp_err_t lcd_write_bytes(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static void lcd_command(uint8_t command)
{
    uint8_t data[2] = {0x00, command};
    lcd_write_bytes(data, sizeof(data));
}

static void lcd_data(const uint8_t *bytes, size_t len)
{
    uint8_t buffer[17] = {0};
    buffer[0] = 0x40;

    while (len > 0) {
        size_t chunk = len > 16 ? 16 : len;
        memcpy(&buffer[1], bytes, chunk);
        lcd_write_bytes(buffer, chunk + 1);
        bytes += chunk;
        len -= chunk;
    }
}

static void lcd_set_cursor(uint8_t page, uint8_t col)
{
    lcd_command(0xB0 | (page & 0x07));
    lcd_command(0x00 | (col & 0x0F));
    lcd_command(0x10 | (col >> 4));
}

static void lcd_clear(void)
{
    uint8_t blank[LCD_WIDTH] = {0};
    for (uint8_t page = 0; page < LCD_PAGES; page++) {
        lcd_set_cursor(page, 0);
        lcd_data(blank, sizeof(blank));
    }
}

static void glyph_for_char(char c, uint8_t out[5])
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    const uint8_t *g = NULL;
    static const uint8_t sp[5] = {0x00,0x00,0x00,0x00,0x00};
    static const uint8_t q[5]  = {0x02,0x01,0x51,0x09,0x06};

    switch (c) {
    case '0': { static const uint8_t v[5] = {0x3E,0x51,0x49,0x45,0x3E}; g = v; break; }
    case '1': { static const uint8_t v[5] = {0x00,0x42,0x7F,0x40,0x00}; g = v; break; }
    case '2': { static const uint8_t v[5] = {0x42,0x61,0x51,0x49,0x46}; g = v; break; }
    case '3': { static const uint8_t v[5] = {0x21,0x41,0x45,0x4B,0x31}; g = v; break; }
    case '4': { static const uint8_t v[5] = {0x18,0x14,0x12,0x7F,0x10}; g = v; break; }
    case '5': { static const uint8_t v[5] = {0x27,0x45,0x45,0x45,0x39}; g = v; break; }
    case '6': { static const uint8_t v[5] = {0x3C,0x4A,0x49,0x49,0x30}; g = v; break; }
    case '7': { static const uint8_t v[5] = {0x01,0x71,0x09,0x05,0x03}; g = v; break; }
    case '8': { static const uint8_t v[5] = {0x36,0x49,0x49,0x49,0x36}; g = v; break; }
    case '9': { static const uint8_t v[5] = {0x06,0x49,0x49,0x29,0x1E}; g = v; break; }
    case 'A': { static const uint8_t v[5] = {0x7E,0x11,0x11,0x11,0x7E}; g = v; break; }
    case 'B': { static const uint8_t v[5] = {0x7F,0x49,0x49,0x49,0x36}; g = v; break; }
    case 'C': { static const uint8_t v[5] = {0x3E,0x41,0x41,0x41,0x22}; g = v; break; }
    case 'D': { static const uint8_t v[5] = {0x7F,0x41,0x41,0x22,0x1C}; g = v; break; }
    case 'E': { static const uint8_t v[5] = {0x7F,0x49,0x49,0x49,0x41}; g = v; break; }
    case 'F': { static const uint8_t v[5] = {0x7F,0x09,0x09,0x09,0x01}; g = v; break; }
    case 'G': { static const uint8_t v[5] = {0x3E,0x41,0x49,0x49,0x7A}; g = v; break; }
    case 'H': { static const uint8_t v[5] = {0x7F,0x08,0x08,0x08,0x7F}; g = v; break; }
    case 'I': { static const uint8_t v[5] = {0x00,0x41,0x7F,0x41,0x00}; g = v; break; }
    case 'J': { static const uint8_t v[5] = {0x20,0x40,0x41,0x3F,0x01}; g = v; break; }
    case 'K': { static const uint8_t v[5] = {0x7F,0x08,0x14,0x22,0x41}; g = v; break; }
    case 'L': { static const uint8_t v[5] = {0x7F,0x40,0x40,0x40,0x40}; g = v; break; }
    case 'M': { static const uint8_t v[5] = {0x7F,0x02,0x0C,0x02,0x7F}; g = v; break; }
    case 'N': { static const uint8_t v[5] = {0x7F,0x04,0x08,0x10,0x7F}; g = v; break; }
    case 'O': { static const uint8_t v[5] = {0x3E,0x41,0x41,0x41,0x3E}; g = v; break; }
    case 'P': { static const uint8_t v[5] = {0x7F,0x09,0x09,0x09,0x06}; g = v; break; }
    case 'Q': { static const uint8_t v[5] = {0x3E,0x41,0x51,0x21,0x5E}; g = v; break; }
    case 'R': { static const uint8_t v[5] = {0x7F,0x09,0x19,0x29,0x46}; g = v; break; }
    case 'S': { static const uint8_t v[5] = {0x46,0x49,0x49,0x49,0x31}; g = v; break; }
    case 'T': { static const uint8_t v[5] = {0x01,0x01,0x7F,0x01,0x01}; g = v; break; }
    case 'U': { static const uint8_t v[5] = {0x3F,0x40,0x40,0x40,0x3F}; g = v; break; }
    case 'V': { static const uint8_t v[5] = {0x1F,0x20,0x40,0x20,0x1F}; g = v; break; }
    case 'W': { static const uint8_t v[5] = {0x3F,0x40,0x38,0x40,0x3F}; g = v; break; }
    case 'X': { static const uint8_t v[5] = {0x63,0x14,0x08,0x14,0x63}; g = v; break; }
    case 'Y': { static const uint8_t v[5] = {0x07,0x08,0x70,0x08,0x07}; g = v; break; }
    case 'Z': { static const uint8_t v[5] = {0x61,0x51,0x49,0x45,0x43}; g = v; break; }
    case ':': { static const uint8_t v[5] = {0x00,0x36,0x36,0x00,0x00}; g = v; break; }
    case '-': { static const uint8_t v[5] = {0x08,0x08,0x08,0x08,0x08}; g = v; break; }
    case '/': { static const uint8_t v[5] = {0x20,0x10,0x08,0x04,0x02}; g = v; break; }
    case '.': { static const uint8_t v[5] = {0x00,0x60,0x60,0x00,0x00}; g = v; break; }
    case ' ': g = sp; break;
    default: g = q; break;
    }

    memcpy(out, g, 5);
}

static void lcd_write_line(uint8_t page, const char *text)
{
    lcd_set_cursor(page, 0);

    uint8_t blank[LCD_WIDTH] = {0};
    lcd_data(blank, sizeof(blank));
    lcd_set_cursor(page, 0);

    uint8_t glyph[6] = {0};
    for (uint8_t i = 0; text[i] != '\0' && i < 21; i++) {
        glyph_for_char(text[i], glyph);
        glyph[5] = 0x00;
        lcd_data(glyph, sizeof(glyph));
    }
}

esp_err_t lcd_display_init(uint8_t i2c_addr)
{
    s_addr = i2c_addr;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
        .clk_flags = 0,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    esp_err_t err = i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    const uint8_t init_cmds[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0x7F, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3,
        0x00, 0xD5, 0x80, 0xD9, 0xF1, 0xDA, 0x12, 0xDB,
        0x40, 0x8D, 0x14, 0xAF
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        lcd_command(init_cmds[i]);
    }

    lcd_clear();
    lcd_write_line(0, "PORTON");
    lcd_write_line(2, "INICIANDO");
    ESP_LOGI(TAG, "OLED SSD1306 I2C addr=0x%02X SDA=%d SCL=%d", s_addr, PIN_I2C_SDA, PIN_I2C_SCL);
    return ESP_OK;
}

void lcd_display_show_status(porton_state_t state, int32_t encoder_count, const app_inputs_t *inputs, bool mqtt_connected)
{
    char line[32];

    snprintf(line, sizeof(line), "PORTON %s", porton_state_name(state));
    lcd_write_line(0, line);

    snprintf(line, sizeof(line), "ENC:%ld", (long)encoder_count);
    lcd_write_line(2, line);

    snprintf(line, sizeof(line), "LC:%d LA:%d FTC:%d",
             inputs->limit_closed ? 1 : 0,
             inputs->limit_open ? 1 : 0,
             inputs->ftc_blocked ? 1 : 0);
    lcd_write_line(4, line);

    snprintf(line, sizeof(line), "MQTT:%s", mqtt_connected ? "OK" : "OFF");
    lcd_write_line(6, line);
}
