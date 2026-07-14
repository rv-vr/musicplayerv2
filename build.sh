#!/bin/bash
set -e

MODE="${1:-dynamic}"

echo "=== Music Player Build ==="
echo "Mode: $MODE"

mkdir -p build
cd build

CMAKE_ARGS=""
if [ "$MODE" = "static" ]; then
    CMAKE_ARGS="-DSTATIC_QT=ON"
fi

cmake .. $CMAKE_ARGS
make -j$(nproc)
cd ..

rm -rf dist
mkdir -p dist/lib

cp build/musicplayer dist/
cp lib/libbass.so dist/lib/
cp lib/libbass_aac.so dist/lib/
cp run_import.sh dist/
cp clean_flac.py dist/
cp lrcput.py dist/
cp extract_metadata.py dist/

if [ "$MODE" = "static" ]; then
    strip --strip-all dist/musicplayer
    echo "Binary stripped."
fi

echo "=== Done ==="
echo "Run: cd dist && ./musicplayer"
