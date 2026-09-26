#!/usr/bin/env python3
"""Builds the ShillGramm themes in the style of the agents panel.

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
            # 'value | fallback' -> keep the primary value only.
            result[m.group(1)] = m.group(2).split('|')[0].strip()
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

# Palette of the agents panel (Панель управления агентами Claude) as it is
# rendered: cool neutrals, orange "hot" counters, green only for links/focus.
DAY = dict(bg='#f0f0f2', panel='#fafafb', side='#f1f1f3', soft='#eaeaed',
           pill='#e0e0e5', line='#e2e2e6', lineSoft='#ececef', ink='#18191b',
           inkOver='#000000', muted='#5e6268', faint='#8a8e95', accent='#1d6b48',
           accentSoft='#e4efe9', sel='#e6ebf6', selLine='#c9d5f2', hot='#b25a0c',
           note='#f8efe2', noteLine='#ecd6b8', ok='#1d7a50', warn='#b25a0c', bad='#c0362c')
# Night: the graphite-navy of the agents panel icon (#1b2130 -> #2a3346)
# with its status dots: amber counters, green accent.
NIGHT = dict(bg='#171c28', panel='#1b2130', side='#181d2a', soft='#242b3b',
             pill='#2a3346', line='#2a3244', lineSoft='#222938', ink='#e6e9ef',
             inkOver='#ffffff', muted='#a0a9b8', faint='#7a8496', accent='#22c55e',
             accentSoft='#1d3329', sel='#26314a', selLine='#34425f', hot='#f5a524',
             note='#2b2a22', noteLine='#4a4128', ok='#22c55e', warn='#f5a524', bad='#ef6a5e')


def warm(l, a='', dark=False):
    """Cool neutral like the panel's grays (#f7f7f8, #e7e7ea)."""
    return hexc(l, l, min(1, l + 0.004), a)


def mono(key, value, dark):
    P = NIGHT if dark else DAY
    r, g, b, a = rgba(value)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    if key.startswith(KEEP_COLOUR):
        return value  # answer green / hang up red keep their meaning
    decorative = 'Peer' in key or 'Userpic' in key
    if meaningful(h, s) and not decorative:
        deg = h * 360
        return (P['warn'] if 28 < deg < 52 else P['bad']) + a
    fg = 'Fg' in key
    if 'Userpic' in key:
        m = re.search(r'Peer(\d)', key)
        n = int(m.group(1)) if m else 0
        return warm((0.30 if dark else 0.24) + 0.06 * (n % 4), a, dark)
    if decorative:  # member name colours
        return warm((0.78 if dark else 0.28) + 0.04 * (sum(map(ord, key)) % 3), a, dark)
    if s > 0.25:  # an accent colour
        if fg:
            return P['accent'] + a
        if l >= 0.8 or (dark and l < 0.3):
            return P['sel'] + a
        return P['ink'] + a
    if dark and l < 0.3:  # bluish night surfaces -> warm near black
        return warm(l * 0.62, a, True)
    return warm(l, a, dark)


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
            theme[k] = (NIGHT if dark else DAY)['faint']


day_src, nite_src = dict(day), dict(nite)
swap_out(day, nite_src, False)
swap_out(nite, day_src, True)


