#include "epd_canvas.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "font_render.h"

static const char *TAG = "canvas";

namespace {

// One font face shared by every canvas, with one glyph renderer per pixel
// size, as in M5EPD.
font_face_t s_face;
bool s_font_loaded = false;
std::map<uint16_t, font_render_t> s_renders;

const uint8_t kAlphaMap[16] = {0,   17,  34,  51,  68,  85,  102, 119,
                               136, 153, 170, 187, 204, 221, 238, 255};

// Blend a 4-bit glyph coverage value between two colour indexes.
uint16_t AlphaBlend16(uint8_t alpha, uint8_t fgc, uint8_t bgc) {
    if (alpha == 15) return fgc;
    if (alpha == 0) return bgc;
    if (bgc == fgc) return fgc;
    uint8_t step = abs(kAlphaMap[fgc] - kAlphaMap[bgc]) >> 4;
    if (fgc > bgc) return bgc + ((alpha * step) >> 4);
    return bgc - ((alpha * step) >> 4);
}

}  // namespace

EpdCanvas::~EpdCanvas() {
    deleteCanvas();
}

void EpdCanvas::operator=(const EpdCanvas &src) {
    if (this == &src) return;
    deleteCanvas();
    createCanvas(src._width, src._height);
    if (_buf && src._buf) memcpy(_buf, src._buf, _buffer_size);
}

void *EpdCanvas::createCanvas(int16_t width, int16_t height) {
    deleteCanvas();
    if (width < 1 || height < 1) return nullptr;
    _width     = width;
    _height    = height;
    _iwidth    = (width + 3) & 0xFFFC;
    _bytewidth = _iwidth >> 1;
    // One spare byte, like M5EPD, so fillCanvas() can write it.
    _buffer_size = _bytewidth * height + 1;
    // PSRAM first, like M5EPD: the GUI keeps dozens of button canvases alive
    // and internal RAM must stay free for WiFi/DMA buffers.
    _buf = (uint8_t *)heap_caps_calloc(1, _buffer_size,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (_buf == nullptr) {
        _buf = (uint8_t *)heap_caps_calloc(1, _buffer_size, MALLOC_CAP_8BIT);
    }
    if (_buf == nullptr) {
        ESP_LOGE(TAG, "createCanvas(%d, %d) failed", width, height);
        _buffer_size = 0;
        return nullptr;
    }
    _textarea_x = 0;
    _textarea_y = 0;
    _textarea_w = _iwidth;
    _textarea_h = _height;
    return _buf;
}

void EpdCanvas::deleteCanvas() {
    if (_buf) {
        heap_caps_free(_buf);
        _buf = nullptr;
    }
    _buffer_size = 0;
}

// ---------------------------------------------------------------- output

void EpdCanvas::pushCanvas(int32_t x, int32_t y, m5epd_update_mode_t mode) {
    if (!_buf) return;
    _last_push_x = x;
    _last_push_y = y;
    EPD.lock();
    EPD.WritePartGram4bpp(x, y, _iwidth, _height, _buf);
    EPD.UpdateArea(x, y, _iwidth, _height, mode);
    EPD.unlock();
}

void EpdCanvas::pushCanvas(m5epd_update_mode_t mode) {
    if (!_buf) return;
    EPD.UpdateArea(_last_push_x, _last_push_y, _iwidth, _height, mode);
}

void EpdCanvas::pushToCanvas(int32_t x, int32_t y, EpdCanvas *canvas) {
    if (!_buf) return;
    canvas->pushImage(x, y, _iwidth, _height, _buf);
}

// ---------------------------------------------------------------- pixels

void EpdCanvas::fillCanvas(uint32_t color) {
    if (!_buf) return;
    color &= 0x0F;
    memset(_buf, color << 4 | color, _buffer_size);
}

void EpdCanvas::drawPixel(int32_t x, int32_t y, uint32_t color) {
    if (!_buf || x < 0 || y < 0 || x >= _iwidth || y >= _height) return;
    color &= 0x0F;
    uint8_t &b = _buf[y * _bytewidth + (x >> 1)];
    b          = (x & 1) ? ((b & 0xF0) | color) : ((b & 0x0F) | (color << 4));
}

uint16_t EpdCanvas::readPixel(int32_t x, int32_t y) const {
    if (!_buf || x < 0 || y < 0 || x >= _iwidth || y >= _height) return 0;
    uint8_t b = _buf[y * _bytewidth + (x >> 1)];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}

void EpdCanvas::fillRect(int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t color) {
    if (!_buf || x >= _iwidth || y >= _height) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > _iwidth) w = _iwidth - x;
    if (y + h > _height) h = _height - y;
    if (w < 1 || h < 1) return;

    color &= 0x0F;
    uint8_t c = color << 4 | color;
    for (int32_t row = y; row < y + h; row++) {
        int32_t cx = x, cw = w;
        if (cx & 1) {
            drawPixel(cx++, row, color);
            cw--;
        }
        if (cw > 1) memset(_buf + row * _bytewidth + (cx >> 1), c, cw >> 1);
        if (cw & 1) drawPixel(cx + cw - 1, row, color);
    }
}

