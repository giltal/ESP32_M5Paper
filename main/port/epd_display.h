// IT8951 e-paper access for the M5Paper, on top of M5GFX.
//
// Mirrors the M5EPD_Driver API the factory test was written against:
// image data is written with no refresh, then an area is refreshed with a
// chosen waveform. M5GFX merges every pending write into its next refresh, so
// writes go to a full-screen shadow buffer instead, and each Update* call pushes
// only the requested area to the panel just before refreshing it.
#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef enum {
    UPDATE_MODE_INIT  = 0,  // Display initialization (M5GFX: GC16)
    UPDATE_MODE_DU    = 1,  // Monochrome menu, text input
    UPDATE_MODE_GC16  = 2,  // High quality images
    UPDATE_MODE_GL16  = 3,  // Text with white background
    UPDATE_MODE_GLR16 = 4,  // Text with white background (M5GFX: GL16)
    UPDATE_MODE_GLD16 = 5,  // Text and graphics, white bg (M5GFX: GL16)
    UPDATE_MODE_DU4   = 6,  // Fast page flipping
    UPDATE_MODE_A2    = 7,  // Anti-aliased text in menus (M5GFX: DU)
    UPDATE_MODE_NONE  = 8   // Write only, no refresh
} m5epd_update_mode_t;

class EpdDisplay {
   public:
    static constexpr int kWidth  = 540;
    static constexpr int kHeight = 960;

    // Call after M5.begin(): sets portrait rotation and allocates the shadow.
    bool begin();

    // 4bpp data, two pixels per byte, high nibble first, 0 = white, 15 = black.
    void WriteFullGram4bpp(const uint8_t *gram);
    void WritePartGram4bpp(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           const uint8_t *gram);
    // Raw IT8951 pattern: four 4-bit pixels per word, 0xF = white.
    void FillPartGram4bpp(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          uint16_t data);
    // Whites the panel memory; with init == true also refreshes the screen.
    void Clear(bool init = false);

    void UpdateFull(m5epd_update_mode_t mode);
    void UpdateArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    m5epd_update_mode_t mode);
    // Blocks until the panel has finished refreshing.
    void CheckAFSR();

    uint32_t UpdateCount() const {
        return _update_count;
    }
    void ResetUpdateCount() {
        _update_count = 0;
    }

    // Recursive lock shared with EpdCanvas pushes from other tasks.
    void lock();
    void unlock();

   private:
    void writeRect(int x, int y, int w, int h, const uint8_t *src,
                   int src_stride);
    bool clip(int &x, int &y, int &w, int &h) const;

    uint8_t *_shadow           = nullptr;
    SemaphoreHandle_t _mutex   = nullptr;
    uint32_t _update_count     = 0;
};

extern EpdDisplay EPD;
