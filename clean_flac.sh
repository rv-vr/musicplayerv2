#!/bin/bash

# Defaults
DRY_RUN=false
TARGET_DIR="."

# Function to show usage
usage() {
    echo "Usage: $0 [--dry-run] [-d|--directory <path>]"
    exit 1
}

# Parse command-line arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --dry-run) DRY_RUN=true ;;
        -d|--directory) 
            if [[ -n "$2" && ! "$2" =~ ^- ]]; then
                TARGET_DIR="$2"
                shift
            else
                echo "Error: --directory requires a valid path."
                exit 1
            fi
            ;;
        *) usage ;;
    esac
    shift
done

# Check if directory exists
if [[ ! -d "$TARGET_DIR" ]]; then
    echo "Error: Directory '$TARGET_DIR' does not exist."
    exit 1
fi

# Use nullglob so the loop doesn't run if no files match the pattern
shopt -s nullglob

echo "Processing directory: $TARGET_DIR"
[[ "$DRY_RUN" == true ]] && echo "--- MODE: DRY RUN (No files will be moved) ---"

found_any=false

# Loop through flac files containing the explicit tag
for file in "$TARGET_DIR"/*" [Explicit].flac"; do
    found_any=true
    
    # Generate the new name by removing " [Explicit]"
    new_name="${file/ [Explicit]/}"

    if [[ "$DRY_RUN" == true ]]; then
        echo "[PLAN] Rename: '$(basename "$file")' -> '$(basename "$new_name")'"
    else
        mv "$file" "$new_name"
        echo "Done: '$(basename "$file")' -> '$(basename "$new_name")'"
    fi
done

if [[ "$found_any" == false ]]; then
    echo "No files found matching the pattern '* [Explicit].flac'."
fi