void EpdCanvas::drawFastHLine(int32_t x, int32_t y, int32_t w,
                              uint32_t color) {
    fillRect(x, y, w, 1, color);
}

void EpdCanvas::drawFastVLine(int32_t x, int32_t y, int32_t h,
                              uint32_t color) {
    fillRect(x, y, 1, h, color);
}

void EpdCanvas::drawRect(int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t color) {
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void EpdCanvas::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                         uint32_t color) {
    int32_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int32_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void EpdCanvas::ReverseColor() {
    for (uint32_t i = 0; i < _buffer_size; i++) _buf[i] = 0xFF - _buf[i];
}

void EpdCanvas::ReversePartColor(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!_buf || x >= _iwidth || y >= _height) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > _iwidth) w = _iwidth - x;
    if (y + h > _height) h = _height - y;
    for (int32_t j = y; j < y + h; j++) {
        for (int32_t i = x; i < x + w; i++) {
            drawPixel(i, j, 15 - readPixel(i, j));
        }
    }
}

void EpdCanvas::pushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                          const uint8_t *data) {
    if (!_buf || data == nullptr || w <= 0 || h <= 0) return;
    int32_t nbytes = (w >> 1) + (w & 1);
    for (int32_t j = 0; j < h; j++) {
        const uint8_t *row = data + j * nbytes;
        for (int32_t i = 0; i < w; i++) {
            uint8_t c = (i & 1) ? (row[i >> 1] & 0x0F) : (row[i >> 1] >> 4);
            drawPixel(x + i, y + j, c);
        }
    }
}

void EpdCanvas::pushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                          uint8_t transp, const uint8_t *data) {
    if (!_buf || data == nullptr || w <= 0 || h <= 0) return;
    int32_t nbytes = (w >> 1) + (w & 1);
    transp &= 0x0F;
    for (int32_t j = 0; j < h; j++) {
        const uint8_t *row = data + j * nbytes;
        for (int32_t i = 0; i < w; i++) {
            uint8_t c = (i & 1) ? (row[i >> 1] & 0x0F) : (row[i >> 1] >> 4);
            if (c != transp) drawPixel(x + i, y + j, c);
        }
    }
}

// ---------------------------------------------------------------- images

enum { kImgBmp, kImgJpg, kImgPng };

