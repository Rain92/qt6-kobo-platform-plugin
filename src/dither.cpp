#include "dither.h"

// c.f.,
// https://github.com/ImageMagick/ImageMagick/blob/ecfeac404e75f304004f0566557848c53030bad6/config/thresholds.xml#L107
static const uint8_t threshold_map_o8x8[64] = {
    1,  49, 13, 61, 4,  52, 16, 64, 33, 17, 45, 29, 36, 20, 48, 32, 9,  57, 5,  53, 12, 60,
    8,  56, 41, 25, 37, 21, 44, 28, 40, 24, 3,  51, 15, 63, 2,  50, 14, 62, 35, 19, 47, 31,
    34, 18, 46, 30, 11, 59, 7,  55, 10, 58, 6,  54, 43, 27, 39, 23, 42, 26, 38, 22};

static inline uint32_t div255(uint32_t v)
{
    uint32_t _v = v + 128;
    return ((_v >> 8U) + _v) >> 8U;
}

static inline uint8_t dither_o8x8(unsigned short int x, unsigned short int y, uint8_t v)
{
    // Constants:
    // Quantum = 8; Levels = 16; map Divisor = 65
    // QuantumRange = 0xFF
    // QuantumScale = 1.0 / QuantumRange
    //
    // threshold = QuantumScale * v * ((L-1) * (D-1) + 1)
    // NOTE: The initial computation of t (specifically, what we pass to DIV255) would overflow an uint8_t.
    //       With a Q8 input value, we're at no risk of ever underflowing, so, keep to unsigned maths.
    //       Technically, an uint16_t would be wide enough, but it gains us nothing,
    //       and requires a few explicit casts to make GCC happy ;).
    uint32_t t = div255(v * ((15U << 6U) + 1U));
    // level = t / (D-1);
    const uint32_t l = (t >> 6U);
    // t -= l * (D-1);
    t = (t - (l << 6U));

    // map width & height = 8
    // c = ClampToQuantum((l+(t >= map[(x % mw) + mw * (y % mh)])) * QuantumRange / (L-1));
    const uint32_t q = ((l + (t >= threshold_map_o8x8[(x & 7U) + 8U * (y & 7U)])) * 17U);
    // NOTE: We're doing unsigned maths, so, clamping is basically MIN(q, UINT8_MAX) ;).
    //       The only overflow we should ever catch should be for a few white (v = 0xFF) input pixels
    //       that get shifted to the next step (i.e., q = 272 (0xFF + 17)).
    return (q > UINT8_MAX ? UINT8_MAX : (uint8_t)q);
}

#ifdef __ARM_NEON__

// Bit exact vector version of dither_o8x8 for 8 pixels, c64 holds 64 - the thresholds of the 8 columns.
//
// t = div255(961 * v) needs 18 bit intermediates, which is avoided by splitting 961 * v into
// 256 * (3 * v) + 193 * v:
//   a = 193 * v + 128, b = 3 * v
//   div255(961 * v) = b + ((a + b + (a >> 8)) >> 8)   (every intermediate < 65536)
// The level l + (t % 64 >= threshold), with l = t / 64, is (t + 64 - threshold) / 64 for the thresholds 1..64, so
// the offset is added to b, and the level (16 at most, which the clamp to 255 makes 15) is narrowed right away.
static inline uint8x8_t dither8(uint8x8_t v, uint16x8_t c64)
{
    const uint16x8_t a = vmlal_u8(vdupq_n_u16(128), v, vdup_n_u8(193));
    const uint16x8_t s = vsraq_n_u16(vaddq_u16(a, vmull_u8(v, vdup_n_u8(3))), a, 8);  // a + b + (a >> 8)
    const uint16x8_t u = vsraq_n_u16(vmlal_u8(c64, v, vdup_n_u8(3)), s, 8);           // t + 64 - threshold
    const uint8x8_t level = vmin_u8(vshrn_n_u16(u, 6), vdup_n_u8(15));
    return vmul_u8(level, vdup_n_u8(17));
}

