// shared BGRA → RGB/RGBA pixel conversion for render workers
#pragma once

#include <cstdint>

// convert one row of BGRA source pixels to RGB (dstChannels=3) or
// RGBA (dstChannels=4). srcBpp is the source bytes per pixel (3 or 4).
// hoisted conditional keeps inner loop branch-free for auto-vectorization.
inline void convertBgraRow(const uint8_t *src, uint8_t *dst, int width,
                           int srcBpp, int dstChannels) {
  if (dstChannels == 4) {
    for (int x = 0; x < width; x++) {
      dst[0] = src[2]; // R ← B
      dst[1] = src[1]; // G
      dst[2] = src[0]; // B ← R
      dst[3] = src[3]; // A
      src += srcBpp;
      dst += 4;
    }
  } else {
    for (int x = 0; x < width; x++) {
      dst[0] = src[2]; // R ← B
      dst[1] = src[1]; // G
      dst[2] = src[0]; // B ← R
      src += srcBpp;
      dst += 3;
    }
  }
}
