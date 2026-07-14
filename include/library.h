#ifndef LIBRARY_H
#define LIBRARY_H

#include <QString>
#include <QList>
#include <QHash>
#include <QAtomicInt>

struct Song {
    char *filepath;
    char *title;
    char *artist;
    char *album;
    double duration;
    int track_no;
    int disc_no;

    Song() : filepath(nullptr), title(nullptr), artist(nullptr),
             album(nullptr), duration(0.0), track_no(0), disc_no(0) {}
    ~Song() {
        free(filepath);
        free(title);
        free(artist);
        free(album);
    }
};

struct Album {
    char *name;
    char *artist;
    QList<Song*> songs;
    char *cover_path;

    Album() : name(nullptr), artist(nullptr), cover_path(nullptr) {}
    ~Album() {
        free(name);
        free(artist);
        free(cover_path);
        qDeleteAll(songs);
    }
};

struct MusicLibrary {
    QList<Album*> albums;
    QHash<QString, Album*> albumMap;

    MusicLibrary() {}
    ~MusicLibrary() { qDeleteAll(albums); }
};

struct PlayerConfig {
    char *library_path;
    char *import_dest_path;
    double volume;
    bool shuffle;
    bool repeat_mode;

    PlayerConfig() : library_path(nullptr), import_dest_path(nullptr),
                     volume(0.8), shuffle(false), repeat_mode(true) {}
    ~PlayerConfig() {
        free(library_path);
        free(import_dest_path);
    }
};

MusicLibrary *library_new();
void library_free(MusicLibrary *lib);
void library_scan(MusicLibrary *lib, const QString &rootPath, QAtomicInt *scannedCounter);
Album *library_find_album(MusicLibrary *lib, const char *artist, const char *album_name);
QList<Album*> library_get_recent_albums(MusicLibrary *lib, int limit);

PlayerConfig *config_load();
void config_save(PlayerConfig *cfg);
void config_free(PlayerConfig *cfg);

char *resolve_cover_art(const char *song_path);
char *resolve_lyrics(const char *song_path);

#endif // LIBRARY_H
