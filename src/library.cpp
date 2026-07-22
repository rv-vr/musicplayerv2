#include "library.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QAtomicInt>
#include <QDebug>
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sqlite3.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/audioproperties.h>
#include "bass.h"

struct ScanContext {
    MusicLibrary *lib;
    sqlite3 *db;
    QHash<QString, bool> visited;
    QString rootPath;
    QAtomicInt *scannedCounter;
    QMutex dbMutex;
    QMutex libMutex;
};

struct ScanTask {
    QString filepath;
    ScanContext *ctx;
};

static void gatherFilesRecursive(const QString &dirPath, QStringList &files);
static void scanFile(const ScanTask &task);

MusicLibrary *library_new() {
    MusicLibrary *lib = new MusicLibrary;
    lib->albums.clear();
    lib->albumMap.clear();
    return lib;
}

void library_free(MusicLibrary *lib) {
    if (!lib) return;
    qDeleteAll(lib->albums);
    delete lib;
}

static QString getDbPath() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + "/musicplayerv2";
    QDir().mkpath(configDir);
    return configDir + "/library.db";
}

static sqlite3 *dbInit() {
    QString dbPath = getDbPath();
    sqlite3 *db = nullptr;
    int rc = sqlite3_open(dbPath.toUtf8().constData(), &db);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return nullptr;
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS songs ("
                      "filepath TEXT PRIMARY KEY, "
                      "title TEXT, artist TEXT, album TEXT, "
                      "duration REAL, mtime INTEGER, "
                      "track_no INTEGER, disc_no INTEGER, "
                      "album_artist TEXT"
                      ");";
    char *errMsg = nullptr;
    rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        qWarning() << "Failed to create table:" << errMsg;
        sqlite3_free(errMsg);
        sqlite3_close(db);
        QFile::remove(getDbPath());
        dbPath = getDbPath();
        sqlite3_open(dbPath.toUtf8().constData(), &db);
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    } else {
        sqlite3_stmt *chkStmt;
        if (sqlite3_prepare_v2(db, "SELECT track_no, disc_no, album_artist FROM songs LIMIT 1;", -1, &chkStmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "DROP TABLE songs;", nullptr, nullptr, nullptr);
            sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        } else {
            sqlite3_finalize(chkStmt);
        }
    }
    return db;
}

