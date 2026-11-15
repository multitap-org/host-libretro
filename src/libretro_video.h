#pragma once

// The core's latest video frame, kept by the host.
//
// libretro only guarantees the pointer passed to video_refresh is valid DURING
// the callback (invader kept it and broke on duplicate frames). So the host
// copies every frame into its own buffer, rows tightly packed, in one of two ways:
//
//   converted  to XRGB8888 with the X byte set to 0xFF. In memory that is
//              B, G, R, 0xFF per pixel: any frontend can use it (a B8G8R8A8
//              texture, an SDL surface). The software fallback.
//   native     in the core's own pixel format, rows copied as they are (no
//              per-pixel work). For frontends whose GPU samples that format
//              directly (Vulkan: R5G6B5 / A1R5G5B5 / B8G8R8A8 with alpha
//              forced to 1 in the image view). The X / unused bits are left
//              as the core wrote them: never treat them as alpha.

#include <cstddef>
#include <cstdint>

#include "libretro.h"

struct LibretroVideoFrame
{
    uint8_t* pixels          = nullptr;   // height rows of width * bytes_per_pixel bytes, tightly packed
    size_t   capacity_bytes  = 0;         // allocated size of `pixels`
    unsigned width           = 0;
    unsigned height          = 0;

    // The format of `pixels`: XRGB8888 when converted, the core's own when native.
    retro_pixel_format format          = RETRO_PIXEL_FORMAT_XRGB8888;
    unsigned           bytes_per_pixel = 4;

    uint64_t serial          = 0;         // +1 on every new frame (not on duplicates)
    uint64_t duplicates      = 0;         // frames where the core passed NULL ("show the last one again")
};

// 4 for XRGB8888, 2 for RGB565 / 0RGB1555, 0 for anything else.
unsigned libretro_video_bytes_per_pixel(retro_pixel_format format);

// Stores one core frame in `frame` (growing the buffer if needed): converted
// to XRGB8888, or copied in its own format when `native` is true.
// source_pitch is in bytes, as the core passed it. Returns false only on a
// bad format, a bad size or an allocation failure.
bool libretro_video_store_frame(LibretroVideoFrame* frame, const void* source, unsigned width, unsigned height,
                                size_t source_pitch, retro_pixel_format format, bool native);

void libretro_video_free(LibretroVideoFrame* frame);
