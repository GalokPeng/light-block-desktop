#!/bin/sh
# Build compatible LGPL FFmpeg 7.1 shared libraries used by the Qt 6.8.3 plugin.
# Run from the unmodified FFmpeg-n7.1 source directory, in a Windows MSYS2/Git
# Bash environment with MinGW GCC 13.1, make, pkg-config and NASM on PATH.
# Qt upstream recipe and configuration (no FFmpeg source patches):
# https://code.qt.io/cgit/qt/qt5.git/tree/coin/provisioning/common/windows/install-ffmpeg.ps1?h=v6.8.3
# https://code.qt.io/cgit/qt/qt5.git/tree/coin/provisioning/common/shared/ffmpeg_config_options.txt?h=v6.8.3
# Source archive SHA1 matches the Qt recipe: f008a93710a7577e3f85a90f4b632cc615164712
# This is independent of the GPL FFmpeg 8.1.3 CLI used for recording/export.
set -eu
./configure --prefix=installed --disable-programs --disable-doc --disable-debug \
  --enable-network --disable-lzma --enable-pic --disable-vulkan --disable-v4l2-m2m \
  --disable-decoder=truemotion1 --enable-shared --disable-static
make -j "${1:-4}"
make install
