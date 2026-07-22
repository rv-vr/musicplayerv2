#ifndef LIBRARY_H
#define LIBRARY_H

#include <QString>
#include <QList>
#include <QHash>
#include <QMetaType>
#include <atomic>
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
    Q_DISABLE_COPY_MOVE(Album)
    std::string name;
    std::string artist;
    QList<Song*> songs;
    std::string cover_path;

    Album() = default;
    ~Album() {
        qDeleteAll(songs);
        songs.clear();
    }
};

struct MusicLibrary {
    Q_DISABLE_COPY_MOVE(MusicLibrary)
    QList<Album*> albums;
    QHash<QString, Album*> albumMap;

    MusicLibrary() = default;
    ~MusicLibrary() {
        qDeleteAll(albums);
        albums.clear();
        albumMap.clear();
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
void library_scan(MusicLibrary *lib, const QString &rootPath, std::atomic<int> *scannedCounter, std::atomic<int> *totalCounter = nullptr);
Album *library_find_album(const MusicLibrary *lib, const char *artist, const char *album_name);
QList<Album*> library_get_recent_albums(const MusicLibrary *lib, int limit);

PlayerConfig *config_load();
void config_save(PlayerConfig *cfg);
void config_free(PlayerConfig *cfg);

char *resolve_cover_art(const char *song_path);
char *resolve_lyrics(const char *song_path);

QStringList library_get_artists(const MusicLibrary *lib);
QList<Album*> library_get_albums_by_artist(const MusicLibrary *lib, const QString &artist);

Q_DECLARE_METATYPE(Song*)

#endif // LIBRARY_H
