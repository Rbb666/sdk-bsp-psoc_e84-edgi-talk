# SPDX-License-Identifier: Apache-2.0
"""Regenerate the original Orbit demo's embedded Spine 4.3 data (Pillow required)."""

import json
from pathlib import Path

from PIL import Image, ImageDraw

from pack_pma_atlas import encode_pixels


SIZE = 128
SCALE = 4
INK = '#102136'
CYAN = '#92fff2'
TEAL = '#17bda9'
GOLD = '#ffcb63'
LAVENDER = '#bfbaff'
REGIONS = {
    'head': (1, 1, 61, 43),
    'body': (65, 1, 61, 51),
    'arm': (1, 49, 30, 36),
    'thruster': (34, 49, 26, 34),
    'scarf': (63, 55, 63, 27),
    'spark': (1, 89, 31, 31),
    'shadow': (35, 89, 60, 19),
    'antenna': (101, 89, 23, 36),
}


def point(coords):
    return tuple(round(c * SCALE) for c in coords)


def draw_atlas():
    atlas = Image.new('RGBA', (SIZE, SIZE))
    for name, (x, y, width, height) in REGIONS.items():
        sprite = Image.new('RGBA', (width * SCALE, height * SCALE))
        painter = ImageDraw.Draw(sprite)

        def rounded(box, radius, fill, outline=None, stroke=1):
            painter.rounded_rectangle(point(box), radius * SCALE, fill, outline, stroke * SCALE)

        def ellipse(box, fill, outline=None, stroke=1):
            painter.ellipse(point(box), fill, outline, stroke * SCALE)

        def line(points, fill, stroke=1):
            painter.line([point(p) for p in points], fill, stroke * SCALE, joint='curve')

        def polygon(points, fill):
            painter.polygon([point(p) for p in points], fill)

        if name == 'head':
            rounded((2, 15, 58, 32), 7, '#35456c', INK, 2)
            rounded((6, 4, 54, 39), 12, '#139b9a', INK, 2)
            rounded((8, 5, 52, 32), 11, '#46dec2')
            line(((17, 8), (39, 8), (46, 11)), '#c0fff0', 2)
            rounded((11, 13, 49, 33), 7, '#1c4057', INK, 2)
            rounded((14, 15, 46, 30), 5, '#153349')
            line(((16, 17), (28, 17), (33, 17)), '#315f71')
            rounded((19, 20, 24, 26), 2, CYAN)
            rounded((36, 20, 41, 26), 2, CYAN)
            line(((28, 26), (30, 27), (32, 26)), '#5ddfdb', 1)
            line(((20, 35), (27, 36), (38, 36)), TEAL)
        elif name == 'body':
            rounded((11, 2, 50, 48), 13, '#395075', INK, 2)
            polygon(((6, 9), (18, 4), (43, 4), (54, 9), (57, 20), (52, 34),
                     (46, 40), (15, 40), (9, 34), (4, 20)), INK)
            polygon(((8, 11), (18, 6), (43, 6), (52, 11), (55, 21), (50, 33),
                     (45, 38), (16, 38), (11, 33), (6, 21)), '#18a7a8')
            polygon(((8, 11), (18, 6), (43, 6), (52, 11), (54, 18), (46, 22),
                     (15, 22), (7, 18)), '#4ce0c3')
            line(((13, 12), (20, 8), (39, 8)), '#b7f8ed')
            rounded((16, 15, 45, 33), 6, '#155b6c', INK, 2)
            ellipse((24, 17, 37, 30), '#29798c')
            polygon(((31, 18), (34, 23), (39, 24), (34, 25), (31, 30),
                     (28, 25), (23, 24), (28, 23)), GOLD)
            line(((21, 43), (27, 43)), '#709dc6', 2)
            line(((34, 43), (40, 43)), '#709dc6', 2)
        elif name == 'arm':
            rounded((10, 1, 26, 31), 8, '#294e67', INK, 2)
            rounded((11, 3, 24, 22), 6, '#38cab6')
            line(((14, 6), (19, 5), (21, 7)), '#aafdea')
            rounded((3, 19, 25, 33), 7, '#7584bb', INK, 2)
            rounded((5, 20, 22, 27), 3, '#b4b5e5')
            line(((10, 27), (10, 31)), '#41466d')
            line(((15, 27), (15, 32)), '#41466d')
        elif name == 'thruster':
            polygon(((3, 4), (22, 4), (21, 11), (17, 21), (13, 33), (8, 23),
                     (5, 15)), '#4b70db')
            polygon(((5, 6), (20, 6), (19, 12), (15, 21), (13, 28), (9, 19),
                     (6, 12)), '#61d7ff')
            polygon(((8, 7), (17, 7), (16, 13), (13, 22), (10, 15)), '#c0fff4')
            rounded((2, 1, 23, 9), 3, '#6882b0', INK, 2)
            line(((6, 4), (19, 4)), '#abe5e8')
        elif name == 'scarf':
            polygon(((0, 10), (10, 5), (21, 6), (35, 9), (46, 8), (62, 1),
                     (54, 15), (62, 22), (45, 23), (32, 20), (21, 17), (8, 19), (0, 21)), INK)
            polygon(((0, 11), (10, 7), (21, 8), (35, 11), (47, 10), (57, 5),
                     (52, 15), (58, 21), (45, 21), (31, 18), (21, 15), (8, 17), (0, 19)), '#a748e7')
            polygon(((0, 11), (10, 7), (22, 8), (35, 11), (47, 10), (57, 5),
                     (53, 13), (44, 15), (34, 15), (21, 11), (8, 11), (0, 15)), '#dc8dfa')
            line(((12, 8), (23, 9), (34, 12), (43, 12)), '#efc7ff')
        elif name == 'spark':
            polygon(((15, 1), (18, 12), (29, 15), (18, 18), (15, 29), (12, 18),
                     (1, 15), (12, 12)), LAVENDER)
            polygon(((15, 5), (17, 13), (25, 15), (17, 17), (15, 25), (13, 17),
                     (5, 15), (13, 13)), '#e1ffff')
        elif name == 'shadow':
            ellipse((1, 2, 58, 17), '#153e5a')
            ellipse((9, 5, 50, 14), '#1e5572')
            ellipse((16, 7, 44, 12), '#34738a')
        else:
            line(((7, 32), (9, 20), (13, 9)), INK, 6)
            line(((8, 29), (10, 20), (13, 9)), '#8199cd', 2)
            ellipse((6, 1, 21, 15), INK)
            ellipse((8, 2, 19, 13), GOLD)
            ellipse((10, 3, 15, 7), '#fff6ad')
        atlas.paste(sprite.resize((width, height), Image.Resampling.LANCZOS), (x, y))
    return atlas


