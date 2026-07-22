# Qt 6 C++ Music Player with Clean & Lyrics Embedding Integration

A premium, dark-mode Qt 6 music player written in C++ (incorporating custom C database scanning engines) using the **BASS Audio Library** for high-quality playback. It features a modern dark glassmorphic design, dynamic real-time synchronized karaoke lyrics, and native C++ batch lyrics embedding and FLAC-to-AAC VBR transcoding via QAAC (`wine qaac64.exe -q 2 -v 0`).

---

## Features

1. **Player Sidebar (Left Pane - 300px width)**:
   - Dynamic Album Cover art loading (checks local album files, extracts embedded art via TagLib 2.3, or falls back to a clean dark-grey placeholder).
   - Track Title and Artist scrolling labels.
   - Seek slider and elapsed/total duration indicators.
   - Media controls: Prev, Play/Pause, Next, Volume slider (with Mute toggle), Shuffle, and Repeat toggle buttons.

2. **Main Panel (Right Pane)**:
   - **Library Tab**: Scans the designated root folder and displays a side-by-side view: **Albums** on the left, and **Tracks** on the right. Double-clicking any track loads the album into the active queue and starts playing.
   - **Queue Tab**: Lists the current playback queue, highlights the actively playing track, and provides a `[Clear Queue]` option.
   - **Lyrics Tab**: Real-time auto-scrolling karaoke-style synchronized lyrics. The active lyric line is dynamically scaled, highlighted in green/bold, and automatically centered vertically.
   - **Import & Clean Tab**:
     - Source folder picker and Library destination picker.
     - Toggle options: Dry run (`--dry-run`), Delete original FLAC files after conversion, Skip embedding if lyrics already exist, and Delete sidecar `.lrc` files after embedding.
     - A real-time console log viewer capturing worker thread output and progress bar.
     - Automatically triggers a library re-scan on success.

---

## Prerequisites

- **Build Utilities**: `gcc`, `g++`, `cmake`, `make`, and `pkg-config`.
- **GUI Libraries**: Qt 6 Widgets development package (e.g. `qt6-qtbase-devel` on Fedora/RHEL, or `qt6-base-dev` on Debian/Ubuntu).
- **TagLib Development Libraries**: `taglib-devel` (TagLib 2.3+).
- **SQLite3 Development Libraries**: `sqlite-devel` or `libsqlite3-dev`.
- **WINE & qaac**: WINE configured to run `qaac` at `~/.wine/drive_c/qaac/qaac64.exe` (needed for FLAC to AAC VBR transcode).

---

## Compilation and Packaging

Build the application and package it into `dist/` by running:
```bash
./build.sh
```

To clean build files:
```bash
rm -rf build dist
```

---

## Running the Application

After building, run the application from the `dist/` folder:

```bash
cd dist
./musicplayer
```

---

## Configuration & Paths

- **Config File**: Settings (library path, volume, shuffle, repeat) are persisted in `~/.config/musicplayerv2/config.ini`.
- **Database**: SQLite metadata cache is stored in `~/.config/musicplayerv2/library.db`.
- **Temp Assets**: Extracted artwork and lyrics are dumped to `/tmp/musicplayerv2_cover.png` and `/tmp/musicplayerv2_lyrics.lrc` respectively during runtime.
