#!/bin/sh
# Builds Vesper's libretro cores from upstream, at pinned commits, with
# Lume's toolchain (the lightos-buildroot volume, read-only), for the
# Cortex-A35. Output: build/cores/*_libretro.so, plus their licences.
#
#   tools/build_cores.sh
#
# gpSP (GBA, ARMv8 dynarec) and Gambatte (GB/GBC) are GPL-2.0; each stays a
# separate .so that Vesper loads with dlopen.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
export DOCKER_HOST="${DOCKER_HOST:-unix://$HOME/.colima/default/docker.sock}"

GPSP_URL=https://github.com/libretro/gpsp.git
GPSP_REV=5819380c2ffb0900219d700a382ee68c464ebb99
GAMBATTE_URL=https://github.com/libretro/gambatte-libretro.git
GAMBATTE_REV=77f2567d718b8973d02caa2daa8dce4953faf811

src="$here/build/cores-src"
mkdir -p "$src" "$here/build/cores"
fetch() { # dir url rev
  if [ ! -d "$src/$1/.git" ]; then
    git clone -q "$2" "$src/$1"
  fi
  git -C "$src/$1" fetch -q origin "$3" 2>/dev/null || true
  git -C "$src/$1" checkout -q "$3"
  git -C "$src/$1" clean -qfdx
}
fetch gpsp "$GPSP_URL" "$GPSP_REV"
fetch gambatte "$GAMBATTE_URL" "$GAMBATTE_REV"

docker run --rm -v lightos-buildroot:/b:ro -v "$here/build:/w" debian:trixie bash -euc '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq >/dev/null && apt-get install -y -qq make git >/dev/null 2>&1
  git config --global --add safe.directory "*"
  export PATH=/b/output/host/bin:$PATH
  X=aarch64-buildroot-linux-gnu
  J=$(nproc)
  # CFLAGS from the environment: the Makefiles add theirs with +=.
  cd /w/cores-src/gpsp
  CFLAGS="-mcpu=cortex-a35" make -s -j$J platform=arm64 CC=$X-gcc CXX=$X-g++ >/dev/null
  $X-strip -o /w/cores/gpsp_libretro.so gpsp_libretro.so
  cd /w/cores-src/gambatte
  CFLAGS="-mcpu=cortex-a35" CXXFLAGS="-mcpu=cortex-a35" \
    make -s -j$J -f Makefile.libretro platform=unix CC=$X-gcc CXX=$X-g++ >/dev/null
  $X-strip -o /w/cores/gambatte_libretro.so gambatte_libretro.so
'
cp "$src/gpsp/COPYING" "$here/build/cores/gpsp-COPYING"
cp "$src/gambatte/COPYING" "$here/build/cores/gambatte-COPYING"
{
  echo "gpsp $GPSP_URL $GPSP_REV"
  echo "gambatte $GAMBATTE_URL $GAMBATTE_REV"
} > "$here/build/cores/SOURCES.txt"
ls -l "$here/build/cores"
