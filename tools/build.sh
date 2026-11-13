#!/bin/sh
# Builds Vesper for the R36S, against Lume's userland.
#
#   tools/build.sh            # build/aarch64/vesper
#   tools/build.sh test       # the host tests, in Linux (gcc, ASan, UBSan)
#
# It uses Lume's Buildroot output (the Docker volume lightos-buildroot that
# r36s-lightos/tools/build_rootfs.sh fills), read-only: the same gcc, glibc
# 2.42, libdrm, Mesa (GBM, EGL, GLES) and alsa-lib as the card.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
export DOCKER_HOST="${DOCKER_HOST:-unix://$HOME/.colima/default/docker.sock}"

case "${1:-build}" in
build)
  docker run --rm -v lightos-buildroot:/b:ro -v "$here:/src" debian:trixie bash -euc '
    export PATH=/b/output/host/bin:$PATH
    cmake -S /src -B /src/build/aarch64 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=/b/output/host/share/buildroot/toolchainfile.cmake \
      -DCMAKE_BUILD_TYPE=Release -DVESPER_TUNE=-mcpu=cortex-a35 >/dev/null
    cmake --build /src/build/aarch64
    /b/output/host/bin/aarch64-buildroot-linux-gnu-strip -o /src/build/aarch64/vesper.stripped /src/build/aarch64/vesper
  '
  ls -l "$here/build/aarch64/vesper.stripped"
  ;;
test)
  docker run --rm -v "$here:/src" debian:trixie bash -euc '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq >/dev/null && apt-get install -y -qq cmake gcc libc6-dev >/dev/null 2>&1
    cmake -S /src -B /tmp/t -DVESPER_TESTS=ON -DVESPER_APP=OFF -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" >/dev/null
    cmake --build /tmp/t --target vesper_tests >/dev/null
    /tmp/t/vesper_tests
  '
  ;;
*)
  echo "usage: $0 [build|test]" >&2
  exit 2
  ;;
esac
