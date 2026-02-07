# Roboto Fonts for Vietnamese

This component provides Roboto fonts with full Vietnamese Unicode support for the xiaozhi-esp32 project.

## Fonts Included

- `font_roboto_14_1` - Size 14px, 1 BPP
- `font_roboto_16_4` - Size 16px, 4 BPP
- `font_roboto_20_4` - Size 20px, 4 BPP
- `font_roboto_30_4` - Size 30px, 4 BPP

## Vietnamese Unicode Ranges

The fonts include the following Unicode ranges for complete Vietnamese support:

- `0x20-0x7F`: Basic Latin
- `0xA0-0xFF`: Latin-1 Supplement
- `0x100-0x24F`: Latin Extended-A & B (includes Ă, Đ, Ơ, Ư)
- `0x300-0x36F`: Combining Diacritical Marks
- `0x1E00-0x1EFF`: Latin Extended Additional (Main Vietnamese accents)
- `0x20AB`: Vietnamese Dong Currency (₫)

## Usage

Include the header in your code:

```c
#include "font_roboto.h"

// Use the font
lv_obj_set_style_text_font(label, &font_roboto_20_4, 0);
```

## Regenerating Fonts

To regenerate the fonts (e.g., to add more characters or change sizes):

1. Ensure you have the Roboto font file at `C:\Users\phucn\Downloads\Roboto\static\Roboto-Regular.ttf`
2. Run the generation script: `python scripts/generate_roboto_font.py`
3. Copy the generated files from `managed_components/78__xiaozhi-fonts/src/` to `components/roboto-fonts/src/`

## License

Roboto font is licensed under Apache License 2.0
