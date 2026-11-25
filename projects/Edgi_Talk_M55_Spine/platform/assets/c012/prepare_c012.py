#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare user-provided Spine 4.0 C012 exports for the Edgi-Talk GPU."""
import argparse
from pathlib import Path
from shutil import copyfile
import sys

from PIL import Image, ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pack_pma_atlas import encode_pixels


ATLAS_SIZE = 560
MODEL = "c012_00"


def read_atlas(text):
    sections = []
    for line in text.splitlines():
        key, separator, value = line.strip().partition(":")
        if not key:
            continue
        if not separator:
            sections.append((key, {}))
        else:
            sections[-1][1][key.strip()] = value.strip()
    return sections


def resize_atlas(text, source_size):
    sections = read_atlas(text)
    page, fields = sections[0]
    if page != MODEL + ".png" or fields.get("pma") != "true":
        raise ValueError("Expected the original premultiplied C012 atlas.")
    if tuple(map(int, fields["size"].split(","))) != (source_size, source_size):
        raise ValueError("Atlas and PNG dimensions disagree.")

    def edge(value):
        return (value * ATLAS_SIZE + source_size // 2) // source_size

    fields["size"] = f"{ATLAS_SIZE},{ATLAS_SIZE}"
    for name, fields in sections[1:]:
        x, y, width, height = map(int, fields["bounds"].split(","))
        rotation = fields.get("rotate", "0")
        swapped = rotation in ("true", "90", "270")
        packed_width, packed_height = (height, width) if swapped else (width, height)
        left, top = edge(x), edge(y)
        right, bottom = edge(x + packed_width), edge(y + packed_height)
        if not (0 <= left < right <= ATLAS_SIZE and 0 <= top < bottom <= ATLAS_SIZE):
            raise ValueError(f"Scaled region {name!r} has invalid bounds.")
        packed_width, packed_height = right - left, bottom - top
        width, height = (packed_height, packed_width) if swapped else (packed_width, packed_height)
        fields["bounds"] = f"{left},{top},{width},{height}"
        if "offsets" in fields:
            offset_x, offset_y, original_width, original_height = map(edge, map(int, fields["offsets"].split(",")))
            if offset_x + width > original_width or offset_y + height > original_height:
                raise ValueError(f"Scaled region {name!r} exceeds its original size.")
            fields["offsets"] = f"{offset_x},{offset_y},{original_width},{original_height}"

    lines = []
    for name, fields in sections:
        lines.append(name)
        lines.extend(f"{key}:{value}" for key, value in fields.items())
    return "\n".join(lines) + "\n"


def resize_pma(source):
    # Resample channels directly because the source is already premultiplied.
    # Clamp rounding errors and Lanczos ringing to the premultiplied invariant.
    alpha = source.getchannel("A")
    bands = [ImageChops.darker(channel, alpha) for channel in source.split()[:3]]
    r, g, b, alpha = [channel.resize((ATLAS_SIZE, ATLAS_SIZE), Image.Resampling.LANCZOS)
                      for channel in [*bands, alpha]]
    return Image.merge("RGBA", tuple(ImageChops.darker(channel, alpha) for channel in (r, g, b)) + (alpha,))


def validate_texture(encoded, expected):
    decoded = bytearray()
    source = 0
    while source < len(encoded):
        token = encoded[source]
        source += 1
        count = (token & 0x7f) + 1
        if token & 0x80:
            decoded.extend(encoded[source:source + 4] * count)
            source += 4
        else:
            size = count * 4
            decoded.extend(encoded[source:source + size])
            source += size
    if source != len(encoded) or decoded != expected:
        raise ValueError("Packed texture failed the RGBA round-trip check.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Directory containing the original C012 .atlas, .png and .skel")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("Choose a separate output directory to preserve the original exports.")
    atlas = args.source / f"{MODEL}.atlas"
    skeleton = args.source / f"{MODEL}.skel"
    with Image.open(args.source / f"{MODEL}.png") as source:
        image = source.convert("RGBA")
    if image.width != image.height:
        parser.error("Expected the square C012 source PNG.")
    if skeleton.read_bytes()[8:15] != b"\x074.0.47":
        parser.error("Expected the original Spine 4.0.47 binary skeleton.")

    atlas_text = resize_atlas(atlas.read_text(encoding="utf-8-sig"), image.width)
    texture = resize_pma(image)
    rle = encode_pixels(texture.getdata())
    validate_texture(rle, texture.tobytes())

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / f"{MODEL}.atlas").write_bytes(atlas_text.encode("utf-8"))
    (args.output / f"{MODEL}.rle").write_bytes(rle)
    copyfile(skeleton, args.output / f"{MODEL}.skel")
    print(f"{ATLAS_SIZE}x{ATLAS_SIZE} premultiplied RGBA ({len(rle):,} RLE bytes)")


if __name__ == "__main__":
    main()
