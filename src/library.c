#include "library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sqlite3.h>
#include "bass.h"

typedef struct {
    MusicLibrary *lib;
    sqlite3 *db;
    GHashTable *visited; // Key: filepath, Value: 1
    const char *root_path;
    volatile int *scanned_counter;
    GMutex db_mutex;
    GMutex lib_mutex;
} ScanContext;

typedef struct {
    char *filepath;
    ScanContext *ctx;
} ScanTask;

static void gather_files_recursive(const char *dir_path, GList **files);
static void scan_file_task(gpointer data, gpointer user_data);
static sqlite3 *db_init();
static void clean_orphaned_cache(sqlite3 *db, GHashTable *visited, const char *root_path);

MusicLibrary *library_new() {
    MusicLibrary *lib = g_new0(MusicLibrary, 1);
    lib->albums = NULL;
    lib->album_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    return lib;
}

static void free_song(gpointer data) {
    Song *song = (Song *)data;
    if (song) {
        g_free(song->filepath);
        g_free(song->title);
        g_free(song->artist);
        g_free(song->album);
        g_free(song);
    }
}

static void free_album(gpointer data) {
    Album *album = (Album *)data;
    if (album) {
        g_free(album->name);
        g_free(album->artist);
        g_free(album->cover_path);
        g_list_free_full(album->songs, free_song);
        g_free(album);
    }
}

void library_free(MusicLibrary *lib) {
    if (lib) {
        g_hash_table_destroy(lib->album_map);
        g_list_free_full(lib->albums, free_album);
        g_free(lib);
    }
}

static char *get_db_path() {
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "musicplayerv2", NULL);
    g_mkdir_with_parents(config_dir, 0755);
    gchar *db_path = g_build_filename(config_dir, "library.db", NULL);
    g_free(config_dir);
    return db_path;
}

static sqlite3 *db_init() {
    char *db_path = get_db_path();
    sqlite3 *db = NULL;
    int rc = sqlite3_open(db_path, &db);
    g_free(db_path);
    if (rc != SQLITE_OK) {
        if (db) {
            sqlite3_close(db);
        }
        return NULL;
    }
    
    char *err_msg = NULL;
    const char *sql = "CREATE TABLE IF NOT EXISTS songs ("
                      "filepath TEXT PRIMARY KEY, "
                      "title TEXT, "
                      "artist TEXT, "
                      "album TEXT, "
                      "duration REAL, "
                      "mtime INTEGER, "
                      "track_no INTEGER, "
                      "disc_no INTEGER, "
                      "album_artist TEXT"
                      ");";
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        g_printerr("Failed to create table: %s. Re-creating db file.\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        
        char *path = get_db_path();
        unlink(path);
        g_free(path);
        
        db_path = get_db_path();
        rc = sqlite3_open(db_path, &db);
        g_free(db_path);
        if (rc == SQLITE_OK) {
            sqlite3_exec(db, sql, NULL, NULL, NULL);
        } else {
            if (db) sqlite3_close(db);
            return NULL;
        }
    } else {
        sqlite3_stmt *chk_stmt;
        if (sqlite3_prepare_v2(db, "SELECT track_no, disc_no, album_artist FROM songs LIMIT 1;", -1, &chk_stmt, NULL) != SQLITE_OK) {
            sqlite3_exec(db, "DROP TABLE songs;", NULL, NULL, NULL);
            sqlite3_exec(db, sql, NULL, NULL, NULL);
        } else {
            sqlite3_finalize(chk_stmt);
        }
    }
    return db;
}

