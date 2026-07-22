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
- `include/library.h` / `src/library.cpp` — C++17 library scan + TagLib metadata engine.
- `include/lyrics.h` / `src/lyrics.cpp` — LRC parser, active line lookup.
- `include/importer.h` / `src/importer.cpp` — Native C++ FLAC import, LRC merger, QAAC transcode pipeline.
- `src/mainwindow.cpp` — Qt6 Widgets UI + BASS audio engine.
- `lib/libbass.so`, `lib/libbass_aac.so`, `lib/libbassflac.so` — BASS audio engine (bundled).

## Engine
- **Audio**: BASS 2.4 + BASS_AAC + BASS_FLAC plugins. Init at 44.1kHz.
  - BASS plugins loaded from `lib/` relative to binary.
  - Formats: mp3, flac, m4a/aac.
- **Metadata cache**: SQLite3 at `~/.config/musicplayerv2/library.db`.
  - Tables: `songs` (filepath, title, artist, album, duration, mtime, track_no, disc_no, album_artist).
  - Cache invalidated by mtime change.
- **Async scan**: QThreadPool (8 threads) with QAtomicInt progress counter.
  - Native TagLib C++ reader (100x faster than legacy scripts).
- **Cover art**: Local `cover.jpg/png` / `folder.jpg/png`, or native TagLib embedded artwork extracted to `~/.cache/musicplayerv2/covers/`.
- **Lyrics**: Sidecar `.lrc` files, or native TagLib embedded lyrics extracted to `~/.cache/musicplayerv2/lyrics/`.

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
