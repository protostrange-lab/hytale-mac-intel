#!/usr/bin/env bash
# Uruchamia Hytale na Macu z procesorem Intel, od zera do menu gry.
#
# Jak to działa (żadnego obchodzenia zabezpieczeń):
#   1. Otwiera oficjalny launcher pod Wine.
#   2. Otwiera port debugowania WebView2 przez rejestr (tego Wails nie nadpisuje),
#      bo interfejs launchera pod Wine jest niewidoczny — ale jego DOM żyje.
#   3. Klika "Sign in" w DOM launchera. TY logujesz się w przeglądarce swoimi
#      danymi. Launcher dostaje od serwera Hytale Twój token offline.
#   4. Klika "PLAY". Launcher uruchamia klienta z Twoim tokenem — jak na Windowsie.
#
# Pierwsze uruchomienie wymaga logowania w przeglądarce. Kolejne są automatyczne,
# dopóki sesja launchera jest ważna (plik account.dat).
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WINE_FLAVOUR="${WINE_FLAVOUR:-staging}"
case "$WINE_FLAVOUR" in
  staging) WINE_APPNAME="Wine Staging" ;;
  devel)   WINE_APPNAME="Wine Devel" ;;
  stable)  WINE_APPNAME="Wine Stable" ;;
  *) echo "nieznany wariant Wine: $WINE_FLAVOUR" >&2; exit 1 ;;
esac
WINE_BIN="/Applications/${WINE_APPNAME}.app/Contents/Resources/wine/bin"
PREFIX="${WINEPREFIX:-$HOME/WinePrefixes/hytale}"
GAME_ROOT="${HYTALE_ROOT:-/Volumes/SHARED/Hytale}"
LAUNCHER='C:\users\'"$USER"'\AppData\Local\Programs\Hypixel Studios\Hytale Launcher\hytale-launcher.exe'
CDP_PORT="${CDP_PORT:-9222}"
# Nazwa użytkownika Windows z instalacji gry (ścieżki w grze wskazują C:\Users\<to>).
# Ustaw w środowisku (HYTALE_WIN_USER=...) albo w pliku play.local.sh (nieśledzonym).
[[ -f "$HERE/play.local.sh" ]] && source "$HERE/play.local.sh"
WIN_USER="${HYTALE_WIN_USER:?ustaw HYTALE_WIN_USER na nazwę użytkownika Windows z instalacji gry (np. w play.local.sh: HYTALE_WIN_USER=TwojaNazwa)}"

