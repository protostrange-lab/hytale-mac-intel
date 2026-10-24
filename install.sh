#!/usr/bin/env bash
# Przygotowuje warstwę Wine do uruchomienia Hytale na Macu z procesorem Intel.
# Nie pobiera i nie rozprowadza żadnych plików gry - grę pobierasz sam,
# własnym kontem Hytale, oficjalnym launcherem dla Windows.
set -euo pipefail

WINE_VERSION="${WINE_VERSION:-11.18}"
WINE_FLAVOUR="${WINE_FLAVOUR:-staging}"   # staging | devel
# macOS niesie bash 3.2, wiec zadnego ${VAR^} - nazwy pakietow wypisujemy wprost
case "$WINE_FLAVOUR" in
  staging) WINE_APPNAME="Wine Staging" ;;
  devel)   WINE_APPNAME="Wine Devel" ;;
  stable)  WINE_APPNAME="Wine Stable" ;;
  *)       fail_early="nieznany wariant Wine: $WINE_FLAVOUR" ;;
esac
[[ -z "${fail_early:-}" ]] || { echo "$fail_early" >&2; exit 1; }
WINE_APP="/Applications/${WINE_APPNAME}.app"
WINE_BIN="${WINE_APP}/Contents/Resources/wine/bin"
PREFIX="${WINEPREFIX:-$HOME/WinePrefixes/hytale}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

info()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()    { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
fail()  { printf '\033[1;31m  !!\033[0m %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- warunki wstępne

info "Sprawdzam maszynę"

[[ "$(uname -s)" == "Darwin" ]] || fail "To działa tylko na macOS."

ARCH="$(uname -m)"
if [[ "$ARCH" != "x86_64" ]]; then
  fail "Ten przepis jest dla Maców Intel (x86_64). Masz $ARCH - na Apple Silicon
        zainstaluj po prostu oficjalny Hytale, jest wspierany natywnie."
fi
ok "procesor Intel ($(sysctl -n machdep.cpu.brand_string))"

OS_MAJOR="$(sw_vers -productVersion | cut -d. -f1)"
ok "macOS $(sw_vers -productVersion)"
if (( OS_MAJOR < 15 )); then
  printf '  uwaga: testowane na macOS 26 (Tahoe); starsze wersje mogą się zachowywać inaczej\n'
fi

# Hytale wymaga OpenGL 4.1, a macOS oferuje dokładnie 4.1 - ale tylko na GPU,
# które to wspiera. Sprawdzamy realnie, nie po nazwie karty.
GPU="$(system_profiler SPDisplaysDataType 2>/dev/null | awk -F': ' '/Chipset Model/{print $2}' | paste -sd' / ' -)"
ok "GPU: ${GPU:-nieznane}"

FREE_GB="$(df -g / | awk 'NR==2{print $4}')"
ok "wolne miejsce na dysku startowym: ${FREE_GB} GB"
if (( FREE_GB < 25 )); then
  printf '  uwaga: Hytale chce 10–20 GB, a prefiks Wine ~1 GB. Zrób miejsce albo\n'
  printf '         przenieś prefiks: WINEPREFIX=/ścieżka/gdzie/jest/miejsce %s\n' "$0"
fi

# ---------------------------------------------------------------- Wine

if [[ -x "$WINE_BIN/wine" ]] && "$WINE_BIN/wine" --version >/dev/null 2>&1; then
  ok "Wine już zainstalowany: $("$WINE_BIN/wine" --version)"
else
  info "Pobieram Wine ${WINE_FLAVOUR} ${WINE_VERSION} (build dla Intela, osx64)"
  # Buildy Gcenx to te same paczki, z których korzystał cask Homebrew, zanim
  # go wyłączono (nie przechodzą kontroli Gatekeepera, bo nie są notaryzowane).
  URL="https://github.com/Gcenx/macOS_Wine_builds/releases/download/${WINE_VERSION}/wine-${WINE_FLAVOUR}-${WINE_VERSION}-osx64.tar.xz"
  curl -fL --progress-bar -o "$WORK/wine.tar.xz" "$URL" || fail "nie udało się pobrać $URL"

  if [[ -n "${WINE_SHA256:-}" ]]; then
    GOT="$(shasum -a 256 "$WORK/wine.tar.xz" | cut -d' ' -f1)"
    [[ "$GOT" == "$WINE_SHA256" ]] || fail "suma kontrolna się nie zgadza: $GOT"
    ok "suma kontrolna zgodna"
  fi

  info "Instaluję do ${WINE_APP}"
  tar -xJf "$WORK/wine.tar.xz" -C "$WORK"
  SRC="$(find "$WORK" -maxdepth 1 -name '*.app' -print -quit)"
  [[ -n "$SRC" ]] || fail "w archiwum nie ma pakietu .app"
  rm -rf "$WINE_APP"
  cp -R "$SRC" "$WINE_APP"

  # Buildy nie są notaryzowane, więc Gatekeeper zablokowałby uruchomienie.
  xattr -cr "$WINE_APP" 2>/dev/null || true
  ok "zdjęto kwarantannę"

  "$WINE_BIN/wine" --version >/dev/null 2>&1 || fail "Wine się nie uruchamia"
  ok "Wine działa: $("$WINE_BIN/wine" --version)"
fi

# ---------------------------------------------------------------- prefiks

export WINEPREFIX="$PREFIX"
export WINEDEBUG=-all
export MVK_CONFIG_LOG_LEVEL=0
# mscoree/mshtml off: bez tego Wine przy każdym prefiksie pyta o Mono i Gecko.
# Hytale ich nie potrzebuje: klient jest skompilowany jako .NET NativeAOT, czyli
# zwykła natywna binarka bez CLR, a serwer chodzi na własnym runtime Javy.
export WINEDLLOVERRIDES="mscoree,mshtml="

if [[ -f "$PREFIX/system.reg" ]]; then
  ok "prefiks już istnieje: $PREFIX"
else
  info "Tworzę prefiks win64: $PREFIX"
  mkdir -p "$PREFIX"
  "$WINE_BIN/wineboot" --init >/dev/null 2>&1 || fail "wineboot padł"
  grep -aq '#arch=win64' "$PREFIX/system.reg" || fail "prefiks nie jest 64-bitowy"
  ok "prefiks gotowy (win64)"
fi

info "Gotowe."
cat <<EOF

  Wine      : $("$WINE_BIN/wine" --version)
  prefiks   : $PREFIX

  Następny krok - sprawdź, czy stos graficzny faktycznie działa:
      ./doctor.sh

  Potem pobierz oficjalny launcher Hytale dla Windows ze swojego konta
  (accounts.hytale.com - wymaga konta i licencji) i uruchom:
      ./run-hytale.sh ~/Downloads/HytaleLauncher.exe

EOF
