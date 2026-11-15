// Checks libretro_video_store_frame(): converted, every libretro pixel format
// must come out as XRGB8888 with alpha 0xFF; native, the core's pixels must be
// copied unchanged with the row padding removed. Rows must be read with the
// core's pitch, and a pitch shorter than a row must be refused.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libretro_video.h"

static int g_failures = 0;

static void check_pixel(const LibretroVideoFrame* frame, unsigned x, unsigned y, uint32_t expected, const char* what)
{
    uint32_t actual = 0;
    std::memcpy(&actual, frame->pixels + (static_cast<size_t>(y) * frame->width + x) * 4u, sizeof(actual));
    if (actual != expected)
    {
        std::printf("FAIL %s: pixel (%u,%u) = 0x%08x, expected 0x%08x\n", what, x, y, actual, expected);
        g_failures++;
    }
}

static void check_pixel16(const LibretroVideoFrame* frame, unsigned x, unsigned y, uint16_t expected, const char* what)
{
    uint16_t actual = 0;
    std::memcpy(&actual, frame->pixels + (static_cast<size_t>(y) * frame->width + x) * 2u, sizeof(actual));
    if (actual != expected)
    {
        std::printf("FAIL %s: pixel (%u,%u) = 0x%04x, expected 0x%04x\n", what, x, y, actual, expected);
        g_failures++;
    }
}

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

