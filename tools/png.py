"""Minimal PNG writer for 8-bit indexed images (no dependencies)."""
import struct
import zlib


def write_png(path, width, height, pixels, palette6, scale=1):
    """pixels: bytes of width*height palette indices; palette6: 768 bytes of
    6-bit VGA RGB values."""
    pal = bytes(min(255, (v & 0x3F) * 255 // 63) for v in palette6[:768])
    rows = []
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        if scale > 1:
            row = bytes(b for b in row for _ in range(scale))
        for _ in range(scale):
            rows.append(b'\0' + row)
    raw = zlib.compress(b''.join(rows), 9)

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width * scale, height * scale, 8, 3, 0, 0, 0))
    png += chunk(b'PLTE', pal)
    png += chunk(b'IDAT', raw)
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)
