#!/bin/bash
# Build the Windows programs (x64) with Zig as the C++ cross-compiler:
#   pip install ziglang==0.13.0
#   desktop/build_windows.sh
#   -> desktop/SpectacularBid.exe  (the app; needs gui/spectacular-bid.html built first)
#   -> desktop/sbtrain.exe         (unattended training)
set -e
cd "$(dirname "$0")/.."
ENGINE="src/layout.cpp src/game.cpp src/view.cpp src/search.cpp src/session.cpp src/nnue.cpp src/nnue_weights.cpp src/models.cpp"
python3 desktop/embed.py gui/spectacular-bid.html tools/gui_html.h
python3 -m ziglang c++ -target x86_64-windows-gnu -std=c++20 -O2 -DSB_THREADS -DNDEBUG -Isrc \
  $ENGINE src/mlp.cpp tools/desktop.cpp \
  -o desktop/SpectacularBid.exe -lws2_32 -lshell32 -Wl,--subsystem,windows
python3 -m ziglang c++ -target x86_64-windows-gnu -std=c++20 -O2 -DSB_TRAIN_THREADS -DNDEBUG -Isrc \
  $ENGINE src/mlp.cpp tools/sbtrain.cpp \
  -o desktop/sbtrain.exe
rm -f desktop/*.pdb desktop/*.lib *.pdb *.lib
echo "built desktop/SpectacularBid.exe and desktop/sbtrain.exe"
