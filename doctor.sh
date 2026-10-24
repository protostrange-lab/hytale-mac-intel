#!/usr/bin/env bash
# Sprawdza, czy ten Mac naprawdę potrafi to, czego Hytale wymaga:
# kontekst OpenGL 4.1 Core na karcie dyskretnej, widziany z windowsowej Javy.
# Nie dotyka plików gry - testuje sam stos.
set -uo pipefail

WINE_FLAVOUR="${WINE_FLAVOUR:-staging}"
case "$WINE_FLAVOUR" in
  staging) WINE_APPNAME="Wine Staging" ;;
  devel)   WINE_APPNAME="Wine Devel" ;;
  stable)  WINE_APPNAME="Wine Stable" ;;
  *)       echo "nieznany wariant Wine: $WINE_FLAVOUR" >&2; exit 1 ;;
esac
WINE_BIN="/Applications/${WINE_APPNAME}.app/Contents/Resources/wine/bin"
PREFIX="${WINEPREFIX:-$HOME/WinePrefixes/hytale}"
CACHE="${XDG_CACHE_HOME:-$HOME/Library/Caches}/hytale-mac-intel"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

JRE_URL="https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.12.1%2B1/OpenJDK21U-jre_x64_windows_hotspot_21.0.12.1_1.zip"
JRE_SHA="d35f31e712f0fcf6ac5a093edc90204fbff22f720ba3950bd09d331d5e621636"
JRE_DIR="jdk-21.0.12.1+1-jre"
LWJGL_VER="3.3.6"

export WINEPREFIX="$PREFIX"
export WINEDEBUG=-all
export MVK_CONFIG_LOG_LEVEL=0
export WINEDLLOVERRIDES="mscoree,mshtml="

pass=0; failn=0; skip=0
PASS() { printf '\033[1;32m  OK  \033[0m %s\n' "$*"; pass=$((pass+1)); }
FAIL() { printf '\033[1;31m FAIL \033[0m %s\n' "$*"; failn=$((failn+1)); }
SKIP() { printf '\033[1;33m POMIN\033[0m %s\n' "$*"; skip=$((skip+1)); }
HEAD() { printf '\n\033[1;34m== %s\033[0m\n' "$*"; }

mkdir -p "$CACHE"

# ------------------------------------------------------------------ podstawy

HEAD "Maszyna"
printf '  %-14s %s\n' "procesor" "$(sysctl -n machdep.cpu.brand_string 2>/dev/null)"
printf '  %-14s %s\n' "architektura" "$(uname -m)"
printf '  %-14s %s\n' "macOS" "$(sw_vers -productVersion) ($(sw_vers -buildVersion))"
printf '  %-14s %s\n' "RAM" "$(( $(sysctl -n hw.memsize) / 1073741824 )) GB"
system_profiler SPDisplaysDataType 2>/dev/null |
  awk -F': ' '/Chipset Model/{printf "  %-14s %s\n", "GPU", $2}'

[[ "$(uname -m)" == "x86_64" ]] && PASS "Mac z procesorem Intel" \
  || SKIP "to nie jest Mac Intel - ten przepis jest dla Intela"

HEAD "Wine"
if [[ -x "$WINE_BIN/wine" ]]; then
  V="$("$WINE_BIN/wine" --version 2>/dev/null)"
  [[ -n "$V" ]] && PASS "Wine uruchamia się: $V" || FAIL "Wine jest, ale nie startuje"
  A="$(lipo -archs "$WINE_BIN/wine" 2>/dev/null)"
  [[ "$A" == *x86_64* ]] && PASS "binarka Wine x86_64 (bez tłumaczenia CPU)" \
    || FAIL "binarka Wine nie jest x86_64: $A"
else
  FAIL "brak Wine - uruchom najpierw ./install.sh"
fi

HEAD "Prefiks"
if [[ -f "$PREFIX/system.reg" ]]; then
  grep -aq '#arch=win64' "$PREFIX/system.reg" \
    && PASS "prefiks 64-bitowy: $PREFIX" \
    || FAIL "prefiks nie jest win64 - usuń go i uruchom ./install.sh"
else
  FAIL "brak prefiksu: $PREFIX"
fi

HEAD "Vulkan przez MoltenVK (zapasowa droga dla DXVK)"
# Hytale rysuje przez OpenGL, wiec to tylko zapas. Sprawdzamy, czy instalacja
# ma czym obsluzyc Direct3D 11, gdyby cokolwiek po niego siegnelo.
# (Nie da sie tu wylistowac kart: MoltenVK wypisuje je tylko przy pierwszej
# inicjalizacji prefiksu, a potem milczy.)
WINE_LIB="$(dirname "$WINE_BIN")/lib"
vk_ok=1
[[ -f "$PREFIX/drive_c/windows/system32/winevulkan.dll" ]] \
  || { SKIP "brak winevulkan.dll w prefiksie"; vk_ok=0; }
[[ -f "$WINE_LIB/libMoltenVK.dylib" ]] \
  || { SKIP "brak libMoltenVK.dylib w pakiecie Wine"; vk_ok=0; }
if (( vk_ok )); then
  PASS "winevulkan + MoltenVK na miejscu (DXVK da się doinstalować)"
fi
if [[ -f "$PREFIX/drive_c/windows/system32/d3d11.dll.wine-backup" ]]; then
  printf '        d3d11.dll podmieniony na DXVK (cofnij: ./run-hytale.sh --no-dxvk)\n'
else
  printf '        d3d11.dll to wined3d (Direct3D 11 -> OpenGL) - domyślnie i słusznie\n'
