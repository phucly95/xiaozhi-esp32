import os
import json
import shutil

# Configurations
FONT_PATH = r"C:\Users\phucn\Downloads\Roboto\static\Roboto-Regular.ttf"
OUTPUT_DIR = "managed_components/78__xiaozhi-fonts/src"
# Use local node script directly to avoid path issues
LANG_CONFIG_PATH = "main/assets/locales/vi-VN/language.json"
# Use absolute path to node to ensure subprocess finds it
# Use absolute path to node to ensure subprocess finds it
NODE_EXE = r"C:\Program Files\nodejs\node.exe"
LV_FONT_CONV_JS = os.path.abspath("tools/node_modules/lv_font_conv/lv_font_conv.js")
LV_FONT_CONV = f'"{NODE_EXE}" "{LV_FONT_CONV_JS}"'
FLAGS = "--force-fast-kern-format --no-compress --no-prefilter"

import subprocess

def generate_font(name, size, bpp):
    output_file = os.path.join(OUTPUT_DIR, f"font_{name}_{size}_{bpp}.c")
    font_name = f"font_{name}_{size}_{bpp}"
    
    print(f"Generating {output_file}...")
    
    # Use range arguments for lv_font_conv to ensure all required characters are included
    # Vietnamese Ranges:
    # 0x20-0x7F:   Basic Latin
    # 0xA0-0xFF:   Latin-1 Supplement
    # 0x100-0x24F: Latin Extended-A & B (includes Ă, Đ, Ơ, Ư)
    # 0x300-0x36F: Combining Diacritical Marks
    # 0x1E00-0x1EFF: Latin Extended Additional (Main Vietnamese accents)
    # 0x20AB:      Vietnamese Dong Currency
    
    ranges = [
        "0x20-0x7F",
        "0xA0-0xFF",
        "0x100-0x24F",
        "0x300-0x36F",
        "0x1E00-0x1EFF",
        "0x20AB"
    ]
    
    cmd = [
        NODE_EXE,
        LV_FONT_CONV_JS,
        *FLAGS.split(),
        "--font", FONT_PATH,
        "--format", "lvgl",
        "--lv-include", "lvgl.h",
        "--bpp", str(bpp),
        "--size", str(size),
        "-o", output_file,
        "--lv-font-name", font_name
    ]
    
    # Add ranges
    for r in ranges:
        cmd.extend(["--range", r])

    try:
        # shell=False is safer and handles special chars in arguments automatically
        subprocess.run(cmd, check=True, shell=False)
        print(f"Successfully generated {output_file}")
    except subprocess.CalledProcessError as e:
        print(f"Error generating {output_file}: {e}")


def main():
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)
        
    print("Generating fonts with full Vietnamese Unicode ranges...")
    
    # Generate Roboto fonts matches standard sizes
    # Based on existing fonts: 14_1, 16_4, 20_4, 30_4
    # Note: size and bpp are arguments, symbols argument is removed
    generate_font("roboto", 14, 1)
    generate_font("roboto", 16, 4)
    generate_font("roboto", 20, 4)
    generate_font("roboto", 30, 4)
    
if __name__ == "__main__":
    main()
