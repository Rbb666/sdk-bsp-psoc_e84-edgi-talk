#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Attach editable dash and hop timeline data to the pinned Spine 4.0.47 export."""

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import struct

from trim_c012_skel import ANIMATION_COUNT_OFFSET, ANIMATIONS, SOURCE_SHA256


MOTION_NAMES = ('dash', 'hop')
FIRMWARE_ANIMATIONS = ('action', 'idle')
BONE_TIMELINES = {
    'rotate': (0, ('value',)),
    'translate': (1, ('x', 'y')),
}


def read_varint(stream):
    value = 0
    for shift in range(0, 35, 7):
        byte = stream.read(1)[0]
        value |= (byte & 0x7f) << shift
        if byte < 0x80:
            return value
    raise ValueError('Invalid Spine binary varint.')


def write_varint(value):
    encoded = bytearray()
    while value >= 0x80:
        encoded.append((value & 0x7f) | 0x80)
        value >>= 7
    encoded.append(value)
    return encoded


def read_string(stream):
    length = read_varint(stream)
    return stream.read(max(0, length - 1)).decode('utf-8')


def read_bone_indices(original):
    """Read the bone table using the layout in Spine 4.0 SkeletonBinary."""
    stream = io.BytesIO(original)
    stream.seek(8)  # Export hash.
    if read_string(stream) != '4.0.47':
        raise ValueError('The C012 skeleton version must be 4.0.47.')
    stream.seek(16, io.SEEK_CUR)  # x, y, width, height.
    nonessential = stream.read(1) == b'\x01'
    if nonessential:
        stream.seek(4, io.SEEK_CUR)  # FPS.
        read_string(stream)  # Images path.
        read_string(stream)  # Audio path.

    for _ in range(read_varint(stream)):
        read_string(stream)

    indices = {}
    for index in range(read_varint(stream)):
        indices[read_string(stream)] = index
        if index:
            read_varint(stream)  # Parent bone index.
        stream.seek(32, io.SEEK_CUR)  # Eight setup transform floats.
        read_varint(stream)  # Transform mode.
        stream.seek(5 if nonessential else 1, io.SEEK_CUR)  # Skin flag and optional colour.
    return indices


def number(value, label):
    if type(value) not in (int, float):
        raise ValueError(f'{label} must be a finite number.')
    try:
        value = struct.unpack('>f', struct.pack('>f', value))[0]
    except OverflowError as error:
        raise ValueError(f'{label} is outside the Spine float range.') from error
    if not math.isfinite(value):
        raise ValueError(f'{label} must be a finite number.')
    return value


def encode_timeline(name, frames, label):
    if name not in BONE_TIMELINES:
        raise ValueError(f'{label} uses an unsupported bone timeline.')
    if not isinstance(frames, list) or not frames:
        raise ValueError(f'{label} must contain at least one keyframe.')

    timeline_type, values = BONE_TIMELINES[name]
    allowed_keys = {'time', 'curve', *values}
    keys = []
    for index, frame in enumerate(frames, 1):
        key_label = f'{label} key {index}'
        if not isinstance(frame, dict) or frame.keys() - allowed_keys:
            raise ValueError(f'{key_label} has invalid fields.')
        keys.append(tuple(number(frame.get(field, 0), f'{key_label} {field}')
                          for field in ('time', *values)))

    if keys[0][0] < 0 or any(right[0] <= left[0] for left, right in zip(keys, keys[1:])):
        raise ValueError(f'{label} times must be nonnegative and strictly increasing.')
    if 'curve' in frames[-1]:
        raise ValueError(f'{label} has a curve on the final key.')

    # 4.0 stores the next key's values before the segment type and control points.
    key_format = '>f' + 'f' * len(values)
    encoded_frames = bytearray(struct.pack(key_format, *keys[0]))
    bezier_count = 0
    for current, following, frame in zip(keys, keys[1:], frames):
        encoded_frames.extend(struct.pack(key_format, *following))
        curve = frame.get('curve')
        if curve is None:
            encoded_frames.append(0)
        elif curve == 'stepped':
            encoded_frames.append(1)
        elif isinstance(curve, list) and len(curve) == 4 * len(values):
            controls = [number(value, f'{label} curve at {current[0]:g}') for value in curve]
            if any(not current[0] <= time <= following[0] for time in controls[::2]):
                raise ValueError(f'{label} Bezier control times must lie between both keyframes.')
            encoded_frames.append(2)
            encoded_frames.extend(struct.pack('>' + 'f' * len(controls), *controls))
            bezier_count += len(values)
        else:
            raise ValueError(f'{label} curves need four control values per keyed component.')

    return (bytes([timeline_type]) + write_varint(len(keys)) +
            write_varint(bezier_count) + encoded_frames)


