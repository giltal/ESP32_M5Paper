// Direct SPI access to the M5Paper's IT8951 e-paper controller.
//
// M5GFX initialises the controller (power, VCOM, packed writes) and keeps the
// touch panel; image loads and refreshes go through here instead. M5GFX
// converts every pixel through RGB with dithering and only exposes four of the
// panel's waveforms. This streams 4bpp data straight from a buffer in DMA
// bursts and lets the caller pick any waveform. Same protocol as M5Stack's
// M5EPD driver: 0x6000 command / 0x0000 data / 0x1000 read preambles, CS
// toggled per packet, HRDY (busy) checked before each packet.
//
// Callers must serialise access (EpdDisplay holds its lock around every call):
// the SPI bus is shared with M5GFX and the SD card.
#pragma once

#include <cstddef>
#include <cstdint>

namespace it8951 {

// Logical (portrait, 540x960) panel rotation used for loads. 1 = 90 degrees,
// the orientation M5GFX and M5EPD use for the M5Paper.
constexpr uint8_t kRotation = 1;
constexpr int kPanelW       = 960;  // native panel size
constexpr int kPanelH       = 540;

// Adds the SPI devices on the bus M5GFX initialised. Call after M5.begin().
bool begin();

// Image buffer address inside the controller (from GET_DEV_INFO).
uint32_t image_address();

// Load a w x h area at (x, y) in logical coordinates from 4bpp data
// (high nibble first). `src` points at the area's first pixel and rows are
// `stride` bytes apart. invert: source uses
// 0 = white / 15 = black, the controller wants 0 = black.
// x and w must be multiples of 4.
void load_area(int x, int y, int w, int h, const uint8_t *src, int stride,
               bool invert);

// Refresh a logical area with an IT8951 waveform (0 INIT, 1 DU, 2 GC16,
// 3 GL16, 4 GLR16, 5 GLD16, 6 DU4, 7 A2). Returns immediately.
void display_area(int x, int y, int w, int h, int mode);

// True while any refresh is still running.
bool busy();
// Wait until all refreshes have finished (up to timeout_ms).
bool wait_idle(uint32_t timeout_ms = 5000);

// Raw controller memory read (16-bit words), for diagnostics.
bool read_memory(uint32_t address, uint16_t *words, size_t count);

}  // namespace it8951
