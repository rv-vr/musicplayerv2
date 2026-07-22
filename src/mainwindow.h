#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStandardItemModel>
#include <memory>
#include <QTimer>
#include <QProcess>
#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QGridLayout>
#include <QResizeEvent>
#include <QFrame>
#include <QDir>
#include <QHash>
#include <QList>
#include <QThread>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeView>
#include <QScrollArea>
#include <QLineEdit>
#include <QCheckBox>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QListWidget>
#include <atomic>
#include <QFileSystemWatcher>

#include "library.h"
#include "lyrics.h"
#include "importer.h"
#include "bass.h"

int count_audio_files(const QString &dir_path);

class ScanWorker : public QObject {
    Q_OBJECT
public:
    ScanWorker(const QString &path, MusicLibrary *lib, std::atomic<int> *counter, std::atomic<int> *totalCounter)
        : m_path(path), m_lib(lib), m_counter(counter), m_totalCounter(totalCounter) {}

signals:
    void progressUpdated(int scanned, int total);
    void finished(MusicLibrary *tempLib);

public slots:
    void run();

private:
    QString m_path;
    MusicLibrary *m_lib;
    std::atomic<int> *m_counter;
    std::atomic<int> *m_totalCounter;
};

class AlbumCard : public QFrame {
    Q_OBJECT
public:
    AlbumCard(Album *album, QWidget *parent = nullptr);
signals:
    void clicked(Album *album);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    Album *m_album;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onPlayPauseClicked();
    void onPrevClicked();
    void onNextClicked();
    void onShuffleToggled();
    void onRepeatToggled();
    void onMuteClicked();
    void onVolumeChanged(int value);
    void onSeekChanged(int value);
    void onPositionTimer();


    void onQueueActivated(const QModelIndex &index);
    void onClearQueueClicked();

    void onImportSrcBrowse();
    void onImportStart();
    void onImportStop();
    void onImportFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void onSettingsLibBrowse();
    void onSettingsDestBrowse();
    void onSettingsSave();

    void onScanProgress(int scanned, int total);
    void onScanFinished(MusicLibrary *tempLib);

    void onSearchTextChanged(const QString &text);
    void onSearchResultActivated(const QModelIndex &index);
    void onRecentAlbumClicked(Album *album);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUI();
    void applyStyle();
    void startAsyncScan(const QString &path);
    void updateAlbumCover(const QString &song_path);
    void loadSongLyrics(const QString &song_path);
    void updateLyricsDisplay(double position);
    void playSong(Song *song);
    void refreshQueueList();
    void setupQueueForAlbum(Album *album, Song *start_song);

    HSTREAM m_playStream;
    bool m_isPlaying;
    bool m_isMuted;
    double m_savedVolume;

    struct LibDeleter { void operator()(MusicLibrary *p) const { library_free(p); } };
    struct ConfigDeleter { void operator()(PlayerConfig *p) const { config_free(p); } };
    struct LyricsDeleter { void operator()(Lyrics *p) const { lyrics_free(p); } };

    std::unique_ptr<MusicLibrary, LibDeleter> m_library;
    std::unique_ptr<PlayerConfig, ConfigDeleter> m_config;
    QList<Song*> m_queue;
    int m_currentQueueIndex;

    std::unique_ptr<Lyrics, LyricsDeleter> m_currentLyrics;
    int m_activeLyricIndex;
    QList<QLabel*> m_lyricLabels;

    QThread *m_scanThread;
    std::atomic<int> m_scanScannedCount{0};
    std::atomic<int> m_scanTotalCount{0};
    bool m_scanIsRunning;
    QString m_scanPendingPath;

    QThread *m_importThread;

    QWidget *m_centralWidget;

    QLabel *m_albumCoverImg;
    QLabel *m_trackTitleLbl;
    QLabel *m_trackArtistLbl;
    QSlider *m_seekScale;
    QLabel *m_timeLbl;
    QLabel *m_totalTimeLbl;
    QPushButton *m_shuffleBtn;
    QPushButton *m_prevBtn;
    QPushButton *m_playPauseBtn;
    QPushButton *m_nextBtn;
    QPushButton *m_repeatBtn;
    QPushButton *m_muteBtn;
    QSlider *m_volumeScale;

    QTabWidget *m_tabs;

    QTreeView *m_queueTreeView;
    QStandardItemModel *m_queueModel;
    QPushButton *m_clearQueueBtn;

    QScrollArea *m_lyricsScroll;
    QWidget *m_lyricsContainer;
    QWidget *m_topPlayerBar;
    QLabel *m_prevAmbientBackgroundLbl;
    QLabel *m_ambientBackgroundLbl;
    QGraphicsOpacityEffect *m_ambientOpacityEffect;
    QPropertyAnimation *m_ambientFadeAnim;
    void updateAmbientBackground(const QString &coverPath);

    QLineEdit *m_importSrcEdit;
    QPushButton *m_importSrcBtn;
    QLabel *m_importDestLbl;
    QCheckBox *m_importDryRunChk;
    QCheckBox *m_importRemoveChk;
    QCheckBox *m_importSkipChk;
    QCheckBox *m_importReduceChk;
    QPushButton *m_importStartBtn;
    QPushButton *m_importStopBtn;
    QProgressBar *m_importProgress;
    QPlainTextEdit *m_importLogView;

    QLineEdit *m_settingsLibEdit;
    QPushButton *m_settingsLibBtn;
    QLineEdit *m_settingsDestEdit;
    QPushButton *m_settingsDestBtn;
    QPushButton *m_settingsSaveBtn;

    QLabel *m_statusLabel;
    QProgressBar *m_statusProgress;

    void setupHomeTab();
    void refreshRecentAlbums();
    void setupArtistsTab();
    void populateArtistList();
    void populateArtistAlbumGrid(const QString &artist);
    void populateArtistTrackList(Album *album);

    QLineEdit *m_searchEdit;
    QTreeView *m_searchResultsTreeView;
    QStandardItemModel *m_searchModel;
    QWidget *m_recentAlbumsWidget;
    QGridLayout *m_recentAlbumsLayout;

    QListWidget *m_artistList;
    QWidget *m_artistContentPanel;
    QVBoxLayout *m_artistContentLayout;
    QPushButton *m_artistBackBtn;
    QString m_selectedArtist;
    Album *m_selectedAlbum;

    QTimer *m_positionTimer;
    QFileSystemWatcher *m_styleWatcher;
    QList<int> m_lyricLineTargets;
};

#endif
