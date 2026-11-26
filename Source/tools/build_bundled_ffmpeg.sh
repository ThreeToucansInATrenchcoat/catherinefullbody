#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
root="$PWD/Build/ffmpeg"
version=7.1.5
archive="$PWD/Source/third_party/ffmpeg/ffmpeg-$version.tar.xz"
mkdir -p "$root"
printf '%s  %s\n' de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f "$archive" | sha256sum -c
if [[ ! -f "$root/ffmpeg-$version/configure" ]]; then
    tar -xf "$archive" -C "$root"
fi
mkdir -p "$root/obj"
cd "$root/obj"
"$root/ffmpeg-$version/configure" --prefix="$root/install" --libdir="$root/install/lib" \
    --disable-everything --disable-autodetect --disable-programs --disable-doc \
    --disable-static --enable-shared --disable-x86asm --disable-avformat \
    --disable-avdevice --disable-avfilter --disable-swresample --disable-postproc \
    --enable-decoder=h264 --enable-parser=h264
make -j "${1:-4}"
make install
