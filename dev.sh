#!/bin/bash
# Music Player v2 — Development Watcher & Rebuilder

echo "=== Music Player Hot-Reload Dev Environment ==="

./build.sh

if command -v inotifywait >/dev/null 2>&1; then
    echo "[Dev Watcher] Active: Watching src/, include/, style.qss, CMakeLists.txt..."
    while true; do
        inotifywait -e modify,create,delete -r src include CMakeLists.txt 2>/dev/null
        echo "[Dev Watcher] Source change detected. Rebuilding..."
        cmake --build build --parallel && cp build/musicplayer dist/
        echo "[Dev Watcher] Rebuild complete."
    done
elif command -v entr >/dev/null 2>&1; then
    find src include CMakeLists.txt | entr -r sh -c "cmake --build build --parallel && cp build/musicplayer dist/ && ./dist/musicplayer"
else
    echo "[Dev] Running dist/musicplayer..."
    cd dist && ./musicplayer
fi
