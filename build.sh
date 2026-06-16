#!/bin/bash
set -e

echo "=== Starting Music Player Build ==="

# 1. Clean and prepare build directory
mkdir -p build
cd build

echo ">>> Running CMake..."
cmake ..

echo ">>> Compiling..."
make -j$(nproc)

cd ..

# 2. Prepare dist directory
echo ">>> Creating distribution directory (dist/)..."
rm -rf dist
mkdir -p dist/lib

# 3. Copy binary and libraries
cp build/musicplayer dist/
cp lib/libbass.so dist/lib/
cp lib/libbass_aac.so dist/lib/

# Also copy run scripts/helpers if needed
cp run_import.sh dist/
cp clean_flac.py dist/
cp lrcput.py dist/
cp extract_metadata.py dist/

echo "=== Build and Package Completed! ==="
echo "You can now run: cd dist && ./musicplayer"
