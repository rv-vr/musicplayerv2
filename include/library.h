#ifndef LIBRARY_H
#define LIBRARY_H

#include <QString>
#include <QList>
#include <QHash>
#include <QAtomicInt>
#include <string>

struct Song {
    std::string filepath;
    std::string title;
    std::string artist;
    std::string album;
    double duration = 0.0;
    int track_no = 0;
    int disc_no = 0;
};

struct Album {
    std::string name;
    std::string artist;
    QList<Song*> songs;
    std::string cover_path;

    ~Album() {
        qDeleteAll(songs);
    }
};

struct MusicLibrary {
    QList<Album*> albums;
    QHash<QString, Album*> albumMap;

    ~MusicLibrary() {
        qDeleteAll(albums);
    }
};

struct PlayerConfig {
    std::string library_path;
    std::string import_dest_path;
    double volume = 0.8;
    bool shuffle = false;
    bool repeat_mode = true;
};

MusicLibrary *library_new();
void library_free(MusicLibrary *lib);
void library_load_cached(MusicLibrary *lib);
void library_scan(MusicLibrary *lib, const QString &rootPath, QAtomicInt *scannedCounter, QAtomicInt *totalCounter = nullptr);
Album *library_find_album(MusicLibrary *lib, const char *artist, const char *album_name);
QList<Album*> library_get_recent_albums(MusicLibrary *lib, int limit);

PlayerConfig *config_load();
void config_save(PlayerConfig *cfg);
void config_free(PlayerConfig *cfg);

char *resolve_cover_art(const char *song_path);
char *resolve_lyrics(const char *song_path);

QStringList library_get_artists(MusicLibrary *lib);
QList<Album*> library_get_albums_by_artist(MusicLibrary *lib, const QString &artist);

#endif // LIBRARY_H
