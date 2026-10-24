#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "display.h"
#include "font5x7.h"

#define I2C_PORT    I2C_NUM_0
#define PIN_SDA     21
#define PIN_SCL     22
#define OLED_ADDR   0x3C
#define OLED_CLK_HZ 400000       /* 400kHz now that sync mode is reliable */

static const char *TAG = "display";

static uint8_t fb[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
static i2c_master_dev_handle_t dev;
static bool oled_present;

static void ssd1306_write(uint8_t control, const uint8_t *data, size_t len)
{
    static uint8_t txbuf[256];
    if (!oled_present) {
        return;
    }
    txbuf[0] = control;
    memcpy(txbuf + 1, data, len);
    esp_err_t err = i2c_master_transmit(dev, txbuf, len + 1, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c transmit failed: %s", esp_err_to_name(err));
    }
}

/* i2c_master_probe is unreliable in this driver version, so we probe by
 * doing a real 1-byte transmit on the added device. */
static bool try_address(i2c_master_bus_handle_t bus, uint8_t addr,
                        int32_t timeout_ms)
{
    i2c_master_dev_handle_t probe_dev = NULL;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = OLED_CLK_HZ,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &probe_dev) != ESP_OK) {
        return false;
    }
    uint8_t cmd = 0x00;
    esp_err_t err = i2c_master_transmit(probe_dev, &cmd, 1, timeout_ms);
    i2c_master_bus_rm_device(probe_dev);
    return err == ESP_OK;
}

static void detect_oled(i2c_master_bus_handle_t bus)
{
    /* Try the two common SSD1306 addresses. */
    uint8_t addr = OLED_ADDR;
    if (!try_address(bus, OLED_ADDR, 100)) {
        if (!try_address(bus, 0x3D, 100)) {
            ESP_LOGE(TAG, "no OLED found at 0x3C/0x3D (check SDA/SCL wiring and power)");
            return;
        }
        addr = 0x3D;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = OLED_CLK_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) {
        ESP_LOGE(TAG, "failed to add OLED device");
        return;
    }
    oled_present = true;
    ESP_LOGI(TAG, "OLED using address 0x%02X", addr);
}

void display_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .trans_queue_depth = 0,     /* 0 = synchronous mode (reliable); >0 enables experimental async path */
        .flags = { .enable_internal_pullup = true },
    };
    i2c_master_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    detect_oled(bus);
    if (!oled_present) {
        return;
    }

    static const uint8_t init_seq[] = {
        0xAE,             /* display off */
        0xD5, 0x80,       /* clock div */
        0xA8, 0x3F,       /* multiplex 1/64 */
        0xD3, 0x00,       /* display offset 0 */
        0x40,             /* start line 0 */
        0x8D, 0x14,       /* charge pump on */
        0x20, 0x00,       /* horizontal addressing */
        0xA1,             /* segment remap (correct orientation) */
        0xC8,             /* COM scan dec */
        0xDA, 0x12,       /* COM pins */
        0x81, 0xCF,       /* contrast */
        0xD9, 0xF1,       /* precharge */
        0xDB, 0x40,       /* vcom detect */
        0xA4,             /* resume to RAM content */
        0xA6,             /* non-inverted display */
        0xAF,             /* display on */
    };
    ssd1306_write(0x00, init_seq, sizeof(init_seq));

    display_clear();
    display_show();
}

void display_show(void)
{
    uint8_t page_hdr[4];
    for (int page = 0; page < 8; page++) {
        page_hdr[0] = 0xB0 | page;   /* set page address */
        page_hdr[1] = 0x00;          /* column low  */
        page_hdr[2] = 0x10;          /* column high */
        ssd1306_write(0x00, page_hdr, 3);
        ssd1306_write(0x40, &fb[page * DISPLAY_WIDTH], DISPLAY_WIDTH);
    }
}

void display_clear(void)
{
    memset(fb, 0, sizeof(fb));
}

static inline void fb_set(int x, int y, bool on)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }
    uint8_t *byte = &fb[x + (y >> 3) * DISPLAY_WIDTH];
    uint8_t mask = 1u << (y & 7);
    if (on) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
}

void display_draw_pixel(int x, int y, bool on)
{
    fb_set(x, y, on);
}

void display_draw_rect(int x, int y, int w, int h, bool fill, bool on)
{
    if (fill) {
        for (int yy = y; yy < y + h; yy++) {
            for (int xx = x; xx < x + w; xx++) {
                fb_set(xx, yy, on);
            }
        }
        return;
    }
    for (int xx = x; xx < x + w; xx++) {
        fb_set(xx, y, on);
        fb_set(xx, y + h - 1, on);
    }
    for (int yy = y; yy < y + h; yy++) {
        fb_set(x, yy, on);
        fb_set(x + w - 1, yy, on);
    }
}

void display_draw_line(int x0, int y0, int x1, int y1, bool on)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        fb_set(x0, y0, on);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void display_draw_circle(int cx, int cy, int r, bool on)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        fb_set(cx + x, cy + y, on);
        fb_set(cx - x, cy + y, on);
        fb_set(cx + x, cy - y, on);
        fb_set(cx - x, cy - y, on);
        fb_set(cx + y, cy + x, on);
        fb_set(cx - y, cy + x, on);
        fb_set(cx + y, cy - x, on);
        fb_set(cx - y, cy - x, on);
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void display_draw_char(int x, int y, char c, bool on)
{
    if (c < 0x20 || c > 0x7E) {
        c = '?';
    }
    const uint8_t *glyph = font5x7[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1u << row)) {
                fb_set(x + col, y + row, on);
            }
        }
    }
}

void display_draw_text(int x, int y, const char *s, bool on)
{
    while (*s) {
        display_draw_char(x, y, *s, on);
        x += 6;
        s++;
    }
}

void display_draw_text_centered(int y, const char *s, bool on)
{
    int w = display_text_width(s);
    display_draw_text((DISPLAY_WIDTH - w) / 2, y, s, on);
}

int display_text_width(const char *s)
{
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n * 6 - 1;
}