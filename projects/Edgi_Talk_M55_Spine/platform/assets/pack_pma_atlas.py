#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Encode an already premultiplied Spine atlas PNG for the firmware."""
import argparse
from pathlib import Path

from PIL import Image

def encode_pixels(rgba):
    pixels = [bytes(pixel) for pixel in rgba]
    encoded = bytearray()
    index = 0
    while index < len(pixels):
        limit = min(index + 128, len(pixels))
        end = index + 1
        while end < limit and pixels[end] == pixels[index]:
            end += 1
        if end - index > 1:
            encoded.append(0x80 | (end - index - 1))
            encoded.extend(pixels[index])
        else:
            while end < limit:
                if end + 1 < len(pixels) and pixels[end] == pixels[end + 1]:
                    break
                end += 1
            encoded.append(end - index - 1)
            encoded.extend(b''.join(pixels[index:end]))
        index = end
    return encoded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('png', type=Path)
    parser.add_argument('rle', type=Path)
    args = parser.parse_args()

    with Image.open(args.png) as source:
        image = source.convert('RGBA')
    if any(max(r, g, b) > a for r, g, b, a in image.getdata()):
        parser.error('Input atlas is not premultiplied RGBA.')
    args.rle.write_bytes(encode_pixels(image.getdata()))


if __name__ == '__main__':
    main()
