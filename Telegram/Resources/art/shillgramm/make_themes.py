#!/usr/bin/env python3
"""Builds the ShillGramm monochrome themes (docs D-05 of the Flutter client).

Day theme: the built-in palette (lib_ui/ui/colors.palette) made monochrome.
Night theme: the built-in night theme made monochrome.
Colours stay only where they carry meaning: red for errors, amber for
warnings. Outgoing bubbles are inverted: black in the day theme, light in the
night theme.

Usage: make_themes.py <tdesktop/Telegram> <out_dir>
"""
import colorsys, io, re, sys, zipfile, zlib, struct
from pathlib import Path

root, out = Path(sys.argv[1]), Path(sys.argv[2])
LINE = re.compile(r'^([A-Za-z0-9_]+):\s*([^;]+);')


def parse(text):
    result = {}
    for line in text.splitlines():
        m = LINE.match(line.strip())
        if m:
            result[m.group(1)] = m.group(2).strip()
    return result


base = parse((root / 'lib_ui/ui/colors.palette').read_text())
with zipfile.ZipFile(root / 'Resources/night.tdesktop-theme') as z:
    night = dict(base)
    night.update(parse(z.read('colors.tdesktop-theme').decode()))
keys = list(base)


def resolve(pal, key, depth=0):
    v = pal[key]
    if v.startswith('#') or depth > 20:
        return v
    return resolve(pal, v, depth + 1)


def rgba(h):
    h = h.lstrip('#')
    r, g, b = (int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))
    a = h[6:8] if len(h) == 8 else ''
    return r, g, b, a


def hexc(r, g, b, a=''):
    c = lambda x: max(0, min(255, round(x * 255)))
    return '#%02x%02x%02x%s' % (c(r), c(g), c(b), a)


def gray(l, a=''):
    return hexc(l, l, l, a)


def meaningful(h, s):
    deg = h * 360
    return s > 0.35 and (deg < 18 or deg > 342 or 28 < deg < 52)


KEEP_COLOUR = ('callAnswer', 'callHangup', 'callArrowMissed')


def mono(key, value, dark):
    r, g, b, a = rgba(value)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    if key.startswith(KEEP_COLOUR):
        return value  # answer green / hang up red keep their meaning
    decorative = 'Peer' in key or 'Userpic' in key
    if meaningful(h, s) and not decorative:
        return value
    fg = 'Fg' in key
    if 'Userpic' in key:
        m = re.search(r'Peer(\d)', key)
        n = int(m.group(1)) if m else 0
        return gray((0.30 if dark else 0.22) + 0.06 * (n % 4), a)
    if decorative and 'Userpic' not in key:  # member name colours
        return gray((0.78 if dark else 0.30) + 0.04 * (sum(map(ord, key)) % 3), a)
    if s > 0.25:  # an accent colour
        if dark:
            return gray(0.62 + 0.33 * l if fg else 0.18 + 0.55 * l, a)
        return gray(l if l >= 0.8 else 0.07 + 0.25 * (l - 0.3), a)
    if dark and l < 0.3:  # bluish night surfaces -> near black
        return gray(l * 0.62, a)
    return gray(l, a)


def build(pal, dark):
    res = {}
    for k in keys:
        v = pal.get(k, base[k])
        res[k] = mono(k, v, dark) if v.startswith('#') else v
    return res


day, nite = build(base, False), build(night, True)


def swap_out(theme, other, dark):
    """Outgoing bubble keys take the incoming colours of the opposite theme."""
    for k in keys:
        if 'Out' not in k or not (k.startswith('msg') or k.startswith('history')):
            continue
        twin = k.replace('Out', 'In')
        if twin in other:
            theme[k] = resolve(other, twin)
        else:
            theme[k] = '#5e5e63' if dark else '#b8b8bd'


day_src, nite_src = dict(day), dict(nite)
swap_out(day, nite_src, False)
swap_out(nite, day_src, True)

SIDEBAR_DAY = {
    'sideBarBg': '#f2f2f4', 'sideBarBgActive': '#e2e2e6', 'sideBarBgRipple': '#d8d8dc',
    'sideBarTextFg': '#6a6a6f', 'sideBarTextFgActive': '#1d1d1f',
    'sideBarIconFg': '#6a6a6f', 'sideBarIconFgActive': '#1d1d1f',
    'sideBarBadgeBg': '#1d1d1f', 'sideBarBadgeBgActive': '#1d1d1f',
    'sideBarBadgeBgMuted': '#a4a4a9', 'sideBarBadgeBgMutedActive': '#a4a4a9',
    'sideBarBadgeFg': '#ffffff',
}
SIDEBAR_NIGHT = {
    'sideBarBg': '#111113', 'sideBarBgActive': '#1f1f22', 'sideBarBgRipple': '#2c2c30',
    'sideBarTextFg': '#98989d', 'sideBarTextFgActive': '#f5f5f7',
    'sideBarIconFg': '#98989d', 'sideBarIconFgActive': '#f5f5f7',
    'sideBarBadgeBg': '#f5f5f7', 'sideBarBadgeBgActive': '#f5f5f7',
    'sideBarBadgeBgMuted': '#5e5e63', 'sideBarBadgeBgMutedActive': '#5e5e63',
    'sideBarBadgeFg': '#111113',
}
day.update(SIDEBAR_DAY)
nite.update(SIDEBAR_NIGHT)
day.update({
    'windowBg': '#ffffff', 'windowFg': '#1d1d1f', 'windowBgOver': '#f2f2f4',
    'windowSubTextFg': '#6a6a6f', 'windowBgActive': '#1d1d1f',
    'windowActiveTextFg': '#1d1d1f', 'activeButtonBg': '#1d1d1f',
    'activeButtonBgOver': '#3a3a3e', 'activeButtonFg': '#ffffff',
    'msgInBg': '#f0f0f2', 'msgInBgSelected': '#e2e2e6',
    'msgOutBg': '#1d1d1f', 'msgOutBgSelected': '#3a3a3e',
    'historyTextOutFg': '#ffffff', 'msgOutDateFg': '#b8b8bd',
})
nite.update({
    'windowBg': '#0b0b0c', 'windowFg': '#f5f5f7', 'windowBgOver': '#1c1c1f',
    'windowSubTextFg': '#98989d', 'windowActiveTextFg': '#f5f5f7',
    'msgInBg': '#1f1f22', 'msgInBgSelected': '#2c2c30',
    'msgOutBg': '#e8e8ed', 'msgOutBgSelected': '#d0d0d6',
    'historyTextOutFg': '#111113', 'msgOutDateFg': '#5e5e63',
})


def solid_png(hex6):
    r, g, b, _ = rgba(hex6)
    raw = b'\x00' + bytes(round(x * 255) for x in (r, g, b))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


for name, theme, bg in (('shillgramm-day', day, '#ffffff'), ('shillgramm-night', nite, '#000000')):
    text = ''.join(f'{k}: {theme[k]};\n' for k in keys)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('colors.tdesktop-theme', text)
        z.writestr('background.png', solid_png(bg))
    (out / f'{name}.tdesktop-theme').write_bytes(buf.getvalue())
    print(name, len(keys), 'colours')
