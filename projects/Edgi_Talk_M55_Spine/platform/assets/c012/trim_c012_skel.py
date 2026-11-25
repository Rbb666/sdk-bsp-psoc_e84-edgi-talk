#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep selected animations in the supplied C012 Spine 4.0.47 binary."""

import argparse
import hashlib
from pathlib import Path


# Recorded from SkeletonBinary::DataInput::cursor immediately before the
# animation count, then before the name and after each complete animation.
# The reference reader is the official Spine 4.0 runtime at 425ce416bb21.
# The digest pins these offsets to the exact original C012 export.
SOURCE_SHA256 = '68b7a5a817770af31113b27b9f4e820185991ba0a5856bc04b20405198c7d8ea'
ANIMATION_COUNT_OFFSET = 153_808
ANIMATIONS = {
    'action': (153_809, 277_407),
    'angry': (277_407, 293_224),
    'delight': (293_224, 309_143),
    'expression_0': (309_143, 431_234),
    'idle': (431_234, 449_157),
    'no': (449_157, 465_489),
    'pain': (465_489, 481_315),
    'sad': (481_315, 497_124),
    'shy': (497_124, 512_937),
    'talk_end': (512_937, 513_958),
    'talk_start': (513_958, 516_046),
    'void': (516_046, 533_901),
    'worry': (533_901, 549_643),
}


def main():
    directory = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', nargs='?', type=Path, default=directory / 'c012_00.skel')
    parser.add_argument('output', nargs='?', type=Path, default=directory / 'c012_demo_40.skel')
    parser.add_argument('--animations', nargs='+', choices=ANIMATIONS, default=('idle', 'action'))
    args = parser.parse_args()

    if args.source.resolve() == args.output.resolve():
        parser.error('Choose an output path distinct from the original skeleton.')

    original = args.source.read_bytes()
    if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
        parser.error('Input SHA-256 does not match the supplied C012 Spine 4.0.47 export.')

    selected = set(args.animations)
    if len(selected) != len(args.animations):
        parser.error('Each animation may be selected only once.')

    # With at most 13 tracks, the replacement count is a one-byte Spine varint.
    # The rig prefix and each selected animation block are copied verbatim.
    animation_bytes = b''.join(original[start:end] for name, (start, end) in ANIMATIONS.items()
                               if name in selected)
    output = original[:ANIMATION_COUNT_OFFSET] + bytes([len(selected)]) + animation_bytes
    args.output.write_bytes(output)


if __name__ == '__main__':
    main()
