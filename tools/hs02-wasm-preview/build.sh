#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
command -v em++ >/dev/null || { echo "em++ not found. Activate emsdk first (source <emsdk>/emsdk_env.sh)." >&2; exit 1; }
mkdir -p "$root/tools/hs02-wasm-preview/web/generated"
em++ -std=c++17 -O2 -DMODEL_HS02 -DHARDWARE_MAX_WATTAGE_X10=1000 \
  -I"$root/tools/hs02-wasm-preview/host_include" -I"$root/source/Core/BSP/Fnirsi" -I"$root/source/Core/Inc" -I"$root/source/Core/Drivers" \
  "$root/tools/hs02-wasm-preview/src/firmware_ui_host.cpp" \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
  -sEXPORTED_FUNCTIONS='["_hs02_ui_set_state","_hs02_ui_render","_hs02_ui_framebuffer","_hs02_ui_framebuffer_size","_hs02_ui_palette"]' \
  -o "$root/tools/hs02-wasm-preview/web/generated/hs02-ui.mjs"
echo "Built tools/hs02-wasm-preview/web/generated/hs02-ui.mjs"