static void clean_orphaned_cache(sqlite3 *db, GHashTable *visited, const char *root_path) {
    if (!db) return;
    
    sqlite3_stmt *stmt;
    const char *query = "SELECT filepath FROM songs;";
    GList *to_delete = NULL;
    
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *filepath = (const char *)sqlite3_column_text(stmt, 0);
            if (!filepath) continue;
            
            if (!g_hash_table_contains(visited, filepath)) {
                if (g_str_has_prefix(filepath, root_path) || !g_file_test(filepath, G_FILE_TEST_EXISTS)) {
                    to_delete = g_list_prepend(to_delete, g_strdup(filepath));
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    
    if (to_delete) {
        sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
        const char *del_sql = "DELETE FROM songs WHERE filepath = ?;";
        sqlite3_stmt *del_stmt;
        if (sqlite3_prepare_v2(db, del_sql, -1, &del_stmt, NULL) == SQLITE_OK) {
            for (GList *l = to_delete; l != NULL; l = l->next) {
                char *path = (char *)l->data;
                sqlite3_bind_text(del_stmt, 1, path, -1, SQLITE_TRANSIENT);
                sqlite3_step(del_stmt);
                sqlite3_reset(del_stmt);
                g_free(path);
            }
            sqlite3_finalize(del_stmt);
        }
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
        g_list_free(to_delete);
    }
}

static gint compare_albums(gconstpointer a, gconstpointer b) {
    const Album *album_a = (const Album *)a;
    const Album *album_b = (const Album *)b;
    int name_cmp = g_utf8_collate(album_a->name, album_b->name);
    if (name_cmp != 0) {
        return name_cmp;
    }
    return g_utf8_collate(album_a->artist, album_b->artist);
}

static gint compare_songs(gconstpointer a, gconstpointer b) {
    const Song *song_a = (const Song *)a;
    const Song *song_b = (const Song *)b;
    if (song_a->disc_no != song_b->disc_no) {
        return (song_a->disc_no < song_b->disc_no) ? -1 : 1;
    }
    if (song_a->track_no != song_b->track_no) {
        return (song_a->track_no < song_b->track_no) ? -1 : 1;
    }
    return g_utf8_collate(song_a->title, song_b->title);
}

void library_scan(MusicLibrary *lib, const char *root_path, volatile int *scanned_counter) {
    if (lib->albums) {
        g_hash_table_destroy(lib->album_map);
        g_list_free_full(lib->albums, free_album);
        lib->albums = NULL;
        lib->album_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
    
    if (root_path && g_file_test(root_path, G_FILE_TEST_IS_DIR)) {
        sqlite3 *db = db_init();
        if (db) {
            sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
        }
        
        ScanContext ctx;
        ctx.lib = lib;
        ctx.db = db;
        ctx.visited = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        ctx.root_path = root_path;
        ctx.scanned_counter = scanned_counter;
        g_mutex_init(&ctx.db_mutex);
        g_mutex_init(&ctx.lib_mutex);
        
        // Phase 1: Gather files
        GList *files = NULL;
        gather_files_recursive(root_path, &files);
        
        // Phase 2: Process in thread pool
        // Using 8 worker threads for scanning
        GThreadPool *pool = g_thread_pool_new(scan_file_task, NULL, 8, FALSE, NULL);
        if (pool) {
            for (GList *l = files; l != NULL; l = l->next) {
                char *file_path = (char *)l->data;
                ScanTask *task = g_new0(ScanTask, 1);
                task->filepath = file_path;
                task->ctx = &ctx;
                g_thread_pool_push(pool, task, NULL);
            }
            g_thread_pool_free(pool, FALSE, TRUE); // Wait for completion
        }
        
        g_list_free(files);
        
        if (db) {
            sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
            clean_orphaned_cache(db, ctx.visited, root_path);
            sqlite3_close(db);
        }
        
        g_hash_table_destroy(ctx.visited);
        g_mutex_clear(&ctx.db_mutex);
        g_mutex_clear(&ctx.lib_mutex);
        
        // Sort album list alphabetically
        lib->albums = g_list_sort(lib->albums, compare_albums);
        
        // Sort tracklist for each album by disc & track number
        for (GList *l = lib->albums; l != NULL; l = l->next) {
            Album *album = (Album *)l->data;
            album->songs = g_list_sort(album->songs, compare_songs);
        }
    }
}

Album *library_find_album(MusicLibrary *lib, const char *artist, const char *album_name) {
    char *key = g_strdup_printf("%s - %s", artist ? artist : "Unknown Artist", album_name ? album_name : "Unknown Album");
    Album *album = g_hash_table_lookup(lib->album_map, key);
    g_free(key);
    return album;
}

GList *library_get_recent_albums(MusicLibrary *lib, int limit) {
    GList *recent_list = NULL;
    sqlite3 *db = db_init();
    if (!db) return NULL;
    
    sqlite3_stmt *stmt;
    // Group by album name and canonical album artist/artist, and order by the newest file mtime
    const char *query = "SELECT album, album_artist, artist, MAX(mtime) as max_mtime "
                        "FROM songs "
                        "GROUP BY album, COALESCE(NULLIF(album_artist, ''), artist) "
                        "ORDER BY max_mtime DESC LIMIT ?;";
                        
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        // Query double the limit to buffer against missing/unloaded albums in memory
        sqlite3_bind_int(stmt, 1, limit * 2);
        int count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW && count < limit) {
            const char *album_name = (const char *)sqlite3_column_text(stmt, 0);
            const char *album_artist = (const char *)sqlite3_column_text(stmt, 1);
            const char *artist = (const char *)sqlite3_column_text(stmt, 2);
            
            const char *lookup_artist = (album_artist && strlen(album_artist) > 0) ? album_artist : artist;
            if (album_name) {
                Album *album = library_find_album(lib, lookup_artist ? lookup_artist : "", album_name);
                if (album) {
                    // Prevent memory-level duplicate Album* structures
                    if (!g_list_find(recent_list, album)) {
                        recent_list = g_list_append(recent_list, album);
                        count++;
                    }
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return recent_list;
}

static double query_duration(const char *filepath) {
    HSTREAM stream = BASS_StreamCreateFile(FALSE, filepath, 0, 0, BASS_STREAM_DECODE);
    double duration = 0.0;
    if (stream) {
        QWORD len = BASS_ChannelGetLength(stream, BASS_POS_BYTE);
        duration = BASS_ChannelBytes2Seconds(stream, len);
        BASS_StreamFree(stream);
    }
    return duration;
}

static void gather_files_recursive(const char *dir_path, GList **files) {
    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir) return;

    const gchar *entry_name;
    while ((entry_name = g_dir_read_name(dir))) {
        gchar *full_path = g_build_filename(dir_path, entry_name, NULL);
        if (g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
            gather_files_recursive(full_path, files);
            g_free(full_path);
        } else {
            if (g_str_has_suffix(entry_name, ".m4a") ||
                g_str_has_suffix(entry_name, ".mp3") ||
                g_str_has_suffix(entry_name, ".flac") ||
                g_str_has_suffix(entry_name, ".M4A") ||
                g_str_has_suffix(entry_name, ".MP3") ||
                g_str_has_suffix(entry_name, ".FLAC")) {
                *files = g_list_prepend(*files, full_path);
            } else {
                g_free(full_path);
            }
        }
    }
    g_dir_close(dir);
}

static void scan_file_task(gpointer data, gpointer user_data) {
    ScanTask *task = (ScanTask *)data;
    ScanContext *ctx = task->ctx;
    char *full_path = task->filepath;
    
    char *entry_name = g_path_get_basename(full_path);
    
    struct stat st;
    gint64 disk_mtime = 0;
    if (stat(full_path, &st) == 0) {
        disk_mtime = (gint64)st.st_mtime;
    }
    
    char *artist_name = NULL;
    char *album_name = NULL;
    char *album_artist = NULL;
    char *title = NULL;
    double duration = 0.0;
    int track_no = 0;
    int disc_no = 1;
    gboolean cached_found = FALSE;
    
    if (ctx->db) {
        g_mutex_lock(&ctx->db_mutex);
        sqlite3_stmt *stmt;
        const char *query = "SELECT title, artist, album, duration, mtime, track_no, disc_no, album_artist FROM songs WHERE filepath = ?;";
        if (sqlite3_prepare_v2(ctx->db, query, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, full_path, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                gint64 cached_mtime = sqlite3_column_int64(stmt, 4);
                double cached_dur = sqlite3_column_double(stmt, 3);
                if (cached_mtime == disk_mtime && cached_dur > 0.0) {
                    title = g_strdup((const char *)sqlite3_column_text(stmt, 0));
                    artist_name = g_strdup((const char *)sqlite3_column_text(stmt, 1));
                    album_name = g_strdup((const char *)sqlite3_column_text(stmt, 2));
                    duration = cached_dur;
                    track_no = sqlite3_column_int(stmt, 5);
                    disc_no = sqlite3_column_int(stmt, 6);
                    const char *cached_album_artist = (const char *)sqlite3_column_text(stmt, 7);
                    if (cached_album_artist) {
                        album_artist = g_strdup(cached_album_artist);
                    }
                    cached_found = TRUE;
                }
            }
            sqlite3_finalize(stmt);
        }
        g_mutex_unlock(&ctx->db_mutex);
    }
    
    gboolean info_extracted = FALSE;
    if (!cached_found) {
        char *quoted_path = g_shell_quote(full_path);
        char *cmd = g_strdup_printf("python3 /home/XOR/Repositories/musicplayerv2/extract_metadata.py %s --info", quoted_path);
        g_free(quoted_path);
        
        gchar *stdout_buf = NULL;
        gint exit_status = 0;
        
        if (g_spawn_command_line_sync(cmd, &stdout_buf, NULL, &exit_status, NULL) && exit_status == 0 && stdout_buf) {
            gchar **lines = g_strsplit(stdout_buf, "\n", -1);
            for (int i = 0; lines[i] != NULL; i++) {
                char *line = lines[i];
                if (g_str_has_prefix(line, "title: ")) {
                    title = g_strdup(line + 7);
                } else if (g_str_has_prefix(line, "artist: ")) {
                    artist_name = g_strdup(line + 8);
                } else if (g_str_has_prefix(line, "album: ")) {
                    album_name = g_strdup(line + 7);
                } else if (g_str_has_prefix(line, "album_artist: ")) {
                    album_artist = g_strdup(line + 14);
                } else if (g_str_has_prefix(line, "track: ")) {
                    track_no = atoi(line + 7);
                } else if (g_str_has_prefix(line, "disc: ")) {
                    disc_no = atoi(line + 6);
                } else if (g_str_has_prefix(line, "duration: ")) {
                    duration = g_ascii_strtod(line + 10, NULL);
                }
            }
            g_strfreev(lines);
            if (title && duration > 0.0) {
                info_extracted = TRUE;
            }
        }
        g_free(stdout_buf);
        g_free(cmd);
        
        if (!info_extracted) {
            if (!title || strlen(title) == 0) {
                g_free(title);
                title = g_strdup(entry_name);
                char *dot = strrchr(title, '.');
                if (dot) *dot = '\0';
            }
            
            if ((!artist_name || strlen(artist_name) == 0 || !album_name || strlen(album_name) == 0) && g_str_has_prefix(full_path, ctx->root_path)) {
                const char *rel = full_path + strlen(ctx->root_path);
                while (*rel == '/') rel++;
                gchar **parts = g_strsplit(rel, "/", -1);
                guint len = g_strv_length(parts);
                if (!artist_name || strlen(artist_name) == 0) {
                    g_free(artist_name);
                    if (len >= 3) artist_name = g_strdup(parts[len - 3]);
                    else artist_name = g_strdup("Unknown Artist");
                }
                if (!album_name || strlen(album_name) == 0) {
                    g_free(album_name);
                    if (len >= 2) album_name = g_strdup(parts[len - 2]);
                    else album_name = g_strdup("Unknown Album");
                }
                g_strfreev(parts);
            }
        }
        
        if (track_no <= 0) {
            const char *p = entry_name;
            while (*p && g_ascii_isspace(*p)) p++;
            if (g_ascii_isdigit(*p)) {
                int first_num = 0;
                while (*p && g_ascii_isdigit(*p)) {
                    first_num = first_num * 10 + (*p - '0');
                    p++;
                }
                while (*p && (g_ascii_isspace(*p) || *p == '-' || *p == '.' || *p == '_')) p++;
                if (*p == '[') {
                    const char *next = p + 1;
                    if (g_ascii_isdigit(*next)) {
                        int second_num = 0;
                        while (*next && g_ascii_isdigit(*next)) {
                            second_num = second_num * 10 + (*next - '0');
                            next++;
                        }
                        disc_no = first_num;
                        track_no = second_num;
                    } else {
                        track_no = first_num;
                    }
                } else {
                    track_no = first_num;
                }
            }
        }
        if (disc_no <= 0) {
            disc_no = 1;
        }
    }
    
    g_mutex_lock(&ctx->lib_mutex);
    
    if (ctx->db && !cached_found) {
        g_mutex_lock(&ctx->db_mutex);
        sqlite3_stmt *stmt;
        const char *insert_sql = "INSERT OR REPLACE INTO songs (filepath, title, artist, album, duration, mtime, track_no, disc_no, album_artist) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
        if (sqlite3_prepare_v2(ctx->db, insert_sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, full_path, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, title, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, artist_name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, album_name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(stmt, 5, duration);
            sqlite3_bind_int64(stmt, 6, disk_mtime);
            sqlite3_bind_int(stmt, 7, track_no);
            sqlite3_bind_int(stmt, 8, disc_no);
            sqlite3_bind_text(stmt, 9, album_artist ? album_artist : "", -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        g_mutex_unlock(&ctx->db_mutex);
    }
    
    g_hash_table_insert(ctx->visited, g_strdup(full_path), GINT_TO_POINTER(1));
    
    Song *song = g_new0(Song, 1);
    song->filepath = g_strdup(full_path);
    song->title = title;
    song->artist = artist_name;
    song->album = album_name;
    song->duration = duration;
    song->track_no = track_no;
    song->disc_no = disc_no;
    
    char *key = g_strdup_printf("%s - %s", (album_artist && strlen(album_artist) > 0) ? album_artist : artist_name, album_name);
    Album *album = g_hash_table_lookup(ctx->lib->album_map, key);
    if (!album) {
        album = g_new0(Album, 1);
        album->name = g_strdup(album_name);
        album->artist = g_strdup((album_artist && strlen(album_artist) > 0) ? album_artist : artist_name);
        album->songs = NULL;
        album->cover_path = NULL;
        
        char *parent_dir = g_path_get_dirname(full_path);
        const char *cover_names[] = {
            "cover.jpg", "cover.png", "folder.jpg", "folder.png",
            "Cover.jpg", "Cover.png", "Folder.jpg", "Folder.png"
        };
        for (int i = 0; i < 8; i++) {
            char *cov_test = g_build_filename(parent_dir, cover_names[i], NULL);
            if (g_file_test(cov_test, G_FILE_TEST_EXISTS)) {
                album->cover_path = cov_test;
                break;
            }
            g_free(cov_test);
        }
        g_free(parent_dir);
        
        g_hash_table_insert(ctx->lib->album_map, g_strdup(key), album);
        ctx->lib->albums = g_list_append(ctx->lib->albums, album);
    }
    
    album->songs = g_list_append(album->songs, song);
    if (ctx->scanned_counter) {
        g_atomic_int_inc(ctx->scanned_counter);
    }
    g_mutex_unlock(&ctx->lib_mutex);
    
    if (album_artist) g_free(album_artist);
    g_free(key);
    g_free(entry_name);
    g_free(full_path);
    g_free(task);
}

PlayerConfig *config_load() {
    GKeyFile *key_file = g_key_file_new();
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "musicplayerv2", NULL);
    gchar *config_path = g_build_filename(config_dir, "config.ini", NULL);
    g_mkdir_with_parents(config_dir, 0755);
    
    PlayerConfig *cfg = g_new0(PlayerConfig, 1);
    cfg->library_path = NULL;
    cfg->import_dest_path = NULL;
    cfg->volume = 0.8;
    cfg->shuffle = FALSE;
    cfg->repeat_mode = TRUE;
    
    if (g_key_file_load_from_file(key_file, config_path, G_KEY_FILE_NONE, NULL)) {
        cfg->library_path = g_key_file_get_string(key_file, "Player", "LibraryPath", NULL);
        cfg->import_dest_path = g_key_file_get_string(key_file, "Player", "ImportDestPath", NULL);
        
        GError *err = NULL;
        double vol = g_key_file_get_double(key_file, "Player", "Volume", &err);
        if (!err) {
            cfg->volume = vol;
        } else {
            g_clear_error(&err);
        }
        
        gboolean shuf = g_key_file_get_boolean(key_file, "Player", "Shuffle", &err);
        if (!err) {
            cfg->shuffle = shuf;
        } else {
            g_clear_error(&err);
        }
        
        gboolean rep = g_key_file_get_boolean(key_file, "Player", "Repeat", &err);
        if (!err) {
            cfg->repeat_mode = rep;
        } else {
            g_clear_error(&err);
        }
    }
    
    g_key_file_free(key_file);
    g_free(config_dir);
    g_free(config_path);
    return cfg;
}

void config_save(PlayerConfig *cfg) {
    if (!cfg) return;
    
    GKeyFile *key_file = g_key_file_new();
    if (cfg->library_path) {
        g_key_file_set_string(key_file, "Player", "LibraryPath", cfg->library_path);
    } else {
        g_key_file_set_string(key_file, "Player", "LibraryPath", "");
    }
    if (cfg->import_dest_path) {
        g_key_file_set_string(key_file, "Player", "ImportDestPath", cfg->import_dest_path);
    } else {
        g_key_file_set_string(key_file, "Player", "ImportDestPath", "");
    }
    g_key_file_set_double(key_file, "Player", "Volume", cfg->volume);
    g_key_file_set_boolean(key_file, "Player", "Shuffle", cfg->shuffle);
    g_key_file_set_boolean(key_file, "Player", "Repeat", cfg->repeat_mode);
    
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "musicplayerv2", NULL);
    gchar *config_path = g_build_filename(config_dir, "config.ini", NULL);
    g_mkdir_with_parents(config_dir, 0755);
    
    GError *err = NULL;
    g_key_file_save_to_file(key_file, config_path, &err);
    if (err) {
        g_printerr("Failed to save config: %s\n", err->message);
        g_error_free(err);
    }
    
    g_key_file_free(key_file);
    g_free(config_dir);
    g_free(config_path);
}

void config_free(PlayerConfig *cfg) {
    if (cfg) {
        g_free(cfg->library_path);
        g_free(cfg->import_dest_path);
        g_free(cfg);
    }
}

char *resolve_cover_art(const char *song_path) {
    if (!song_path) return NULL;
    
    // Check if cover art exists locally in same directory
    char *dir = g_path_get_dirname(song_path);
    const char *cover_names[] = {
        "cover.jpg", "cover.png", "folder.jpg", "folder.png",
        "Cover.jpg", "Cover.png", "Folder.jpg", "Folder.png"
    };
    for (int i = 0; i < 8; i++) {
        char *cov_test = g_build_filename(dir, cover_names[i], NULL);
        if (g_file_test(cov_test, G_FILE_TEST_EXISTS)) {
            g_free(dir);
            return cov_test;
        }
        g_free(cov_test);
    }
    g_free(dir);
    
    // Call python script to extract embedded artwork to /tmp/musicplayerv2_cover.png
    char *out_path = g_strdup("/tmp/musicplayerv2_cover.png");
    // Remove old tmp cover first
    unlink(out_path);
    
    char *cmd = g_strdup_printf("python3 /home/XOR/Repositories/musicplayerv2/extract_metadata.py \"%s\" --cover \"%s\" >/dev/null 2>&1", song_path, out_path);
    int status = system(cmd);
    g_free(cmd);
    
    if (status == 0 && g_file_test(out_path, G_FILE_TEST_EXISTS)) {
        return out_path;
    }
    
    g_free(out_path);
    return NULL;
}

char *resolve_lyrics(const char *song_path) {
    if (!song_path) return NULL;
    
    // Check for sidecar .lrc file (same name as song, but .lrc extension)
    char *lrc_path = g_strdup(song_path);
    char *dot = strrchr(lrc_path, '.');
    if (dot) {
        strcpy(dot, ".lrc");
        if (g_file_test(lrc_path, G_FILE_TEST_EXISTS)) {
            return lrc_path;
        }
    }
    g_free(lrc_path);
    
    // Attempt to extract embedded lyrics to /tmp/musicplayerv2_lyrics.lrc
    char *out_path = g_strdup("/tmp/musicplayerv2_lyrics.lrc");
    unlink(out_path);
    
    char *cmd = g_strdup_printf("python3 /home/XOR/Repositories/musicplayerv2/extract_metadata.py \"%s\" --lyrics \"%s\" >/dev/null 2>&1", song_path, out_path);
    int status = system(cmd);
    g_free(cmd);
    
    if (status == 0 && g_file_test(out_path, G_FILE_TEST_EXISTS)) {
        return out_path;
    }
    
    g_free(out_path);
    return NULL;
}
