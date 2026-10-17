#ifndef DITHER_H
#define DITHER_H

#include <stdint.h>
#include <stdlib.h>

#include <cstring>

#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

// Ordered 8x8 dithering of 8 bit grayscale to the 16 gray levels of the E-Ink panel.
// The threshold pattern is anchored at the absolute pixel position (x, y), so regions dithered
// separately fit together seamlessly.
void ditherBufferRegion(uint8_t* dst, int dstStride, const uint8_t* src, int srcStride, int x, int y,
                        int width, int height);

void ditherBuffer(uint8_t* bufferDest, uint8_t* bufferSrc, int width, int height);
void ditherBufferInplace(uint8_t* buffer, int width, int height);

void ditherFloydSteinberg(uint8_t* dest, uint8_t* src, int width, int height);
void ditherFloydSteinbergN(uint8_t* dest, uint8_t* src, int width, int height);

#endif  // DITHER_H
