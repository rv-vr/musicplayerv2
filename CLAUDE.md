# CLAUDE.md

## Build & Run Instructions
- **Build**: Run `make` in project root.
- **Clean**: Run `make clean`.
- **Run**: Run `./musicplayer`.

## Project Structure
- `include/library.h` / `src/library.c` - Configuration (`config.ini`) and recursive file scanning (`library_scan`).
- `include/lyrics.h` / `src/lyrics.c` - Synced LRC parser (`lyrics_load`) and timer active line lookup (`lyrics_find_index`).
- `src/main.c` - GTK 3 UI layout, BASS engine, subprocess integration, CSS styling.
- `extract_metadata.py` - Mutagen/eyed3 helper for cover art and lyrics extraction.
- `run_import.sh` - Chained script runner for `lrcput.py` and `clean_flac.py`.
- `Makefile` - Compilation flags and library linking.

## Key APIs & Commands
- **Metadata Extraction**:
  - Cover: `python3 extract_metadata.py <file> --cover <out>`
  - Lyrics: `python3 extract_metadata.py <file> --lyrics <out>`
- **Import Script**:
  - `./run_import.sh <src> <dest> <dry_run> <remove> <skip> <reduce>`

## Configuration & Storage
- **Config**: Stored at `~/.config/musicplayerv2/config.ini`
  - Key parameters:
    - `LibraryPath`: Folder path scanned for playback library.
    - `ImportDestPath`: Destination folder where cleaned imports are saved.
    - `Volume`, `Shuffle`, `Repeat`: Playback state.
- **Database Cache**: SQLite3 database stored at `~/.config/musicplayerv2/library.db` caching metadata (title, artist, album, duration, track_no, disc_no) with `mtime`-based checking.
- **Asynchronous Scanning**: Filesystem scan runs in a background thread (`start_async_scan`) with file counting and a progress timeout handler to update the status bar without UI lag.

## UI Tabs
- **Library**: Album list + track list (paned). Albums sorted alphabetically; tracklist sorted by disc and track number. Double-click track → queue album + play.
- **Play Queue**: Current queue. Double-click → jump to track.
- **Lyrics**: Auto-scroll karaoke-style synced LRC display.
- **Import & Clean**: Source picker, toggles, log viewer. Dest path read-only (set in Settings).
- **Settings**: Library scan path + import dest path choosers. Explicit "Save & Apply" button.

## Design System
- **Layout**: Glassmorphic floating card layout. Both `.sidebar` and `.main-content` panels wrap inside containers with `margin: 10px`, `border-radius: 12px`, `border: 1px solid #28282e`, and `box-shadow` values.
- **Typography**: Global modern sans-serif font family (`Inter` / `Outfit` / `Helvetica Neue`).
- **Icons**: GTK Adwaita symbolic icons (`media-playback-start-symbolic`, etc.). Sizes set via `gtk_image_set_pixel_size()` — NOT CSS `-gtk-icon-size` (GTK 4 only).
- **Icon sizes**: Play/Pause 32px (knob size 46px), Prev/Next 24px, Shuffle/Repeat/Mute 20px.
- **CSS classes**:
  - `.control-btn` — Circular green play/pause button (`min-width: 46px`) with hover glow.
  - `.flat-icon-btn` — Transparent flat button, subtle hover background.
  - `.toggle-icon-btn` — Flat toggle, grey inactive → green active.
  - `.track-title` / `.track-artist` — Sidebar label styles.
  - `.time-label` — Small grey time display.
  - `.lyric-line` / `.lyric-active` — Lyrics display only.
- **Tabs (Stack Switcher)**: Styled as a floating rounded pill tab container (`border-radius: 20px`). Unchecked tabs have transparent backgrounds; checked tab has a solid green background with dark text.
- **Sliders & Lists**:
  - GtkScale troughs thicken on hover, and slider handles transition from `0px` to `12px` size.
  - TreeView rows (`treeview.view row`) have rounded corners (`border-radius: 6px`) and smooth hover/selection transitions.
- **Colors**: Window bg `#121214`, card bg `#18181c`, borders `#28282e`, text `#e1e1e6`, muted text `#a8a8b3`, accent green `#04d361`.
- **GTK 3 CSS gotchas**: No `-gtk-icon-size`. Use `treeview:selected` (or `treeview.view row:selected`) not `treeview::row:selected`.
