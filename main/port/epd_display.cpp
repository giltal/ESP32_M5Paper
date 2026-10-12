#include "epd_display.h"

#include <algorithm>
#include <cstring>

#include <M5Unified.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "it8951.h"

static const char *TAG = "epd";

EpdDisplay EPD;

namespace {

constexpr int kStride = EpdDisplay::kWidth / 2;

// Canvas index i (0 = white, 15 = black) to a gray that M5GFX's IT8951 driver
// turns back into exactly level 15 - i: it computes (R + 2G + B + dither) >> 6
// with dither in [-30, 30], so the gray has to sit in the middle of its band.
struct Palette {
    lgfx::bgr888_t c[16];
    Palette() {
        for (int i = 0; i < 16; i++) {
            uint8_t g = (15 - i) * 16 + 8;
            c[i]      = lgfx::bgr888_t(g, g, g);
        }
    }
};
const Palette kPalette;

epd_mode_t ToEpdMode(m5epd_update_mode_t mode) {
    switch (mode) {
        case UPDATE_MODE_DU4:
            return epd_mode_t::epd_fastest;  // M5GFX: DU4
        case UPDATE_MODE_DU:
        case UPDATE_MODE_A2:
            return epd_mode_t::epd_fast;  // M5GFX: DU
        case UPDATE_MODE_GL16:
        case UPDATE_MODE_GLR16:
        case UPDATE_MODE_GLD16:
            return epd_mode_t::epd_text;  // M5GFX: GL16
        default:
            return epd_mode_t::epd_quality;  // M5GFX: GC16
    }
}

inline uint8_t GetNibble(const uint8_t *row, int x) {
    return (x & 1) ? (row[x >> 1] & 0x0F) : (row[x >> 1] >> 4);
}

inline void SetNibble(uint8_t *row, int x, uint8_t v) {
    uint8_t &b = row[x >> 1];
    b          = (x & 1) ? ((b & 0xF0) | v) : ((b & 0x0F) | (v << 4));
}

}  // namespace

bool EpdDisplay::begin() {
    _mutex = xSemaphoreCreateRecursiveMutex();
    _shadow = (uint8_t *)heap_caps_malloc(kStride * kHeight,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (_shadow == nullptr) {
        ESP_LOGE(TAG, "No memory for the shadow frame buffer");
        return false;
    }
    memset(_shadow, 0, kStride * kHeight);

    M5.Display.setAutoDisplay(false);
    if (M5.Display.width() > M5.Display.height()) {
        M5.Display.setRotation(M5.Display.getRotation() ^ 1);
    }
    ESP_LOGI(TAG, "Panel %dx%d, rotation %d", (int)M5.Display.width(),
             (int)M5.Display.height(), (int)M5.Display.getRotation());

    _direct = it8951::begin();
    if (!_direct) ESP_LOGW(TAG, "IT8951 not reachable directly; using M5GFX");
    return true;
}

void EpdDisplay::lock() {
    if (_mutex) xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
}

void EpdDisplay::unlock() {
    if (_mutex) xSemaphoreGiveRecursive(_mutex);
}

bool EpdDisplay::clip(int &x, int &y, int &w, int &h) const {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > kWidth) w = kWidth - x;
    if (y + h > kHeight) h = kHeight - y;
    return w > 0 && h > 0;
}

void EpdDisplay::writeRect(int x, int y, int w, int h, const uint8_t *src,
                           int src_stride) {
    int sx = 0, sy = 0;
    if (x < 0) sx = -x;
    if (y < 0) sy = -y;
    if (!clip(x, y, w, h)) return;

    lock();
    for (int row = 0; row < h; row++) {
        const uint8_t *s = src + (sy + row) * src_stride;
        uint8_t *d       = _shadow + (y + row) * kStride;
        if (((x | sx) & 1) == 0) {
            memcpy(d + (x >> 1), s + (sx >> 1), w >> 1);
            if (w & 1) SetNibble(d, x + w - 1, GetNibble(s, sx + w - 1));
        } else {
            for (int i = 0; i < w; i++) SetNibble(d, x + i, GetNibble(s, sx + i));
        }
    }
    unlock();
}

