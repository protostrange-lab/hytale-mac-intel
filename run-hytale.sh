#!/usr/bin/env bash
# Uruchamia windowsowego Hytale (launcher albo samą grę) pod Wine na Macu Intel.
#
#   ./run-hytale.sh ~/Downloads/HytaleLauncher.exe   # pierwsze uruchomienie
#   ./run-hytale.sh                                  # kolejne, zapamiętaną ścieżką
#   ./run-hytale.sh --dxvk                           # dołóż DXVK (D3D11 -> Vulkan)
#   ./run-hytale.sh --winecfg                        # otwórz konfigurator Wine
#   ./run-hytale.sh --log                            # uruchom i zapisz pełny log
set -euo pipefail

WINE_FLAVOUR="${WINE_FLAVOUR:-staging}"
case "$WINE_FLAVOUR" in
  staging) WINE_APPNAME="Wine Staging" ;;
  devel)   WINE_APPNAME="Wine Devel" ;;
  stable)  WINE_APPNAME="Wine Stable" ;;
  *)       echo "nieznany wariant Wine: $WINE_FLAVOUR" >&2; exit 1 ;;
esac
WINE_BIN="/Applications/${WINE_APPNAME}.app/Contents/Resources/wine/bin"
PREFIX="${WINEPREFIX:-$HOME/WinePrefixes/hytale}"
STATE="$PREFIX/.hytale-target"
DXVK_VER="${DXVK_VER:-3.1.1}"
LOGDIR="$HOME/Library/Logs/hytale-mac-intel"

export WINEPREFIX="$PREFIX"
export MVK_CONFIG_LOG_LEVEL=0          # MoltenVK gada setkami linii, cisza
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"

# Wyłącza oszczędzanie energii na GPU: bez tego macOS potrafi wrzucić grę na
# układ zintegrowany, który jest poniżej minimum Hytale.
export MVK_CONFIG_FORCE_LOW_POWER_GPU=0

[[ -x "$WINE_BIN/wine" ]] || { echo "Brak Wine. Uruchom ./install.sh" >&2; exit 1; }
[[ -f "$PREFIX/system.reg" ]] || { echo "Brak prefiksu. Uruchom ./install.sh" >&2; exit 1; }

# --------------------------------------------------------------------- DXVK

install_dxvk() {
  # Hytale renderuje przez OpenGL, więc DXVK nie jest potrzebny do samej gry.
  # Bywa przydatny, jeśli launcher albo instalator sięgnie po Direct3D 11.
  local work; work="$(mktemp -d)"
  echo "==> Pobieram DXVK $DXVK_VER"
  curl -fL --progress-bar -o "$work/dxvk.tar.gz" \
    "https://github.com/doitsujin/dxvk/releases/download/v${DXVK_VER}/dxvk-${DXVK_VER}.tar.gz"
  tar -xzf "$work/dxvk.tar.gz" -C "$work"
  local src="$work/dxvk-${DXVK_VER}/x64"
  local dst="$PREFIX/drive_c/windows/system32"
  for dll in d3d11 dxgi d3d10core d3d9; do
    [[ -f "$src/$dll.dll" ]] || continue
    [[ -f "$dst/$dll.dll.wine-backup" ]] || cp "$dst/$dll.dll" "$dst/$dll.dll.wine-backup" 2>/dev/null || true
    cp "$src/$dll.dll" "$dst/$dll.dll"
    "$WINE_BIN/wine" reg add 'HKCU\Software\Wine\DllOverrides' /v "$dll" /d native /f >/dev/null 2>&1
    echo "    podmieniono $dll.dll"
  done
  rm -rf "$work"
  echo "==> DXVK na miejscu. Cofnięcie: ./run-hytale.sh --no-dxvk"
}

remove_dxvk() {
  local dst="$PREFIX/drive_c/windows/system32"
  for dll in d3d11 dxgi d3d10core d3d9; do
    if [[ -f "$dst/$dll.dll.wine-backup" ]]; then
      mv "$dst/$dll.dll.wine-backup" "$dst/$dll.dll"
      "$WINE_BIN/wine" reg delete 'HKCU\Software\Wine\DllOverrides' /v "$dll" /f >/dev/null 2>&1 || true
      echo "    przywrócono $dll.dll (wined3d)"
    fi
  done
}

# --------------------------------------------------------------------- tryby

LOG=0
TARGET=""
for arg in "$@"; do
  case "$arg" in
    --dxvk)     install_dxvk; exit 0 ;;
    --no-dxvk)  remove_dxvk;  exit 0 ;;
    --winecfg)  exec "$WINE_BIN/winecfg" ;;
    --kill)     "$WINE_BIN/wineserver" -k; echo "Zatrzymane."; exit 0 ;;
    --log)      LOG=1 ;;
    -h|--help)  sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *)          TARGET="$arg" ;;
  esac
done

if [[ -n "$TARGET" ]]; then
  [[ -f "$TARGET" ]] || { echo "Nie ma takiego pliku: $TARGET" >&2; exit 1; }
  printf '%s' "$TARGET" > "$STATE"
elif [[ -f "$STATE" ]]; then
  TARGET="$(cat "$STATE")"
else
  cat >&2 <<'EOF'
Podaj plik .exe do uruchomienia, na przykład:

    ./run-hytale.sh ~/Downloads/HytaleLauncher.exe

Launcher pobierz ze swojego konta na accounts.hytale.com (wymaga konta i licencji).
To repozytorium nie rozprowadza plików gry.
EOF
  exit 1
fi

echo "==> Wine     : $("$WINE_BIN/wine" --version)"
echo "==> prefiks  : $PREFIX"
echo "==> uruchamiam: $TARGET"

if (( LOG )); then
  mkdir -p "$LOGDIR"
  STAMP="$(date +%Y%m%d-%H%M%S)"
  F="$LOGDIR/hytale-$STAMP.log"
  echo "==> log      : $F"
  WINEDEBUG="+loaddll,+wgl,+opengl,err+all" \
    "$WINE_BIN/wine" "$TARGET" > "$F" 2>&1 || true
  echo "==> skończone. Najczęstsze błędy:"
  grep -iE "err:|wine: |unimplemented|Assertion" "$F" | head -20 || echo "    (żadnych)"
else
  exec "$WINE_BIN/wine" "$TARGET"
fi
