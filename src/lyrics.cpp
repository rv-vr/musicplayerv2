#include "lyrics.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

static bool lyricLineLessThan(const LyricLine &a, const LyricLine &b) {
    return a.timestamp < b.timestamp;
}

Lyrics *lyrics_load(const char *filepath) {
    if (!filepath) return nullptr;

    FILE *fp = fopen(filepath, "r");
    if (!fp) return nullptr;

    Lyrics *lyrics = new Lyrics;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        const char *p = line;
        QVector<double> timestamps;

        while (*p == '[') {
            int min;
            double sec;
            if (sscanf(p, "[%d:%lf]", &min, &sec) == 2) {
                timestamps.append(min * 60.0 + sec);
            }

            const char *close = strchr(p, ']');
            if (!close) break;
            p = close + 1;
        }

        while (*p == ' ' || *p == '\t') p++;
        char *lyricText = strdup(p);

        for (double ts : timestamps) {
            LyricLine newLine;
            newLine.timestamp = ts;
            newLine.text = strdup(lyricText);
            lyrics->lines.append(newLine);
        }

        free(lyricText);
    }

    fclose(fp);

    std::sort(lyrics->lines.begin(), lyrics->lines.end(), lyricLineLessThan);

    return lyrics;
}

void lyrics_free(Lyrics *lyrics) {
    if (lyrics) {
        for (LyricLine &line : lyrics->lines) {
            free(line.text);
        }
        delete lyrics;
    }
}

int lyrics_find_index(Lyrics *lyrics, double seconds) {
    if (!lyrics || lyrics->lines.isEmpty()) return -1;

    if (seconds < lyrics->lines.first().timestamp) return -1;

    int activeIdx = -1;
    for (int i = 0; i < lyrics->lines.size(); i++) {
        if (seconds >= lyrics->lines[i].timestamp) {
            activeIdx = i;
        } else {
            break;
        }
    }
    return activeIdx;
}