def encode_animation(name, animation, bone_indices):
    if not isinstance(animation, dict) or animation.keys() != {'bones'}:
        raise ValueError(f'{name} must contain only a bones map.')
    bones = animation['bones']
    if not isinstance(bones, dict) or not bones:
        raise ValueError(f'{name} must animate at least one bone.')

    groups = bytearray()
    timeline_count = 0
    for bone, timelines in bones.items():
        if bone not in bone_indices:
            raise ValueError(f'{name} references unknown C012 bone {bone!r}.')
        if not isinstance(timelines, dict) or not timelines:
            raise ValueError(f'{name} bone {bone!r} must have at least one timeline.')
        groups.extend(write_varint(bone_indices[bone]))
        groups.extend(write_varint(len(timelines)))
        timeline_count += len(timelines)
        for timeline, frames in timelines.items():
            groups.extend(encode_timeline(timeline, frames, f'{name}/{bone}/{timeline}'))

    encoded_name = name.encode('utf-8')
    # No slot, IK, transform, path, deform, draw order or event timelines.
    return (write_varint(len(encoded_name) + 1) + encoded_name +
            write_varint(timeline_count) + b'\x00' +
            write_varint(len(bones)) + groups + bytes(6))


def with_animations(original, retained, motion_bytes):
    existing = b''.join(original[start:end] for name, (start, end) in ANIMATIONS.items()
                        if name in retained)
    count = write_varint(len(retained) + len(MOTION_NAMES))
    return original[:ANIMATION_COUNT_OFFSET] + count + existing + motion_bytes


def main():
    directory = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=directory / 'c012_00.skel')
    parser.add_argument('--motion', type=Path, default=directory / 'c012_motion.json')
    parser.add_argument('--output', type=Path, default=directory / 'c012_demo_40.skel',
                        help='Write the four-animation Spine 4.0 intermediate for conversion.')
    parser.add_argument('--full-output', type=Path, help='Write a skeleton with all original animations as well.')
    args = parser.parse_args()

    paths = [args.source, args.motion, args.output]
    if args.full_output is not None:
        paths.append(args.full_output)
    if len(set(path.resolve() for path in paths)) != len(paths):
        parser.error('Choose distinct paths for the original, motion data and outputs.')

    try:
        original = args.source.read_bytes()
        if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
            raise ValueError('Input does not match the supplied C012 Spine 4.0.47 export.')
        data = json.loads(args.motion.read_text(encoding='utf-8'))
        animations = data.get('animations') if isinstance(data, dict) else None
        if not isinstance(animations, dict) or animations.keys() != set(MOTION_NAMES):
            raise ValueError('Motion data must contain exactly dash and hop under animations.')
        bone_indices = read_bone_indices(original)
        motion_bytes = b''.join(encode_animation(name, animations[name], bone_indices)
                                for name in MOTION_NAMES)
        args.output.write_bytes(with_animations(original, FIRMWARE_ANIMATIONS, motion_bytes))
        if args.full_output is not None:
            args.full_output.write_bytes(with_animations(original, ANIMATIONS, motion_bytes))
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
