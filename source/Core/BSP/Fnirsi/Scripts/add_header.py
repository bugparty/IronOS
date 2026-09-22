#!/usr/bin/env python3
import sys
import struct

# usage:
# python3 add_header.py <string> <file.bin>
#   <string> is the three-character bootloader signature (HS-02B requires BIN)

with open(sys.argv[2], 'rb') as input, open(sys.argv[2][:-4]+'_firmware.bin', 'wb') as output:
    data = input.read()

    header = bytearray(0x10)
    format = sys.argv[1].encode('ASCII')
    header[0:8] = struct.pack('<3sBI', format[:3], 0x00, len(data)) # <3

    output.write(header)
    output.write(data)
