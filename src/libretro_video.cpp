#include "libretro_video.h"

#include <cstring>
#include <new>

// Expands an N-bit channel to 8 bits by repeating its top bits, so full
// intensity maps to 255 (not 248) and black stays 0.
static uint32_t expand_5_bits(uint32_t value)
{
    return (value << 3) | (value >> 2);
}

static uint32_t expand_6_bits(uint32_t value)
{
    return (value << 2) | (value >> 4);
}

static constexpr uint32_t ALPHA_OPAQUE = 0xFF000000u;

// Pixel x of a converted row (memcpy: no alignment or aliasing assumptions;
// compilers turn it into a plain store).
static void store_pixel(uint8_t* row, unsigned x, uint32_t pixel)
{
    std::memcpy(row + x * 4u, &pixel, sizeof(pixel));
}

static void convert_row_xrgb8888(uint8_t* destination, const uint8_t* source, unsigned width)
{
    for (unsigned x = 0; x < width; x++)
    {
        uint32_t pixel = 0;
        std::memcpy(&pixel, source + x * 4u, sizeof(pixel));   // memcpy: the core's rows need not be 4-byte aligned
        store_pixel(destination, x, pixel | ALPHA_OPAQUE);
    }
}

static void convert_row_rgb565(uint8_t* destination, const uint8_t* source, unsigned width)
{
    for (unsigned x = 0; x < width; x++)
    {
        uint16_t pixel = 0;
        std::memcpy(&pixel, source + x * 2u, sizeof(pixel));
        uint32_t red   = expand_5_bits((pixel >> 11) & 0x1Fu);
        uint32_t green = expand_6_bits((pixel >> 5) & 0x3Fu);
        uint32_t blue  = expand_5_bits(pixel & 0x1Fu);
        store_pixel(destination, x, ALPHA_OPAQUE | (red << 16) | (green << 8) | blue);
    }
}

static void convert_row_0rgb1555(uint8_t* destination, const uint8_t* source, unsigned width)
{
    for (unsigned x = 0; x < width; x++)
    {
        uint16_t pixel = 0;
        std::memcpy(&pixel, source + x * 2u, sizeof(pixel));
        uint32_t red   = expand_5_bits((pixel >> 10) & 0x1Fu);
        uint32_t green = expand_5_bits((pixel >> 5) & 0x1Fu);
        uint32_t blue  = expand_5_bits(pixel & 0x1Fu);
        store_pixel(destination, x, ALPHA_OPAQUE | (red << 16) | (green << 8) | blue);
    }
}

unsigned libretro_video_bytes_per_pixel(retro_pixel_format format)
{
    switch (format)
    {
        case RETRO_PIXEL_FORMAT_XRGB8888: return 4;
        case RETRO_PIXEL_FORMAT_RGB565:   return 2;
        case RETRO_PIXEL_FORMAT_0RGB1555: return 2;
        default:                          return 0;
    }
}

bool libretro_video_store_frame(LibretroVideoFrame* frame, const void* source, unsigned width, unsigned height,
                                size_t source_pitch, retro_pixel_format format, bool native)
{
    size_t source_bytes_per_pixel = libretro_video_bytes_per_pixel(format);
    if (source_bytes_per_pixel == 0)
    {
        return false;
    }

    // A row in the core's buffer can't be shorter than the pixels it holds; reading
    // with such a pitch would run past the end of the core's buffer.
    size_t source_row_bytes = static_cast<size_t>(width) * source_bytes_per_pixel;
    if (width == 0 || height == 0 || source_pitch < source_row_bytes)
    {
        return false;
    }

    size_t stored_bytes_per_pixel = native ? source_bytes_per_pixel : 4u;
    size_t row_bytes = static_cast<size_t>(width) * stored_bytes_per_pixel;
    size_t needed = row_bytes * static_cast<size_t>(height);
    if (needed > frame->capacity_bytes)
    {
        uint8_t* bigger = new (std::nothrow) uint8_t[needed];
        if (bigger == nullptr)
        {
            return false;
        }
        delete[] frame->pixels;
        frame->pixels = bigger;
        frame->capacity_bytes = needed;
    }

    const uint8_t* source_bytes = static_cast<const uint8_t*>(source);
    if (native)
    {
        // Only the padding at the end of each row goes; one copy if there is none.
        if (source_pitch == row_bytes)
        {
            std::memcpy(frame->pixels, source_bytes, needed);
        }
        else
        {
            for (unsigned y = 0; y < height; y++)
            {
                std::memcpy(frame->pixels + static_cast<size_t>(y) * row_bytes,
                            source_bytes + static_cast<size_t>(y) * source_pitch, row_bytes);
            }
        }
    }
    else
    {
        for (unsigned y = 0; y < height; y++)
        {
            const uint8_t* source_row = source_bytes + static_cast<size_t>(y) * source_pitch;
            uint8_t* destination_row = frame->pixels + static_cast<size_t>(y) * row_bytes;

            switch (format)
            {
                case RETRO_PIXEL_FORMAT_XRGB8888: convert_row_xrgb8888(destination_row, source_row, width); break;
                case RETRO_PIXEL_FORMAT_RGB565:   convert_row_rgb565(destination_row, source_row, width);   break;
                case RETRO_PIXEL_FORMAT_0RGB1555: convert_row_0rgb1555(destination_row, source_row, width); break;
                default: break;
            }
        }
    }

    frame->width = width;
    frame->height = height;
    frame->format = native ? format : RETRO_PIXEL_FORMAT_XRGB8888;
    frame->bytes_per_pixel = static_cast<unsigned>(stored_bytes_per_pixel);
    frame->serial++;
    return true;
}

void libretro_video_free(LibretroVideoFrame* frame)
{
    delete[] frame->pixels;
    *frame = LibretroVideoFrame{};
}
