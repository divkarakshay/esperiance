#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64

#ifdef __cplusplus
extern "C" {
#endif

/* Init I2C bus and the SSD1306 panel (SDA=21, SCL=22, addr 0x3C). */
void display_init(void);

/* Push the internal framebuffer to the panel. Call once per rendered frame. */
void display_show(void);

/* Clear the framebuffer (all pixels off). */
void display_clear(void);

void display_draw_pixel(int x, int y, bool on);
void display_draw_rect(int x, int y, int w, int h, bool fill, bool on);
void display_draw_line(int x0, int y0, int x1, int y1, bool on);
void display_draw_circle(int cx, int cy, int r, bool on);

/* Text, 5x7 font, 6px advance. Characters outside 0x20..0x7E render as '?'. */
void display_draw_char(int x, int y, char c, bool on);
void display_draw_text(int x, int y, const char *s, bool on);
void display_draw_text_centered(int y, const char *s, bool on);
int  display_text_width(const char *s);

#ifdef __cplusplus
}
#endif