static void cleanOrphanedCache(sqlite3 *db, const QHash<QString, bool> &visited, const QString &rootPath) {
    if (!db) return;

    sqlite3_stmt *stmt;
    QStringList toDelete;

    if (sqlite3_prepare_v2(db, "SELECT filepath FROM songs;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *fp = (const char *)sqlite3_column_text(stmt, 0);
            if (!fp) continue;
            QString path = QString::fromUtf8(fp);

            if (!visited.contains(path)) {
                if (path.startsWith(rootPath) || !QFile::exists(path)) {
                    toDelete.append(path);
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    if (!toDelete.isEmpty()) {
        sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        const char *delSql = "DELETE FROM songs WHERE filepath = ?;";
        sqlite3_stmt *delStmt;
        if (sqlite3_prepare_v2(db, delSql, -1, &delStmt, nullptr) == SQLITE_OK) {
            for (const QString &path : toDelete) {
                QByteArray pathBytes = path.toUtf8();
                sqlite3_bind_text(delStmt, 1, pathBytes.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_step(delStmt);
                sqlite3_reset(delStmt);
            }
            sqlite3_finalize(delStmt);
        }
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    }
}

static bool albumLessThan(const Album *a, const Album *b) {
    if (!a || !b) return a < b;
    if (a->name != b->name) return a->name < b->name;
    return a->artist < b->artist;
}

static bool songLessThan(const Song *a, const Song *b) {
    if (!a || !b) return a < b;
    if (a->disc_no != b->disc_no) return a->disc_no < b->disc_no;
    if (a->track_no != b->track_no) return a->track_no < b->track_no;
    if (a->title != b->title) return a->title < b->title;
    return a->filepath < b->filepath;
}

static double queryDuration(const char *filepath) {
    HSTREAM stream = BASS_StreamCreateFile(FALSE, filepath, 0, 0, BASS_STREAM_DECODE);
    double duration = 0.0;
    if (stream) {
        QWORD len = BASS_ChannelGetLength(stream, BASS_POS_BYTE);
        duration = BASS_ChannelBytes2Seconds(stream, len);
        BASS_StreamFree(stream);
    }
    return duration;
}

static void gatherFilesRecursive(const QString &dirPath, QStringList &files) {
    QDirIterator it(dirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.fileInfo().isDir()) continue;
        QString ext = it.fileInfo().suffix().toLower();
        if (ext == "m4a" || ext == "mp3" || ext == "flac") {
            files.append(it.filePath());
        }
    }
}

static void scanFile(const ScanTask &task) {
    const QString &fullPath = task.filepath;
    ScanContext *ctx = task.ctx;

    QFileInfo fi(fullPath);
    QString entryName = fi.fileName();
    qint64 diskMtime = fi.lastModified().toSecsSinceEpoch();

    char *artist_name = nullptr;
    char *album_name = nullptr;
    char *album_artist = nullptr;
    char *title = nullptr;
    double duration = 0.0;
    int track_no = 0;
    int disc_no = 1;
    bool cachedFound = false;

    QByteArray fullPathBytes = fullPath.toUtf8();
    const char *fullPathC = fullPathBytes.constData();

    // Try cache
    if (ctx->db) {
        QMutexLocker locker(&ctx->dbMutex);
        sqlite3_stmt *stmt;
        const char *query = "SELECT title, artist, album, duration, mtime, track_no, disc_no, album_artist "
                            "FROM songs WHERE filepath = ?;";
        if (sqlite3_prepare_v2(ctx->db, query, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, fullPathC, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                qint64 cachedMtime = sqlite3_column_int64(stmt, 4);
                double cachedDur = sqlite3_column_double(stmt, 3);
                if (cachedMtime == diskMtime && cachedDur > 0.0) {
                    const char *tStr = (const char *)sqlite3_column_text(stmt, 0);
                    const char *aStr = (const char *)sqlite3_column_text(stmt, 1);
                    const char *alStr = (const char *)sqlite3_column_text(stmt, 2);
                    const char *caStr = (const char *)sqlite3_column_text(stmt, 7);

                    if (tStr) title = strdup(tStr);
                    if (aStr) artist_name = strdup(aStr);
                    if (alStr) album_name = strdup(alStr);
                    if (caStr) album_artist = strdup(caStr);

                    duration = cachedDur;
                    track_no = sqlite3_column_int(stmt, 5);
                    disc_no = sqlite3_column_int(stmt, 6);
                    cachedFound = true;
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    // Extract metadata using TagLib if not cached
    if (!cachedFound) {
        TagLib::FileRef f(fullPathC);
        if (!f.isNull() && f.tag()) {
            TagLib::Tag *tag = f.tag();
            std::string t = tag->title().to8Bit(true);
            std::string a = tag->artist().to8Bit(true);
            std::string al = tag->album().to8Bit(true);

            if (!t.empty()) title = strdup(t.c_str());
            if (!a.empty()) artist_name = strdup(a.c_str());
            if (!al.empty()) album_name = strdup(al.c_str());

            track_no = tag->track();

            if (f.audioProperties()) {
                duration = f.audioProperties()->lengthInSeconds();
            }

            TagLib::PropertyMap properties = f.file()->properties();
            if (properties.contains("ALBUMARTIST")) {
                std::string aa = properties["ALBUMARTIST"].front().to8Bit(true);
                if (!aa.empty()) album_artist = strdup(aa.c_str());
            } else if (properties.contains("ALBUM ARTIST")) {
                std::string aa = properties["ALBUM ARTIST"].front().to8Bit(true);
                if (!aa.empty()) album_artist = strdup(aa.c_str());
            }

            if (properties.contains("DISCNUMBER")) {
                std::string dn = properties["DISCNUMBER"].front().to8Bit(true);
                int d = std::atoi(dn.c_str());
                if (d > 0) disc_no = d;
            }
        }

        // Fallback: infer from filename
        if (!title || strlen(title) == 0) {
            free(title);
            title = strdup(entryName.toUtf8().constData());
            char *dot = strrchr(title, '.');
            if (dot) *dot = '\0';
        }

        if ((!artist_name || strlen(artist_name) == 0 || !album_name || strlen(album_name) == 0)
            && fullPath.startsWith(ctx->rootPath)) {
            QString rel = fullPath.mid(ctx->rootPath.length());
            while (rel.startsWith('/')) rel = rel.mid(1);
            QStringList parts = rel.split('/');

            if (!artist_name || strlen(artist_name) == 0) {
                free(artist_name);
                artist_name = strdup((parts.size() >= 3) ? parts[parts.size()-3].toUtf8().constData() : "Unknown Artist");
            }
            if (!album_name || strlen(album_name) == 0) {
                free(album_name);
                album_name = strdup((parts.size() >= 2) ? parts[parts.size()-2].toUtf8().constData() : "Unknown Album");
            }
        }

        // Parse track number from filename
        if (track_no <= 0) {
            const char *p = entryName.toUtf8().constData();
            while (*p && std::isspace(*p)) p++;
            if (std::isdigit(*p)) {
                int firstNum = 0;
                while (*p && std::isdigit(*p)) { firstNum = firstNum * 10 + (*p - '0'); p++; }
                while (*p && (std::isspace(*p) || *p == '-' || *p == '.' || *p == '_')) p++;
                if (*p == '[') {
                    const char *next = p + 1;
                    if (std::isdigit(*next)) {
                        int secondNum = 0;
                        while (*next && std::isdigit(*next)) { secondNum = secondNum * 10 + (*next - '0'); next++; }
                        disc_no = firstNum;
                        track_no = secondNum;
                    } else {
                        track_no = firstNum;
                    }
                } else {
                    track_no = firstNum;
                }
            }
        }
        if (disc_no <= 0) disc_no = 1;
    }

    // Insert into library
    {
        QMutexLocker locker(&ctx->libMutex);

        // Save to db if new
        if (ctx->db && !cachedFound) {
            QMutexLocker dbLocker(&ctx->dbMutex);
            sqlite3_stmt *stmt;
            const char *insertSql = "INSERT OR REPLACE INTO songs "
                "(filepath, title, artist, album, duration, mtime, track_no, disc_no, album_artist) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
            if (sqlite3_prepare_v2(ctx->db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, fullPathC, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, title, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, artist_name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, album_name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 5, duration);
                sqlite3_bind_int64(stmt, 6, diskMtime);
                sqlite3_bind_int(stmt, 7, track_no);
                sqlite3_bind_int(stmt, 8, disc_no);
                sqlite3_bind_text(stmt, 9, album_artist ? album_artist : "", -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        ctx->visited.insert(fullPath, true);

        // Build album key & insert into memory if lib provided
        if (ctx->lib) {
            const char *effectiveArtist = (album_artist && strlen(album_artist) > 0) ? album_artist : (artist_name ? artist_name : "Unknown Artist");
            const char *effectiveAlbum = (album_name && strlen(album_name) > 0) ? album_name : "Unknown Album";
            QString key = (QString::fromUtf8(effectiveArtist) + " - " + QString::fromUtf8(effectiveAlbum)).toLower();

            Album *album = ctx->lib->albumMap.value(key, nullptr);
            if (!album) {
                album = new Album;
                album->name = effectiveAlbum;
                album->artist = effectiveArtist;
                album->cover_path.clear();

                // Search for cover art in parent dir
                QString parentDir = fi.absolutePath();
                const char *coverNames[] = {
                    "cover.jpg", "cover.png", "folder.jpg", "folder.png",
                    "Cover.jpg", "Cover.png", "Folder.jpg", "Folder.png"
                };
                for (int i = 0; i < 8; i++) {
                    QString covTest = parentDir + "/" + coverNames[i];
                    if (QFile::exists(covTest)) {
                        album->cover_path = covTest.toUtf8().constData();
                        break;
                    }
                }

                ctx->lib->albumMap.insert(key, album);
                if (!ctx->lib->albums.contains(album)) {
                    ctx->lib->albums.append(album);
                }
            }

            Song *song = new Song;
            song->filepath = fullPathC;
            if (title) song->title = title;
            if (artist_name) song->artist = artist_name;
            if (album_name) song->album = album_name;
            song->duration = duration;
            song->track_no = track_no;
            song->disc_no = disc_no;

            album->songs.append(song);
        }

        if (ctx->scannedCounter) {
            (*ctx->scannedCounter)++;
        }
    }

    free(artist_name);
    free(album_name);
    free(album_artist);
    free(title);
}

void library_load_cached(MusicLibrary *lib) {
    if (!lib) return;

    qDeleteAll(lib->albums);
    lib->albums.clear();
    lib->albumMap.clear();

    sqlite3 *db = dbInit();
    if (!db) return;

    sqlite3_stmt *stmt;
    const char *query = "SELECT filepath, title, artist, album, duration, track_no, disc_no, album_artist "
                        "FROM songs;";

    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *fp = (const char *)sqlite3_column_text(stmt, 0);
            const char *ti = (const char *)sqlite3_column_text(stmt, 1);
            const char *ar = (const char *)sqlite3_column_text(stmt, 2);
            const char *al = (const char *)sqlite3_column_text(stmt, 3);
            double dur = sqlite3_column_double(stmt, 4);
            int trk = sqlite3_column_int(stmt, 5);
            int dsc = sqlite3_column_int(stmt, 6);
            const char *aar = (const char *)sqlite3_column_text(stmt, 7);

            if (!fp) continue;

            const char *effArtist = (aar && strlen(aar) > 0) ? aar : (ar ? ar : "Unknown Artist");
            const char *effAlbum = (al && strlen(al) > 0) ? al : "Unknown Album";
            QString key = (QString::fromUtf8(effArtist) + " - " + QString::fromUtf8(effAlbum)).toLower();

            Album *album = lib->albumMap.value(key, nullptr);
            if (!album) {
                album = new Album;
                album->name = effAlbum;
                album->artist = effArtist;

                QFileInfo fi(fp);
                QString parentDir = fi.absolutePath();
                const char *coverNames[] = {
                    "cover.jpg", "cover.png", "folder.jpg", "folder.png",
                    "Cover.jpg", "Cover.png", "Folder.jpg", "Folder.png"
                };
                for (int i = 0; i < 8; i++) {
                    QString covTest = parentDir + "/" + coverNames[i];
                    if (QFile::exists(covTest)) {
                        album->cover_path = covTest.toUtf8().constData();
                        break;
                    }
                }

                lib->albumMap.insert(key, album);
                if (!lib->albums.contains(album)) {
                    lib->albums.append(album);
                }
            }

            Song *song = new Song;
            song->filepath = fp;
            song->title = ti ? ti : "";
            song->artist = ar ? ar : "";
            song->album = al ? al : "";
            song->duration = dur;
            song->track_no = trk;
            song->disc_no = dsc;

            album->songs.append(song);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);

    std::sort(lib->albums.begin(), lib->albums.end(), albumLessThan);
    for (Album *album : lib->albums) {
        std::sort(album->songs.begin(), album->songs.end(), songLessThan);
    }
}

void library_scan(MusicLibrary *lib, const QString &rootPath, QAtomicInt *scannedCounter, QAtomicInt *totalCounter) {
    if (lib) {
        qDeleteAll(lib->albums);
        lib->albums.clear();
        lib->albumMap.clear();
    }

    if (rootPath.isEmpty() || !QDir(rootPath).exists()) return;

    // Gather files (single pass)
    QStringList files;
    gatherFilesRecursive(rootPath, files);

    if (totalCounter) {
        totalCounter->storeRelaxed(files.size());
    }

    // Init DB
    sqlite3 *db = dbInit();
    if (db) sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // Process with thread pool
    ScanContext ctx;
    ctx.lib = lib;
    ctx.db = db;
    ctx.rootPath = rootPath;
    ctx.scannedCounter = scannedCounter;

    QThreadPool pool;
    pool.setMaxThreadCount(8);

    QList<ScanTask> tasks;
    for (const QString &fp : files) {
        tasks.append({fp, &ctx});
    }

    for (const ScanTask &task : tasks) {
        ScanTask *heapTask = new ScanTask(task);
        QRunnable *runnable = QRunnable::create([heapTask]() {
            scanFile(*heapTask);
            delete heapTask;
        });
        pool.start(runnable);
    }
    pool.waitForDone();

    if (db) {
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        cleanOrphanedCache(db, ctx.visited, rootPath);
        sqlite3_close(db);
    }

    // Sort if lib provided
    if (lib) {
        std::sort(lib->albums.begin(), lib->albums.end(), albumLessThan);
        for (Album *album : lib->albums) {
            std::sort(album->songs.begin(), album->songs.end(), songLessThan);
        }
    }
}

Album *library_find_album(MusicLibrary *lib, const char *artist, const char *album_name) {
    if (!lib || !album_name) return nullptr;
    QString key = (QString::fromUtf8(artist ? artist : "Unknown Artist")
                  + " - " + QString::fromUtf8(album_name)).toLower();
    return lib->albumMap.value(key, nullptr);
}

QList<Album *> library_get_recent_albums(MusicLibrary *lib, int limit) {
    QList<Album *> recentList;
    sqlite3 *db = dbInit();
    if (!db) return recentList;

    sqlite3_stmt *stmt;
    const char *query = "SELECT album, album_artist, artist, MAX(mtime) as max_mtime "
                        "FROM songs "
                        "GROUP BY album, COALESCE(NULLIF(album_artist, ''), artist) "
                        "ORDER BY max_mtime DESC LIMIT ?;";

    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit * 2);
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && count < limit) {
            const char *albumName = (const char *)sqlite3_column_text(stmt, 0);
            const char *albumArtist = (const char *)sqlite3_column_text(stmt, 1);
            const char *artist = (const char *)sqlite3_column_text(stmt, 2);

            const char *lookupArtist = (albumArtist && strlen(albumArtist) > 0) ? albumArtist : artist;
            if (albumName) {
                Album *album = library_find_album(lib, lookupArtist ? lookupArtist : "", albumName);
                if (album && !recentList.contains(album)) {
                    recentList.append(album);
                    count++;
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return recentList;
}

PlayerConfig *config_load() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + "/musicplayerv2";
    QDir().mkpath(configDir);
    QString configPath = configDir + "/config.ini";

    QSettings settings(configPath, QSettings::IniFormat);

    PlayerConfig *cfg = new PlayerConfig;

    QString libPath = settings.value("Player/LibraryPath").toString();
    if (!libPath.isEmpty()) cfg->library_path = libPath.toStdString();

    QString importPath = settings.value("Player/ImportDestPath").toString();
    if (!importPath.isEmpty()) cfg->import_dest_path = importPath.toStdString();

    cfg->volume = settings.value("Player/Volume", 0.8).toDouble();
    cfg->shuffle = settings.value("Player/Shuffle", false).toBool();
    cfg->repeat_mode = settings.value("Player/Repeat", true).toBool();

    return cfg;
}

void config_save(PlayerConfig *cfg) {
    if (!cfg) return;

    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + "/musicplayerv2";
    QDir().mkpath(configDir);
    QString configPath = configDir + "/config.ini";

    QSettings settings(configPath, QSettings::IniFormat);
    settings.setValue("Player/LibraryPath", QString::fromStdString(cfg->library_path));
    settings.setValue("Player/ImportDestPath", QString::fromStdString(cfg->import_dest_path));
    settings.setValue("Player/Volume", cfg->volume);
    settings.setValue("Player/Shuffle", cfg->shuffle);
    settings.setValue("Player/Repeat", cfg->repeat_mode);
    settings.sync();
}

void config_free(PlayerConfig *cfg) {
    delete cfg;
}

QStringList library_get_artists(MusicLibrary *lib) {
    QStringList artists;
    if (!lib) return artists;
    for (const auto *album : lib->albums) {
        QString artist = QString::fromStdString(album->artist);
        if (!artists.contains(artist)) {
            artists.append(artist);
        }
    }
    artists.sort();
    return artists;
}

QList<Album*> library_get_albums_by_artist(MusicLibrary *lib, const QString &artist) {
    QList<Album*> result;
    if (!lib || artist.isEmpty()) return result;
    for (auto *album : lib->albums) {
        if (artist == QString::fromStdString(album->artist)) {
            result.append(album);
        }
    }
    return result;
}

static QString findCoverInDir(const QString &dir) {
    const char *coverNames[] = {
        "cover.jpg", "cover.png", "folder.jpg", "folder.png",
        "Cover.jpg", "Cover.png", "Folder.jpg", "Folder.png"
    };
    for (int i = 0; i < 8; i++) {
        QString path = dir + "/" + coverNames[i];
        if (QFile::exists(path)) return path;
    }
    return QString();
}

char *resolve_cover_art(const char *song_path) {
    if (!song_path) return nullptr;

    QString sp = QString::fromUtf8(song_path);
    QFileInfo fi(sp);
    QString dir = fi.absolutePath();

    QString coverPath = findCoverInDir(dir);
    if (!coverPath.isEmpty()) return strdup(coverPath.toUtf8().constData());

    return nullptr;
}

char *resolve_lyrics(const char *song_path) {
    if (!song_path) return nullptr;

    // Sidecar .lrc
    QString lrcPath = QString::fromUtf8(song_path);
    int dot = lrcPath.lastIndexOf('.');
    if (dot >= 0) {
        lrcPath = lrcPath.left(dot) + ".lrc";
        if (QFile::exists(lrcPath)) return strdup(lrcPath.toUtf8().constData());
    }

    return nullptr;
}