bool EpdCanvas::drawImageFile(const char *path, int32_t x, int32_t y,
                              int type) {
    if (!_buf || x >= _iwidth || y >= _height) return false;

    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data =
        len > 0 ? (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM) : nullptr;
    bool ok = data && fread(data, 1, len, f) == (size_t)len;
    fclose(f);
    if (!ok) {
        heap_caps_free(data);
        return false;
    }

    // Decode into an RGB565 sprite with M5GFX, then map luminance to 16 grays.
    M5Canvas tmp;
    tmp.setPsram(true);
    tmp.setColorDepth(16);
    int32_t w = _iwidth - x, h = _height - y;
    if (tmp.createSprite(w, h) == nullptr) {
        heap_caps_free(data);
        return false;
    }
    tmp.fillSprite(TFT_WHITE);
    switch (type) {
        case kImgBmp:
            ok = tmp.drawBmp(data, len, 0, 0);
            break;
        case kImgJpg:
            ok = tmp.drawJpg(data, len, 0, 0);
            break;
        default:
            ok = tmp.drawPng(data, len, 0, 0);
            break;
    }
    heap_caps_free(data);
    if (!ok) return false;

    const uint16_t *px = (const uint16_t *)tmp.getBuffer();
    for (int32_t j = 0; j < h; j++) {
        for (int32_t i = 0; i < w; i++) {
            uint16_t c = __builtin_bswap16(px[j * w + i]);  // stored swapped
            uint32_t r = ((c >> 11) & 0x1F) * 255 / 31;
            uint32_t g = ((c >> 5) & 0x3F) * 255 / 63;
            uint32_t b = (c & 0x1F) * 255 / 31;
            uint32_t lum = (r * 299 + g * 587 + b * 114) / 1000;
            drawPixel(x + i, y + j, ((255 - lum) * 15 + 127) / 255);
        }
    }
    return true;
}

bool EpdCanvas::drawBmpFile(const char *path, int32_t x, int32_t y) {
    return drawImageFile(path, x, y, kImgBmp);
}

bool EpdCanvas::drawJpgFile(const char *path, int32_t x, int32_t y) {
    return drawImageFile(path, x, y, kImgJpg);
}

bool EpdCanvas::drawPngFile(const char *path, int32_t x, int32_t y) {
    return drawImageFile(path, x, y, kImgPng);
}

// ---------------------------------------------------------------- fonts

esp_err_t EpdCanvas::loadFont(const char *path) {
    unloadFont();
    if (font_face_init_fs(&s_face, path) != ESP_OK) return ESP_FAIL;
    s_font_loaded = true;
    return ESP_OK;
}

esp_err_t EpdCanvas::loadFont(const uint8_t *data, size_t length) {
    unloadFont();
    if (font_face_init(&s_face, data, length) != ESP_OK) return ESP_FAIL;
    s_font_loaded = true;
    return ESP_OK;
}

esp_err_t EpdCanvas::unloadFont() {
    if (!s_font_loaded) return ESP_OK;
    for (auto &r : s_renders) font_render_destroy(&r.second);
    s_renders.clear();
    font_face_destroy(&s_face);
    s_font_loaded = false;
    return ESP_OK;
}

esp_err_t EpdCanvas::createRender(uint16_t size, uint16_t cache_size) {
    if (!s_font_loaded) {
        ESP_LOGE(TAG, "No font loaded.");
        return ESP_FAIL;
    }
    if (s_renders.count(size) != 0) {
        ESP_LOGE(TAG, "Size %d already exists.", size);
        return ESP_FAIL;
    }
    font_render_t render;
    memset(&render, 0, sizeof(render));
    if (font_render_init(&render, &s_face, size, cache_size) != ESP_OK) {
        ESP_LOGE(TAG, "Render creation failed.");
        return ESP_FAIL;
    }
    if (font_render_glyph(&render, 'g') != ESP_OK) {
        ESP_LOGE(TAG, "Font render failed.");
        font_render_destroy(&render);
        return ESP_FAIL;
    }
    // Distance from the top of the text box to the baseline.
    render.origin =
        render.pixel_size - render.bitmap_height + render.bitmap_top;
    auto it = s_renders.emplace(size, render).first;
    // font_render_glyph() left bitmap pointing into the cache; keep it valid.
    it->second.bitmap = it->second.glyph_cache;
    return ESP_OK;
}

