#!/usr/bin/env bash
# JEDNO polecenie, ktore przygotowuje wszystko: Wine, late CNG i aplikacje
# "Play Hytale". Po nim zostaje tylko skopiowac pliki gry z Windowsa i kliknac
# Play Hytale. Nic z gry nie jest pobierane ani rozprowadzane przez ten skrypt.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; cd "$HERE"
step() { printf '\n\033[1;36m=== %s ===\033[0m\n' "$*"; }

# --- Chowa projekt do standardowego miejsca na dysku (Application Support), zeby na
#     pulpicie zostala tylko aplikacja "Play Hytale". Robi to raz; potem skrypt
#     uruchamia sie juz stamtad i ten blok jest pomijany. ---
TARGET="$HOME/Library/Application Support/hytale-mac-intel"
if [ "$HERE" != "$TARGET" ]; then
  mkdir -p "$(dirname "$TARGET")"
  if [ -e "$TARGET" ]; then
    echo "Projekt juz jest w: $TARGET - uruchamiam stamtad."
    echo "Folder, z ktorego wlasnie klikasz ($HERE), mozesz potem usunac."
    exec /bin/bash "$TARGET/setup.sh"
  fi
  echo "Chowam projekt do: $TARGET"
  if mv "$HERE" "$TARGET" 2>/dev/null; then
    exec /bin/bash "$TARGET/setup.sh"
  elif cp -R "$HERE" "$TARGET" 2>/dev/null; then
    echo "(skopiowano; folder \"$HERE\" mozesz teraz usunac)"
    exec /bin/bash "$TARGET/setup.sh"
  else
    echo "Nie udalo sie przeniesc do Application Support - kontynuuje z $HERE."
  fi
fi

step "1/3  Wine + prefiks (install.sh)"
./install.sh || { echo "install.sh nie przeszedl - zajrzyj w komunikat wyzej i sprobuj ponownie"; exit 1; }

step "2/3  Lata CNG (wine-fix) - bez niej gra nie wejdzie do swiata"
if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    echo "instaluje kompilator mingw-w64 przez Homebrew..."
    brew install mingw-w64 || true
  else
    echo "Potrzebny kompilator mingw-w64. Zainstaluj Homebrew (https://brew.sh),"
    echo "potem: brew install mingw-w64, i uruchom ./setup.sh jeszcze raz."
  fi
fi
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  (cd wine-fix && ./build.sh)
else
  echo "POMINIETO budowanie laty (brak mingw-w64). Do czasu jej zbudowania gra"
  echo "dojdzie do menu, ale nie wejdzie do swiata."
fi

step "3/3  Aplikacja Play Hytale (ikona + jeden klik)"
python3 -c "import PIL" >/dev/null 2>&1 || pip3 install --user --quiet Pillow >/dev/null 2>&1 || true
./launcher/make-app.sh || true

cat <<'EOF'

======================================================================
 GOTOWE z instalacja narzedzi. Zostaly dwie rzeczy:

 1. Skopiuj pliki gry z Windowsa (Boot Camp na tym Macu albo inny PC)
    na dysk widoczny z macOS. Jak dokladnie: README, sekcja
    "Skad wziac pliki gry".

 2. Kliknij dwukrotnie "Play Hytale" na Pulpicie. Pierwszy raz
    zalogujesz sie w przegladarce. Potem juz grasz.

 Chcesz staly przycisk? Przeciagnij "Play Hytale" na Dock.
======================================================================
EOF
