#!/usr/bin/env bash
# Uruchamia samego klienta Hytale pod Wine, z pominięciem launchera.
#
# Stan na dziś: klient dochodzi do ekranu tuż przed wejściem do świata
# i zatrzymuje się, bo tryb offline wymaga zmiennej HYTALE_OFFLINE_TOKEN,
# którą wydaje wyłącznie oficjalny launcher. Ten skrypt NIE obchodzi tej
# bramki - jeśli zmienna jest ustawiona w środowisku, po prostu ją przekaże.
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

# Gdzie leży gra. Domyślnie partycja współdzielona z Boot Campem.
GAME_ROOT="${HYTALE_ROOT:-/Volumes/SHARED/Hytale}"
CLIENT_REL="install/release/package/game/latest/Client"
CLIENT_WIN='D:\Hytale\install\release\package\game\latest\Client\HytaleClient.exe'

PLAYER_NAME="${HYTALE_NAME:-Gracz}"
PLAYER_UUID="${HYTALE_UUID:-}"

export WINEPREFIX="$PREFIX"
export WINEDEBUG="${WINEDEBUG:--all}"
export MVK_CONFIG_LOG_LEVEL=0            # MoltenVK gada setkami linii
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"
export MVK_CONFIG_FORCE_LOW_POWER_GPU=0  # nie spadaj na uklad zintegrowany

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31m  !!\033[0m %s\n' "$*" >&2; exit 1; }

[[ -x "$WINE_BIN/wine" ]] || fail "brak Wine - uruchom ./install.sh"
[[ -f "$PREFIX/system.reg" ]] || fail "brak prefiksu - uruchom ./install.sh"
[[ -d "$GAME_ROOT/$CLIENT_REL" ]] || fail "nie znalazłem gry w $GAME_ROOT
     Ustaw HYTALE_ROOT na katalog, w którym leży Hytale."

# Dysk D: w prefiksie ma wskazywać tam, gdzie gra - tak jak pod Windowsem.
GAME_PARENT="$(dirname "$GAME_ROOT")"
ln -sfn "$GAME_PARENT" "$PREFIX/dosdevices/d:"
# Klient szuka swoich danych w %APPDATA%\Hytale; odwzorowujemy złącze z Windows.
APPDATA_HYTALE="$PREFIX/drive_c/users/$USER/AppData/Roaming/Hytale"
if [[ -e "$APPDATA_HYTALE" && ! -L "$APPDATA_HYTALE" ]]; then
  mv "$APPDATA_HYTALE" "$APPDATA_HYTALE.backup-$(date +%Y%m%d-%H%M%S)"
fi
ln -sfn "$GAME_ROOT" "$APPDATA_HYTALE"

ARGS=(--auth-mode offline --name "$PLAYER_NAME")
[[ -n "$PLAYER_UUID" ]] && ARGS+=(--uuid "$PLAYER_UUID")

info "Wine    : $("$WINE_BIN/wine" --version)"
info "gra     : $GAME_ROOT"
info "gracz   : $PLAYER_NAME${PLAYER_UUID:+ ($PLAYER_UUID)}"
if [[ -n "${HYTALE_OFFLINE_TOKEN:-}" ]]; then
  info "token   : obecny w środowisku, przekazuję"
else
  printf '\033[1;33m  uwaga\033[0m HYTALE_OFFLINE_TOKEN nie jest ustawiony.\n'
  printf '        Klient dojdzie do inicjalizacji grafiki i zatrzyma się na\n'
  printf '        braku tokenu. Wydaje go wyłącznie oficjalny launcher.\n'
fi

cd "$GAME_ROOT/$CLIENT_REL"
info "uruchamiam klienta"
exec "$WINE_BIN/wine" "$CLIENT_WIN" "${ARGS[@]}"
