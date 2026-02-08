
#include "lvgl.h"

#ifndef FONT_AWESOME_14_1
#define FONT_AWESOME_14_1 1
#endif

#if FONT_AWESOME_14_1

/*-----------------
 *    BITMAPS
 *----------------*/
/*Dummy bitmap*/
static const uint8_t glyph_bitmap[] = { 0x00 };

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/
static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/
static const lv_font_fmt_txt_cmap_t cmaps[] = {
    { .range_start = 0 } // Zero initialize
};


/*--------------------
 *  FONT DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
};

lv_font_t font_awesome_14_1 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 14,                                 /*The maximum line height required by the font*/
    .base_line = 0,                                    /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,           /*The custom font data. Will be set by `lv_font_fmt_txt_dsc_t`*/
#if LV_VERSION_CHECK(8, 0, 0)
    .fallback = NULL,
#endif
    .user_data = NULL
};

#endif /*#if FONT_AWESOME_14_1*/
