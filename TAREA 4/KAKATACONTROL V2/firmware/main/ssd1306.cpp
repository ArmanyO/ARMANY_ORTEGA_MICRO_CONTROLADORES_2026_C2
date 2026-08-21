#include "ssd1306.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SSD1306";

#define SSD1306_CMD_DISPLAY_OFF         0xAE
#define SSD1306_CMD_DISPLAY_ON          0xAF
#define SSD1306_CMD_SET_DISPLAY_CLOCK   0xD5
#define SSD1306_CMD_SET_MULTIPLEX       0xA8
#define SSD1306_CMD_SET_DISPLAY_OFFSET  0xD3
#define SSD1306_CMD_SET_START_LINE      0x40
#define SSD1306_CMD_CHARGE_PUMP         0x8D
#define SSD1306_CMD_MEMORY_MODE         0x20
#define SSD1306_CMD_SEG_REMAP           0xA1
#define SSD1306_CMD_COM_SCAN_DEC        0xC8
#define SSD1306_CMD_SET_COMPINS         0xDA
#define SSD1306_CMD_SET_CONTRAST        0x81
#define SSD1306_CMD_SET_PRECHARGE       0xD9
#define SSD1306_CMD_SET_VCOM_DETECT     0xDB
#define SSD1306_CMD_DISPLAY_ALL_ON      0xA5
#define SSD1306_CMD_NORMAL_DISPLAY      0xA6
#define SSD1306_CMD_SET_COL_ADDR        0x21
#define SSD1306_CMD_SET_PAGE_ADDR       0x22

typedef struct {
    i2c_port_t i2c_port;
    uint8_t address;
    uint8_t width;
    uint8_t height;
    uint8_t pages;
    uint8_t *buffer;
    const ssd1306_font_t *font;
    ssd1306_color_t color;
    uint8_t cursor_x;
    uint8_t cursor_y;
} ssd1306_dev_t;

static const uint8_t init_sequence[] = {
    SSD1306_CMD_DISPLAY_OFF,
    SSD1306_CMD_SET_DISPLAY_CLOCK, 0x80,
    SSD1306_CMD_SET_MULTIPLEX, 0x3F,
    SSD1306_CMD_SET_DISPLAY_OFFSET, 0x00,
    SSD1306_CMD_SET_START_LINE | 0x00,
    SSD1306_CMD_CHARGE_PUMP, 0x14,
    SSD1306_CMD_MEMORY_MODE, 0x00,
    SSD1306_CMD_SEG_REMAP,
    SSD1306_CMD_COM_SCAN_DEC,
    SSD1306_CMD_SET_COMPINS, 0x12,
    SSD1306_CMD_SET_CONTRAST, 0xCF,
    SSD1306_CMD_SET_PRECHARGE, 0xF1,
    SSD1306_CMD_SET_VCOM_DETECT, 0x40,
    SSD1306_CMD_DISPLAY_ALL_ON,
    SSD1306_CMD_NORMAL_DISPLAY,
    SSD1306_CMD_DISPLAY_ON,
};

const ssd1306_font_t Font_7x10 = {7, 10, 32};
const ssd1306_font_t Font_11x18 = {11, 18, 32};
const ssd1306_font_t Font_16x26 = {16, 26, 32};

static const uint8_t font_7x10_data[] = {
#include "font_7x10.inc"
};

static const uint8_t font_11x18_data[] = {
#include "font_11x18.inc"
};

static const uint8_t font_16x26_data[] = {
#include "font_16x26.inc"
};

ssd1306_handle_t ssd1306_create(i2c_port_t i2c_port, uint8_t address) {
    ssd1306_dev_t *dev = (ssd1306_dev_t *)malloc(sizeof(ssd1306_dev_t));
    if (!dev) return NULL;

    dev->i2c_port = i2c_port;
    dev->address = address;
    dev->width = 128;
    dev->height = 64;
    dev->pages = dev->height / 8;
    dev->buffer = (uint8_t *)malloc(dev->width * dev->pages);
    dev->font = &Font_7x10;
    dev->color = SSD1306_WHITE;
    dev->cursor_x = 0;
    dev->cursor_y = 0;

    if (!dev->buffer) {
        free(dev);
        return NULL;
    }
    memset(dev->buffer, 0, dev->width * dev->pages);
    return dev;
}

void ssd1306_delete(ssd1306_handle_t dev) {
    if (dev) {
        free(dev->buffer);
        free(dev);
    }
}

static esp_err_t ssd1306_write_cmd(ssd1306_handle_t dev, uint8_t cmd) {
    uint8_t data[2] = {0x00, cmd};
    return i2c_master_write_to_device(dev->i2c_port, dev->address, data, 2, pdMS_TO_TICKS(100));
}

static esp_err_t ssd1306_write_data(ssd1306_handle_t dev, uint8_t *data, size_t len) {
    uint8_t *buf = (uint8_t *)malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    buf[0] = 0x40;
    memcpy(buf + 1, data, len);
    esp_err_t ret = i2c_master_write_to_device(dev->i2c_port, dev->address, buf, len + 1, pdMS_TO_TICKS(100));
    free(buf);
    return ret;
}

esp_err_t ssd1306_init(ssd1306_handle_t dev) {
    if (!dev) return ESP_ERR_INVALID_ARG;

    for (size_t i = 0; i < sizeof(init_sequence); i++) {
        esp_err_t ret = ssd1306_write_cmd(dev, init_sequence[i]);
        if (ret != ESP_OK) return ret;
    }
    return ESP_OK;
}

esp_err_t ssd1306_clear(ssd1306_handle_t dev) {
    if (!dev) return ESP_ERR_INVALID_ARG;
    memset(dev->buffer, dev->color == SSD1306_WHITE ? 0xFF : 0x00, dev->width * dev->pages);
    return ESP_OK;
}

