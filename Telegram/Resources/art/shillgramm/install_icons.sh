#!/bin/bash
# Render the ShillGramm icon (SHILLVPN style) into every place the app uses.
set -e
B=/Volumes/TBuild/brand; T=/Volumes/TBuild/tdesktop/Telegram; R=$B/render
MAC=$B/shillvpn/mark_mac.svg; RND=$B/shillvpn/mark.svg
IS=$T/Telegram/Images.xcassets/Icon.iconset; AS=$T/Telegram/Images.xcassets/Icon.appiconset
for s in 16 32 128 256 512; do
  $R $MAC $IS/icon_${s}x${s}.png $s; $R $MAC $IS/icon_${s}x${s}@2x.png $((s*2))
  cp $IS/icon_${s}x${s}.png $AS/icon$s.png; cp $IS/icon_${s}x${s}@2x.png $AS/icon$s@2x.png
done
A=$T/Resources/art
for s in 16 32 48 64 128 256 512; do
  $R $RND $A/icon$s.png $s; $R $RND $A/icon$s@2x.png $((s*2))
done
cp $A/icon512@2x.png $A/icon_round512@2x.png
TMP=$(mktemp -d)
for s in 16 24 32 48 64 128 256; do $R $RND $TMP/$s.png $s; done
python3 - "$TMP" "$A/icon256.ico" <<'PY'
import struct, sys
tmp, out = sys.argv[1], sys.argv[2]
sizes = [16, 24, 32, 48, 64, 128, 256]
datas = [open(f'{tmp}/{s}.png', 'rb').read() for s in sizes]
head = struct.pack('<HHH', 0, 1, len(sizes))
offset = 6 + 16 * len(sizes)
entries = b''
for s, d in zip(sizes, datas):
    entries += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(d), offset)
    offset += len(d)
open(out, 'wb').write(head + entries + b''.join(datas))
PY
cp $A/icon256.ico $A/ayu/default/app_icon.ico
cp $RND $A/ayu/default/app.svg
cp $RND $A/shillgramm/mark.svg; cp $MAC $A/shillgramm/mark_mac.svg
rm -rf $TMP
rm -f /Volumes/TBuild/tdesktop/out-release/Telegram/AppIcon-Default.icns
touch $T/Resources/qrc/telegram/telegram.qrc $T/Resources/qrc/*.qrc 2>/dev/null || true
echo icons installed
