#!/usr/bin/env bash
# Renders every HS-02 UI screen for the given languages and builds side-by-side
# contact sheets, so translations can be checked for overflow without hardware.
#
# Usage: tools/hs02-lang-preview/run.sh EN RU SV    -> out/sheets/*.png
#
# A native host build: the production Display/LCD drivers, settings menu, warnings
# and colour screens are compiled with g++ against small RTOS/SPI shims (shim/),
# one binary per language, linked with that language's generated Translation.<LANG>.cpp.
set -euo pipefail
[ $# -ge 1 ] || { echo "usage: $0 LANG [LANG...]   e.g. $0 EN RU SV" >&2; exit 1; }
here="$(cd "$(dirname "$0")" && pwd)"; src="$(cd "$here/../../source" && pwd)"
out="$here/out"; mkdir -p "$out/obj"
CXX="${CXX:-g++}"
DEFS="-DMODEL_HS02 -DMODEL_HS02A -DCANT_DIRECT_READ_SETTINGS"
INC="-I$here/shim -I$src/Core/BSP/Fnirsi -I$src/Core/BSP -I$src/Core/Inc -I$src/Core/Drivers -I$src/Core/Threads \
     -I$src/Core/Threads/UI -I$src/Core/Threads/UI/logic -I$src/Core/Threads/UI/logic/utils -I$src/Core/Threads/UI/drawing \
     -I$src/Core/Threads/UI/drawing/color_160x80 -I$src/Core/Drivers/usb-pd"
FLAGS="-std=gnu++17 -O1 -g -fshort-wchar -fshort-enums -w -fpermissive"
COMMON=( "$src/Core/Drivers/Display.cpp" "$src/Core/Drivers/LCD.cpp" "$src/Core/Src/settingsGUI.cpp" "$src/Core/Src/ScrollMessage.cpp"
         "$src/Core/Src/Settings.cpp" "$src/Core/LangSupport/lang_single.cpp" "$src/Core/Src/Translation.cpp"
         $src/Core/Threads/UI/drawing/color_160x80/*.cpp "$here/host.cpp" )
objs=()
for f in "${COMMON[@]}"; do
  o="$out/obj/$(basename "${f%.*}").o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then $CXX $FLAGS $DEFS $INC -c "$f" -o "$o"; fi
  objs+=("$o")
done
for lang in "$@"; do
  gen="$src/Core/Gen/Translation.$lang.cpp"
  # Regenerate when the translation JSON is newer (or the file is missing).
  if [ ! -f "$gen" ] || [ "$src/../Translations/translation_$lang.json" -nt "$gen" ]; then
    make -C "$src" -s model=HS02 "Core/Gen/Translation.$lang.cpp"
  fi
  $CXX $FLAGS $DEFS $INC -c "$gen" -o "$out/obj/Translation.$lang.o"
  $CXX -o "$out/render_$lang" "${objs[@]}" "$out/obj/Translation.$lang.o"
  rm -rf "$out/$lang"
  "$out/render_$lang" "$out/$lang"
done
rm -rf "$out/sheets"
python3 "$here/render.py" "$out" "$@"