int main()
{
    LibretroVideoFrame frame = {};

    // --- XRGB8888: 2x2, pitch padded to 16 bytes, X byte garbage (must become 0xFF).
    {
        uint8_t buffer[16 * 2] = {};
        uint32_t row0[2] = { 0x00FF0000u, 0x1200FF00u };   // red, green with junk in X
        uint32_t row1[2] = { 0xAB0000FFu, 0x00000000u };   // blue with junk, black
        std::memcpy(buffer, row0, sizeof(row0));
        std::memcpy(buffer + 16, row1, sizeof(row1));

        check(libretro_video_store_frame(&frame, buffer, 2, 2, 16, RETRO_PIXEL_FORMAT_XRGB8888, false), "XRGB8888 store");
        check_pixel(&frame, 0, 0, 0xFFFF0000u, "XRGB8888 red");
        check_pixel(&frame, 1, 0, 0xFF00FF00u, "XRGB8888 green");
        check_pixel(&frame, 0, 1, 0xFF0000FFu, "XRGB8888 blue");
        check_pixel(&frame, 1, 1, 0xFF000000u, "XRGB8888 black");
        check(frame.serial == 1, "XRGB8888 serial");
    }

    // --- RGB565: full intensity must expand to 255, not 248/252.
    {
        uint8_t buffer[8 * 2] = {};
        uint16_t row0[2] = { 0xF800u, 0x07E0u };   // red, green
        uint16_t row1[2] = { 0x001Fu, 0xFFFFu };   // blue, white
        std::memcpy(buffer, row0, sizeof(row0));
        std::memcpy(buffer + 8, row1, sizeof(row1));

        check(libretro_video_store_frame(&frame, buffer, 2, 2, 8, RETRO_PIXEL_FORMAT_RGB565, false), "RGB565 store");
        check_pixel(&frame, 0, 0, 0xFFFF0000u, "RGB565 red");
        check_pixel(&frame, 1, 0, 0xFF00FF00u, "RGB565 green");
        check_pixel(&frame, 0, 1, 0xFF0000FFu, "RGB565 blue");
        check_pixel(&frame, 1, 1, 0xFFFFFFFFu, "RGB565 white");
    }

    // --- 0RGB1555: the top bit is unused and must be ignored.
    {
        uint8_t buffer[4 * 2] = {};
        uint16_t row0[2] = { 0x7C00u, 0x83E0u };   // red, green (with the unused top bit set)
        uint16_t row1[2] = { 0x001Fu, 0x7FFFu };   // blue, white
        std::memcpy(buffer, row0, sizeof(row0));
        std::memcpy(buffer + 4, row1, sizeof(row1));

        check(libretro_video_store_frame(&frame, buffer, 2, 2, 4, RETRO_PIXEL_FORMAT_0RGB1555, false), "0RGB1555 store");
        check_pixel(&frame, 0, 0, 0xFFFF0000u, "0RGB1555 red");
        check_pixel(&frame, 1, 0, 0xFF00FF00u, "0RGB1555 green");
        check_pixel(&frame, 0, 1, 0xFF0000FFu, "0RGB1555 blue");
        check_pixel(&frame, 1, 1, 0xFFFFFFFFu, "0RGB1555 white");
    }

    // --- Refusals: pitch shorter than a row, zero size, unknown format. Frame must stay as it was.
    {
        uint8_t buffer[64] = {};
        uint64_t serial_before = frame.serial;
        check(!libretro_video_store_frame(&frame, buffer, 4, 2, 8, RETRO_PIXEL_FORMAT_XRGB8888, false), "pitch too small refused");
        check(!libretro_video_store_frame(&frame, buffer, 0, 2, 8, RETRO_PIXEL_FORMAT_XRGB8888, false), "zero width refused");
        check(!libretro_video_store_frame(&frame, buffer, 2, 2, 8, static_cast<retro_pixel_format>(77), false), "bad format refused");
        check(frame.serial == serial_before, "refused frames don't count");
        check_pixel(&frame, 1, 1, 0xFFFFFFFFu, "refused frames leave the old image");
    }

    // --- Growing: a bigger frame reallocates.
    {
        uint32_t big[8 * 8] = {};
        big[63] = 0x00123456u;
        check(libretro_video_store_frame(&frame, big, 8, 8, 32, RETRO_PIXEL_FORMAT_XRGB8888, false), "grow store");
        check(frame.capacity_bytes >= 64 * 4, "grow capacity");
        check_pixel(&frame, 7, 7, 0xFF123456u, "grow last pixel");
    }

    // --- Native RGB565: copied as is, padding (pitch 8 for 2 pixels) removed.
    {
        uint8_t buffer[8 * 2] = {};
        uint16_t row0[2] = { 0xF800u, 0x07E0u };
        uint16_t row1[2] = { 0x001Fu, 0xFFFFu };
        std::memcpy(buffer, row0, sizeof(row0));
        std::memcpy(buffer + 8, row1, sizeof(row1));

        check(libretro_video_store_frame(&frame, buffer, 2, 2, 8, RETRO_PIXEL_FORMAT_RGB565, true), "native RGB565 store");
        check(frame.format == RETRO_PIXEL_FORMAT_RGB565 && frame.bytes_per_pixel == 2, "native RGB565 format");
        check_pixel16(&frame, 0, 0, 0xF800u, "native RGB565 red");
        check_pixel16(&frame, 1, 0, 0x07E0u, "native RGB565 green");
        check_pixel16(&frame, 0, 1, 0x001Fu, "native RGB565 blue (second row, after the padding)");
        check_pixel16(&frame, 1, 1, 0xFFFFu, "native RGB565 white");
    }

    // --- Native 0RGB1555: the unused top bit is kept as the core wrote it (the
    //     GPU ignores it through the image view), tight pitch = one copy.
    {
        uint16_t buffer[2 * 2] = { 0x7C00u, 0x83E0u, 0x001Fu, 0x7FFFu };
        check(libretro_video_store_frame(&frame, buffer, 2, 2, 4, RETRO_PIXEL_FORMAT_0RGB1555, true), "native 0RGB1555 store");
        check(frame.format == RETRO_PIXEL_FORMAT_0RGB1555 && frame.bytes_per_pixel == 2, "native 0RGB1555 format");
        check_pixel16(&frame, 1, 0, 0x83E0u, "native 0RGB1555 top bit kept");
        check_pixel16(&frame, 1, 1, 0x7FFFu, "native 0RGB1555 white");
    }

    // --- Native XRGB8888: X byte kept, padding removed; converted again afterwards
    //     switches the frame back to XRGB8888 with alpha 0xFF.
    {
        uint8_t buffer[12 * 2] = {};
        uint32_t row0[2] = { 0x12FF0000u, 0x0000FF00u };
        uint32_t row1[2] = { 0x000000FFu, 0xAB000000u };
        std::memcpy(buffer, row0, sizeof(row0));
        std::memcpy(buffer + 12, row1, sizeof(row1));

        check(libretro_video_store_frame(&frame, buffer, 2, 2, 12, RETRO_PIXEL_FORMAT_XRGB8888, true), "native XRGB8888 store");
        check(frame.format == RETRO_PIXEL_FORMAT_XRGB8888 && frame.bytes_per_pixel == 4, "native XRGB8888 format");
        check_pixel(&frame, 0, 0, 0x12FF0000u, "native XRGB8888 X byte kept");
        check_pixel(&frame, 1, 1, 0xAB000000u, "native XRGB8888 last pixel");

        uint16_t small[1] = { 0xF800u };
        check(libretro_video_store_frame(&frame, small, 1, 1, 2, RETRO_PIXEL_FORMAT_RGB565, false), "converted after native");
        check(frame.format == RETRO_PIXEL_FORMAT_XRGB8888 && frame.bytes_per_pixel == 4, "converted format");
        check_pixel(&frame, 0, 0, 0xFFFF0000u, "converted after native red");
    }

    libretro_video_free(&frame);

    if (g_failures == 0)
    {
        std::printf("video conversion: all checks passed\n");
        return 0;
    }
    std::printf("video conversion: %d check(s) failed\n", g_failures);
    return 1;
}
