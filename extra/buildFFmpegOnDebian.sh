#!/bin/bash

set -e
set -x

#This file is just extract from https://trac.ffmpeg.org/wiki/CompilationGuide/Ubuntu
#define some options here
BUILD_YASM=0
BUILD_LIBX264=0
BUILD_LIBLAME=0
BUILD_LIBOPUS=0

FFMPEG_FILE=ffmpeg-3.0.tar.bz2

###Get the Dependencies
#Copy and paste the whole code box for each step. First install the dependencies:

sudo apt-get update
sudo apt-get -y --force-yes install autoconf automake build-essential libass-dev libfreetype6-dev \
  libsdl1.2-dev libtheora-dev libtool libva-dev libvdpau-dev libvorbis-dev libxcb1-dev libxcb-shm0-dev \
  libxcb-xfixes0-dev pkg-config texinfo zlib1g-dev
#Note: Server users can omit the ffplay and x11grab dependencies: libsdl1.2-dev libva-dev libvdpau-dev
#libxcb1-dev libxcb-shm0-dev libxcb-xfixes0-dev.
#Now make a directory for the source files that will be downloaded later in this guide:

rm -rf ~/ffmpeg_sources
rm -rf ~/ffmpeg_build
mkdir -p ~/ffmpeg_sources

###Compilation & Installation
#You can compile ffmpeg to your liking. If you do not require certain encoders you may skip the relevant
#section and then remove the appropriate ./configure option in FFmpeg. For example, if libopus is not needed,
#then skip that section and then remove --enable-libopus from the Install FFmpeg section.

#This guide is designed to be non-intrusive and will create several directories in your home directory:

#ffmpeg_sources - Where the source files will be downloaded.
#ffmpeg_build - Where the files will be built and libraries installed.
#bin - Where the resulting binaries (ffmpeg, ffplay, ffserver, x264, and yasm) will be installed.
#You can easily undo any of this as shown in Reverting Changes Made by This Guide.

###Yasm

#An assembler for x86 optimizations used by x264 and FFmpeg. Highly recommended or your resulting build may be very slow.

#If your repository offers a yasm package = 1.2.0 then you can install that instead of compiling:

if [ $BUILD_YASM = "0" ]; then
    sudo apt-get -y install yasm
else
    cd ~/ffmpeg_sources
    wget http://www.tortall.net/projects/yasm/releases/yasm-1.3.0.tar.gz
    tar xzvf yasm-1.3.0.tar.gz
    cd yasm-1.3.0
    ./configure --prefix="$HOME/ffmpeg_build" --bindir="$HOME/bin"
    make -j 8
    make install
    make distclean
fi

###libx264

#H.264 video encoder. See the H.264 Encoding Guide for more information and usage examples.

#Requires ffmpeg to be configured with --enable-gpl --enable-libx264.

#If your repository offers a libx264-dev package = 0.118 then you can install that instead of compiling:

if [ $BUILD_LIBX264 = "0" ]; then
    sudo apt-get -y install libx264-dev
else
    cd ~/ffmpeg_sources
    wget http://download.videolan.org/pub/x264/snapshots/last_x264.tar.bz2
    tar xjvf last_x264.tar.bz2
    cd x264-snapshot*
    PATH="$HOME/bin:$PATH" ./configure --prefix="$HOME/ffmpeg_build" --bindir="$HOME/bin" --enable-static
    PATH="$HOME/bin:$PATH" make -j 8
    make install
    make distclean
fi


###libx265

#H.265/HEVC video encoder. See the H.265 Encoding Guide for more information and usage examples.

sudo apt-get -y install cmake mercurial
cd ~/ffmpeg_sources
hg clone https://bitbucket.org/multicoreware/x265
cd ~/ffmpeg_sources/x265/build/linux
PATH="$HOME/bin:$PATH" cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX="$HOME/ffmpeg_build" -DENABLE_SHARED:bool=off ../../source
make -j 8
make install
#make distclean