static void ditherRegion_NEON(uint8_t* dst, int dstStride, const uint8_t* src, int srcStride, int x0,
                              int y0, int width, int height)
{
    uint8_t c64Row[8];

    for (int y = 0; y < height; y++)
    {
        const uint8_t* s = src + y * srcStride;
        uint8_t* d = dst + y * dstStride;
        const int py = (y0 + y) & 7;

        // 64 - the thresholds of the 8 columns starting at x0 (the pattern repeats every 8 pixels)
        for (int i = 0; i < 8; i++)
            c64Row[i] = 64 - threshold_map_o8x8[((x0 + i) & 7) + 8 * py];
        const uint16x8_t c64 = vmovl_u8(vld1_u8(c64Row));

        int x = 0;
        for (; x + 16 <= width; x += 16)
        {
            __builtin_prefetch(s + x + 256);
            uint8x16_t v = vld1q_u8(s + x);
            uint8x8_t lo = dither8(vget_low_u8(v), c64);
            uint8x8_t hi = dither8(vget_high_u8(v), c64);
            vst1q_u8(d + x, vcombine_u8(lo, hi));
        }
        if (x + 8 <= width)
        {
            vst1_u8(d + x, dither8(vld1_u8(s + x), c64));
            x += 8;
        }
        for (; x < width; x++)
            d[x] = dither_o8x8(x0 + x, y0 + y, s[x]);
    }
}

#else

static void ditherRegion_fallback(uint8_t* dst, int dstStride, const uint8_t* src, int srcStride, int x0,
                                  int y0, int width, int height)
{
    for (int y = 0; y < height; y++)
    {
        const uint8_t* s = src + y * srcStride;
        uint8_t* d = dst + y * dstStride;
        for (int x = 0; x < width; x++)
            d[x] = dither_o8x8(x0 + x, y0 + y, s[x]);
    }
}

#endif

void ditherBufferRegion(uint8_t* dst, int dstStride, const uint8_t* src, int srcStride, int x, int y,
                        int width, int height)
{
#ifdef __ARM_NEON__
    ditherRegion_NEON(dst, dstStride, src, srcStride, x, y, width, height);
#else
    ditherRegion_fallback(dst, dstStride, src, srcStride, x, y, width, height);
#endif
}

void ditherBuffer(uint8_t* bufferDest, uint8_t* bufferSrc, int width, int height)
{
    ditherBufferRegion(bufferDest, width, bufferSrc, width, 0, 0, width, height);
}

void ditherBufferInplace(uint8_t* buffer, int width, int height)
{
    ditherBufferRegion(buffer, width, buffer, width, 0, 0, width, height);
}

const uint8_t VALUES_12BPP[] = {0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255};

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMPED(x, xmin, xmax) MAX((xmin), MIN((xmax), (x)))

//	Floyd-Steinberg dither uses constants 7/16 5/16 3/16 and 1/16
//	But instead of using real arythmetic, I will use integer on by
//	applying shifting ( << 8 )
//	When use the constants, don't foget to shift back the result ( >> 8 )
#define f7_16 112  // const int	f7	= (7 << 8) / 16;
#define f5_16 80   // const int	f5	= (5 << 8) / 16;
#define f3_16 48   // const int	f3	= (3 << 8) / 16;
#define f1_16 16   // const int	f1	= (1 << 8) / 16;