fi

# ------------------------------------------------------------------ test natywny

HEAD "OpenGL 4.1 natywnie na macOS"
if command -v swiftc >/dev/null 2>&1; then
  if swiftc -O -o "$CACHE/glnative" "$HERE/tests/GlNative.swift" 2>/dev/null; then
    OUT="$("$CACHE/glnative" 2>/dev/null)"
    if grep -q "^GL_VERSION : 4.1" <<<"$OUT"; then
      sed 's/^/        /' <<<"$OUT"
      PASS "macOS daje kontekst OpenGL 4.1"
    else
      sed 's/^/        /' <<<"$OUT"
      FAIL "brak kontekstu 4.1 - Hytale tego wymaga"
    fi
  else
    SKIP "nie udało się skompilować testu Swift"
  fi
else
  SKIP "brak swiftc (zainstaluj: xcode-select --install)"
fi

# ------------------------------------------------------------------ test WGL

HEAD "OpenGL 4.1 widziany przez aplikację Windows (WGL pod Wine)"
if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  SKIP "brak mingw-w64 (opcjonalnie: brew install mingw-w64)"
elif [[ ! -x "$WINE_BIN/wine" ]]; then
  SKIP "brak Wine"
else
  if x86_64-w64-mingw32-gcc -O2 -o "$CACHE/wgltest.exe" "$HERE/tests/wgltest.c" \
       -lopengl32 -lgdi32 2>/dev/null; then
    OUT="$("$WINE_BIN/wine" "$CACHE/wgltest.exe" 2>/dev/null)"
    sed 's/^/        /' <<<"$OUT"
    grep -q "kontekst 4.1 Core dziala" <<<"$OUT" \
      && PASS "windowsowy .exe dostaje OpenGL 4.1 Core" \
      || FAIL "windowsowy .exe nie dostał kontekstu 4.1"
  else
    SKIP "nie udało się skompilować testu WGL"
  fi
fi

# ------------------------------------------------- test pełnego stosu Hytale

HEAD "Pełny stos: windowsowa Java + LWJGL + OpenGL 4.1"
if ! command -v javac >/dev/null 2>&1; then
  SKIP "brak javac na macOS (potrzebny do zbudowania testu)"
elif [[ ! -x "$WINE_BIN/wine" ]]; then
  SKIP "brak Wine"
else
  # windowsowy JRE - taki, jaki launcher Hytale pobiera sobie sam
  if [[ ! -x "$CACHE/$JRE_DIR/bin/java.exe" ]]; then
    printf '        pobieram windowsowy JRE 21 (47 MB)...\n'
    if curl -fsL --max-time 600 -o "$CACHE/jre.zip" "$JRE_URL"; then
      GOT="$(shasum -a 256 "$CACHE/jre.zip" | cut -d' ' -f1)"
      if [[ "$GOT" == "$JRE_SHA" ]]; then
        unzip -qo "$CACHE/jre.zip" -d "$CACHE"
      else
        FAIL "suma kontrolna JRE się nie zgadza: $GOT"
      fi
    else
      SKIP "nie udało się pobrać JRE"
    fi
  fi

  # LWJGL z Maven Central - ta sama biblioteka, przez którą Hytale gada z OpenGL
  CP_PARTS=()
  miss=0
  for m in lwjgl lwjgl-glfw lwjgl-opengl; do
    for cls in "" "-natives-windows"; do
      f="${m}-${LWJGL_VER}${cls}.jar"
      [[ -f "$CACHE/$f" ]] || curl -fsL --max-time 180 -o "$CACHE/$f" \
        "https://repo1.maven.org/maven2/org/lwjgl/$m/${LWJGL_VER}/$f" || miss=1
      CP_PARTS+=("$CACHE/$f")
    done
  done

  if [[ -x "$CACHE/$JRE_DIR/bin/java.exe" && $miss -eq 0 ]]; then
    HOSTCP="$(IFS=:; echo "${CP_PARTS[*]}")"
    if javac --release 21 -cp "$HOSTCP" -d "$CACHE/classes" "$HERE/tests/GlTest.java" 2>/dev/null; then
      # Wine mapuje Z: na katalog główny systemu
      wpath() { printf 'Z:%s' "$(tr '/' '\\' <<<"$1")"; }
      WCP="$(wpath "$CACHE/classes")"
      for p in "${CP_PARTS[@]}"; do WCP="$WCP;$(wpath "$p")"; done
      OUT="$("$WINE_BIN/wine" "$CACHE/$JRE_DIR/bin/java.exe" -cp "$WCP" GlTest 2>/dev/null)"
      sed 's/^/        /' <<<"$OUT"
      grep -q "dziala pod Wine" <<<"$OUT" \
        && PASS "pełny stos Hytale działa na tej maszynie" \
        || FAIL "stos nie przeszedł testu"
    else
      SKIP "nie udało się skompilować testu Javy"
    fi
  else
    SKIP "brak JRE albo bibliotek LWJGL"
  fi
fi

# ------------------------------------------------------------------ podsumowanie

HEAD "Podsumowanie"
printf '  zdane: %d   niezdane: %d   pominięte: %d\n\n' "$pass" "$failn" "$skip"
if (( failn > 0 )); then
  echo "  Coś nie działa - bez tego Hytale nie ruszy. Zajrzyj w README (sekcja Kłopoty)."
  exit 1
fi
echo "  Stos jest gotowy. Pobierz oficjalny launcher Hytale dla Windows ze swojego"
echo "  konta i uruchom: ./run-hytale.sh ~/Downloads/HytaleLauncher.exe"