def skeleton_data():
    bones = [{'name': 'root'}]
    for name, parent, x, y in (
        ('body', 'root', 0, 0), ('head', 'body', 0, 33),
        ('arm-left', 'body', -27, 4), ('arm-right', 'body', 27, 4),
        ('thruster', 'body', 0, -18), ('scarf-tip', 'body', 62, 10),
        ('antenna', 'head', 6, 17),
        ('spark', 'root', -49, 24),
    ):
        bones.append(dict(name=name, parent=parent, x=x, y=y))

    slots = []
    attachments = {}

    def add(slot, bone, name, attachment, **settings):
        slots.append(dict(name=slot, bone=bone, attachment=name, **settings))
        attachments[slot] = {name: attachment}

    def quad(slot, bone, name, width, height, **pose):
        add(slot, bone, name, dict(width=width, height=height, **pose))

    quad('shadow', 'root', 'shadow', 83, 17, y=-54)
    quad('arm-right', 'arm-right', 'arm', 26, 36, x=9, y=-12, rotation=11, scaleX=-1)
    quad('thruster', 'thruster', 'thruster', 27, 32, y=-14)

    # The middle column shares two bones so the scarf deforms with the tip.
    vertices = []
    for x, y, tip_weight in ((20, 18, 0), (51, 18, .55), (82, 18, 1),
                             (20, -9, 0), (51, -9, .55), (82, -9, 1)):
        influences = []
        if tip_weight < 1:
            influences.append((1, x, y, 1-tip_weight))
        if tip_weight > 0:
            influences.append((6, x-62, y-10, tip_weight))
        vertices.append(len(influences))
        for influence in influences:
            vertices.extend(influence)
    add('scarf', 'body', 'scarf', dict(type='mesh', width=63, height=27,
                                      uvs=[0, 0, .5, 0, 1, 0, 0, 1, .5, 1, 1, 1],
                                      triangles=[0, 3, 1, 1, 3, 4, 1, 4, 2, 2, 4, 5],
                                      hull=6, vertices=vertices))
    quad('body', 'body', 'body', 64, 52)
    quad('arm-left', 'arm-left', 'arm', 26, 36, x=-9, y=-12, rotation=-11)
    quad('antenna', 'antenna', 'antenna', 23, 34, y=12)
    quad('head', 'head', 'head', 64, 46)
    add('spark-window', 'root', 'spark-window', dict(type='clipping', end='spark',
        convex=True, vertexCount=4, vertices=[-69, 45, -32, 45, -32, 18, -69, 18]))
    add('spark', 'spark', 'spark', dict(width=27, height=27), blend='additive', color='aac4ffaa')

    def keys(times, values, default):
        frames = []
        for time, value in zip(times, values):
            frame = {}
            if time != 0:
                frame['time'] = time
            if value != default:
                frame['value'] = value
            frames.append(frame)
        return frames

    idle_times = (0, .4, .8, 1.2, 1.6)
    action_times = (0, .13, .31, .53, .72, .94)

    def motion(times, **channels):
        return {kind: keys(times, values, 1 if kind.startswith('scale') else 0)
                for kind, values in channels.items()}

    return dict(skeleton=dict(spine='4.3.0', x=-73, y=-64, width=161, height=148),
        bones=bones, slots=slots, skins=[dict(name='default', attachments=attachments)],
        animations={
            'idle': {'bones': {
                'body': motion(idle_times, translatey=(0, 4, 8, 4, 0), rotate=(2, 0, -2, 0, 2)),
                'head': motion(idle_times, rotate=(-4, -1, 4, 1, -4)),
                'arm-left': motion(idle_times, rotate=(9, 0, -9, 0, 9)),
                'arm-right': motion(idle_times, rotate=(-7, 0, 7, 0, -7)),
                'antenna': motion(idle_times, rotate=(2, 8, 2, -5, 2)),
                'scarf-tip': motion(idle_times, rotate=(-4, -12, 4, 12, -4), translatey=(0, -4, 2, 5, 0)),
                'thruster': motion(idle_times, scaley=(.94, 1.11, .96, 1.12, .94)),
                'spark': motion(idle_times, rotate=(0, 45, 90, 135, 180), translatey=(0, 9, 15, 9, 0)),
            }},
            'action': {'bones': {
                'body': motion(action_times, translatey=(0, -9, 24, 14, 3, 0), rotate=(0, 12, -13, -7, 4, 0)),
                'head': motion(action_times, rotate=(0, 18, -15, 4, -5, 0)),
                'arm-left': motion(action_times, rotate=(0, 24, -119, -80, -21, 0)),
                'arm-right': motion(action_times, rotate=(0, -21, 73, 44, 13, 0)),
                'antenna': motion(action_times, rotate=(0, -20, 15, -20, 12, 0)),
                'scarf-tip': motion(action_times, rotate=(0, 16, -35, -22, 9, 0), translatey=(0, 9, -13, -8, 4, 0)),
                'thruster': motion(action_times, scaley=(1, .6, 1.5, 1.2, 1, 1)),
                'spark': motion(action_times, rotate=(0, 35, 90, 160, 225, 270), translatey=(0, 8, 15, 10, 4, 0)),
            }},
        })