def common(P, dark):
    btn = P['ink']
    btnFg = P['panel'] if dark else '#ffffff'
    badgeFg = P['panel'] if dark else '#ffffff'
    return {
        'windowBg': P['panel'], 'windowFg': P['ink'], 'windowBgOver': P['side'],
        'windowBgRipple': P['pill'], 'windowSubTextFg': P['muted'],
        'windowSubTextFgOver': P['faint'], 'windowBoldFg': P['ink'],
        'windowBgActive': btn, 'windowFgActive': btnFg,
        'windowActiveTextFg': P['accent'], 'windowShadowFgFallback': P['line'],
        'activeButtonBg': btn, 'activeButtonBgOver': P['inkOver'],
        'activeButtonBgRipple': P['muted'], 'activeButtonFg': btnFg,
        'activeButtonFgOver': btnFg, 'activeLineFg': P['ink'],
        'lightButtonBg': P['panel'], 'lightButtonBgOver': P['soft'],
        'lightButtonBgRipple': P['pill'], 'lightButtonFg': P['ink'],
        'lightButtonFgOver': P['ink'],
        'menuBgOver': P['soft'], 'menuBgRipple': P['pill'],
        'filterInputBorderFg': P['line'], 'filterInputInactiveBg': P['side'],
        'filterInputActiveBg': P['panel'],
        'inputBorderFg': P['line'], 'scrollBarBg': P['faint'] + '66',
        'dialogsBg': P['panel'], 'dialogsBgOver': P['side'],
        'dialogsBgActive': P['sel'], 'dialogsRippleBgActive': P['selLine'],
        'dialogsNameFg': P['ink'], 'dialogsNameFgActive': P['ink'],
        'dialogsNameFgOver': P['ink'],
        'dialogsTextFg': P['muted'], 'dialogsTextFgActive': P['muted'],
        'dialogsTextFgOver': P['muted'],
        'dialogsTextFgService': P['ink'], 'dialogsTextFgServiceActive': P['ink'],
        'dialogsTextFgServiceOver': P['ink'],
        'dialogsDateFg': P['faint'], 'dialogsDateFgActive': P['faint'],
        'dialogsDateFgOver': P['faint'],
        'dialogsUnreadBg': P['hot'], 'dialogsUnreadFg': badgeFg,
        'dialogsUnreadBgOver': P['hot'], 'dialogsUnreadBgActive': P['hot'],
        'dialogsUnreadFgActive': badgeFg, 'dialogsUnreadFgOver': badgeFg,
        'dialogsUnreadBgMuted': P['faint'], 'dialogsUnreadBgMutedOver': P['faint'],
        'dialogsUnreadBgMutedActive': P['faint'],
        'dialogsSentIconFg': P['accent'], 'dialogsSentIconFgActive': P['accent'],
        'dialogsSentIconFgOver': P['accent'],
        'dialogsVerifiedIconBg': P['ink'], 'dialogsVerifiedIconBgActive': P['ink'],
        'dialogsVerifiedIconFgActive': P['panel'],
        'dialogsChatIconFgActive': P['ink'], 'dialogsOnlineBadgeFg': P['ok'],
        'dialogsMenuIconFg': P['muted'],
        'sideBarBg': P['side'], 'sideBarBgActive': P['pill'], 'sideBarBgRipple': P['pill'],
        'sideBarTextFg': P['muted'], 'sideBarTextFgActive': P['ink'],
        'sideBarIconFg': P['muted'], 'sideBarIconFgActive': P['ink'],
        'sideBarBadgeBg': P['hot'], 'sideBarBadgeBgActive': P['hot'],
        'sideBarBadgeBgMuted': P['faint'], 'sideBarBadgeBgMutedActive': P['faint'],
        'sideBarBadgeFg': badgeFg,
        'shadowFg': '#0000001a' if not dark else '#00000055',
        'historyComposeAreaBg': P['panel'], 'historyComposeAreaFg': P['ink'],
        'historyComposeAreaFgService': P['muted'], 'historyComposeIconFg': P['faint'],
        'historyComposeIconFgOver': P['ink'], 'historySendIconFg': P['ink'],
        'historySendIconFgOver': P['inkOver'], 'historyComposeButtonBg': P['panel'],
        'historyComposeButtonBgOver': P['soft'], 'historyReplyBg': P['panel'],
        'historyReplyIconFg': P['accent'], 'topBarBg': P['panel'],
        'placeholderFg': P['faint'], 'placeholderFgActive': P['faint'],
        'msgInBg': P['soft'], 'msgInBgSelected': P['pill'],
        'msgInShadow': '#00000000', 'msgInShadowSelected': '#00000000',
        'historyTextInFg': P['ink'], 'historyLinkInFg': P['accent'],
        'msgInDateFg': P['faint'], 'msgInServiceFg': P['accent'],
        'msgOutBg': P['sel'], 'msgOutBgSelected': P['selLine'],
        'msgOutShadow': '#00000000', 'msgOutShadowSelected': '#00000000',
        'historyTextOutFg': P['ink'], 'historyLinkOutFg': P['accent'],
        'msgOutDateFg': P['faint'], 'msgOutServiceFg': P['accent'],
        'historyOutIconFg': P['accent'], 'historyOutIconFgSelected': P['accent'],
        'msgServiceBg': P['soft'], 'msgServiceFg': P['muted'],
        'historyUnreadBarBg': P['note'], 'historyUnreadBarBorder': P['noteLine'],
        'historyUnreadBarFg': P['hot'],
        'historyToDownBg': P['panel'], 'historyToDownBgOver': P['soft'],
        'historyToDownFg': P['muted'], 'historyToDownShadow': '#00000022',
        'boxBg': P['panel'], 'boxTitleFg': P['ink'], 'boxDividerBg': P['side'],
        'checkboxFg': P['faint'],
        'titleBg': P['side'], 'titleBgActive': P['side'], 'titleFg': P['muted'],
        'titleFgActive': P['ink'],
    }


day.update(common(DAY, False))
nite.update(common(NIGHT, True))


def solid_png(hex6):
    r, g, b, _ = rgba(hex6)
    raw = b'\x00' + bytes(round(x * 255) for x in (r, g, b))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


for name, theme, bg in (('shillgramm-day', day, DAY['bg']), ('shillgramm-night', nite, NIGHT['bg'])):
    text = ''.join(f'{k}: {theme[k]};\n' for k in keys)
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('colors.tdesktop-theme', text)
        z.writestr('background.png', solid_png(bg))
    (out / f'{name}.tdesktop-theme').write_bytes(buf.getvalue())
    print(name, len(keys), 'colours')
