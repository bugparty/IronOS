#!/usr/bin/env python3
import os
from PIL import Image

def convert_image(image_path, output_path):
    if not os.path.exists(image_path):
        print(f"Error: {image_path} file not found.")
        return False

    # LCD 규격인 160x80으로 리사이즈
    img = Image.open(image_path).convert('RGB')
    img = img.resize((160, 80), Image.Resampling.LANCZOS)
    
    pixels = img.load()
    W, H = 160, 80
    out_bytes = []
    
    # column-major (세로 우선)
    for x in range(W):
        for y in range(H):
            r, g, b = pixels[x, y]
            
            # RGB565 변환
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F
            rgb565 = (r5 << 11) | (g6 << 5) | b5
            
            # 패널 INVON 특성 대응 색상 반전
            rgb565_inv = rgb565 ^ 0xFFFF
            
            out_bytes.append((rgb565_inv >> 8) & 0xFF)
            out_bytes.append(rgb565_inv & 0xFF)

    with open(output_path, 'w') as f:
        f.write('// Auto-generated Boot Logo for Fnirsi HS-02 (IronOS)\n')
        f.write('#pragma once\n')
        f.write('#include <stdint.h>\n\n')
        f.write('#define FNIRSI_LOGO_W 160\n')
        f.write('#define FNIRSI_LOGO_H 80\n')
        f.write('#define FNIRSI_LOGO_X 0\n')
        f.write('#define FNIRSI_LOGO_Y 0\n\n')
        f.write('static const uint8_t fnirsiBootLogo[FNIRSI_LOGO_W * FNIRSI_LOGO_H * 2] = {\n')
        
        for i in range(0, len(out_bytes), 16):
            chunk = out_bytes[i:i+16]
            hex_line = ",".join([f"0x{b:02x}" for b in chunk])
            f.write(f"  {hex_line}{',' if i + 16 < len(out_bytes) else ''}\n")
        
        f.write('};\n')
    
    print(f"Done! New bootlogo generated: {output_path}")
    return True

if __name__ == "__main__":
    convert_image('bootlogo.png', './source/Core/BSP/Fnirsi/FnirsiBootLogo.h')
