#ifndef LYRICS_H
#define LYRICS_H

#include <glib.h>

typedef struct {
    double timestamp; // in seconds
    char *text;
} LyricLine;

typedef struct {
    GArray *lines; // Array of LyricLine structs
} Lyrics;

Lyrics *lyrics_load(const char *filepath);
void lyrics_free(Lyrics *lyrics);
int lyrics_find_index(Lyrics *lyrics, double seconds);

#endif // LYRICS_H