esp_err_t EpdCanvas::destoryRender(uint16_t size) {
    auto it = s_renders.find(size);
    if (it == s_renders.end()) {
        ESP_LOGE(TAG, "Size %d not found.", size);
        return ESP_FAIL;
    }
    font_render_destroy(&it->second);
    s_renders.erase(it);
    return ESP_OK;
}

bool EpdCanvas::isRenderExist(uint16_t size) {
    return s_renders.count(size) != 0;
}

font_render *EpdCanvas::currentRender() const {
    auto it = s_renders.find(_font_size);
    return it == s_renders.end() ? nullptr : &it->second;
}

void EpdCanvas::setTextSize(uint16_t size) {
    _font_size = size;
    font_render_t *r = currentRender();
    if (r == nullptr) {
        ESP_LOGE(TAG, "Freetype: Size %d not found.", size);
        return;
    }
    _ft_linespace      = r->pixel_size >> 3;
    _write_font_height = r->pixel_size;
}

esp_err_t EpdCanvas::preRender(uint16_t code) {
    font_render_t *r = currentRender();
    return r ? font_render_glyph(r, code) : ESP_FAIL;
}

void EpdCanvas::drawFreetypeBitmap(int32_t cx, int32_t cy, uint16_t bw,
                                   uint16_t bh, const uint8_t *bitmap) {
    uint32_t pos      = 0;
    uint8_t fg        = _textcolor & 0x0F;
    uint8_t bg        = _textbgcolor & 0x0F;
    bool transparent  = _textcolor == _textbgcolor;
    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            if (transparent) bg = readPixel(cx + x, cy + y);
            uint8_t a = (pos & 1) ? (bitmap[pos >> 1] & 0x0F)
                                  : (bitmap[pos >> 1] >> 4);
            drawPixel(cx + x, cy + y, AlphaBlend16(a, fg, bg));
            pos++;
        }
    }
}

uint16_t EpdCanvas::decodeUTF8(const uint8_t *buf, uint16_t *index,
                               uint16_t remaining) {
    uint16_t c = buf[(*index)++];
    if ((c & 0x80) == 0x00) return c;
    if (((c & 0xE0) == 0xC0) && (remaining > 1))
        return ((c & 0x1F) << 6) | (buf[(*index)++] & 0x3F);
    if (((c & 0xF0) == 0xE0) && (remaining > 2)) {
        c = ((c & 0x0F) << 12) | ((buf[(*index)++] & 0x3F) << 6);
        return c | (buf[(*index)++] & 0x3F);
    }
    return c;  // 4-byte sequences unsupported: fall back to extended ASCII
}

uint16_t EpdCanvas::decodeUTF8(uint8_t c) {
    if ((c & 0x80) == 0x00) {
        _decoder_state = 0;
        return c;
    }
    if (_decoder_state == 0) {
        if ((c & 0xE0) == 0xC0) {
            _decoder_buffer = (c & 0x1F) << 6;
            _decoder_state  = 1;
            return 0;
        }
        if ((c & 0xF0) == 0xE0) {
            _decoder_buffer = (c & 0x0F) << 12;
            _decoder_state  = 2;
            return 0;
        }
    } else {
        if (_decoder_state == 2) {
            _decoder_buffer |= (c & 0x3F) << 6;
            _decoder_state--;
            return 0;
        }
        _decoder_buffer |= c & 0x3F;
        _decoder_state = 0;
        return _decoder_buffer;
    }
    _decoder_state = 0;
    return c;
}

