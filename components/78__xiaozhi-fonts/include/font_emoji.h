#ifndef FONT_EMOJI_H
#define FONT_EMOJI_H

#ifdef __cplusplus
extern "C" {
#endif

// Declare the emoji font
// This should match the font declared in your LVGL configuration or font files
// If you don't have a specific emoji font, you might need to use a fallback or define it
// For now, we'll declare it as an external LVGL font.
#include "lvgl.h"

LV_FONT_DECLARE(font_emoji_32);
LV_FONT_DECLARE(font_emoji_64);

#ifdef __cplusplus
}
#endif

#endif // FONT_EMOJI_H