void EpdDisplay::WriteFullGram4bpp(const uint8_t *gram) {
    writeRect(0, 0, kWidth, kHeight, gram, kStride);
}

void EpdDisplay::WritePartGram4bpp(uint16_t x, uint16_t y, uint16_t w,
                                   uint16_t h, const uint8_t *gram) {
    x = (x + 3) & 0xFFFC;
    if (w & 0x03) {
        ESP_LOGE(TAG, "Gram width %d not a multiple of 4.", w);
        return;
    }
    writeRect(x, y, w, h, gram, w >> 1);
}

void EpdDisplay::FillPartGram4bpp(uint16_t x, uint16_t y, uint16_t w,
                                  uint16_t h, uint16_t data) {
    x     = (x + 3) & 0xFFFC;
    int X = x, Y = y, W = w, H = h;
    if (!clip(X, Y, W, H)) return;

    lock();
    for (int row = Y; row < Y + H; row++) {
        uint8_t *d = _shadow + row * kStride;
        for (int col = X; col < X + W; col++) {
            uint8_t raw = (data >> (12 - 4 * (col & 3))) & 0x0F;
            SetNibble(d, col, 15 - raw);
        }
    }
    unlock();
}

void EpdDisplay::Clear(bool init) {
    lock();
    memset(_shadow, 0, kStride * kHeight);
    if (init) {
        UpdateFull(UPDATE_MODE_INIT);
    }
    unlock();
}

void EpdDisplay::UpdateFull(m5epd_update_mode_t mode) {
    UpdateArea(0, 0, kWidth, kHeight, mode);
}

void EpdDisplay::UpdateArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            m5epd_update_mode_t mode) {
    if (mode == UPDATE_MODE_NONE) return;

    x     = (x + 3) & 0xFFFC;
    w     = (w + 3) & 0xFFFC;  // controller loads whole 4-pixel groups
    int X = x, Y = y, W = w, H = h;
    if (!clip(X, Y, W, H)) return;

    lock();
    if (_direct) {
        // Don't load new pixels under a refresh that is still running.
        waitRefresh(X, Y, W, H);
        it8951::load_area(X, Y, W, H, _shadow + Y * kStride + X / 2, kStride,
                          true);
        it8951::display_area(X, Y, W, H, mode);
        // Track the union of refreshes issued since the panel was last idle.
        if (_last_w == 0) {
            _last_x = X;
            _last_y = Y;
            _last_w = W;
            _last_h = H;
        } else {
            int r = std::max(_last_x + _last_w, X + W);
            int b = std::max(_last_y + _last_h, Y + H);
            _last_x = std::min(_last_x, X);
            _last_y = std::min(_last_y, Y);
            _last_w = r - _last_x;
            _last_h = b - _last_y;
        }
        _last_us = esp_timer_get_time();
    } else {
        // M5GFX fallback. Data is always written in quality mode: in the fast
        // modes M5GFX reduces it to black and white.
        M5.Display.setEpdMode(epd_mode_t::epd_quality);
        M5.Display.setClipRect(X, Y, W, H);
        M5.Display.pushImage(0, 0, kWidth, kHeight, _shadow,
                             lgfx::color_depth_t::palette_4bit, kPalette.c);
        M5.Display.clearClipRect();
        M5.Display.setEpdMode(ToEpdMode(mode));
        M5.Display.display(X, Y, W, H);
    }
    _update_count++;
    unlock();
}

void EpdDisplay::waitRefresh(int x, int y, int w, int h) {
    if (_last_w == 0) return;
    if (w != 0 && (x >= _last_x + _last_w || _last_x >= x + w ||
                   y >= _last_y + _last_h || _last_y >= y + h)) {
        return;  // no overlap: the controller runs them in parallel
    }
    // The busy register reads idle for a few ms after a refresh command,
    // until the refresh has actually started.
    int64_t since = esp_timer_get_time() - _last_us;
    if (since < 30000) vTaskDelay(pdMS_TO_TICKS(30 - since / 1000) + 1);
    it8951::wait_idle(5000);
    _last_w = 0;
}

void EpdDisplay::CheckAFSR() {
    lock();
    if (_direct) {
        waitRefresh(0, 0, 0, 0);
    } else {
        M5.Display.waitDisplay();
    }
    unlock();
}
