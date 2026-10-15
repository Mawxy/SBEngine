#!/bin/bash
# Build the engine as WebAssembly with clang + the WASI sysroot (no Emscripten needed).
#   WASI_SYSROOT   path to wasi-sysroot (github.com/WebAssembly/wasi-sdk releases, wasi-sysroot-24.0)
#   WASI_RESOURCE  clang resource dir containing lib/wasip1/libclang_rt.builtins-wasm32.a
#                  (from libclang_rt.builtins-wasm32-wasi-24.0) and include -> clang's builtin headers
set -e
cd "$(dirname "$0")/.."
clang++ --target=wasm32-wasip1 --sysroot="$WASI_SYSROOT" -resource-dir "$WASI_RESOURCE" \
  -std=c++20 -O3 -fno-exceptions -DNDEBUG -mexec-model=reactor -Isrc \
  src/*.cpp wasm/wasm_api.cpp \
  -o wasm/sbengine.wasm -Wl,--strip-all
python3 gui/build.py wasm/sbengine.wasm arena gui/spectacular-bid.html --standalone
