import struct
import sys


def read_bmp(path):
    with open(path, "rb") as file:
        data = file.read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits not in (16, 24):
        sys.exit(f"{path}: {bits} bpp not supported")
    pixel_bytes = bits // 8
    stride = (width * pixel_bytes + 3) & ~3
    rows = []
    for row in range(abs(height)):
        source_row = abs(height) - 1 - row if height > 0 else row
        start = offset + source_row * stride
        source = data[start:start + width * pixel_bytes]
        if bits == 24:
            rows.append(source)
            continue
        converted = bytearray()
        for x in range(width):
            value = source[x * 2] | source[x * 2 + 1] << 8
            converted += bytes((((value >> shift) & 0x1f) * 255 // 31) for shift in (0, 5, 10))
        rows.append(bytes(converted))
    return width, abs(height), rows


def main():
    if len(sys.argv) != 4:
        sys.exit("Usage: bmp-diff.py REFERENCE.bmp CANDIDATE.bmp DIFF.bmp")
    reference, candidate, output = sys.argv[1:]
    width, height, reference_rows = read_bmp(reference)
    other_width, other_height, candidate_rows = read_bmp(candidate)
    if (width, height) != (other_width, other_height):
        print(f"size differs: {width}x{height} against {other_width}x{other_height}")
        return
    differing = 0
    left, top, right, bottom = width, height, -1, -1
    diff_rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            a = reference_rows[y][x * 3:x * 3 + 3]
            b = candidate_rows[y][x * 3:x * 3 + 3]
            if a != b:
                differing += 1
                left, top, right, bottom = min(left, x), min(top, y), max(right, x), max(bottom, y)
                row += b"\x00\x00\xff"
            else:
                row += bytes(value // 4 + 160 for value in a)
        diff_rows.append(bytes(row))
    stride = (width * 3 + 3) & ~3
    padding = b"\0" * (stride - width * 3)
    body = b"".join(diff_rows[height - 1 - row] + padding for row in range(height))
    with open(output, "wb") as file:
        file.write(struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54))
        file.write(struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(body), 2835, 2835, 0, 0))
        file.write(body)
    if differing:
        print(f"{differing} pixels differ, bounds ({left},{top})-({right + 1},{bottom + 1})")
    else:
        print("identical")


main()
