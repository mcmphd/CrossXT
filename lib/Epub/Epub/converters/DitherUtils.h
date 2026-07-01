#pragma once

#include <stdint.h>

#include "BlueNoiseMask.h"

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Apply Bayer dithering and quantize to 4 levels (0-3)
// Stateless - works correctly with any pixel processing order
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  int bayer = bayer4x4[y & 3][x & 3];
  int dither = (bayer - 8) * 5;  // Scale to +/-40 (half of quantization step 85)

  int adjusted = gray + dither;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  if (adjusted < 64) return 0;
  if (adjusted < 128) return 1;
  if (adjusted < 192) return 2;
  return 3;
}

// Apply blue-noise dithering and quantize to 4 levels (0-3). Same +/-40
// perturbation range and threshold bands as applyBayerDither4Level (so
// switching between the two doesn't change overall image brightness), but
// the 64x64 blue-noise mask has no periodic structure -- see BlueNoiseMask.h
// -- so it doesn't show a visible repeating tile the way the 4x4 Bayer
// matrix can. Same O(1) lookup-and-threshold cost per pixel; the mask lives
// in flash (const array), not RAM.
inline uint8_t applyBlueNoiseDither4Level(uint8_t gray, int x, int y) {
  int noise = blueNoise64x64[y & 63][x & 63];    // 0-255
  int dither = (noise - 128) * 80 / 255;         // Scale to +/-40, same range as Bayer

  int adjusted = gray + dither;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  if (adjusted < 64) return 0;
  if (adjusted < 128) return 1;
  if (adjusted < 192) return 2;
  return 3;
}
