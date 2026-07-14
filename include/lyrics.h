#ifndef LYRICS_H
#define LYRICS_H

#include <QVector>

typedef struct {
    double timestamp;
    char *text;
} LyricLine;

typedef struct {
    QVector<LyricLine> lines;
} Lyrics;

Lyrics *lyrics_load(const char *filepath);
void lyrics_free(Lyrics *lyrics);
int lyrics_find_index(Lyrics *lyrics, double seconds);

#endif
