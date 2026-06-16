#!/bin/bash

# Arguments:
# $1: Source Directory
# $2: Destination Library Directory
# $3: Dry Run (true/false)
# $4: Remove Source FLAC (true/false)
# $5: Skip existing lyrics (true/false)
# $6: Reduce/delete LRC files after embedding (true/false)

SRC_DIR="$1"
DEST_DIR="$2"
DRY_RUN="$3"
REMOVE_SOURCE="$4"
SKIP_EXISTING="$5"
REDUCE_LRC="$6"

echo "========================================="
echo "Starting Import & Organization Process"
echo "Source: $SRC_DIR"
echo "Destination: $DEST_DIR"
echo "Dry Run: $DRY_RUN"
echo "Remove FLAC: $REMOVE_SOURCE"
echo "Skip Existing Lyrics: $SKIP_EXISTING"
echo "Delete LRC: $REDUCE_LRC"
echo "========================================="
echo ""

# 1. Run LRC embedding on Source Folder
echo ">>> Running Lyrics Embedding (lrcput)..."
LRC_ARGS="-d \"$SRC_DIR\" -R"
if [ "$SKIP_EXISTING" = "true" ]; then
    LRC_ARGS="$LRC_ARGS -s"
fi
if [ "$REDUCE_LRC" = "true" ]; then
    LRC_ARGS="$LRC_ARGS -r"
fi

eval python3 /home/XOR/Repositories/musicplayerv2/lrcput.py $LRC_ARGS
LRC_STATUS=$?
echo "Lyrics embedding finished with code $LRC_STATUS"
echo ""

# 2. Run FLAC cleaning and conversion
echo ">>> Running FLAC Cleaning & M4A Conversion (clean_flac)..."
CLEAN_ARGS="-d \"$SRC_DIR\" -D \"$DEST_DIR\""
if [ "$DRY_RUN" = "true" ]; then
    CLEAN_ARGS="$CLEAN_ARGS --dry-run"
fi
if [ "$REMOVE_SOURCE" = "true" ]; then
    CLEAN_ARGS="$CLEAN_ARGS -r"
fi

eval python3 /home/XOR/Repositories/musicplayerv2/clean_flac.py $CLEAN_ARGS
CLEAN_STATUS=$?
echo "FLAC clean/conversion finished with code $CLEAN_STATUS"
echo ""

echo "========================================="
if [ $LRC_STATUS -eq 0 ] && [ $CLEAN_STATUS -eq 0 ]; then
    echo "Import Process Completed Successfully!"
else
    echo "Import Process completed with errors."
fi
echo "========================================="
exit $((LRC_STATUS + CLEAN_STATUS))