export WINEPREFIX="$PREFIX"
export WINEDEBUG="${WINEDEBUG:--all}"
export MVK_CONFIG_LOG_LEVEL=0
export MVK_CONFIG_FORCE_LOW_POWER_GPU=0
# zachowaj ewentualne istniejące WINEDLLOVERRIDES, dołóż nasze (version=n,b krytyczne dla łaty)
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}mscoree,mshtml=;version=n,b"
export CDP_PORT

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m  uwaga\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31m  !!\033[0m %s\n' "$*" >&2; exit 1; }

command -v node >/dev/null || fail "potrzebny Node.js (ma wbudowany WebSocket)"
[[ -x "$WINE_BIN/wine" ]] || fail "brak Wine — uruchom ./install.sh"
[[ -f "$PREFIX/system.reg" ]] || fail "brak prefiksu — uruchom ./install.sh"
[[ -d "$GAME_ROOT/install" ]] || fail "nie znalazłem gry w $GAME_ROOT (ustaw HYTALE_ROOT)"

# --- mapowania ścieżek: launcher i klient oczekują windowsowych lokalizacji ---
info "Przygotowuję ścieżki"
# D: tam, gdzie gra (patchline/klient używają D:\Hytale\...)
ln -sfn "$(dirname "$GAME_ROOT")" "$PREFIX/dosdevices/d:"
# %APPDATA%\Hytale dla użytkownika Wine
APPDATA="$PREFIX/drive_c/users/$USER/AppData/Roaming"
mkdir -p "$APPDATA"
[[ -L "$APPDATA/Hytale" ]] || { [[ -e "$APPDATA/Hytale" ]] && mv "$APPDATA/Hytale" "$APPDATA/Hytale.bak-$(date +%s)"; }
ln -sfn "$GAME_ROOT" "$APPDATA/Hytale"
# Ścieżka zapisana na sztywno w instalacji z Windowsa: C:\Users\<WIN_USER>\AppData\Roaming\Hytale
WIN_APPDATA="$PREFIX/drive_c/users/$WIN_USER/AppData/Roaming"
mkdir -p "$WIN_APPDATA"
ln -sfn "$GAME_ROOT" "$WIN_APPDATA/Hytale"
ok "D: -> $(dirname "$GAME_ROOT"); %APPDATA%\\Hytale i C:\\Users\\$WIN_USER\\...\\Hytale -> gra"

# --- łata CNG Wine: wtyczka version.dll pozwala klientowi dokończyć lokalny mTLS ---
# (bez niej klient pada na 0x80090027 przy wejściu do świata — patrz wine-fix/README.md)
CLIENT_DIR="$GAME_ROOT/install/release/package/game/latest/Client"
FIX_DLL="$HERE/wine-fix/version.dll"
# prawdziwe version.dll Wine = cel forwardu proxy; można nadpisać WINE_VERSION_DLL
WINE_VERSION_DLL="${WINE_VERSION_DLL:-$WINE_BIN/../lib/wine/x86_64-windows/version.dll}"
if [[ ! -d "$CLIENT_DIR" ]]; then
  warn "nie znalazłem katalogu klienta: $CLIENT_DIR — pomijam łatę CNG"
elif [[ ! -f "$FIX_DLL" ]]; then
  # usuń stare proxy, by nie zostawić forwardu bez celu (klient by nie wstał)
  rm -f "$CLIENT_DIR/version.dll" "$CLIENT_DIR/versionw.dll"
  warn "brak $FIX_DLL — zbuduj: (cd wine-fix && ./build.sh). Bez tego gra nie wejdzie do świata."
elif [[ ! -f "$WINE_VERSION_DLL" ]]; then
  # proxy version.dll forwarduje KAŻDY eksport do versionw.dll; bez celu klient nie wstanie
  rm -f "$CLIENT_DIR/version.dll" "$CLIENT_DIR/versionw.dll"
  warn "nie znalazłem builtin version.dll Wine ($WINE_VERSION_DLL) — pomijam łatę (ustaw WINE_VERSION_DLL)"
else
  # najpierw cel forwardu (versionw.dll), potem proxy (version.dll)
  if cp -f "$WINE_VERSION_DLL" "$CLIENT_DIR/versionw.dll" && cp -f "$FIX_DLL" "$CLIENT_DIR/version.dll"; then
    ok "łata CNG wgrana (wine-fix/version.dll → Client/)"
  else
    rm -f "$CLIENT_DIR/version.dll" "$CLIENT_DIR/versionw.dll"
    warn "nie udało się wgrać łaty CNG do $CLIENT_DIR"
  fi
fi

# --- port debugowania WebView2 przez politykę rejestru (Wails tego nie nadpisze) ---
"$WINE_BIN/wineserver" -k 2>/dev/null; sleep 2
info "Włączam dostęp do DOM launchera (rejestr)"
for root in HKLM HKCU; do
  "$WINE_BIN/wine" reg add "$root\\SOFTWARE\\Policies\\Microsoft\\Edge\\WebView2\\AdditionalBrowserArguments" \
    /ve /t REG_SZ /d "--remote-debugging-port=$CDP_PORT --remote-allow-origins=*" /f >/dev/null 2>&1
done
ok "polityka ustawiona"

# --- start launchera ---
info "Uruchamiam launcher"
nohup "$WINE_BIN/wine" "$LAUNCHER" >/tmp/hytale-launcher.log 2>&1 &
disown
# czekaj aż DevTools wstanie
for i in $(seq 1 40); do
  curl -s --max-time 2 "http://127.0.0.1:$CDP_PORT/json" >/dev/null 2>&1 && break
  sleep 1
done
curl -s --max-time 3 "http://127.0.0.1:$CDP_PORT/json" >/dev/null 2>&1 || fail "launcher nie wystawił DevTools"
ok "launcher gotowy"

# --- czy już zalogowany? sprawdź, czy od razu jest ekran z PLAY ---
sleep 6
STATE="$(node "$HERE/tools/cdp.mjs" eval 'location.pathname' 2>/dev/null | tr -d '"')"
if [[ "$STATE" == *login* ]]; then
  info "Potrzebne logowanie — klikam \"Sign in\", otworzy się przeglądarka"
  node "$HERE/tools/cdp-wait-click.mjs" "button.login__sign-in-button" 30 >/dev/null 2>&1 \
    || warn "nie udało się kliknąć Sign in"
  echo
  warn "ZALOGUJ SIĘ w oknie przeglądarki, które się właśnie otworzyło."
  warn "Czekam, aż launcher przejdzie na ekran gry (do 5 minut)..."
  for i in $(seq 1 150); do
    P="$(node "$HERE/tools/cdp.mjs" eval 'location.pathname' 2>/dev/null | tr -d '"')"
    [[ "$P" == *launch-game* ]] && { ok "zalogowano"; break; }
    sleep 2
  done
else
  ok "już zalogowany"
fi

# --- PLAY ---
info "Klikam PLAY"
node "$HERE/tools/cdp-wait-click.mjs" "button.hybutton-split__main" 60 >/dev/null 2>&1 \
  || fail "nie udało się kliknąć PLAY"

info "Czekam na okno gry..."
for i in $(seq 1 60); do
  pgrep -f HytaleClient >/dev/null 2>&1 && { ok "klient wystartował"; break; }
  sleep 2
done
if pgrep -f HytaleClient >/dev/null 2>&1; then
  echo
  ok "Hytale działa. Jeśli walidacja plików zgłosi błąd, sprawdź HYTALE_WIN_USER."
else
  warn "klient nie wystartował — zajrzyj do logu launchera: /tmp/hytale-launcher.log"
  warn "oraz do $GAME_ROOT/UserData/Logs/"
fi