###libfdk-aac

#AAC audio encoder. See the AAC Audio Encoding Guide for more information and usage examples.

#Requires ffmpeg to be configured with --enable-libfdk-aac (and --enable-nonfree if you also included --enable-gpl).

cd ~/ffmpeg_sources
wget -O fdk-aac.tar.gz https://github.com/mstorsjo/fdk-aac/tarball/master
tar xzvf fdk-aac.tar.gz
cd mstorsjo-fdk-aac*
autoreconf -fiv
./configure --prefix="$HOME/ffmpeg_build" --disable-shared --with-pic
make -j 8
make install
make distclean


###libmp3lame

#MP3 audio encoder.

#Requires ffmpeg to be configured with --enable-libmp3lame.

#If your repository offers a libmp3lame-dev package = 3.98.3 then you can install that instead of compiling:

if [ $BUILD_LIBLAME = "0" ]; then
    sudo apt-get -y install libmp3lame-dev
else
    sudo apt-get -y install nasm
    cd ~/ffmpeg_sources
    wget http://downloads.sourceforge.net/project/lame/lame/3.99/lame-3.99.5.tar.gz
    tar xzvf lame-3.99.5.tar.gz
    cd lame-3.99.5
    ./configure --prefix="$HOME/ffmpeg_build" --enable-nasm --disable-shared
    make -j 8
    make install
    make distclean
fi


###libopus

#Opus audio decoder and encoder.

#Requires ffmpeg to be configured with --enable-libopus.

#If your repository offers a libopus-dev package = 1.1 then you can install that instead of compiling:

if [ $BUILD_LIBOPUS = "0" ]; then
    sudo apt-get -y install libopus-dev
else
    cd ~/ffmpeg_sources
    wget http://downloads.xiph.org/releases/opus/opus-1.1.tar.gz
    tar xzvf opus-1.1.tar.gz
    cd opus-1.1
    ./configure --prefix="$HOME/ffmpeg_build" --disable-shared
    make -j 8
    make install
    make clean
fi


###libvpx

#VP8/VP9 video encoder and decoder. See the VP8 Video Encoding Guide for more information and usage examples.

#Requires ffmpeg to be configured with --enable-libvpx.

cd ~/ffmpeg_sources
wget http://storage.googleapis.com/downloads.webmproject.org/releases/webm/libvpx-1.5.0.tar.bz2
tar xjvf libvpx-1.5.0.tar.bz2
cd libvpx-1.5.0
PATH="$HOME/bin:$PATH" ./configure --prefix="$HOME/ffmpeg_build"\
  --disable-examples \
  --disable-unit-tests \
  --enable-pic
PATH="$HOME/bin:$PATH" make -j 8
make install
make distclean



###ffmpeg


cd ~/ffmpeg_sources
wget http://ffmpeg.org/releases/${FFMPEG_FILE}
tar xjvf ${FFMPEG_FILE}
cd ffmpeg-3.0
PATH="$HOME/bin:$PATH" PKG_CONFIG_PATH="$HOME/ffmpeg_build/lib/pkgconfig" ./configure \
  --prefix="$HOME/ffmpeg_build" \
  --pkg-config-flags="--static" \
  --extra-cflags="-I$HOME/ffmpeg_build/include" \
  --extra-ldflags="-L$HOME/ffmpeg_build/lib" \
  --bindir="$HOME/bin" \
  --enable-gpl \
  --enable-libass \
  --enable-libfdk-aac \
  --enable-libfreetype \
  --enable-libmp3lame \
  --enable-libopus \
  --enable-libtheora \
  --enable-libvorbis \
  --enable-libvpx \
  --enable-libx264 \
  --enable-libx265 \
  --enable-nonfree \
  --enable-pic \
  --disable-static \
  --enable-shared \

PATH="$HOME/bin:$PATH" make -j 8
make install
make distclean
hash -r

