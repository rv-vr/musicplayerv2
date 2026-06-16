import argparse
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from mutagen.flac import FLAC

# Characters that are invalid on Windows/Wine filesystems or cause shell issues
_INVALID_CHARS = re.compile(r'[<>:"/\\|?*]')

def sanitize_path_part(name):
    """Replaces illegal path characters with dashes."""
    if not name:
        return "Unknown"
    return _INVALID_CHARS.sub("-", name).strip()

def sanitize_filename(name):
    """Sanitize a filename for safe use with Wine/qaac.
    Replaces characters that are invalid on Windows (*, ?, <, >, etc.)
    with underscores, preserving the extension."""
    stem = Path(name).stem
    suffix = Path(name).suffix
    clean_stem = _INVALID_CHARS.sub("_", stem).strip()
    return f"{clean_stem}{suffix}"

def process_music(source_dir, output_root, dry_run=False, remove_source=False):
    src = Path(source_dir)
    dest_root = Path(output_root)
    pattern = " [Explicit]"

    # qaac binary path (invoked via wine)
    qaac_exe = os.path.expanduser("~/.wine/drive_c/qaac/qaac64.exe")

    if not src.is_dir():
        print(f"Error: Source directory '{source_dir}' not found.")
        return

    print(f"Organizing and converting to: {dest_root.resolve()}")
    if dry_run:
        print("--- MODE: DRY RUN (No files will be converted or deleted) ---\n")

    for file_path in src.glob("*.flac"):
        try:
            audio = FLAC(file_path)
            tags_changed = False

            # 1. Clean Title and Album Tags in the FLAC metadata
            for tag in ["title", "album"]:
                if tag in audio and pattern in audio[tag][0]:
                    audio[tag] = audio[tag][0].replace(pattern, "")
                    tags_changed = True

            # 2. Extract Metadata for Folder Structure
            artist_tag = audio.get("albumartist") or audio.get("artist") or ["Unknown Artist"]
            album_tag = audio.get("album") or ["Unknown Album"]

            clean_artist = sanitize_path_part(artist_tag[0])
            clean_album = sanitize_path_part(album_tag[0])

            # 3. Define the new filename and destination path
            clean_filename = file_path.name.replace(pattern, "").replace(".flac", ".m4a")
            clean_filename = sanitize_filename(clean_filename)
            target_dir = dest_root / clean_artist / clean_album
            output_file = target_dir / clean_filename

            if dry_run:
                print(f"[PLAN] {file_path.name} -> {clean_artist}/{clean_album}/{clean_filename}")
                if remove_source:
                    print(f"       └─ [PLAN] Delete original: {file_path.name}")
                continue

            # Save tag changes to the FLAC so qaac reads the cleaned tags
            if tags_changed:
                audio.save()

            # Ensure destination folders exist
            target_dir.mkdir(parents=True, exist_ok=True)

            # 4. Stage file in a temp directory with a sanitized name
            #    This avoids qaac/Wine choking on *, ?, etc. in paths
            staging_dir = None
            try:
                staging_dir = tempfile.mkdtemp(prefix="qaac_stage_")
                safe_input_name = sanitize_filename(file_path.name)
                staged_input = Path(staging_dir) / safe_input_name
                safe_output_name = sanitize_filename(clean_filename)
                staged_output = Path(staging_dir) / safe_output_name

                # Copy source FLAC to staging with a safe name
                shutil.copy2(str(file_path), str(staged_input))

                # Build command as a list (no shell=True, no glob expansion)
                env = os.environ.copy()
                env["WINEDEBUG"] = "-all"

                cmd = [
                    "wine", qaac_exe,
                    "--copy-artwork",
                    "-v", "0",
                    "-q", "2",
                    "-o", str(staged_output),
                    str(staged_input),
                ]

                print(f"[RUNNING] Converting: {file_path.name}")
                result = subprocess.run(cmd, env=env)

                if result.returncode == 0:
                    # Move the encoded file from staging to its final destination
                    shutil.move(str(staged_output), str(output_file))
                    print(f"  └─ Success: {clean_filename}")

                    # 5. Handle source file deletion if requested
                    if remove_source:
                        file_path.unlink()
                        print(f"  └─ Deleted original: {file_path.name}")
                else:
                    print(f"  └─ Error: qaac failed for {file_path.name} (Original retained)")

            finally:
                # Always clean up the staging directory
                if staging_dir and os.path.isdir(staging_dir):
                    shutil.rmtree(staging_dir, ignore_errors=True)

        except Exception as e:
            print(f"Error processing {file_path.name}: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Clean, convert to M4A, and organize FLAC files.")
    parser.add_argument("-d", "--directory", default=".", help="Source directory containing FLACs")
    parser.add_argument("-D", "--destination", required=True, help="Root directory for the organized library")
    parser.add_argument("-r", "--remove", action="store_true", help="Remove original FLAC files after successful conversion")
    parser.add_argument("--dry-run", action="store_true", help="Show the plan without executing")

    args = parser.parse_args()
    process_music(args.directory, args.destination, args.dry_run, args.remove)
