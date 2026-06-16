#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStandardItemModel>
#include <QTimer>
#include <QProcess>
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

extern "C" {
#include "library.h"
#include "lyrics.h"
#include "bass.h"
}

// Helper struct for audio files counting
int count_audio_files(const QString &dir_path);

// Worker class for async library scanning
class ScanWorker : public QObject {
    Q_OBJECT
public:
    ScanWorker(const QString &path, MusicLibrary *lib, volatile int *counter, volatile int *total_counter) 
        : m_path(path), m_lib(lib), m_counter(counter), m_total_counter(total_counter) {}

signals:
    void progressUpdated(int scanned, int total);
    void finished(MusicLibrary *temp_lib);

public slots:
    void run();

private:
    QString m_path;
    MusicLibrary *m_lib;
    volatile int *m_counter;
    volatile int *m_total_counter;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Playback slots
    void onPlayPauseClicked();
    void onPrevClicked();
    void onNextClicked();
    void onShuffleToggled();
    void onRepeatToggled();
    void onMuteClicked();
    void onVolumeChanged(int value);
    void onSeekChanged(int value);
    void onPositionTimer();
    
    // UI selection slots
    void onAlbumSelected();
    void onTrackActivated(const QModelIndex &index);
    void onQueueActivated(const QModelIndex &index);
    void onClearQueueClicked();
    
    // Import slots
    void onImportSrcBrowse();
    void onImportStart();
    void onImportStop();
    void onImportFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onImportReadyRead();
    
    // Settings slots
    void onSettingsLibBrowse();
    void onSettingsDestBrowse();
    void onSettingsSave();
    
    // Async scan slots
    void onScanProgress(int scanned, int total);
    void onScanFinished(MusicLibrary *temp_lib);

private:
    void setupUI();
    void applyStyle();
    void startAsyncScan(const QString &path);
    void updateAlbumCover(const QString &song_path);
    void loadSongLyrics(const QString &song_path);
    void updateLyricsDisplay(double position);
    void playSong(Song *song);
    void refreshAlbumList();
    void refreshQueueList();
    void setupQueueForAlbum(Album *album, Song *start_song);

    // Audio State
    HSTREAM m_playStream;
    bool m_isPlaying;
    bool m_isMuted;
    double m_savedVolume;
    
    // Library & Config State
    MusicLibrary *m_library;
    PlayerConfig *m_config;
    QList<Song*> m_queue;
    int m_currentQueueIndex;
    
    // Lyrics State
    Lyrics *m_currentLyrics;
    int m_activeLyricIndex;
    QList<QLabel*> m_lyricLabels;
    
    // Async Scan State
    QThread *m_scanThread;
    volatile int m_scanScannedCount;
    volatile int m_scanTotalCount;
    bool m_scanIsRunning;
    QString m_scanPendingPath;
    
    // Import process
    QProcess *m_importProcess;

    // UI Widgets
    QWidget *m_centralWidget;
    
    // Sidebar
    QLabel *m_albumCoverImg;
    QLabel *m_trackTitleLbl;
    QLabel *m_trackArtistLbl;
    QSlider *m_seekScale;
    QLabel *m_timeLbl;
    QPushButton *m_shuffleBtn;
    QPushButton *m_prevBtn;
    QPushButton *m_playPauseBtn;
    QPushButton *m_nextBtn;
    QPushButton *m_repeatBtn;
    QPushButton *m_muteBtn;
    QSlider *m_volumeScale;
    
    // Right tab area
    QTabWidget *m_tabs;
    
    // Library tab
    QTreeView *m_albumTreeView;
    QTreeView *m_trackTreeView;
    QStandardItemModel *m_albumModel;
    QStandardItemModel *m_trackModel;
    
    // Queue tab
    QTreeView *m_queueTreeView;
    QStandardItemModel *m_queueModel;
    QPushButton *m_clearQueueBtn;
    
    // Lyrics tab
    QScrollArea *m_lyricsScroll;
    QWidget *m_lyricsContainer;
    
    // Import tab
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
    
    // Settings tab
    QLineEdit *m_settingsLibEdit;
    QPushButton *m_settingsLibBtn;
    QLineEdit *m_settingsDestEdit;
    QPushButton *m_settingsDestBtn;
    QPushButton *m_settingsSaveBtn;
    
    // Status Bar
    QLabel *m_statusLabel;
    QProgressBar *m_statusProgress;
    
    // Timers
    QTimer *m_positionTimer;
};

#endif // MAINWINDOW_H