int16_t EpdCanvas::drawString(const char *string, int32_t poX, int32_t poY) {
    int16_t sumX     = 0;
    font_render_t *r = currentRender();
    if (r == nullptr || !_buf) {
        ESP_LOGE(TAG, "Render is not available.");
        return sumX;
    }

    const uint8_t *s = (const uint8_t *)string;
    uint16_t len     = strlen(string);
    uint16_t n       = 0;
    int32_t base_y, fill_y;
    uint8_t vdatum = _textdatum & 0x1C;  // top / middle / bottom / baseline
    uint8_t hdatum = _textdatum & 0x03;  // left / center / right
    if (vdatum == 0) {
        base_y = poY + r->origin;
        fill_y = poY;
    } else if (vdatum == 4) {
        base_y = poY + r->origin - (r->pixel_size >> 1);
        fill_y = poY - (r->pixel_size >> 1);
    } else if (vdatum == 8) {
        base_y = poY + r->origin - r->pixel_size;
        fill_y = poY - r->pixel_size;
    } else {
        base_y = poY;
        fill_y = poY - r->origin;
    }

    bool fill_bg     = _textcolor != _textbgcolor;
    int32_t margin   = r->pixel_size >> 3;
    int32_t fill_h   = r->pixel_size + (margin << 1);
    int32_t fill_top = fill_y - margin;

    if (hdatum == 0) {  // left aligned: render as we go
        if (fill_bg) fillRect(poX - margin, fill_top, margin, fill_h, _textbgcolor);
        while (n < len) {
            if (poX > _iwidth) return sumX;
            uint16_t code = decodeUTF8(s, &n, len - n);
            if (font_render_glyph(r, code) != ESP_OK) {
                ESP_LOGE(TAG, "Font render failed.");
                return sumX;
            }
            if (fill_bg) fillRect(poX, fill_top, r->advance, fill_h, _textbgcolor);
            drawFreetypeBitmap(poX + r->bitmap_left, base_y - r->bitmap_top,
                               r->bitmap_width, r->bitmap_height, r->bitmap);
            poX += r->advance;
            sumX += r->advance;
        }
        if (fill_bg) fillRect(poX, fill_top, margin, fill_h, _textbgcolor);
        return sumX;
    }

    std::vector<uint16_t> codes;
    while (n < len) codes.push_back(decodeUTF8(s, &n, len - n));

    if (hdatum == 2) {  // right aligned: render backwards from poX
        if (fill_bg) fillRect(poX, fill_top, margin, fill_h, _textbgcolor);
        for (int i = (int)codes.size() - 1; i >= 0; i--) {
            if (poX < 0) return sumX;
            if (font_render_glyph(r, codes[i]) != ESP_OK) {
                ESP_LOGE(TAG, "Font render failed.");
                return sumX;
            }
            poX -= r->advance;
            if (fill_bg) fillRect(poX, fill_top, r->advance, fill_h, _textbgcolor);
            drawFreetypeBitmap(poX + r->bitmap_left, base_y - r->bitmap_top,
                               r->bitmap_width, r->bitmap_height, r->bitmap);
            sumX += r->advance;
        }
        if (fill_bg) fillRect(poX - margin, fill_top, margin, fill_h, _textbgcolor);
        return sumX;
    }

    // Centered: measure first. The glyph cache may be smaller than the
    // string, so copy each distinct glyph out before drawing.
    struct Glyph {
        uint16_t code;
        std::vector<uint8_t> bitmap;
        int advance, left, top;
        uint16_t w, h;
    };
    std::vector<Glyph> glyphs;
    std::vector<int> order;
    int32_t cwidth = 0;
    for (uint16_t code : codes) {
        int found = -1;
        for (size_t i = 0; i < glyphs.size(); i++) {
            if (glyphs[i].code == code) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            if (font_render_glyph(r, code) != ESP_OK) {
                ESP_LOGE(TAG, "Font render failed, unicode = %04X.", code);
                return sumX;
            }
            size_t size = ((r->bitmap_height * r->bitmap_width) >> 1) + 1;
            Glyph g;
            g.code = code;
            g.bitmap.assign(r->bitmap, r->bitmap + size);
            g.advance = r->advance;
            g.left    = r->bitmap_left;
            g.top     = r->bitmap_top;
            g.w       = r->bitmap_width;
            g.h       = r->bitmap_height;
            glyphs.push_back(std::move(g));
            found = glyphs.size() - 1;
        }
        order.push_back(found);
        cwidth += glyphs[found].advance;
    }

    poX -= cwidth / 2;
    if (poX < 0) poX = 0;
    if (poX + cwidth > _iwidth) poX = _iwidth - cwidth;
    if (fill_bg) {
        fillRect(poX - margin, fill_top, cwidth + (margin << 1), fill_h,
                 _textbgcolor);
    }
    for (int idx : order) {
        const Glyph &g = glyphs[idx];
        drawFreetypeBitmap(poX + g.left, base_y - g.top, g.w, g.h,
                           g.bitmap.data());
        poX += g.advance;
    }
    sumX += cwidth;
    return sumX;
}

