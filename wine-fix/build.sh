#!/usr/bin/env bash
# Buduje version.dll (wtyczkę naprawiającą CNG Wine dla Hytale).
# Wymaga: x86_64-w64-mingw32-gcc (brew install mingw-w64).
set -euo pipefail
cd "$(dirname "$0")"
MH=minhook/src
# .def forwarduje prawdziwe version.dll do versionw.dll (kopia builtin obok)
x86_64-w64-mingw32-gcc -O2 -shared -o version.dll \
  hytale-wine-fix.c "$MH/buffer.c" "$MH/hook.c" "$MH/trampoline.c" "$MH/hde/hde64.c" \
  version.def -lbcrypt -municode -Iminhook/src \
  -Wl,--no-insert-timestamp   # powtarzalny build (bez znacznika czasu w PE)
echo "zbudowano $(ls -la version.dll | awk '{print $5}') B"
