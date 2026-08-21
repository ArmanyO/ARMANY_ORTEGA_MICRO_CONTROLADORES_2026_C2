#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ssd1306_dev *ssd1306_handle_t;

typedef enum {
    SSD1306_BLACK = 0,
    SSD1306_WHITE = 1,
} ssd1306_color_t;

typedef struct {
    uint8_t width;
    uint8_t height;
    uint8_t offset;
} ssd1306_font_t;

extern const ssd1306_font_t Font_7x10;
extern const ssd1306_font_t Font_11x18;
extern const ssd1306_font_t Font_16x26;

ssd1306_handle_t ssd1306_create(i2c_port_t i2c_port, uint8_t address);
void ssd1306_delete(ssd1306_handle_t dev);
esp_err_t ssd1306_init(ssd1306_handle_t dev);
esp_err_t ssd1306_clear(ssd1306_handle_t dev);
esp_err_t ssd1306_update_screen(ssd1306_handle_t dev);
esp_err_t ssd1306_set_cursor(ssd1306_handle_t dev, uint8_t x, uint8_t y);
esp_err_t ssd1306_write_string(ssd1306_handle_t dev, const char *str);
esp_err_t ssd1306_write_char(ssd1306_handle_t dev, char ch);
esp_err_t ssd1306_set_font(ssd1306_handle_t dev, const ssd1306_font_t *font);
esp_err_t ssd1306_set_color(ssd1306_handle_t dev, ssd1306_color_t color);
esp_err_t ssd1306_fill_rect(ssd1306_handle_t dev, uint8_t x, uint8_t y, uint8_t w, uint8_t h, ssd1306_color_t color);
esp_err_t ssd1306_draw_pixel(ssd1306_handle_t dev, uint8_t x, uint8_t y, ssd1306_color_t color);

#ifdef __cplusplus
}
#endif

#endif // SSD1306_H