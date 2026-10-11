// 16-level grayscale off-screen canvas for the M5Paper.
//
// A port of M5EPD_Canvas without TFT_eSPI/Arduino. The pixel buffer keeps
// M5EPD's layout (4bpp, high nibble first, width rounded up to a multiple of 4,
// colour index 0 = white ... 15 = black) because the app copies image resources
// straight into frameBuffer(). Text uses FreeType through font_render, with the
// same metrics and layout rules as M5EPD so screens and pagination match.
#pragma once

#include <cstdint>
#include <string>

#include <M5GFX.h>

#include "epd_display.h"

struct font_render;

class EpdCanvas {
   public:
    EpdCanvas() = default;
    ~EpdCanvas();
    EpdCanvas(const EpdCanvas &)  = delete;
    // Copies size and pixels (M5EPD semantics; text settings are kept).
    void operator=(const EpdCanvas &src);

    void *createCanvas(int16_t width, int16_t height);
    void deleteCanvas();
    void *frameBuffer() const {
        return _buf;
    }
    uint32_t getBufferSize() const {
        return _buffer_size;
    }
    int16_t width() const {
        return _width;
    }
    int16_t height() const {
        return _height;
    }

    // Push to the panel at (x, y) and refresh that area with `mode`.
    void pushCanvas(int32_t x, int32_t y, m5epd_update_mode_t mode);
    // Refresh the last pushed area again without sending data.
    void pushCanvas(m5epd_update_mode_t mode);
    void pushToCanvas(int32_t x, int32_t y, EpdCanvas *canvas);

    void fillCanvas(uint32_t color);
    void drawPixel(int32_t x, int32_t y, uint32_t color);
    uint16_t readPixel(int32_t x, int32_t y) const;
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color);
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color);
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                  uint32_t color);
    void ReverseColor();
    void ReversePartColor(int32_t x, int32_t y, int32_t w, int32_t h);

    // 4bpp image data, two pixels per byte, high nibble first.
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                   const uint8_t *data);
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t transp,
                   const uint8_t *data);

    // Decode an image file (any VFS path) into the canvas at (x, y).
    bool drawBmpFile(const char *path, int32_t x = 0, int32_t y = 0);
    bool drawJpgFile(const char *path, int32_t x = 0, int32_t y = 0);
    bool drawPngFile(const char *path, int32_t x = 0, int32_t y = 0);

    // ---- Text (FreeType). Font and renders are shared by all canvases. ----
    static esp_err_t loadFont(const char *path);
    static esp_err_t loadFont(const uint8_t *data, size_t length);
    static esp_err_t unloadFont();
    static esp_err_t createRender(uint16_t size, uint16_t cache_size = 1);
    static esp_err_t destoryRender(uint16_t size);
    static bool isRenderExist(uint16_t size);

    esp_err_t preRender(uint16_t code);
    void setTextSize(uint16_t size);
    void setTextColor(uint16_t color) {
        _textcolor = _textbgcolor = color;
    }
    void setTextColor(uint16_t fg, uint16_t bg) {
        _textcolor   = fg;
        _textbgcolor = bg;
    }
    void setTextDatum(lgfx::textdatum_t datum) {
        _textdatum = datum;
    }
    int16_t drawString(const char *string, int32_t x, int32_t y);
    int16_t drawString(const std::string &string, int32_t x, int32_t y) {
        return drawString(string.c_str(), x, y);
    }

    // Wrapping text output into a rectangle (used by the textbox and reader).
    void setTextArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    size_t print(const char *str);
    size_t print(const std::string &str) {
        return print(str.c_str());
    }
    // Bytes consumed by print() before text ran past the bottom of the text
    // area (0 if everything fitted). Resets the counter.
    uint32_t getExceedOffset();

    static uint16_t decodeUTF8(const uint8_t *buf, uint16_t *index,
                               uint16_t remaining);

   private:
    bool drawImageFile(const char *path, int32_t x, int32_t y, int type);
    void drawFreetypeBitmap(int32_t cx, int32_t cy, uint16_t bw, uint16_t bh,
                            const uint8_t *bitmap);
    void drawFreetypeGlyph(uint16_t code);
    size_t write(uint8_t utf8);
    uint16_t decodeUTF8(uint8_t c);
    struct font_render *currentRender() const;

    uint8_t *_buf          = nullptr;
    uint32_t _buffer_size  = 0;
    int16_t _width         = 0;  // as requested
    int16_t _height        = 0;
    int16_t _iwidth        = 0;  // rounded up to a multiple of 4
    int16_t _bytewidth     = 0;
    int32_t _last_push_x   = 0;
    int32_t _last_push_y   = 0;

    uint16_t _textcolor             = 15;
    uint16_t _textbgcolor           = 0;  // TFT_eSPI default: opaque text
    lgfx::textdatum_t _textdatum    = TL_DATUM;
    uint16_t _font_size             = 0;
    uint16_t _ft_linespace          = 0;
    uint16_t _write_font_height     = 0;

    int32_t _cursor_x = 0, _cursor_y = 0;
    uint16_t _textarea_x = 0, _textarea_y = 0, _textarea_w = 0,
             _textarea_h = 0;
    bool _textwrap_x = true, _textwrap_y = false;
    uint32_t _write_len = 0, _last_write_len = 0, _write_unicode_len = 0;
    bool _write_exceed_area = false;
    uint8_t _decoder_state = 0;
    uint16_t _decoder_buffer = 0;
};