void EpdCanvas::setTextArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (x > _iwidth || y > _height) return;
    if (x + w > _iwidth) w = _iwidth - x;
    if (y + h > _height) h = _height - y;
    _textarea_x = x;
    _textarea_y = y;
    _textarea_w = w;
    _textarea_h = h;
    _cursor_x   = x;
    _cursor_y   = y;
}

size_t EpdCanvas::print(const char *str) {
    size_t n = 0;
    while (*str) n += write((uint8_t)*str++);
    return n;
}

uint32_t EpdCanvas::getExceedOffset() {
    _write_exceed_area = false;
    _write_len         = 0;
    uint32_t len       = _last_write_len;
    _last_write_len    = 0;
    return len;
}

size_t EpdCanvas::write(uint8_t utf8) {
    _write_len++;
    if (utf8 == '\r') return 1;

    uint16_t code = decodeUTF8(utf8);
    _write_unicode_len++;
    if (code == 0) return 1;  // inside a multi-byte sequence

    if (!_textwrap_y &&
        (_cursor_y + _write_font_height >= _textarea_y + _textarea_h)) {
        if (!_write_exceed_area) {
            // Bytes consumed before the character that no longer fits.
            _last_write_len    = _write_len - _write_unicode_len;
            _write_len         = 0;
            _write_exceed_area = true;
            _write_unicode_len = 0;
        }
        return 1;
    }
    _write_unicode_len = 0;
    drawFreetypeGlyph(code);
    return 1;
}

void EpdCanvas::drawFreetypeGlyph(uint16_t code) {
    font_render_t *r = currentRender();
    if (r == nullptr) return;

    if (code == '\n') {
        _cursor_x = _textarea_x;
        _cursor_y += r->pixel_size + _ft_linespace;
        if (_textwrap_y &&
            _cursor_y + r->pixel_size >= _textarea_y + _textarea_h) {
            _cursor_y = _textarea_y;
        }
        return;
    }

    if (font_render_glyph(r, code) != ESP_OK) {
        ESP_LOGE(TAG, "Font render failed.");
        return;
    }

    if (_textwrap_x && (_cursor_x + r->advance > _textarea_x + _textarea_w)) {
        _cursor_y += r->pixel_size + _ft_linespace;
        _cursor_x = _textarea_x;
    }
    if (_textwrap_y &&
        (_cursor_y + r->pixel_size >= _textarea_y + _textarea_h)) {
        _cursor_y = _textarea_y;
    }
    if (_cursor_x == _textarea_x) _cursor_x -= r->bitmap_left;

    int32_t cy = _cursor_y - r->bitmap_top + r->origin;
    int32_t cx = _cursor_x + r->bitmap_left;
    drawFreetypeBitmap(cx, cy, r->bitmap_width, r->bitmap_height, r->bitmap);
    _cursor_x += r->advance;
}
