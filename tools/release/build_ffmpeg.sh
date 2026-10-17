#!/bin/sh
# A minimal shared FFmpeg for the Linux release (the demo movies, CLERIA_FFMPEG): the AVI demuxer, the
# MPEG-4 Part 2 (XviD) decoder and libswscale, nothing else and no external libraries, so the four .so
# files ship next to cleria-view and load on any distribution. LGPL 2.1+ (no GPL or nonfree parts).
#
#   tools/release/build_ffmpeg.sh <prefix> [jobs]     then PKG_CONFIG_PATH=<prefix>/lib/pkgconfig
set -eu
VERSION=7.1.1
prefix=$(mkdir -p "$1" && cd "$1" && pwd)
jobs=${2:-$(nproc)}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz" | tar -xJ -C "$tmp"
cd "$tmp/ffmpeg-$VERSION"
./configure --prefix="$prefix" --cc="${CC:-cc}" --enable-shared --disable-static --disable-programs --disable-doc \
  --disable-everything --disable-autodetect --disable-network --disable-avdevice --disable-avfilter \
  --disable-swresample --disable-x86asm --enable-protocol=file --enable-demuxer=avi --enable-decoder=mpeg4 \
  --enable-parser=mpeg4video --enable-swscale
make -j"$jobs"
make install
cp COPYING.LGPLv2.1 "$prefix/LICENSE-ffmpeg.txt"
