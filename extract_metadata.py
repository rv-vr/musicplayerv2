#!/usr/bin/env python3
import sys
import os
import argparse
from mutagen.flac import FLAC
from mutagen.mp4 import MP4
import eyed3

def extract_cover(audio_path, output_path):
    ext = os.path.splitext(audio_path.lower())[1]
    try:
        if ext == '.flac':
            audio = FLAC(audio_path)
            if audio.pictures:
                with open(output_path, 'wb') as f:
                    f.write(audio.pictures[0].data)
                return True
        elif ext == '.m4a':
            audio = MP4(audio_path)
            if 'covr' in audio.tags:
                cover_data = audio.tags['covr'][0]
                with open(output_path, 'wb') as f:
                    f.write(cover_data)
                return True
        elif ext == '.mp3':
            audio = eyed3.load(audio_path)
            if audio and audio.tag and audio.tag.images:
                # Find front cover or just get first image
                cover = None
                for img in audio.tag.images:
                    if img.image_type == 3: # Front cover
                        cover = img
                        break
                if not cover:
                    cover = audio.tag.images[0]
                with open(output_path, 'wb') as f:
                    f.write(cover.image_data)
                return True
    except Exception as e:
        print(f"Error extracting cover: {e}", file=sys.stderr)
    return False

def extract_lyrics(audio_path, output_path):
    ext = os.path.splitext(audio_path.lower())[1]
    try:
        lyrics = None
        if ext == '.flac':
            audio = FLAC(audio_path)
            # Standard tags for lyrics in FLAC
            for key in ['LYRICS', 'UNSYNCEDLYRICS', 'UNSYNCED LYRICS']:
                if key in audio:
                    lyrics = audio[key][0]
                    break
        elif ext == '.m4a':
            audio = MP4(audio_path)
            if '\xa9lyr' in audio.tags:
                lyrics = audio.tags['\xa9lyr'][0]
        elif ext == '.mp3':
            audio = eyed3.load(audio_path)
            if audio and audio.tag and audio.tag.lyrics:
                for l in audio.tag.lyrics:
                    lyrics = l.text
                    break
        
        if lyrics:
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(lyrics)
            return True
    except Exception as e:
        print(f"Error extracting lyrics: {e}", file=sys.stderr)
    return False

def extract_info(audio_path):
    ext = os.path.splitext(audio_path.lower())[1]
    title = ""
    artist = ""
    album = ""
    album_artist = ""
    track_no = 0
    disc_no = 1
    duration = 0.0
    
    try:
        if ext == '.flac':
            audio = FLAC(audio_path)
            title = audio.get('title', [''])[0]
            artist = audio.get('artist', [''])[0]
            album = audio.get('album', [''])[0]
            album_artist = audio.get('albumartist', [''])[0] or audio.get('album artist', [''])[0]
            
            track_str = audio.get('tracknumber', ['0'])[0]
            if '/' in track_str:
                track_str = track_str.split('/')[0]
            try:
                track_no = int(track_str)
            except ValueError:
                pass
                
            disc_str = audio.get('discnumber', ['1'])[0]
            if '/' in disc_str:
                disc_str = disc_str.split('/')[0]
            try:
                disc_no = int(disc_str)
            except ValueError:
                pass
                
            duration = audio.info.length
            
        elif ext == '.m4a':
            audio = MP4(audio_path)
            title = audio.tags.get('\xa9nam', [''])[0] if audio.tags else ""
            artist = audio.tags.get('\xa9ART', [''])[0] if audio.tags else ""
            album = audio.tags.get('\xa9alb', [''])[0] if audio.tags else ""
            album_artist = audio.tags.get('aART', [''])[0] if audio.tags else ""
            
            trkn = audio.tags.get('trkn', [(0, 0)])[0] if audio.tags else 0
            track_no = trkn[0] if isinstance(trkn, tuple) else (trkn or 0)
            
            disk = audio.tags.get('disk', [(1, 0)])[0] if audio.tags else 1
            disc_no = disk[0] if isinstance(disk, tuple) else (disk or 1)
            
            duration = audio.info.length
            
        elif ext == '.mp3':
            audio = eyed3.load(audio_path)
            if audio and audio.tag:
                title = audio.tag.title or ""
                artist = audio.tag.artist or ""
                album = audio.tag.album or ""
                album_artist = audio.tag.album_artist or ""
                
                if audio.tag.track_num:
                    track_no = audio.tag.track_num[0] or 0
                if audio.tag.disc_num:
                    disc_no = audio.tag.disc_num[0] or 1
                    
            if audio and audio.info:
                duration = audio.info.time_secs
                
        # Clean up type inconsistencies
        if isinstance(title, list) or isinstance(title, tuple):
            title = title[0] if title else ""
        if isinstance(artist, list) or isinstance(artist, tuple):
            artist = artist[0] if artist else ""
        if isinstance(album, list) or isinstance(album, tuple):
            album = album[0] if album else ""
        if isinstance(album_artist, list) or isinstance(album_artist, tuple):
            album_artist = album_artist[0] if album_artist else ""
            
        print(f"title: {title}")
        print(f"artist: {artist}")
        print(f"album: {album}")
        print(f"album_artist: {album_artist}")
        print(f"track: {track_no}")
        print(f"disc: {disc_no}")
        print(f"duration: {duration:.4f}")
        return True
    except Exception as e:
        print(f"Error extracting info: {e}", file=sys.stderr)
        return False

def main():
    parser = argparse.ArgumentParser(description="Extract cover art or lyrics from audio files.")
    parser.add_argument("file", help="Path to audio file")
    parser.add_argument("--cover", help="Output path for cover image")
    parser.add_argument("--lyrics", help="Output path for lyrics text file")
    parser.add_argument("--info", action="store_true", help="Print all metadata to stdout")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.file):
        print(f"File not found: {args.file}", file=sys.stderr)
        sys.exit(1)
        
    success = False
    if args.cover:
        success = extract_cover(args.file, args.cover)
    elif args.lyrics:
        success = extract_lyrics(args.file, args.lyrics)
    elif args.info:
        success = extract_info(args.file)
    else:
        print("Must specify --cover, --lyrics, or --info", file=sys.stderr)
        sys.exit(1)
        
    if success:
        sys.exit(0)
    else:
        sys.exit(2)

if __name__ == "__main__":
    main()
