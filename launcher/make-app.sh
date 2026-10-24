#!/usr/bin/env bash
# Buduje aplikacje "Play Hytale.app": jeden klik uruchamia gre (przez play.sh),
# z ikona Hytale. Nic cudzego nie jest trzymane w repo - logo pobierane jest
# ze strony Hytale przy budowaniu, u Ciebie na dysku.
#
# Uzycie:  ./launcher/make-app.sh            (postawi app na Pulpicie)
#          ./launcher/make-app.sh ~/Applications
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
DEST="${1:-$HOME/Desktop}"
APP="$DEST/Play Hytale.app"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m  uwaga\033[0m %s\n' "$*"; }

# ---- ikona (opcjonalna: app zadziala nawet bez niej) ----
ICNS=""
info "Przygotowuje ikone Hytale"
if curl -fsL --max-time 25 -o "$TMP/logo.webp" "https://hytale.com/images/logo-h.webp" 2>/dev/null; then
  if python3 -c "import PIL" 2>/dev/null; then
    # ladna wersja: srebrne H na granatowym tle, zaokraglone rogi macOS
    python3 - "$TMP/logo.webp" "$TMP/icon.png" <<'PY'
import sys
from PIL import Image, ImageDraw, ImageFilter
src, out = sys.argv[1], sys.argv[2]; SZ = 1024; R = 229
top, bot = (30, 48, 78), (11, 18, 33)
grad = Image.new('RGB', (SZ, SZ)); gd = ImageDraw.Draw(grad)
for y in range(SZ):
    t = y / (SZ - 1)
    gd.line([(0, y), (SZ, y)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)))
glow = Image.new('L', (SZ, SZ), 0)
ImageDraw.Draw(glow).ellipse([SZ*0.1, -SZ*0.35, SZ*0.9, SZ*0.55], fill=60)
grad = Image.composite(Image.new('RGB', (SZ, SZ), (60, 90, 140)), grad,
                       glow.filter(ImageFilter.GaussianBlur(120)))
mask = Image.new('L', (SZ, SZ), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, SZ-1, SZ-1], radius=R, fill=255)
icon = Image.new('RGBA', (SZ, SZ), (0, 0, 0, 0)); icon.paste(grad, (0, 0), mask)
h = Image.open(src).convert('RGBA'); s = 640 / h.height
hn = h.resize((max(1, int(h.width * s)), 640), Image.LANCZOS)
icon.alpha_composite(hn, ((SZ - hn.width)//2, (SZ - hn.height)//2)); icon.save(out)
PY
  else
    # prosta wersja bez zaleznosci: H na granatowym kwadracie
    sips -s format png "$TMP/logo.webp" --out "$TMP/h.png" >/dev/null 2>&1
    sips --padToHeightWidth 1024 1024 --padColor 1a2b45 "$TMP/h.png" --out "$TMP/icon.png" >/dev/null 2>&1
    warn "prosta ikona (dla ladniejszej: pip3 install Pillow, potem uruchom ponownie)"
  fi
  if [[ -f "$TMP/icon.png" ]]; then
    mkdir "$TMP/i.iconset"
    for sz in 16 32 128 256 512; do
      sips -z $sz $sz "$TMP/icon.png" --out "$TMP/i.iconset/icon_${sz}x${sz}.png" >/dev/null 2>&1
      d=$((sz*2)); sips -z $d $d "$TMP/icon.png" --out "$TMP/i.iconset/icon_${sz}x${sz}@2x.png" >/dev/null 2>&1
    done
    cp "$TMP/icon.png" "$TMP/i.iconset/icon_512x512@2x.png"
    iconutil -c icns "$TMP/i.iconset" -o "$TMP/hytale.icns" 2>/dev/null && { ICNS="$TMP/hytale.icns"; ok "ikona gotowa"; }
  fi
else
  warn "nie pobrano logo (brak internetu?) - app dostanie domyslna ikone"
fi

# ---- aplikacja ----
info "Buduje aplikacje"
cat > "$TMP/launcher.applescript" <<OSA
tell application "Terminal"
	activate
	do script "clear; cd '$REPO' && ./play.sh"
end tell
OSA
rm -rf "$APP"
osacompile -o "$APP" "$TMP/launcher.applescript" || { echo "blad: osacompile"; exit 1; }
[[ -n "$ICNS" ]] && cp "$ICNS" "$APP/Contents/Resources/applet.icns"
/usr/libexec/PlistBuddy -c "Set :CFBundleName 'Play Hytale'" "$APP/Contents/Info.plist" 2>/dev/null
touch "$APP"
ok "gotowe: $APP"
echo
echo "Dwuklik w \"Play Hytale\" uruchamia gre."
echo "Chcesz staly przycisk? Przeciagnij te aplikacje na Dock."