esp_err_t ssd1306_update_screen(ssd1306_handle_t dev) {
    if (!dev) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = ssd1306_write_cmd(dev, SSD1306_CMD_SET_COL_ADDR);
    if (ret != ESP_OK) return ret;
    ret = ssd1306_write_cmd(dev, 0);
    if (ret != ESP_OK) return ret;
    ret = ssd1306_write_cmd(dev, dev->width - 1);
    if (ret != ESP_OK) return ret;

    ret = ssd1306_write_cmd(dev, SSD1306_CMD_SET_PAGE_ADDR);
    if (ret != ESP_OK) return ret;
    ret = ssd1306_write_cmd(dev, 0);
    if (ret != ESP_OK) return ret;
    ret = ssd1306_write_cmd(dev, dev->pages - 1);
    if (ret != ESP_OK) return ret;

    for (uint8_t page = 0; page < dev->pages; page++) {
        ret = ssd1306_write_data(dev, &dev->buffer[page * dev->width], dev->width);
        if (ret != ESP_OK) return ret;
    }
    return ESP_OK;
}

esp_err_t ssd1306_set_cursor(ssd1306_handle_t dev, uint8_t x, uint8_t y) {
    if (!dev) return ESP_ERR_INVALID_ARG;
    dev->cursor_x = x;
    dev->cursor_y = y;
    return ESP_OK;
}

esp_err_t ssd1306_set_font(ssd1306_handle_t dev, const ssd1306_font_t *font) {
    if (!dev || !font) return ESP_ERR_INVALID_ARG;
    dev->font = font;
    return ESP_OK;
}

esp_err_t ssd1306_set_color(ssd1306_handle_t dev, ssd1306_color_t color) {
    if (!dev) return ESP_ERR_INVALID_ARG;
    dev->color = color;
    return ESP_OK;
}

static const uint8_t *get_font_data(const ssd1306_font_t *font, char ch) {
    if (ch < font->offset) return NULL;
    uint8_t index = ch - font->offset;
    if (font == &Font_7x10) return &font_7x10_data[index * (7 * 10 / 8 + 1)];
    if (font == &Font_11x18) return &font_11x18_data[index * (11 * 18 / 8 + 1)];
    if (font == &Font_16x26) return &font_16x26_data[index * (16 * 26 / 8 + 1)];
    return NULL;
}

static int get_font_bytes_per_char(const ssd1306_font_t *font) {
    return (font->width * font->height + 7) / 8;
}

esp_err_t ssd1306_write_char(ssd1306_handle_t dev, char ch) {
    if (!dev || !dev->font) return ESP_ERR_INVALID_ARG;

    const uint8_t *font_data = get_font_data(dev->font, ch);
    if (!font_data) return ESP_ERR_NOT_FOUND;

    int bytes_per_char = get_font_bytes_per_char(dev->font);
    uint8_t char_width = dev->font->width;
    uint8_t char_height = dev->font->height;

    if (dev->cursor_x + char_width >= dev->width) {
        dev->cursor_x = 0;
        dev->cursor_y += char_height;
    }
    if (dev->cursor_y + char_height >= dev->height) {
        dev->cursor_y = 0;
    }

    for (uint8_t col = 0; col < char_width; col++) {
        uint8_t byte_idx = col * char_height / 8;
        uint8_t bit_offset = col * char_height % 8;

        for (uint8_t row = 0; row < char_height; row++) {
            bool pixel = false;
            if (bit_offset == 0) {
                pixel = font_data[byte_idx + row / 8] & (1 << (row % 8));
            } else {
                uint16_t bits = (font_data[byte_idx + row / 8] | (font_data[byte_idx + row / 8 + 1] << 8)) >> bit_offset;
                pixel = bits & 1;
            }

            uint8_t x = dev->cursor_x + col;
            uint8_t y = dev->cursor_y + row;
            if (x < dev->width && y < dev->height) {
                uint16_t buf_idx = x + (y / 8) * dev->width;
                if (dev->color == SSD1306_WHITE) {
                    dev->buffer[buf_idx] |= (1 << (y % 8));
                } else {
                    dev->buffer[buf_idx] &= ~(1 << (y % 8));
                }
            }
        }
    }

    dev->cursor_x += char_width;
    return ESP_OK;
}

esp_err_t ssd1306_write_string(ssd1306_handle_t dev, const char *str) {
    if (!dev || !str) return ESP_ERR_INVALID_ARG;
    while (*str) {
        esp_err_t ret = ssd1306_write_char(dev, *str++);
        if (ret != ESP_OK) return ret;
    }
    return ESP_OK;
}

esp_err_t ssd1306_draw_pixel(ssd1306_handle_t dev, uint8_t x, uint8_t y, ssd1306_color_t color) {
    if (!dev || x >= dev->width || y >= dev->height) return ESP_ERR_INVALID_ARG;
    uint16_t idx = x + (y / 8) * dev->width;
    if (color == SSD1306_WHITE) {
        dev->buffer[idx] |= (1 << (y % 8));
    } else {
        dev->buffer[idx] &= ~(1 << (y % 8));
    }
    return ESP_OK;
}

esp_err_t ssd1306_fill_rect(ssd1306_handle_t dev, uint8_t x, uint8_t y, uint8_t w, uint8_t h, ssd1306_color_t color) {
    if (!dev) return ESP_ERR_INVALID_ARG;
    for (uint8_t i = 0; i < w; i++) {
        for (uint8_t j = 0; j < h; j++) {
            ssd1306_draw_pixel(dev, x + i, y + j, color);
        }
    }
    return ESP_OK;
}