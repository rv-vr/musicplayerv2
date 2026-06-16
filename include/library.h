#ifndef LIBRARY_H
#define LIBRARY_H

#include <glib.h>

typedef struct {
    char *filepath;
    char *title;
    char *artist;
    char *album;
    double duration; // in seconds
    int track_no;
    int disc_no;
} Song;

typedef struct {
    char *name;
    char *artist;
    GList *songs; // List of Song pointers
    char *cover_path; // Local cover path if found (cover.jpg etc.)
} Album;

typedef struct {
    GList *albums; // List of Album pointers
    GHashTable *album_map; // Key: "Artist - Album", Value: Album pointer
} MusicLibrary;

// Configuration structure
typedef struct {
    char *library_path;
    char *import_dest_path;
    double volume;      // 0.0 to 1.0
    gboolean shuffle;
    gboolean repeat_mode; // TRUE = repeat queue, FALSE = no repeat
} PlayerConfig;

// Library Functions
MusicLibrary *library_new();
void library_free(MusicLibrary *lib);
void library_scan(MusicLibrary *lib, const char *root_path, volatile int *scanned_counter);
Album *library_find_album(MusicLibrary *lib, const char *artist, const char *album_name);
GList *library_get_recent_albums(MusicLibrary *lib, int limit);

// Config Functions
PlayerConfig *config_load();
void config_save(PlayerConfig *cfg);
void config_free(PlayerConfig *cfg);

// Helper function to extract metadata
char *resolve_cover_art(const char *song_path);
char *resolve_lyrics(const char *song_path);

#endif // LIBRARY_H
