#!/bin/sh
# Puts together the vesper/ folder for the card in build/card/vesper.
#
#   tools/dist.sh [--cores DIR] [--background IMAGE]
#
# DIR holds aarch64 libretro cores (gpsp_libretro.so, mgba_libretro.so,
# gambatte_libretro.so); it defaults to build/cores (tools/build_cores.sh).
# IMAGE is the wallpaper for tools/make_background.py; without it the folder
# keeps build/card/background.vimg.
# Copy build/card/vesper to the root of the card's FAT partition.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cores="$here/build/cores" background=
while [ $# -gt 0 ]; do
  case "$1" in
  --cores) cores=$2; shift 2 ;;
  --background) background=$2; shift 2 ;;
  *) echo "usage: $0 [--cores DIR] [--background IMAGE]" >&2; exit 2 ;;
  esac
done

"$here/tools/build.sh"
out="$here/build/card/vesper"
mkdir -p "$out/bin" "$out/cores" "$out/assets" "$out/games" "$out/saves" "$out/system"
cp "$here/build/aarch64/vesper.stripped" "$out/bin/vesper"
if [ -n "$background" ]; then
  python3 "$here/tools/make_background.py" "$background" "$out/assets/background.vimg"
elif [ -f "$here/build/card/background.vimg" ]; then
  cp "$here/build/card/background.vimg" "$out/assets/"
fi
if [ -d "$cores" ]; then
  for c in gpsp mgba gambatte; do
    [ -f "$cores/${c}_libretro.so" ] && cp "$cores/${c}_libretro.so" "$out/cores/"
    [ -f "$cores/${c}-COPYING" ] && cp "$cores/${c}-COPYING" "$out/cores/"
  done
  [ -f "$cores/SOURCES.txt" ] && cp "$cores/SOURCES.txt" "$out/cores/"
fi
echo "$out:"
(cd "$out" && find . -type f | sort)
