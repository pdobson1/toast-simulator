#!/usr/bin/env python3
"""Pads the ROM to a whole number of 128KB blocks, then writes the ROM end address and checksum into its header."""
import struct, sys

path = sys.argv[1]
rom = bytearray(open(path, 'rb').read())
size = max(0x20000, (len(rom) + 0x1FFFF) & ~0x1FFFF)
rom += b'\xff' * (size - len(rom))
struct.pack_into('>I', rom, 0x1A4, size - 1)
checksum = sum(struct.unpack_from('>%dH' % ((size - 0x200) // 2), rom, 0x200)) & 0xFFFF
struct.pack_into('>H', rom, 0x18E, checksum)
open(path, 'wb').write(rom)
print('%s: %d KB, checksum %04X' % (path, size // 1024, checksum))
