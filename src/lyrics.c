#include "lyrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gint compare_lyric_lines(gconstpointer a, gconstpointer b) {
    const LyricLine *la = (const LyricLine *)a;
    const LyricLine *lb = (const LyricLine *)b;
    if (la->timestamp < lb->timestamp) return -1;
    if (la->timestamp > lb->timestamp) return 1;
    return 0;
}

Lyrics *lyrics_load(const char *filepath) {
    if (!filepath) return NULL;
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return NULL;
    
    Lyrics *lyrics = g_new0(Lyrics, 1);
    lyrics->lines = g_array_new(FALSE, FALSE, sizeof(LyricLine));
    
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // Strip trailing newline characters
        g_strstrip(line);
        if (strlen(line) == 0) continue;
        
        const char *p = line;
        GList *timestamps = NULL;
        
        // Parse all consecutive timestamps at start of line
        while (*p == '[') {
            int min;
            double sec;
            if (sscanf(p, "[%d:%lf]", &min, &sec) == 2) {
                double *ts = g_new(double, 1);
                *ts = min * 60.0 + sec;
                timestamps = g_list_append(timestamps, ts);
            }
            
            // Move to closing bracket
            const char *close = strchr(p, ']');
            if (!close) break;
            p = close + 1;
        }
        
        // Clean the lyrics text
        char *lyric_text = g_strdup(p);
        g_strstrip(lyric_text);
        
        // Add lyric line for each timestamp
        for (GList *l = timestamps; l != NULL; l = l->next) {
            double *ts = (double *)l->data;
            LyricLine new_line;
            new_line.timestamp = *ts;
            new_line.text = g_strdup(lyric_text);
            g_array_append_val(lyrics->lines, new_line);
            g_free(ts);
        }
        
        g_list_free(timestamps);
        g_free(lyric_text);
    }
    
    fclose(fp);
    
    // Sort lines by timestamp
    g_array_sort(lyrics->lines, compare_lyric_lines);
    
    return lyrics;
}

void lyrics_free(Lyrics *lyrics) {
    if (lyrics) {
        if (lyrics->lines) {
            for (guint i = 0; i < lyrics->lines->len; i++) {
                LyricLine *line = &g_array_index(lyrics->lines, LyricLine, i);
                g_free(line->text);
            }
            g_array_free(lyrics->lines, TRUE);
        }
        g_free(lyrics);
    }
}

int lyrics_find_index(Lyrics *lyrics, double seconds) {
    if (!lyrics || !lyrics->lines || lyrics->lines->len == 0) return -1;
    
    LyricLine *first = &g_array_index(lyrics->lines, LyricLine, 0);
    if (seconds < first->timestamp) return -1;
    
    int active_idx = -1;
    for (guint i = 0; i < lyrics->lines->len; i++) {
        LyricLine *line = &g_array_index(lyrics->lines, LyricLine, i);
        if (seconds >= line->timestamp) {
            active_idx = i;
        } else {
            break;
        }
    }
    return active_idx;
}