def c_string(text):
    return '\n'.join('    ' + json.dumps(text[start:start+96]) for start in range(0, len(text), 96))


def main():
    atlas = [f'orbit.rgba\nsize: {SIZE}, {SIZE}\nfilter: Linear, Linear\npma: true\n']
    for name, bounds in REGIONS.items():
        atlas.append(f'{name}\nbounds: {", ".join(map(str, bounds))}\n')
    atlas_text = ''.join(atlas)
    rig_text = json.dumps(skeleton_data(), separators=(',', ':'))
    pixels = encode_pixels(((r*a+127)//255, (g*a+127)//255, (b*a+127)//255, a)
                           for r, g, b, a in draw_atlas().getdata())
    pixel_lines = ['    ' + ', '.join(f'0x{byte:02x}' for byte in pixels[i:i+16]) + ','
                   for i in range(0, len(pixels), 16)]
    contents = '/* SPDX-License-Identifier: Apache-2.0 */\n// Generated by generate_demo.py.\n\n'
    contents += 'const char spine_demo_atlas[] =\n' + c_string(atlas_text) + ';\n'
    contents += '\n'
    contents += 'const char spine_demo_skeleton[] =\n' + c_string(rig_text) + ';\n\n'
    contents += 'static const unsigned char packed_pixels[] = {\n' + '\n'.join(pixel_lines) + '\n};\n'
    Path(__file__).with_name('spine_demo_data.inc').write_text(contents, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
