#include "champions_assets.h"
#include "champions_model.h"

#include <stddef.h>
#include <stdint.h>

extern const uint8_t champions_blob_start[] asm("_binary_champions_assets_bin_start");
extern const uint8_t champions_blob_end[] asm("_binary_champions_assets_bin_end");

#define LOGO_LARGE 52
#define LOGO_SMALL 22
#define MARK_LARGE 70
#define MARK_SMALL 28
#define IMAGE_BYTES(side) ((side) * (side) * 3)
#define BLOB_BYTES (CHAMPIONS_TEAM_COUNT * (IMAGE_BYTES(LOGO_LARGE) + IMAGE_BYTES(LOGO_SMALL)) + \
                    IMAGE_BYTES(MARK_LARGE) + IMAGE_BYTES(MARK_SMALL))

static lv_image_dsc_t s_large[CHAMPIONS_TEAM_COUNT];
static lv_image_dsc_t s_small[CHAMPIONS_TEAM_COUNT];
static lv_image_dsc_t s_mark_large, s_mark_small;
static bool s_available;
static bool s_light_background[CHAMPIONS_TEAM_COUNT];

static lv_image_dsc_t descriptor(const uint8_t *data, uint16_t side)
{
    return (lv_image_dsc_t){
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565A8,
            .w = side,
            .h = side,
            .stride = side * 2,
        },
        .data_size = IMAGE_BYTES(side),
        .data = data,
    };
}

bool champions_assets_init(void)
{
    if ((size_t)(champions_blob_end - champions_blob_start) != BLOB_BYTES) return false;
    const uint8_t *data = champions_blob_start;
    for (int i = 0; i < CHAMPIONS_TEAM_COUNT; ++i) {
        s_large[i] = descriptor(data, LOGO_LARGE);
        uint32_t luminance = 0, opaque_pixels = 0;
        for (int pixel = 0; pixel < LOGO_LARGE * LOGO_LARGE; ++pixel) {
            if (data[LOGO_LARGE * LOGO_LARGE * 2 + pixel] < 220) continue;
            uint16_t color = data[pixel * 2] | ((uint16_t)data[pixel * 2 + 1] << 8);
            uint32_t r = (color >> 11) * 255 / 31;
            uint32_t g = ((color >> 5) & 63) * 255 / 63;
            uint32_t b = (color & 31) * 255 / 31;
            luminance += (299 * r + 587 * g + 114 * b) / 1000;
            ++opaque_pixels;
        }
        s_light_background[i] = opaque_pixels && luminance < opaque_pixels * 120;
        data += IMAGE_BYTES(LOGO_LARGE);
        s_small[i] = descriptor(data, LOGO_SMALL);
        data += IMAGE_BYTES(LOGO_SMALL);
    }
    s_mark_large = descriptor(data, MARK_LARGE);
    data += IMAGE_BYTES(MARK_LARGE);
    s_mark_small = descriptor(data, MARK_SMALL);
    s_available = true;
    return true;
}

const lv_image_dsc_t *champions_assets_logo(int team_index, bool small)
{
    if (!s_available || team_index < 0 || team_index >= CHAMPIONS_TEAM_COUNT) return NULL;
    return small ? &s_small[team_index] : &s_large[team_index];
}

const lv_image_dsc_t *champions_assets_mark(bool small)
{
    if (!s_available) return NULL;
    return small ? &s_mark_small : &s_mark_large;
}

bool champions_assets_light_background(int team_index)
{
    return s_available && team_index >= 0 && team_index < CHAMPIONS_TEAM_COUNT &&
           s_light_background[team_index];
}
