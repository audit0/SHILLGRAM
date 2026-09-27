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
# Apple system neutrals with the agents panel's restraint: graphite for
# emphasis, one quiet green for links, no loud colours.
DAY = dict(bg='#dcdce2', panel='#e9e9ed', side='#e2e2e6', soft='#dfdfe4',
           pill='#d2d2d9', line='#cfcfd6', lineSoft='#dadae0', ink='#1d1d1f',
           inkOver='#000000', muted='#636368', faint='#7c7c82', accent='#1d6b48',
           accentSoft='#dde9e2', sel='#d5d5dc', selLine='#c5c5cd', hot='#3a3a3c',
           note='#efe8dc', noteLine='#dfd0b6', ok='#248a3d', warn='#b25000', bad='#d70015',
           outBg='#2c2c2e', outSel='#3a3a3c', outFg='#f5f5f7', outSub='#aeaeb2',
           outLink='#8fd3ad', inBg='#f0f0f3', inSel='#e0e0e5',
           userpics=['#a3a8b2', '#959ba7', '#878e9b', '#aab0b9'])
NIGHT = dict(bg='#121214', panel='#1c1c1e', side='#161618', soft='#2c2c2e',
             pill='#3a3a3c', line='#38383a', lineSoft='#2c2c2e', ink='#f5f5f7',
             inkOver='#ffffff', muted='#98989d', faint='#8e8e93', accent='#5fc38e',
             accentSoft='#1d3329', sel='#2c2c2e', selLine='#3a3a3c', hot='#e5e5ea',
             note='#2a261e', noteLine='#4a4128', ok='#30d158', warn='#ff9f0a', bad='#ff453a',
             outBg='#e5e5ea', outSel='#d1d1d6', outFg='#1d1d1f', outSub='#6e6e73',
             outLink='#1d6b48', inBg='#1c1c1e', inSel='#2c2c2e',
             userpics=['#5a5f69', '#4e535d', '#63686f', '#44484f'])


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
        return P['userpics'][n % 4] + a
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
    btnFg = '#1d1d1f' if dark else '#ffffff'
    badgeFg = '#1d1d1f' if dark else '#ffffff'
    return {
        'windowBg': P['panel'], 'windowFg': P['ink'], 'windowBgOver': P['side'],
        'windowBgRipple': P['pill'], 'windowSubTextFg': P['muted'],
        'windowSubTextFgOver': P['faint'], 'windowBoldFg': P['ink'],
        'windowBgActive': btn, 'windowFgActive': btnFg,
        'windowActiveTextFg': P['ink'], 'windowShadowFgFallback': P['line'],
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
        'dialogsBgActive': P['sel'], 'dialogsRippleBgActive': P['selLine'], 'dialogsBgOver': P['soft'],
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
        'historyComposeAreaBg': P['inBg'], 'historyComposeAreaFg': P['ink'],
        'historyComposeAreaFgService': P['muted'], 'historyComposeIconFg': P['faint'],
        'historyComposeIconFgOver': P['ink'], 'historySendIconFg': P['ink'],
        'historySendIconFgOver': P['inkOver'], 'historyComposeButtonBg': P['panel'],
        'historyComposeButtonBgOver': P['soft'], 'historyReplyBg': P['bg'],
        'historyReplyIconFg': P['accent'], 'topBarBg': P['panel'],
        'placeholderFg': P['faint'], 'placeholderFgActive': P['faint'],
        'msgInBg': P['inBg'], 'msgInBgSelected': P['inSel'],
        'msgInShadow': '#00000000', 'msgInShadowSelected': '#00000000',
        'historyTextInFg': P['ink'], 'historyLinkInFg': P['accent'],
        'msgInDateFg': P['faint'], 'msgInServiceFg': P['accent'],
        'msgOutBg': P['outBg'], 'msgOutBgSelected': P['outSel'],
        'msgOutShadow': '#00000000', 'msgOutShadowSelected': '#00000000',
        'historyTextOutFg': P['outFg'], 'historyLinkOutFg': P['outLink'],
        'msgOutDateFg': P['outSub'], 'msgOutServiceFg': P['outLink'],
        'historyOutIconFg': P['outSub'], 'historyOutIconFgSelected': P['outSub'],
        'msgOutDateFgSelected': P['outSub'], 'msgOutServiceFgSelected': P['outLink'],
        'historyFileOutIconFg': P['outBg'], 'msgFileOutBg': P['outFg'],
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


# Readability: bot buttons and service pills get graphite text on a
# clearly visible plate instead of gray on pale gray.
day.update({'msgServiceBg': '#f0f0f3', 'msgServiceFg': '#1d1d1f',
            'msgServiceBgSelected': '#e3e3e8', 'msgBotKbOverBgAdd': '#0000000a',
            'msgBotKbRippleBg': '#00000014', 'botKbBg': '#dedee4',
            'botKbDownBg': '#d2d2d9', 'botKbColor': '#1d1d1f'})
nite.update({'msgServiceBg': '#2c2c2e', 'msgServiceFg': '#f5f5f7',
             'msgServiceBgSelected': '#3a3a3c', 'botKbBg': '#2c2c2e',
             'botKbDownBg': '#3a3a3c', 'botKbColor': '#f5f5f7'})

# Glass: the window behind these areas is the macOS vibrancy layer.
GLASS_KEYS = ('dialogsBg', 'sideBarBg', 'titleBg', 'titleBgActive', 'topBarBg')
for theme, alpha in ((day, '66'), (nite, '33')):
    for k in GLASS_KEYS:
        v = theme[k]
        if v.startswith('#') and len(v) == 7:
            theme[k] = v + alpha


# Menus: popup windows get the macOS menu material behind them, so their
# background is only lightly tinted (child dropdowns stay readable).
def solid(theme, key):
    value = theme.get(key, '')
    while value and not value.startswith('#') and value in theme:
        value = theme[value]  # follow 'menuBg: windowBg;' references
    return value if value.startswith('#') and len(value) == 7 else None


for theme, alpha, hover, over in (
        (day, '73', None, 'b3'),
        (nite, '4d', '#3a3a3c', '99')):
    base = solid(theme, 'menuBg') or solid(theme, 'windowBg')
    if base:
        theme['menuBg'] = base + alpha
    hover = hover or solid(theme, 'menuBgOver')
    if hover:
        theme['menuBgOver'] = hover + over


# Message input: the band blends with the glass chat area, the capsule is
# a slightly denser piece of glass (the field itself is transparent).
for theme, band, capsule in ((day, '80', 'b3'), (nite, '4d', '8c')):
    for key, alpha in (('historyReplyBg', band),
                       ('historyComposeAreaBg', capsule)):
        value = solid(theme, key)
        if value:
            theme[key] = value + alpha


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