//	Color Floyd-Steinberg dither using 4 bit per color plane
void ditherFloydSteinberg(uint8_t* dest, uint8_t* src, int width, int height)
{
    const int size = width * height;

    int16_t* errorB = (int16_t*)malloc(size * sizeof(int16_t));
    memset(errorB, 0, size * sizeof(int16_t));

    for (int y = 0, offset = 0, i = 0; y < height; y++, offset += width)
    {
        for (int x = 0; x < width; x++, i++)
        {
            const uint8_t oldVal = src[offset + x];

            int oldValErr = oldVal + (errorB[i] >> 8);

            //	The error could produce values beyond the borders, so need to keep the color in range
            int idxB = CLAMPED(oldVal + (errorB[i] >> 8), 0, 255);

            int newVal = VALUES_12BPP[idxB >> 4];  //	x >> 4 is the same as x / 16

            dest[offset + x] = newVal;

            int quantError = oldValErr - newVal;

            if (x + 1 < width)
                errorB[i + 1] += (quantError * f7_16);

            if (x - 1 > 0 && y + 1 < height)
                errorB[i + width - 1] += (quantError * f3_16);

            if (y + 1 < height)
                errorB[i + width + 0] += (quantError * f5_16);

            if (x + 1 < width && y + 1 < height)
                errorB[i + width + 1] += (quantError * f1_16);
        }
    }

    free(errorB);
}

//	Color Floyd-Steinberg dither using 4 bit per color plane
void ditherFloydSteinbergN(uint8_t* dest, uint8_t* src, int width, int height)
{
#ifdef __ARM_NEON__
    const int size = width * height;

    int16_t* errorB = (int16_t*)malloc(size * sizeof(int16_t));
    memset(errorB, 0, size * sizeof(int16_t));

    int16_t* errorLine = (int16_t*)malloc(width * sizeof(int16_t));

    for (int y = 0, offset = 0, i = 0; y < height; y++, offset += width)
    {
        for (int x = 0; x < width; x++, i++)
        {
            const uint8_t oldVal = src[offset + x];

            int oldValErr = oldVal + (errorB[i] >> 8);

            //	The error could produce values beyond the borders, so need to keep the color in range
            int idxB = CLAMPED(oldVal + (errorB[i] >> 8), 0, 255);

            int newVal = VALUES_12BPP[idxB >> 4];  //	x >> 4 is the same as x / 16

            dest[offset + x] = newVal;

            int quantError = oldValErr - newVal;
            errorLine[x] = quantError;

            int errorIdx = i + 1;
            if (x + 1 < width)
                errorB[errorIdx] += (quantError * f7_16);
        }
        if (y + 1 < height)
        {
            int x = 0;
            {
                int16_t quantError = errorLine[x];
                // errorB[i + x - 1] += (quantError * f3_16);
                errorB[i + x + 0] += (quantError * f5_16);
                errorB[i + x + 1] += (quantError * f1_16);
            }
            for (x = 1; x + 7 < width - 1; x += 8)
            {
                int16x8_t quantErrorv = vld1q_s16(&errorLine[x]);

                int16x8_t multv1 = vmulq_n_s16(quantErrorv, f3_16);
                int16x8_t resErrorv1 = vld1q_s16(&errorB[i + x - 1]);
                int16x8_t resAddv1 = vaddq_s16(resErrorv1, multv1);
                vst1q_s16(&errorB[i + x - 1], resAddv1);

                int16x8_t multv2 = vmulq_n_s16(quantErrorv, f5_16);
                int16x8_t resErrorv2 = vld1q_s16(&errorB[i + x]);
                int16x8_t resAddv2 = vaddq_s16(resErrorv2, multv2);
                vst1q_s16(&errorB[i + x], resAddv2);

                int16x8_t multv3 = vmulq_n_s16(quantErrorv, f1_16);
                int16x8_t resErrorv3 = vld1q_s16(&errorB[i + x + 1]);
                int16x8_t resAddv3 = vaddq_s16(resErrorv3, multv3);
                vst1q_s16(&errorB[i + x + 1], resAddv3);
            }
            for (; x < width; x++)
            {
                int16_t quantError = errorLine[x];
                errorB[i + x - 1] += (quantError * f3_16);
                errorB[i + x + 0] += (quantError * f5_16);
                if (x + 1 < width)
                    errorB[i + x + 1] += (quantError * f1_16);
            }
        }
    }

    free(errorB);
    free(errorLine);
#else
    ditherFloydSteinberg(dest, src, width, height);
#endif
}
