#ifndef CBIN_FONT_H
#define CBIN_FONT_H

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Create an LVGL font from binary data
lv_font_t* cbin_font_create(const uint8_t* data);

// Delete/Free the created font
void cbin_font_delete(lv_font_t* font);

// Create an LVGL image descriptor from binary data
lv_image_dsc_t* cbin_img_dsc_create(const uint8_t* data);

// Delete/Free the created image descriptor
void cbin_img_dsc_delete(lv_image_dsc_t* img_dsc);

#ifdef __cplusplus
}
#endif

#endif // CBIN_FONT_H
