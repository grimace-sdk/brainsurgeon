import struct
import sys


def read_bmp(path):
    with open(path, "rb") as file:
        data = file.read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits != 24:
        raise ValueError(f"{path}: {bits} bpp, expected 24")
    stride = (width * 3 + 3) & ~3
    rows = []
    for row in range(abs(height)):
        source_row = abs(height) - 1 - row if height > 0 else row
        start = offset + source_row * stride
        rows.append(data[start:start + width * 3])
    return width, abs(height), rows


def write_bmp(path, width, height, rows):
    stride = (width * 3 + 3) & ~3
    padding = b"\0" * (stride - width * 3)
    body = b"".join(rows[height - 1 - row] + padding for row in range(height))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    with open(path, "wb") as file:
        file.write(header + info + body)


def main():
    if len(sys.argv) < 5:
        sys.exit("Usage: bmp-contact-sheet.py OUTPUT.bmp COLUMNS SCALE INPUT.bmp...")
    output, columns, scale, inputs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:]
    images = [read_bmp(path) for path in inputs]
    tile_width = max(image[0] for image in images) // scale
    tile_height = max(image[1] for image in images) // scale
    gap = 4
    rows_of_tiles = (len(images) + columns - 1) // columns
    sheet_width = columns * (tile_width + gap) + gap
    sheet_height = rows_of_tiles * (tile_height + gap) + gap
    sheet = [bytearray(b"\x40" * sheet_width * 3) for _ in range(sheet_height)]
    for index, (width, height, rows) in enumerate(images):
        left = gap + (index % columns) * (tile_width + gap)
        top = gap + (index // columns) * (tile_height + gap)
        for y in range(height // scale):
            source = rows[y * scale]
            target = sheet[top + y]
            for x in range(width // scale):
                target[(left + x) * 3:(left + x) * 3 + 3] = source[x * scale * 3:x * scale * 3 + 3]
    write_bmp(output, sheet_width, sheet_height, [bytes(row) for row in sheet])


main()
