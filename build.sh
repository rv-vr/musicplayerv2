#!/bin/bash
set -e

echo "=== Music Player Build ==="

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
cp lib/libbassflac.so dist/lib/
cp style.qss dist/
cp run_import.sh dist/
cp clean_flac.py dist/
cp lrcput.py dist/
cp extract_metadata.py dist/

echo "=== Done ==="
echo "Run: cd dist && ./musicplayer"
