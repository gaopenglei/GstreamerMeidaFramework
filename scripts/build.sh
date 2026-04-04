#!/bin/bash
#
# Quick build script without CMake
# Use this if CMake is not available
#

set -e

# Configuration
CC=gcc
CFLAGS="-Wall -Wextra -O2 -D_GNU_SOURCE"
LDFLAGS="-lpthread -lm -ldl -lrt"

# GStreamer flags
GST_CFLAGS=$(pkg-config --cflags gstreamer-1.0 gstreamer-pbutils-1.0 gstreamer-video-1.0 gstreamer-audio-1.0)
GST_LIBS=$(pkg-config --libs gstreamer-1.0 gstreamer-pbutils-1.0 gstreamer-video-1.0 gstreamer-audio-1.0)

# Directories
SRC_DIR=src
INCLUDE_DIR=include
BUILD_DIR=build

# Create build directory
mkdir -p $BUILD_DIR

echo "Building GStreamer Media Framework..."

# Compile source files
echo "Compiling core modules..."
$CC $CFLAGS $GST_CFLAGS -I$INCLUDE_DIR -c $SRC_DIR/core/media_types.c -o $BUILD_DIR/media_types.o
$CC $CFLAGS $GST_CFLAGS -I$INCLUDE_DIR -c $SRC_DIR/core/media_controller.c -o $BUILD_DIR/media_controller.o

echo "Compiling functional modules..."
$CC $CFLAGS $GST_CFLAGS -I$INCLUDE_DIR -c $SRC_DIR/modules/player.c -o $BUILD_DIR/player.o
$CC $CFLAGS $GST_CFLAGS -I$INCLUDE_DIR -c $SRC_DIR/modules/recorder.c -o $BUILD_DIR/recorder.o
$CC $CFLAGS $GST_CFLAGS -I$INCLUDE_DIR -c $SRC_DIR/modules/transcoder.c -o $BUILD_DIR/transcoder.o

echo "Compiling utility modules..."
$CC $CFLAGS $GST_CFLAGS -I$INCLUDE_DIR -c $SRC_DIR/utils/logger.c -o $BUILD_DIR/logger.o
$CC $CFLAGS $GST_CFLAGS -I$INCLUDE_DIR -c $SRC_DIR/utils/error.c -o $BUILD_DIR/error.o

echo "Compiling main program..."
$CC $CFLAGS $GST_CFLAGS -I$INCLUDE_DIR -c $SRC_DIR/main.c -o $BUILD_DIR/main.o

# Link
echo "Linking..."
$CC $BUILD_DIR/*.o -o $BUILD_DIR/media_framework $GST_LIBS $LDFLAGS

echo "Build complete: $BUILD_DIR/media_framework"
echo ""
echo "Run with: $BUILD_DIR/media_framework --help"
