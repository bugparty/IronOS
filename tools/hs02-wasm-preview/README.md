# HS-02 UI Wasm preview

This target compiles the real `draw_home_gauge.cpp` plus the generated English
font into WebAssembly.  Hardware/RTOS inputs are replaced by the small host
state in `src/firmware_ui_host.cpp`; the drawing code itself is not copied.

Build it with an activated Emscripten SDK:

```sh
./tools/hs02-wasm-preview/build.sh
python3 -m http.server --directory tools/hs02-wasm-preview 8080
```

Then open `http://localhost:8080/`. The build output is deliberately ignored:
only `src/`, `web/`, and `build.sh` are source controlled.
