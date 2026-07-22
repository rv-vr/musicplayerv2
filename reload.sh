#!/bin/bash
set -e

echo "=== Music Player Build ==="

rm -rf build
rm -rf dist


mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..

rm -rf dist
mkdir -p dist/lib

cp build/musicplayer dist/
cp lib/libbass.so dist/lib/
cp lib/libbass_aac.so dist/lib/
cp style.qss dist/

echo "=== Done ==="
./dist/musicplayer
