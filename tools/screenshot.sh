#!/bin/sh
# Screenshots of Vesper without the console: builds it in a Debian
# container and draws with Mesa's software renderer (llvmpipe) into a
# pbuffer. The PNGs land in build/shots/.
#
#   tools/screenshot.sh [ROM CORE.so]
#
# With a ROM and an aarch64 libretro core, it also shoots the game screen.
# The shelf is filled with made-up names (plus the ROM). The background is
# build/card/background.vimg (tools/make_background.py) when it exists.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
export DOCKER_HOST="${DOCKER_HOST:-unix://$HOME/.colima/default/docker.sock}"
mkdir -p "$here/build/shots"
extra=
if [ $# -ge 2 ]; then
  mkdir -p "$here/build/shots/in"
  cp "$1" "$here/build/shots/in/game.${1##*.}"
  cp "$2" "$here/build/shots/in/core.so"
  extra=yes
fi

docker run --rm -v "$here:/src" -e EXTRA="$extra" debian:trixie bash -euc '
  apt-get update -qq >/dev/null
  apt-get install -y -qq cmake gcc pkg-config libdrm-dev libgbm-dev libegl-dev libgles-dev \
    libasound2-dev libegl-mesa0 libgl1-mesa-dri >/dev/null 2>&1
  cmake -S /src -B /tmp/b -DCMAKE_BUILD_TYPE=Debug >/dev/null
  cmake --build /tmp/b --target vesper >/dev/null
  r=/tmp/root
  mkdir -p $r/games/gba $r/games/gbc $r/games/nds $r/assets $r/cores $r/saves
  [ -f /src/build/card/background.vimg ] && cp /src/build/card/background.vimg $r/assets/
  for n in "Astro Lanterna (Europe).gba" "Il Faro di Vetro.gba" "Kite Ridge Racing (USA).gba" \
           "Orbita Quieta.gba" "Una storia con un titolo davvero molto lungo per lo scaffale.gba"; do
    : > "$r/games/gba/$n"
  done
  for n in "Lumaca e Riccio.gbc" "Tiny Tower Tactics.gbc"; do : > "$r/games/gbc/$n"; done
  : > "$r/games/nds/Pianeta Doppio.nds"
  export VESPER_ROOT=$r EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1
  /tmp/b/vesper --screenshot /src/build/shots/shelf.ppm shelf
  /tmp/b/vesper --screenshot /src/build/shots/power.ppm power
  if [ -n "$EXTRA" ]; then
    rm -rf $r/games/*
    cp /src/build/shots/in/game.* $r/games/
    cp /src/build/shots/in/core.so $r/cores/gpsp_libretro.so
    cp /src/build/shots/in/core.so $r/cores/gambatte_libretro.so
    /tmp/b/vesper --screenshot /src/build/shots/game.ppm game 900
  fi
' 2>&1 | grep -v "^vesper: .*\[warn\]" || true

python3 - "$here/build/shots" <<'EOF'
import glob, os, sys
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], "*.ppm")):
    Image.open(p).save(p[:-4] + ".png")
    os.remove(p)
    print(p[:-4] + ".png")
EOF
