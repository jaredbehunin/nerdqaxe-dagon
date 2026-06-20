#!/usr/bin/env python3
"""RGB565 PNG->C converter with LITTLE-ENDIAN byte order (low byte first),
matching what this firmware's LVGL (LV_COLOR_16_SWAP 0, little-endian ESP32-S3)
expects. The repo's convert_single.py emits big-endian, which renders R/B-swapped
(orange shows as blue). Output is LV_IMG_CF_TRUE_COLOR."""
import sys, os
from PIL import Image

def convert(theme, image_path, screen):
    img = Image.open(image_path).convert('RGB')
    w, h = img.size
    var = f"{theme}_{screen}" if theme else screen
    out = f"ui_img_{screen}_png.c"
    px = img.load()
    data = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            # big-endian (high byte first) to match LV_COLOR_16_SWAP=1
            data.append((v >> 8) & 0xFF)
            data.append(v & 0xFF)
    with open(out, 'w') as f:
        f.write('#include "lvgl.h"\n\n')
        f.write('#ifndef LV_ATTRIBUTE_MEM_ALIGN\n    #define LV_ATTRIBUTE_MEM_ALIGN\n#endif\n\n')
        f.write(f'// IMAGE DATA: {os.path.basename(image_path)} (little-endian RGB565)\n')
        f.write(f'const LV_ATTRIBUTE_MEM_ALIGN uint8_t ui_img_{var}_png_data[] = {{\n')
        for i, val in enumerate(data):
            if i % 16 == 0:
                f.write('    ')
            f.write(f'0x{val:02X}, ')
            if (i + 1) % 16 == 0:
                f.write('\n')
        f.write('\n};\n\n')
        f.write(f'const lv_img_dsc_t ui_img_{var}_png = {{\n')
        f.write('    .header.always_zero = 0,\n')
        f.write(f'    .header.w = {w},\n')
        f.write(f'    .header.h = {h},\n')
        f.write(f'    .data_size = sizeof(ui_img_{var}_png_data),\n')
        f.write('    .header.cf = LV_IMG_CF_TRUE_COLOR,\n')
        f.write(f'    .data = ui_img_{var}_png_data\n')
        f.write('};\n')
    print(f"wrote {out} ({w}x{h}, {len(data)} bytes)")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2], sys.argv[3])
