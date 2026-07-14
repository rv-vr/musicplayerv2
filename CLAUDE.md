# CLAUDE.md — Music Player v2 (Qt6 + BASS)

## Build & Run
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./musicplayer
```

Static Qt build: `cmake -DSTATIC_QT=ON ..` (requires Qt6 built at `/opt/qt6-static`)

## Project Structure
- `include/library.h` / `src/library.cpp` — C++11 library scan + config (Qt-native, no GLib2).
- `include/lyrics.h` / `src/lyrics.cpp` — LRC parser, active line lookup.
- `src/mainwindow.cpp` — Qt6 Widgets UI + BASS audio engine.
- `extract_metadata.py` — Mutagen/eyed3 helper for cover art and lyrics extraction.
- `run_import.sh` — Chained script runner for `lrcput.py` and `clean_flac.py`.
- `lib/libbass.so`, `lib/libbass_aac.so` — BASS audio engine (bundled).

## Engine
- **Audio**: BASS 2.4 + BASS_AAC plugin. Init at 44.1kHz.
  - BASS plugin loaded from `lib/libbass_aac.so` relative to binary.
  - Formats: mp3, flac, m4a/aac.
- **Metadata cache**: SQLite3 at `~/.config/musicplayerv2/library.db`.
  - Tables: `songs` (filepath, title, artist, album, duration, mtime, track_no, disc_no, album_artist).
  - Cache invalidated by mtime change.
- **Async scan**: QThreadPool (8 threads) with QAtomicInt progress counter.
  - File count calculated first, then parallel scan updates progress in real time.
  - GLib2 fully removed. All Qt-native.
- **Cover art**: Local `cover.jpg/png` / `folder.jpg/png`, then embedded via `extract_metadata.py --cover`.
- **Lyrics**: Sidecar `.lrc` files, then embedded via `extract_metadata.py --lyrics`.

## Configuration
- **File**: `~/.config/musicplayerv2/config.ini` (QSettings INI format).
- **Keys**: `LibraryPath`, `ImportDestPath`, `Volume` (0.0-1.0), `Shuffle` (bool), `Repeat` (bool).

## UI (Qt6 Widgets + QSS)
- **Tabs**: Home (search + recent albums grid), Library (album/track splitter), Play Queue, Lyrics, Import & Clean, Settings.
- **Sidebar**: Cover art (220x220), track info, seek slider, playback controls, volume.
- **All QSS inline** in `applyStyle()` — dark theme, glassmorphic cards.

## Build Dependencies
- Qt6 Widgets (system dynamic or custom static)
- SQLite3 (system or amalgamation)
- BASS + BASS_AAC (bundled .so)

## Static Qt Build (optional)
```bash
# Requires libxcb-static-devel and other static deps
./configure -static -static-runtime -ltcg -optimize-size -no-pch \
  -no-framework -no-dbus -no-opengl -no-vulkan \
  -prefix /opt/qt6-static
cmake --build . --parallel
sudo cmake --install .
```
