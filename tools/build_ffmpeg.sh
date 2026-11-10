#!/bin/sh
# Build the independent screen-recording executable with the portable MinGW toolchain.
set -eu
jobs="$1"
root="$(pwd -P)"
prefix="$root/local"
mkdir -p "$prefix/include" "$prefix/lib/pkgconfig"

cd zlib-*
make -j "$jobs" -f win32/Makefile.gcc libz.a
cp zlib.h zconf.h "$prefix/include/"
cp libz.a "$prefix/lib/"

cd "$root"/x264-*
./configure --host=x86_64-w64-mingw32 --bit-depth=8 --enable-static --disable-cli --disable-opencl
make -j "$jobs"
cp x264.h x264_config.h "$prefix/include/"
cp libx264.a "$prefix/lib/"
sed 's|^prefix=.*|prefix=../local|' x264.pc > "$prefix/lib/pkgconfig/x264.pc"

cd "$root"/ffmpeg-*
export PKG_CONFIG_PATH=../local/lib/pkgconfig
export TMPDIR=.
./configure --arch=x86_64 --target-os=mingw32 --enable-cross-compile \
    --cc=gcc --ar=ar --ranlib=ranlib --strip=strip --host-cc=gcc \
    --enable-static --disable-shared --disable-autodetect --disable-network \
    --disable-debug --disable-doc --disable-ffplay --disable-ffprobe --disable-pthreads \
    --disable-indevs --enable-indev=gdigrab,lavfi --disable-outdevs \
    --disable-encoders --enable-encoder=libx264,ffv1,gif,png,aac,flac,rawvideo,wrapped_avframe \
    --disable-muxers --enable-muxer=mp4,matroska,gif,image2,image2pipe,null,rawvideo \
    --disable-filters --enable-filter=abuffer,abuffersink,aformat,anull,aresample,buffer,buffersink,color,crop,format,fps,hflip,null,overlay,pad,palettegen,paletteuse,rotate,scale,testsrc2,transpose,vflip \
    --enable-gpl --enable-version3 --enable-libx264 --enable-zlib \
    --pkg-config-flags=--static --extra-cflags=-I../local/include \
    --extra-ldflags="-static -L../local/lib"
make -j "$jobs" V=1 ffmpeg.exe
cp ffmpeg.exe "$root/"
