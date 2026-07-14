#include "mainwindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QStatusBar>
#include <QCoreApplication>
#include <QFile>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QStandardItem>
#include <QScrollBar>
#include <QFileInfo>
#include <QDebug>

// Helper recursive file counter
static void count_recursive_qt(const QString &dir_path, int *count) {
    QDir dir(dir_path);
    if (!dir.exists()) return;
    
    QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            count_recursive_qt(entry.absoluteFilePath(), count);
        } else {
            QString ext = entry.suffix().toLower();
            if (ext == "mp3" || ext == "m4a" || ext == "flac") {
                (*count)++;
            }
        }
    }
}

int count_audio_files(const QString &dir_path) {
    int count = 0;
    count_recursive_qt(dir_path, &count);
    return count;
}

// AlbumCard Implementation
AlbumCard::AlbumCard(Album *album, QWidget *parent)
    : QFrame(parent), m_album(album)
{
    setFixedSize(140, 200);
    setFrameShape(QFrame::StyledPanel);
    setObjectName("albumCard");
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    
    QLabel *coverLbl = new QLabel(this);
    coverLbl->setFixedSize(124, 124);
    coverLbl->setAlignment(Qt::AlignCenter);
    
    if (!album->songs.isEmpty()) {
        Song *first = album->songs.first();
        char *cov_path = resolve_cover_art(first->filepath);
        if (cov_path && QFile::exists(cov_path)) {
            QPixmap pm(QString::fromUtf8(cov_path));
            coverLbl->setPixmap(pm.scaled(124, 124, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        } else {
            QPixmap fallback(124, 124);
            fallback.fill(QColor("#202024"));
            coverLbl->setPixmap(fallback);
        }
        free(cov_path);
    } else {
        QPixmap fallback(124, 124);
        fallback.fill(QColor("#202024"));
        coverLbl->setPixmap(fallback);
    }
    layout->addWidget(coverLbl);
    
    QLabel *titleLbl = new QLabel(QString::fromUtf8(album->name), this);
    titleLbl->setStyleSheet("font-size: 12px; font-weight: 700; color: #ffffff;");
    titleLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFontMetrics fm(titleLbl->font());
    QString elidedTitle = fm.elidedText(titleLbl->text(), Qt::ElideRight, 120);
    titleLbl->setText(elidedTitle);
    layout->addWidget(titleLbl);
    
    QLabel *artistLbl = new QLabel(QString::fromUtf8(album->artist), this);
    artistLbl->setStyleSheet("font-size: 11px; color: #a8a8b3;");
    artistLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QString elidedArtist = fm.elidedText(artistLbl->text(), Qt::ElideRight, 120);
    artistLbl->setText(elidedArtist);
    layout->addWidget(artistLbl);
    
    layout->addStretch();
}

void AlbumCard::mousePressEvent(QMouseEvent *event) {
    emit clicked(m_album);
    QFrame::mousePressEvent(event);
}

// Worker thread run implementation
void ScanWorker::run() {
    m_totalCounter->storeRelaxed(count_audio_files(m_path));
    library_scan(m_lib, m_path, m_counter);
    emit finished(m_lib);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_playStream(0),
      m_isPlaying(false),
      m_isMuted(false),
      m_savedVolume(0.8),
      m_library(nullptr),
      m_config(nullptr),
      m_currentQueueIndex(-1),
      m_currentLyrics(nullptr),
      m_activeLyricIndex(-1),
      m_scanThread(nullptr),
      m_scanIsRunning(false),
      m_importProcess(nullptr),
      m_searchEdit(nullptr),
      m_searchResultsTreeView(nullptr),
      m_searchModel(nullptr),
      m_recentAlbumsWidget(nullptr),
      m_recentAlbumsLayout(nullptr)
{
    // Initialize BASS
    if (!BASS_Init(-1, 44100, 0, NULL, NULL)) {
        qWarning() << "BASS_Init error:" << BASS_ErrorGetCode();
    }
    
    // Load AAC Plugin
    QString appDir = QCoreApplication::applicationDirPath();
    QString pluginPath = appDir + "/lib/libbass_aac.so";
    if (!QFile::exists(pluginPath)) {
        pluginPath = appDir + "/../lib/libbass_aac.so";
    }
    if (BASS_PluginLoad(pluginPath.toUtf8().constData(), 0) == 0) {
        qWarning() << "Warning: BASS plugin not loaded at" << pluginPath;
    }
    
    // Initialize Backend State
    m_library = library_new();
    m_config = config_load();
    
    // Setup UI
    setupUI();
    
    // Apply Stylesheet
    applyStyle();
    
    // Position Update Timer
    m_positionTimer = new QTimer(this);
    connect(m_positionTimer, &QTimer::timeout, this, &MainWindow::onPositionTimer);
    m_positionTimer->start(100);
    
    // Initial Scan
    if (m_config->library_path && strlen(m_config->library_path) > 0) {
        startAsyncScan(QString::fromUtf8(m_config->library_path));
    }
}

MainWindow::~MainWindow() {
    if (m_playStream) {
        BASS_StreamFree(m_playStream);
    }
    BASS_Free();
    
    if (m_library) {
        library_free(m_library);
    }
    if (m_config) {
        config_free(m_config);
    }
    if (m_currentLyrics) {
        lyrics_free(m_currentLyrics);
    }
    
    if (m_importProcess && m_importProcess->state() != QProcess::NotRunning) {
        m_importProcess->kill();
        m_importProcess->waitForFinished();
    }
}

void MainWindow::setupUI() {
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    QHBoxLayout *mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // -------------------------------------------------------------
    // SIDEBAR CARD
    // -------------------------------------------------------------
    QWidget *sidebarCard = new QWidget(m_centralWidget);
    sidebarCard->setObjectName("sidebarCard");
    sidebarCard->setFixedWidth(300);
    
    QVBoxLayout *sidebarLayout = new QVBoxLayout(sidebarCard);
    sidebarLayout->setContentsMargins(16, 16, 16, 16);
    sidebarLayout->setSpacing(10);
    
    // Cover image container
    QWidget *coverContainer = new QWidget(sidebarCard);
    coverContainer->setObjectName("coverContainer");
    QVBoxLayout *coverLayout = new QVBoxLayout(coverContainer);
    coverLayout->setContentsMargins(10, 10, 10, 10);
    m_albumCoverImg = new QLabel(coverContainer);
    m_albumCoverImg->setAlignment(Qt::AlignCenter);
    coverLayout->addWidget(m_albumCoverImg);
    sidebarLayout->addWidget(coverContainer);
    
    updateAlbumCover(""); // Load default
    
    // Track labels
    m_trackTitleLbl = new QLabel("Not Playing", sidebarCard);
    m_trackTitleLbl->setObjectName("trackTitleLbl");
    m_trackTitleLbl->setAlignment(Qt::AlignCenter);
    m_trackTitleLbl->setWordWrap(true);
    m_trackTitleLbl->setStyleSheet("font-size: 16px; font-weight: 700; color: #ffffff; margin-top: 4px;");
    sidebarLayout->addWidget(m_trackTitleLbl);
    
    m_trackArtistLbl = new QLabel("", sidebarCard);
    m_trackArtistLbl->setAlignment(Qt::AlignCenter);
    m_trackArtistLbl->setWordWrap(true);
    m_trackArtistLbl->setStyleSheet("font-size: 13px; color: #a8a8b3; font-weight: 500;");
    sidebarLayout->addWidget(m_trackArtistLbl);
    
    // Seek scale
    m_seekScale = new QSlider(Qt::Horizontal, sidebarCard);
    m_seekScale->setRange(0, 1000);
    connect(m_seekScale, &QSlider::sliderMoved, this, &MainWindow::onSeekChanged);
    sidebarLayout->addWidget(m_seekScale);
    
    m_timeLbl = new QLabel("00:00 / 00:00", sidebarCard);
    m_timeLbl->setAlignment(Qt::AlignCenter);
    m_timeLbl->setStyleSheet("font-size: 11px; color: #7c7c8a; font-family: monospace;");
    sidebarLayout->addWidget(m_timeLbl);
    
    // Playback buttons layout
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(12);
    controlsLayout->setAlignment(Qt::AlignCenter);
    
    m_shuffleBtn = new QPushButton(sidebarCard);
    m_shuffleBtn->setProperty("class", "flatBtn");
    m_shuffleBtn->setCheckable(true);
    m_shuffleBtn->setIcon(QIcon::fromTheme("media-playlist-shuffle"));
    m_shuffleBtn->setChecked(m_config->shuffle);
    connect(m_shuffleBtn, &QPushButton::clicked, this, &MainWindow::onShuffleToggled);
    controlsLayout->addWidget(m_shuffleBtn);
    
    m_prevBtn = new QPushButton(sidebarCard);
    m_prevBtn->setProperty("class", "flatBtn");
    m_prevBtn->setIcon(QIcon::fromTheme("media-skip-backward"));
    connect(m_prevBtn, &QPushButton::clicked, this, &MainWindow::onPrevClicked);
    controlsLayout->addWidget(m_prevBtn);
    
    m_playPauseBtn = new QPushButton(sidebarCard);
    m_playPauseBtn->setObjectName("playPauseBtn");
    m_playPauseBtn->setIcon(QIcon::fromTheme("media-playback-start"));
    connect(m_playPauseBtn, &QPushButton::clicked, this, &MainWindow::onPlayPauseClicked);
    controlsLayout->addWidget(m_playPauseBtn);
    
    m_nextBtn = new QPushButton(sidebarCard);
    m_nextBtn->setProperty("class", "flatBtn");
    m_nextBtn->setIcon(QIcon::fromTheme("media-skip-forward"));
    connect(m_nextBtn, &QPushButton::clicked, this, &MainWindow::onNextClicked);
    controlsLayout->addWidget(m_nextBtn);
    
    m_repeatBtn = new QPushButton(sidebarCard);
    m_repeatBtn->setProperty("class", "flatBtn");
    m_repeatBtn->setCheckable(true);
    m_repeatBtn->setIcon(QIcon::fromTheme("media-playlist-repeat"));
    m_repeatBtn->setChecked(m_config->repeat_mode);
    connect(m_repeatBtn, &QPushButton::clicked, this, &MainWindow::onRepeatToggled);
    controlsLayout->addWidget(m_repeatBtn);
    
    sidebarLayout->addLayout(controlsLayout);
    
    // Volume layout
    QHBoxLayout *volumeLayout = new QHBoxLayout();
    volumeLayout->setSpacing(8);
    
    m_muteBtn = new QPushButton(sidebarCard);
    m_muteBtn->setProperty("class", "flatBtn");
    m_muteBtn->setIcon(QIcon::fromTheme("audio-volume-high"));
    connect(m_muteBtn, &QPushButton::clicked, this, &MainWindow::onMuteClicked);
    volumeLayout->addWidget(m_muteBtn);
    
    m_volumeScale = new QSlider(Qt::Horizontal, sidebarCard);
    m_volumeScale->setRange(0, 100);
    m_volumeScale->setValue(m_config->volume * 100);
    connect(m_volumeScale, &QSlider::valueChanged, this, &MainWindow::onVolumeChanged);
    volumeLayout->addWidget(m_volumeScale);
    
    sidebarLayout->addLayout(volumeLayout);
    sidebarLayout->addStretch();
    
    mainLayout->addWidget(sidebarCard);
    
    // -------------------------------------------------------------
    // MAIN CONTENT CARD
    // -------------------------------------------------------------
    QWidget *mainCard = new QWidget(m_centralWidget);
    mainCard->setObjectName("mainCard");
    QVBoxLayout *mainCardLayout = new QVBoxLayout(mainCard);
    mainCardLayout->setContentsMargins(16, 16, 16, 16);
    
    m_tabs = new QTabWidget(mainCard);
    setupHomeTab();
    mainCardLayout->addWidget(m_tabs);
    mainLayout->addWidget(mainCard);
    
    // TAB 1: LIBRARY
    QSplitter *libSplitter = new QSplitter(Qt::Horizontal, m_tabs);
    m_albumTreeView = new QTreeView(libSplitter);
    m_albumTreeView->setHeaderHidden(false);
    m_albumTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_albumTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_albumTreeView->setAllColumnsShowFocus(true);
    m_albumModel = new QStandardItemModel(0, 2, m_albumTreeView);
    m_albumModel->setHeaderData(0, Qt::Horizontal, "Album");
    m_albumModel->setHeaderData(1, Qt::Horizontal, "Artist");
    m_albumTreeView->setModel(m_albumModel);
    m_albumTreeView->header()->setSectionResizeMode(QHeaderView::Stretch);
    connect(m_albumTreeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onAlbumSelected);
    
    m_trackTreeView = new QTreeView(libSplitter);
    m_trackTreeView->setHeaderHidden(false);
    m_trackTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_trackTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trackTreeView->setAllColumnsShowFocus(true);
    m_trackModel = new QStandardItemModel(0, 4, m_trackTreeView);
    m_trackModel->setHeaderData(0, Qt::Horizontal, "#");
    m_trackModel->setHeaderData(1, Qt::Horizontal, "Title");
    m_trackModel->setHeaderData(2, Qt::Horizontal, "Duration");
    m_trackModel->setHeaderData(3, Qt::Horizontal, "SongPtr"); // Hidden column
    m_trackTreeView->setModel(m_trackModel);
    m_trackTreeView->setColumnHidden(3, true);
    m_trackTreeView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_trackTreeView->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_trackTreeView->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(m_trackTreeView, &QTreeView::doubleClicked, this, &MainWindow::onTrackActivated);
    
    libSplitter->addWidget(m_albumTreeView);
    libSplitter->addWidget(m_trackTreeView);
    libSplitter->setSizes(QList<int>() << 300 << 500);
    m_tabs->addTab(libSplitter, "Library");
    
    // TAB 2: QUEUE
    QWidget *queueTab = new QWidget(m_tabs);
    QVBoxLayout *queueLayout = new QVBoxLayout(queueTab);
    m_queueTreeView = new QTreeView(queueTab);
    m_queueTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_queueTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_queueTreeView->setAllColumnsShowFocus(true);
    m_queueModel = new QStandardItemModel(0, 5, m_queueTreeView);
    m_queueModel->setHeaderData(0, Qt::Horizontal, "#");
    m_queueModel->setHeaderData(1, Qt::Horizontal, "Title");
    m_queueModel->setHeaderData(2, Qt::Horizontal, "Artist");
    m_queueModel->setHeaderData(3, Qt::Horizontal, "Duration");
    m_queueModel->setHeaderData(4, Qt::Horizontal, "SongPtr");
    m_queueTreeView->setModel(m_queueModel);
    m_queueTreeView->setColumnHidden(4, true);
    m_queueTreeView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_queueTreeView->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_queueTreeView->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_queueTreeView->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(m_queueTreeView, &QTreeView::doubleClicked, this, &MainWindow::onQueueActivated);
    queueLayout->addWidget(m_queueTreeView);
    
    QHBoxLayout *queueBtnLayout = new QHBoxLayout();
    m_clearQueueBtn = new QPushButton("Clear Queue", queueTab);
    connect(m_clearQueueBtn, &QPushButton::clicked, this, &MainWindow::onClearQueueClicked);
    queueBtnLayout->addWidget(m_clearQueueBtn);
    queueBtnLayout->addStretch();
    queueLayout->addLayout(queueBtnLayout);
    m_tabs->addTab(queueTab, "Play Queue");
    
    // TAB 3: LYRICS
    m_lyricsScroll = new QScrollArea(m_tabs);
    m_lyricsScroll->setWidgetResizable(true);
    m_lyricsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_lyricsContainer = new QWidget(m_lyricsScroll);
    m_lyricsContainer->setStyleSheet("background-color: transparent;");
    QVBoxLayout *lyricsLayout = new QVBoxLayout(m_lyricsContainer);
    lyricsLayout->setSpacing(4);
    lyricsLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_lyricsScroll->setWidget(m_lyricsContainer);
    m_tabs->addTab(m_lyricsScroll, "Lyrics");
    
    // TAB 4: IMPORT & CLEAN
    QWidget *importTab = new QWidget(m_tabs);
    QVBoxLayout *importLayout = new QVBoxLayout(importTab);
    importLayout->setContentsMargins(15, 15, 15, 15);
    
    QGridLayout *importGrid = new QGridLayout();
    importGrid->setVerticalSpacing(12);
    importGrid->setHorizontalSpacing(10);
    
    importGrid->addWidget(new QLabel("Source folder:", importTab), 0, 0, Qt::AlignRight);
    m_importSrcEdit = new QLineEdit(importTab);
    importGrid->addWidget(m_importSrcEdit, 0, 1);
    m_importSrcBtn = new QPushButton("Browse", importTab);
    connect(m_importSrcBtn, &QPushButton::clicked, this, &MainWindow::onImportSrcBrowse);
    importGrid->addWidget(m_importSrcBtn, 0, 2);
    
    importGrid->addWidget(new QLabel("Import Destination:", importTab), 1, 0, Qt::AlignRight);
    m_importDestLbl = new QLabel(m_config->import_dest_path ? m_config->import_dest_path : "Not configured", importTab);
    importGrid->addWidget(m_importDestLbl, 1, 1, 1, 2);
    importLayout->addLayout(importGrid);
    
    QHBoxLayout *importOpts = new QHBoxLayout();
    m_importDryRunChk = new QCheckBox("Dry Run", importTab);
    m_importRemoveChk = new QCheckBox("Delete FLAC after conversion", importTab);
    m_importSkipChk = new QCheckBox("Skip if lyrics embedded", importTab);
    m_importSkipChk->setChecked(true);
    m_importReduceChk = new QCheckBox("Delete LRC files", importTab);
    importOpts->addWidget(m_importDryRunChk);
    importOpts->addWidget(m_importRemoveChk);
    importOpts->addWidget(m_importSkipChk);
    importOpts->addWidget(m_importReduceChk);
    importLayout->addLayout(importOpts);
    
    QHBoxLayout *importActions = new QHBoxLayout();
    m_importStartBtn = new QPushButton("Start Import & Clean", importTab);
    m_importStartBtn->setObjectName("importStartBtn");
    connect(m_importStartBtn, &QPushButton::clicked, this, &MainWindow::onImportStart);
    importActions->addWidget(m_importStartBtn);
    m_importStopBtn = new QPushButton("Stop", importTab);
    m_importStopBtn->setEnabled(false);
    connect(m_importStopBtn, &QPushButton::clicked, this, &MainWindow::onImportStop);
    importActions->addWidget(m_importStopBtn);
    importActions->addStretch();
    importLayout->addLayout(importActions);
    
    m_importProgress = new QProgressBar(importTab);
    importLayout->addWidget(m_importProgress);
    
    m_importLogView = new QPlainTextEdit(importTab);
    m_importLogView->setReadOnly(true);
    m_importLogView->setStyleSheet("font-family: monospace; font-size: 12px;");
    importLayout->addWidget(m_importLogView);
    m_tabs->addTab(importTab, "Import & Clean");
    
    // TAB 5: SETTINGS
    QWidget *settingsTab = new QWidget(m_tabs);
    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsTab);
    settingsLayout->setContentsMargins(20, 20, 20, 20);
    
    QGridLayout *settingsGrid = new QGridLayout();
    settingsGrid->setVerticalSpacing(15);
    settingsGrid->setHorizontalSpacing(12);
    
    settingsGrid->addWidget(new QLabel("Library Scan Folder:", settingsTab), 0, 0, Qt::AlignRight);
    m_settingsLibEdit = new QLineEdit(settingsTab);
    m_settingsLibEdit->setText(m_config->library_path ? m_config->library_path : "");
    settingsGrid->addWidget(m_settingsLibEdit, 0, 1);
    m_settingsLibBtn = new QPushButton("Browse", settingsTab);
    connect(m_settingsLibBtn, &QPushButton::clicked, this, &MainWindow::onSettingsLibBrowse);
    settingsGrid->addWidget(m_settingsLibBtn, 0, 2);
    
    settingsGrid->addWidget(new QLabel("Import Destination Folder:", settingsTab), 1, 0, Qt::AlignRight);
    m_settingsDestEdit = new QLineEdit(settingsTab);
    m_settingsDestEdit->setText(m_config->import_dest_path ? m_config->import_dest_path : "");
    settingsGrid->addWidget(m_settingsDestEdit, 1, 1);
    m_settingsDestBtn = new QPushButton("Browse", settingsTab);
    connect(m_settingsDestBtn, &QPushButton::clicked, this, &MainWindow::onSettingsDestBrowse);
    settingsGrid->addWidget(m_settingsDestBtn, 1, 2);
    
    m_settingsSaveBtn = new QPushButton("Save & Apply Settings", settingsTab);
    m_settingsSaveBtn->setObjectName("settingsSaveBtn");
    connect(m_settingsSaveBtn, &QPushButton::clicked, this, &MainWindow::onSettingsSave);
    settingsGrid->addWidget(m_settingsSaveBtn, 2, 1, Qt::AlignLeft);
    
    settingsLayout->addLayout(settingsGrid);
    settingsLayout->addStretch();
    m_tabs->addTab(settingsTab, "Settings");
    
    // -------------------------------------------------------------
    // STATUS BAR
    // -------------------------------------------------------------
    m_statusLabel = new QLabel("Ready.", this);
    m_statusLabel->setObjectName("statusLabel");
    statusBar()->addWidget(m_statusLabel, 1);
    
    m_statusProgress = new QProgressBar(this);
    m_statusProgress->setObjectName("statusProgress");
    m_statusProgress->setFixedWidth(200);
    statusBar()->addPermanentWidget(m_statusProgress);
    m_statusProgress->hide();
    
    resize(1000, 680);
    setWindowTitle("Music Player v2 (Qt 6)");
}

void MainWindow::applyStyle() {
    QString qss = 
        /* -- Base -- */
        "QMainWindow {\n"
        "    background-color: #121214;\n"
        "}\n"
        "* {\n"
        "    color: #e1e1e6;\n"
        "    font-family: 'Inter', 'Outfit', 'Cantarell', 'Helvetica Neue', sans-serif;\n"
        "}\n"
        "QLabel {\n"
        "    color: #e1e1e6;\n"
        "}\n"
        "\n"
        /* -- Cards -- */
        "#sidebarCard {\n"
        "    background-color: #18181c;\n"
        "    border: 1px solid #28282e;\n"
        "    border-radius: 12px;\n"
        "    margin: 10px 5px 10px 10px;\n"
        "    padding: 16px;\n"
        "}\n"
        "#mainCard {\n"
        "    background-color: #18181c;\n"
        "    border: 1px solid #28282e;\n"
        "    border-radius: 12px;\n"
        "    margin: 10px 10px 10px 5px;\n"
        "    padding: 16px;\n"
        "}\n"
        "\n"
        "/* -- AlbumCard (Home Tab) -- */\n"
        "#albumCard {\n"
        "    background-color: rgba(255, 255, 255, 0.03);\n"
        "    border: 1px solid #28282e;\n"
        "    border-radius: 10px;\n"
        "}\n"
        "#albumCard:hover {\n"
        "    background-color: rgba(255, 255, 255, 0.08);\n"
        "    border: 1px solid #04d361;\n"
        "}\n"
        "\n"
        /* -- Scrollbar -- */
        "QScrollBar:vertical {\n"
        "    background: transparent;\n"
        "    width: 6px;\n"
        "    margin: 0;\n"
        "}\n"
        "QScrollBar::handle:vertical {\n"
        "    background-color: rgba(255, 255, 255, 0.1);\n"
        "    border-radius: 3px;\n"
        "    min-height: 20px;\n"
        "}\n"
        "QScrollBar::handle:vertical:hover {\n"
        "    background-color: rgba(255, 255, 255, 0.2);\n"
        "}\n"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {\n"
        "    height: 0px;\n"
        "}\n"
        "\n"
        /* -- Album Cover -- */
        "#coverContainer {\n"
        "    border-radius: 10px;\n"
        "    background-color: #121214;\n"
        "    border: 1px solid #28282e;\n"
        "    padding: 10px;\n"
        "    margin-bottom: 8px;\n"
        "}\n"
        "\n"
        /* -- Seek & Volume Sliders -- */
        "QSlider::trough:horizontal {\n"
        "    background-color: #28282e;\n"
        "    height: 4px;\n"
        "    border-radius: 2px;\n"
        "}\n"
        "QSlider::sub-page:horizontal {\n"
        "    background-color: #04d361;\n"
        "    border-radius: 2px;\n"
        "}\n"
        "QSlider::handle:horizontal {\n"
        "    background-color: #ffffff;\n"
        "    width: 12px;\n"
        "    height: 12px;\n"
        "    margin: -4px 0;\n"
        "    border-radius: 6px;\n"
        "    border: none;\n"
        "}\n"
        "\n"
        /* -- Default buttons -- */
        "QPushButton {\n"
        "    background-color: #202024;\n"
        "    color: #e1e1e6;\n"
        "    border: 1px solid #28282e;\n"
        "    border-radius: 8px;\n"
        "    padding: 6px 14px;\n"
        "    font-weight: 600;\n"
        "    font-size: 13px;\n"
        "}\n"
        "QPushButton:hover {\n"
        "    background-color: #282830;\n"
        "    border-color: #3e3e4a;\n"
        "}\n"
        "QPushButton:pressed {\n"
        "    background-color: #18181c;\n"
        "}\n"
        "\n"
        /* -- Primary control button -- */
        "#playPauseBtn {\n"
        "    background-color: #04d361;\n"
        "    border: none;\n"
        "    border-radius: 23px;\n"
        "    min-width: 46px;\n"
        "    min-height: 46px;\n"
        "    max-width: 46px;\n"
        "    max-height: 46px;\n"
        "}\n"
        "#playPauseBtn:hover {\n"
        "    background-color: #06e06c;\n"
        "}\n"
        "#playPauseBtn:pressed {\n"
        "    background-color: #03b352;\n"
        "}\n"
        "\n"
        /* -- Flat icon buttons -- */
        "[class=\"flatBtn\"] {\n"
        "    background-color: transparent;\n"
        "    border: none;\n"
        "    border-radius: 18px;\n"
        "    min-width: 36px;\n"
        "    min-height: 36px;\n"
        "    max-width: 36px;\n"
        "    max-height: 36px;\n"
        "}\n"
        "[class=\"flatBtn\"]:hover {\n"
        "    background-color: rgba(255, 255, 255, 0.08);\n"
        "}\n"
        "[class=\"flatBtn\"]:pressed {\n"
        "    background-color: rgba(255, 255, 255, 0.12);\n"
        "}\n"
        "\n"
        /* -- Tab switcher capsules -- */
        "QTabWidget::pane {\n"
        "    border: none;\n"
        "}\n"
        "QTabBar {\n"
        "    background-color: #121214;\n"
        "    border-radius: 20px;\n"
        "    border: 1px solid #28282e;\n"
        "    padding: 4px;\n"
        "}\n"
        "QTabBar::tab {\n"
        "    padding: 6px 16px;\n"
        "    border-radius: 16px;\n"
        "    background: transparent;\n"
        "    color: #a8a8b3;\n"
        "    font-weight: 700;\n"
        "    font-size: 13px;\n"
        "    margin: 2px;\n"
        "}\n"
        "QTabBar::tab:hover {\n"
        "    color: #ffffff;\n"
        "    background-color: rgba(255, 255, 255, 0.04);\n"
        "}\n"
        "QTabBar::tab:selected {\n"
        "    color: #0c0c0e;\n"
        "    background-color: #04d361;\n"
        "}\n"
        "\n"
        /* -- TreeView -- */
        "QTreeView {\n"
        "    background-color: #121214;\n"
        "    border: none;\n"
        "    border-radius: 8px;\n"
        "    padding: 4px;\n"
        "}\n"
        "QTreeView::item {\n"
        "    padding: 10px 8px;\n"
        "    color: #e1e1e6;\n"
        "}\n"
        "QTreeView::item:hover {\n"
        "    background-color: rgba(255, 255, 255, 0.05);\n"
        "}\n"
        "QTreeView::item:selected {\n"
        "    background-color: #04d361;\n"
        "    color: #0c0c0e;\n"
        "    font-weight: 700;\n"
        "}\n"
        "QHeaderView::section {\n"
        "    background-color: #18181c;\n"
        "    color: #8a8a92;\n"
        "    border: none;\n"
        "    border-bottom: 1px solid #28282e;\n"
        "    font-weight: 700;\n"
        "    font-size: 12px;\n"
        "    padding: 8px;\n"
        "}\n"
        "\n"
        /* -- Line Edits & Text View -- */
        "QLineEdit {\n"
        "    background-color: #0c0c0e;\n"
        "    border: 1px solid #28282e;\n"
        "    border-radius: 8px;\n"
        "    padding: 6px;\n"
        "}\n"
        "QPlainTextEdit {\n"
        "    background-color: #0c0c0e;\n"
        "    border: 1px solid #28282e;\n"
        "    border-radius: 8px;\n"
        "    color: #a8a8b3;\n"
        "}\n"
        "\n"
        /* -- Status Bar -- */
        "QStatusBar {\n"
        "    background-color: #121214;\n"
        "    border-top: 1px solid #28282e;\n"
        "}\n"
        "#statusLabel {\n"
        "    font-size: 12px;\n"
        "    color: #a8a8b3;\n"
        "    font-weight: 500;\n"
        "    padding: 6px;\n"
        "}\n"
        "#statusProgress::chunk {\n"
        "    background-color: #04d361;\n"
        "    border-radius: 2px;\n"
        "}\n"
        "#statusProgress {\n"
        "    background-color: #28282e;\n"
        "    color: transparent;\n"
        "    border: none;\n"
        "    max-height: 4px;\n"
        "    border-radius: 2px;\n"
        "}\n";
        
    setStyleSheet(qss);
}

// -------------------------------------------------------------
// Playback Logic
// -------------------------------------------------------------

void MainWindow::playSong(Song *song) {
    if (!song) return;
    
    if (m_playStream) {
        BASS_StreamFree(m_playStream);
        m_playStream = 0;
    }
    
    m_playStream = BASS_StreamCreateFile(FALSE, song->filepath, 0, 0, 0);
    if (m_playStream) {
        BASS_ChannelSetAttribute(m_playStream, BASS_ATTRIB_VOL, m_isMuted ? 0.0 : m_config->volume);
        if (BASS_ChannelPlay(m_playStream, FALSE)) {
            m_isPlaying = true;
            m_playPauseBtn->setIcon(QIcon::fromTheme("media-playback-pause"));
            
            // Set metadata labels
            m_trackTitleLbl->setText(QString::fromUtf8(song->title));
            m_trackArtistLbl->setText(QString::fromUtf8(song->artist));
            
            updateAlbumCover(QString::fromUtf8(song->filepath));
            loadSongLyrics(QString::fromUtf8(song->filepath));
            
            // Refresh queue selection highlight
            refreshQueueList();
        }
    } else {
        QMessageBox::critical(this, "Playback Error", "Failed to play: " + QString::fromUtf8(song->filepath));
    }
}

void MainWindow::onPlayPauseClicked() {
    if (m_playStream) {
        if (m_isPlaying) {
            BASS_ChannelPause(m_playStream);
            m_isPlaying = false;
            m_playPauseBtn->setIcon(QIcon::fromTheme("media-playback-start"));
        } else {
            if (BASS_ChannelPlay(m_playStream, FALSE)) {
                m_isPlaying = true;
                m_playPauseBtn->setIcon(QIcon::fromTheme("media-playback-pause"));
            }
        }
    } else if (!m_queue.isEmpty()) {
        m_currentQueueIndex = 0;
        playSong(m_queue.at(0));
    }
}

void MainWindow::onPrevClicked() {
    if (m_queue.isEmpty()) return;
    
    m_currentQueueIndex--;
    if (m_currentQueueIndex < 0) {
        m_currentQueueIndex = m_config->repeat_mode ? (m_queue.size() - 1) : 0;
    }
    
    playSong(m_queue.at(m_currentQueueIndex));
}

void MainWindow::onNextClicked() {
    if (m_queue.isEmpty()) return;
    
    m_currentQueueIndex++;
    if (m_currentQueueIndex >= m_queue.size()) {
        m_currentQueueIndex = m_config->repeat_mode ? 0 : (m_queue.size() - 1);
    }
    
    playSong(m_queue.at(m_currentQueueIndex));
}

void MainWindow::onShuffleToggled() {
    m_config->shuffle = m_shuffleBtn->isChecked();
    config_save(m_config);
}

void MainWindow::onRepeatToggled() {
    m_config->repeat_mode = m_repeatBtn->isChecked();
    config_save(m_config);
}

void MainWindow::onMuteClicked() {
    m_isMuted = !m_isMuted;
    if (m_isMuted) {
        m_muteBtn->setIcon(QIcon::fromTheme("audio-volume-muted"));
        if (m_playStream) {
            BASS_ChannelSetAttribute(m_playStream, BASS_ATTRIB_VOL, 0.0);
        }
    } else {
        m_muteBtn->setIcon(QIcon::fromTheme("audio-volume-high"));
        if (m_playStream) {
            BASS_ChannelSetAttribute(m_playStream, BASS_ATTRIB_VOL, m_config->volume);
        }
    }
}

void MainWindow::onVolumeChanged(int value) {
    m_config->volume = (double)value / 100.0;
    config_save(m_config);
    if (m_playStream && !m_isMuted) {
        BASS_ChannelSetAttribute(m_playStream, BASS_ATTRIB_VOL, m_config->volume);
    }
}

void MainWindow::onSeekChanged(int value) {
    if (m_playStream) {
        double len = BASS_ChannelBytes2Seconds(m_playStream, BASS_ChannelGetLength(m_playStream, BASS_POS_BYTE));
        double targetSecs = (double)value / 1000.0 * len;
        QWORD targetPos = BASS_ChannelSeconds2Bytes(m_playStream, targetSecs);
        BASS_ChannelSetPosition(m_playStream, targetPos, BASS_POS_BYTE);
    }
}

void MainWindow::onPositionTimer() {
    if (m_scanIsRunning) {
        int total = m_scanTotalCount.loadRelaxed();
        int scanned = m_scanScannedCount.loadRelaxed();
        if (total == 0) {
            m_statusLabel->setText("Calculating library files...");
            m_statusProgress->setRange(0, 0);
        } else {
            m_statusLabel->setText(QString("Scanning library: %1 / %2 files...").arg(scanned).arg(total));
            m_statusProgress->setRange(0, total);
            m_statusProgress->setValue(scanned);
        }
    }

    if (!m_playStream || !m_isPlaying) return;
    
    QWORD pos = BASS_ChannelGetPosition(m_playStream, BASS_POS_BYTE);
    QWORD len = BASS_ChannelGetLength(m_playStream, BASS_POS_BYTE);
    
    double secPos = BASS_ChannelBytes2Seconds(m_playStream, pos);
    double secLen = BASS_ChannelBytes2Seconds(m_playStream, len);
    
    if (secPos >= secLen && secLen > 0.0) {
        // Track finished, trigger Next
        onNextClicked();
        return;
    }
    
    // Update Slider
    if (secLen > 0.0) {
        m_seekScale->blockSignals(true);
        m_seekScale->setValue((int)((secPos / secLen) * 1000));
        m_seekScale->blockSignals(false);
    }
    
    // Update Time Label
    int curMin = (int)secPos / 60;
    int curSec = (int)secPos % 60;
    int totMin = (int)secLen / 60;
    int totSec = (int)secLen % 60;
    m_timeLbl->setText(QString("%1:%2 / %3:%4")
                       .arg(curMin, 2, 10, QChar('0'))
                       .arg(curSec, 2, 10, QChar('0'))
                       .arg(totMin, 2, 10, QChar('0'))
                       .arg(totSec, 2, 10, QChar('0')));
                       
    // Update Lyrics highlight
    updateLyricsDisplay(secPos);
}

// -------------------------------------------------------------
// UI List Updates & Actions
// -------------------------------------------------------------

void MainWindow::refreshAlbumList() {
    m_albumModel->removeRows(0, m_albumModel->rowCount());

    for (Album *album : m_library->albums) {
        QList<QStandardItem*> row;
        row << new QStandardItem(QString::fromUtf8(album->name))
            << new QStandardItem(QString::fromUtf8(album->artist));
        m_albumModel->appendRow(row);
    }
}

void MainWindow::onAlbumSelected() {
    QModelIndexList selected = m_albumTreeView->selectionModel()->selectedRows();
    if (selected.isEmpty()) return;

    QString album_name = m_albumModel->item(selected.first().row(), 0)->text();
    QString artist_name = m_albumModel->item(selected.first().row(), 1)->text();

    Album *album = library_find_album(m_library, artist_name.toUtf8().constData(), album_name.toUtf8().constData());
    m_trackModel->removeRows(0, m_trackModel->rowCount());

    if (album) {
        int track_idx = 1;
        for (Song *song : album->songs) {
            int min = (int)song->duration / 60;
            int sec = (int)song->duration % 60;
            QString durStr = QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));

            QString trackNoStr;
            if (song->disc_no > 1) {
                trackNoStr = QString("%1-%2").arg(song->disc_no).arg(song->track_no, 2, 10, QChar('0'));
            } else if (song->track_no > 0) {
                trackNoStr = QString("%1").arg(song->track_no, 2, 10, QChar('0'));
            } else {
                trackNoStr = QString("%1").arg(track_idx++, 2, 10, QChar('0'));
            }

            QList<QStandardItem*> row;
            row << new QStandardItem(trackNoStr)
                << new QStandardItem(QString::fromUtf8(song->title))
                << new QStandardItem(durStr);

            QStandardItem *ptrItem = new QStandardItem();
            ptrItem->setData(QVariant::fromValue((void*)song));
            row << ptrItem;

            m_trackModel->appendRow(row);
        }
    }
}

void MainWindow::setupQueueForAlbum(Album *album, Song *start_song) {
    m_queue.clear();
    m_currentQueueIndex = -1;

    int idx = 0;
    for (Song *song : album->songs) {
        m_queue.append(song);
        if (song == start_song) {
            m_currentQueueIndex = idx;
        }
        idx++;
    }
    
    // Handle shuffle
    if (m_config->shuffle && m_queue.size() > 1) {
        // Keep start_song at currentQueueIndex, shuffle the rest
        Song *active = m_queue.takeAt(m_currentQueueIndex);
        
        // Fisher-Yates shuffle
        for (int i = m_queue.size() - 1; i > 0; --i) {
            int j = rand() % (i + 1);
            m_queue.swapItemsAt(i, j);
        }
        m_queue.insert(0, active);
        m_currentQueueIndex = 0;
    }
    
    refreshQueueList();
}

void MainWindow::refreshQueueList() {
    m_queueModel->removeRows(0, m_queueModel->rowCount());
    
    int idx = 1;
    for (int i = 0; i < m_queue.size(); ++i) {
        Song *song = m_queue.at(i);
        int min = (int)song->duration / 60;
        int sec = (int)song->duration % 60;
        QString durStr = QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
        
        QList<QStandardItem*> row;
        row << new QStandardItem(QString::number(idx++))
            << new QStandardItem(QString::fromUtf8(song->title))
            << new QStandardItem(QString::fromUtf8(song->artist))
            << new QStandardItem(durStr);
            
        QStandardItem *ptrItem = new QStandardItem();
        ptrItem->setData(QVariant::fromValue((void*)song));
        row << ptrItem;
        
        m_queueModel->appendRow(row);
        
        // Highlight active track
        if (i == m_currentQueueIndex) {
            for (int col = 0; col < 4; ++col) {
                m_queueModel->item(i, col)->setBackground(QBrush(QColor("#04d361")));
                m_queueModel->item(i, col)->setForeground(QBrush(QColor("#0c0c0e")));
            }
        }
    }
}

void MainWindow::onTrackActivated(const QModelIndex &index) {
    int row = index.row();
    QStandardItem *ptrItem = m_trackModel->item(row, 3);
    if (!ptrItem) return;
    
    Song *song = (Song *)ptrItem->data().value<void*>();
    if (!song) return;
    
    Album *album = library_find_album(m_library, song->artist, song->album);
    if (album) {
        setupQueueForAlbum(album, song);
        playSong(song);
    }
}

void MainWindow::onQueueActivated(const QModelIndex &index) {
    int row = index.row();
    if (row >= 0 && row < m_queue.size()) {
        m_currentQueueIndex = row;
        playSong(m_queue.at(row));
    }
}

void MainWindow::onClearQueueClicked() {
    m_queue.clear();
    m_currentQueueIndex = -1;
    refreshQueueList();
}

// -------------------------------------------------------------
// Cover Art & Lyrics Resolvers
// -------------------------------------------------------------

void MainWindow::updateAlbumCover(const QString &song_path) {
    if (song_path.isEmpty()) {
        QPixmap fallback(220, 220);
        fallback.fill(QColor("#202024"));
        m_albumCoverImg->setPixmap(fallback);
        return;
    }

    char *cover_path = resolve_cover_art(song_path.toUtf8().constData());
    if (cover_path && QFile::exists(cover_path)) {
        QPixmap pm(QString::fromUtf8(cover_path));
        m_albumCoverImg->setPixmap(pm.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        QPixmap fallback(220, 220);
        fallback.fill(QColor("#202024"));
        m_albumCoverImg->setPixmap(fallback);
    }

    free(cover_path);
}

void MainWindow::loadSongLyrics(const QString &song_path) {
    // Clear old labels
    qDeleteAll(m_lyricLabels);
    m_lyricLabels.clear();
    m_activeLyricIndex = -1;
    
    if (m_currentLyrics) {
        lyrics_free(m_currentLyrics);
        m_currentLyrics = nullptr;
    }
    
    if (song_path.isEmpty()) {
        QLabel *lbl = new QLabel("Lyrics Not Loaded", m_lyricsContainer);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("font-size: 16px; color: #7c7c8a; font-weight: 500; padding: 20px;");
        m_lyricsContainer->layout()->addWidget(lbl);
        m_lyricLabels.append(lbl);
        return;
    }
    
    char *lrc_path = resolve_lyrics(song_path.toUtf8().constData());
    if (lrc_path && QFile::exists(lrc_path)) {
        m_currentLyrics = lyrics_load(lrc_path);
    }
    free(lrc_path);
    
    if (m_currentLyrics && !m_currentLyrics->lines.isEmpty()) {
        for (const LyricLine &line : m_currentLyrics->lines) {
            QLabel *lbl = new QLabel(QString::fromUtf8(line.text), m_lyricsContainer);
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setProperty("class", "lyric-line");
            lbl->setStyleSheet("font-size: 16px; color: #7c7c8a; padding: 10px 0; font-weight: 500;");
            m_lyricsContainer->layout()->addWidget(lbl);
            m_lyricLabels.append(lbl);
        }
    } else {
        QLabel *lbl = new QLabel("Instrumental / No Lyrics Found", m_lyricsContainer);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("font-size: 16px; color: #7c7c8a; font-weight: 500; padding: 20px;");
        m_lyricsContainer->layout()->addWidget(lbl);
        m_lyricLabels.append(lbl);
    }
}

void MainWindow::updateLyricsDisplay(double position) {
    if (!m_currentLyrics || m_currentLyrics->lines.isEmpty() || m_lyricLabels.isEmpty()) return;
    
    int index = lyrics_find_index(m_currentLyrics, position);
    if (index == m_activeLyricIndex) return;
    
    // Reset old highlighted lyric
    if (m_activeLyricIndex >= 0 && m_activeLyricIndex < m_lyricLabels.size()) {
        m_lyricLabels.at(m_activeLyricIndex)->setStyleSheet("font-size: 16px; color: #7c7c8a; padding: 10px 0; font-weight: 500;");
    }
    
    m_activeLyricIndex = index;
    
    // Highlight new active lyric
    if (m_activeLyricIndex >= 0 && m_activeLyricIndex < m_lyricLabels.size()) {
        QLabel *activeLabel = m_lyricLabels.at(m_activeLyricIndex);
        activeLabel->setStyleSheet("font-size: 20px; color: #04d361; font-weight: 700; padding: 10px 0;");
        
        // Centered scroll
        m_lyricsScroll->ensureWidgetVisible(activeLabel, 0, m_lyricsScroll->height() / 2);
    }
}

// -------------------------------------------------------------
// Async Library Scan
// -------------------------------------------------------------

void MainWindow::startAsyncScan(const QString &path) {
    if (m_scanIsRunning) return;
    
    if (path.isEmpty() || !QDir(path).exists()) {
        m_statusLabel->setText("No library path set or directory invalid.");
        m_statusProgress->hide();
        return;
    }
    
    m_scanIsRunning = true;
    m_scanScannedCount = 0;
    m_scanTotalCount = 0;
    m_scanPendingPath = path;
    
    m_statusProgress->show();
    m_statusProgress->setRange(0, 0); // Indefinite pulsing during calculation
    m_statusProgress->setValue(0);
    m_statusLabel->setText("Calculating library files...");
    
    m_scanThread = new QThread(this);
    ScanWorker *worker = new ScanWorker(path, library_new(), &m_scanScannedCount, &m_scanTotalCount);
    worker->moveToThread(m_scanThread);
    
    connect(m_scanThread, &QThread::started, worker, &ScanWorker::run);
    connect(worker, &ScanWorker::finished, this, &MainWindow::onScanFinished);
    
    // Auto cleanup
    connect(worker, &ScanWorker::finished, worker, &QObject::deleteLater);
    connect(m_scanThread, &QThread::finished, m_scanThread, &QObject::deleteLater);
    
    m_scanThread->start();
    
    // Progress poller timer (reusing position timer or starting one-shot progress check)
    // We can just query counts directly inside the position timer callback!
    // Since position timer runs every 100ms, it is perfect.
}

void MainWindow::onScanProgress(int scanned, int total) {
    // Left empty since we read progress counters inside onPositionTimer/poller
}

void MainWindow::onScanFinished(MusicLibrary *temp_lib) {
    m_scanThread->quit();
    m_scanThread->wait();
    
    if (m_library) {
        library_free(m_library);
    }
    m_library = temp_lib;
    
    refreshAlbumList();
    refreshRecentAlbums();
    
    m_scanIsRunning = false;
    m_scanPendingPath = "";
    
    m_statusLabel->setText("Library loaded.");
    m_statusProgress->hide();
}

// -------------------------------------------------------------
// Settings Tab Slots
// -------------------------------------------------------------

void MainWindow::onSettingsLibBrowse() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Library scan folder", m_settingsLibEdit->text());
    if (!dir.isEmpty()) {
        m_settingsLibEdit->setText(dir);
    }
}

void MainWindow::onSettingsDestBrowse() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Destination folder", m_settingsDestEdit->text());
    if (!dir.isEmpty()) {
        m_settingsDestEdit->setText(dir);
    }
}

void MainWindow::onSettingsSave() {
    QString lib = m_settingsLibEdit->text();
    QString dest = m_settingsDestEdit->text();
    
    if (lib.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please select a library folder.");
        return;
    }
    if (dest.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please select an import destination folder.");
        return;
    }
    
    free(m_config->library_path);
    m_config->library_path = strdup(lib.toUtf8().constData());

    free(m_config->import_dest_path);
    m_config->import_dest_path = strdup(dest.toUtf8().constData());
    
    config_save(m_config);
    
    m_importDestLbl->setText(dest);
    
    QMessageBox::information(this, "Settings Saved", "Settings saved successfully! Rescanning library.");
    startAsyncScan(lib);
}

// -------------------------------------------------------------
// Import Tab Slots
// -------------------------------------------------------------

void MainWindow::onImportSrcBrowse() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Import Source Folder", m_importSrcEdit->text());
    if (!dir.isEmpty()) {
        m_importSrcEdit->setText(dir);
    }
}

void MainWindow::onImportStart() {
    QString src = m_importSrcEdit->text();
    if (src.isEmpty() || !QDir(src).exists()) {
        QMessageBox::warning(this, "Input Error", "Please select a valid source folder.");
        return;
    }
    
    if (!m_config->import_dest_path || strlen(m_config->import_dest_path) == 0) {
        QMessageBox::warning(this, "Settings Error", "Import Destination Folder is not set in Settings.");
        return;
    }
    
    m_importLogView->clear();
    m_importProgress->setRange(0, 0); // Pulse indicator
    
    m_importStartBtn->setEnabled(false);
    m_importStopBtn->setEnabled(true);
    
    m_importProcess = new QProcess(this);
    connect(m_importProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onImportReadyRead);
    connect(m_importProcess, &QProcess::readyReadStandardError, this, &MainWindow::onImportReadyRead);
    
    // Qt 6 readyRead slots hook
    typedef void (QProcess::*FinishedSignal)(int, QProcess::ExitStatus);
    connect(m_importProcess, (FinishedSignal)&QProcess::finished, this, &MainWindow::onImportFinished);
    
    QStringList args;
    args << src;
    args << QString::fromUtf8(m_config->import_dest_path);
    args << (m_importDryRunChk->isChecked() ? "true" : "false");
    args << (m_importRemoveChk->isChecked() ? "true" : "false");
    args << (m_importSkipChk->isChecked() ? "true" : "false");
    args << (m_importReduceChk->isChecked() ? "true" : "false");
    
    m_importProcess->start("./run_import.sh", args);
}

void MainWindow::onImportStop() {
    if (m_importProcess && m_importProcess->state() != QProcess::NotRunning) {
        m_importProcess->kill();
    }
}

void MainWindow::onImportFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_importStartBtn->setEnabled(true);
    m_importStopBtn->setEnabled(false);
    m_importProgress->setRange(0, 100);
    m_importProgress->setValue(100);
    
    m_importLogView->appendPlainText("\n*** Done. Library Re-scanned. ***");
    m_importProcess->deleteLater();
    m_importProcess = nullptr;
    
    // Rescan library
    if (m_config->library_path && strlen(m_config->library_path) > 0) {
        startAsyncScan(QString::fromUtf8(m_config->library_path));
    }
}

void MainWindow::onImportReadyRead() {
    if (m_importProcess) {
        QByteArray output = m_importProcess->readAllStandardOutput();
        QByteArray errOutput = m_importProcess->readAllStandardError();
        if (!output.isEmpty()) {
            m_importLogView->appendPlainText(QString::fromUtf8(output));
        }
        if (!errOutput.isEmpty()) {
            m_importLogView->appendPlainText(QString::fromUtf8(errOutput));
        }
        
        // Auto scroll to bottom
        m_importLogView->verticalScrollBar()->setValue(m_importLogView->verticalScrollBar()->maximum());
    }
}

void MainWindow::setupHomeTab() {
    QWidget *homeTab = new QWidget(m_tabs);
    QVBoxLayout *homeLayout = new QVBoxLayout(homeTab);
    homeLayout->setContentsMargins(16, 16, 16, 16);
    homeLayout->setSpacing(12);
    
    // Search edit
    m_searchEdit = new QLineEdit(homeTab);
    m_searchEdit->setPlaceholderText("🔍 Search songs, artists, or albums...");
    m_searchEdit->setStyleSheet(
        "QLineEdit {"
        "    background-color: #121214;"
        "    border: 1px solid #28282e;"
        "    border-radius: 8px;"
        "    padding: 10px 14px;"
        "    color: #ffffff;"
        "    font-size: 14px;"
        "}"
        "QLineEdit:focus {"
        "    border: 1px solid #04d361;"
        "}"
    );
    homeLayout->addWidget(m_searchEdit);
    
    // Recently Added container
    m_recentAlbumsWidget = new QWidget(homeTab);
    QVBoxLayout *recentLayout = new QVBoxLayout(m_recentAlbumsWidget);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(10);
    
    QLabel *recentHeader = new QLabel("Recently Added Albums", m_recentAlbumsWidget);
    recentHeader->setStyleSheet("font-size: 16px; font-weight: 700; color: #04d361;");
    recentLayout->addWidget(recentHeader);
    
    QScrollArea *scroll = new QScrollArea(m_recentAlbumsWidget);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background-color: transparent;");
    
    QWidget *scrollContainer = new QWidget(scroll);
    scrollContainer->setStyleSheet("background-color: transparent;");
    m_recentAlbumsLayout = new QGridLayout(scrollContainer);
    m_recentAlbumsLayout->setContentsMargins(0, 0, 0, 0);
    m_recentAlbumsLayout->setSpacing(12);
    m_recentAlbumsLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    
    scrollContainer->setLayout(m_recentAlbumsLayout);
    scroll->setWidget(scrollContainer);
    recentLayout->addWidget(scroll);
    
    homeLayout->addWidget(m_recentAlbumsWidget);
    
    // Search Results tree view
    m_searchResultsTreeView = new QTreeView(homeTab);
    m_searchResultsTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_searchResultsTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_searchResultsTreeView->setAllColumnsShowFocus(true);
    m_searchResultsTreeView->setHeaderHidden(false);
    
    m_searchModel = new QStandardItemModel(0, 6, m_searchResultsTreeView);
    m_searchModel->setHeaderData(0, Qt::Horizontal, "#");
    m_searchModel->setHeaderData(1, Qt::Horizontal, "Title");
    m_searchModel->setHeaderData(2, Qt::Horizontal, "Artist");
    m_searchModel->setHeaderData(3, Qt::Horizontal, "Album");
    m_searchModel->setHeaderData(4, Qt::Horizontal, "Duration");
    m_searchModel->setHeaderData(5, Qt::Horizontal, "SongPtr");
    
    m_searchResultsTreeView->setModel(m_searchModel);
    m_searchResultsTreeView->setColumnHidden(5, true);
    m_searchResultsTreeView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_searchResultsTreeView->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_searchResultsTreeView->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_searchResultsTreeView->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_searchResultsTreeView->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    
    m_searchResultsTreeView->hide();
    homeLayout->addWidget(m_searchResultsTreeView);
    
    m_tabs->addTab(homeTab, "Home");
    
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(m_searchResultsTreeView, &QTreeView::doubleClicked, this, &MainWindow::onSearchResultActivated);
}

void MainWindow::refreshRecentAlbums() {
    // Clear old cards
    QLayoutItem *child;
    while ((child = m_recentAlbumsLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
    
    if (!m_library) return;
    
    // Determine the number of rows based on window height (3 to 5 rows)
    int h = this->height();
    int num_rows = 3;
    if (h >= 1000) {
        num_rows = 5;
    } else if (h >= 820) {
        num_rows = 4;
    } else {
        num_rows = 3;
    }
    
    // Fetch enough albums to populate the grid (limit = rows * 8 columns)
    int limit = num_rows * 8;
    QList<Album*> recent = library_get_recent_albums(m_library, limit);
    int i = 0;
    for (Album *album : recent) {
        AlbumCard *card = new AlbumCard(album, m_recentAlbumsWidget);
        connect(card, &AlbumCard::clicked, this, &MainWindow::onRecentAlbumClicked);

        int row = i % num_rows;
        int col = i / num_rows;
        m_recentAlbumsLayout->addWidget(card, row, col);
        i++;
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_library && m_recentAlbumsWidget && m_recentAlbumsWidget->isVisible()) {
        static int last_num_rows = -1;
        int h = this->height();
        int num_rows = 3;
        if (h >= 1000) {
            num_rows = 5;
        } else if (h >= 820) {
            num_rows = 4;
        } else {
            num_rows = 3;
        }
        
        if (num_rows != last_num_rows) {
            last_num_rows = num_rows;
            refreshRecentAlbums();
        }
    }
}

void MainWindow::onSearchTextChanged(const QString &text) {
    if (text.trimmed().isEmpty()) {
        m_searchResultsTreeView->hide();
        m_recentAlbumsWidget->show();
        m_searchModel->removeRows(0, m_searchModel->rowCount());
        return;
    }
    
    m_recentAlbumsWidget->hide();
    m_searchResultsTreeView->show();
    m_searchModel->removeRows(0, m_searchModel->rowCount());
    
    if (!m_library) return;
    
    int result_idx = 1;
    for (Album *album : m_library->albums) {
        for (Song *song : album->songs) {
            
            QString title = QString::fromUtf8(song->title);
            QString artist = QString::fromUtf8(song->artist);
            QString albumName = QString::fromUtf8(song->album);
            
            if (title.contains(text, Qt::CaseInsensitive) ||
                artist.contains(text, Qt::CaseInsensitive) ||
                albumName.contains(text, Qt::CaseInsensitive)) {
                
                int min = (int)song->duration / 60;
                int sec = (int)song->duration % 60;
                QString durStr = QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
                
                QList<QStandardItem*> row;
                row << new QStandardItem(QString::number(result_idx++))
                    << new QStandardItem(title)
                    << new QStandardItem(artist)
                    << new QStandardItem(albumName)
                    << new QStandardItem(durStr);
                    
                QStandardItem *ptrItem = new QStandardItem();
                ptrItem->setData(QVariant::fromValue((void*)song));
                row << ptrItem;
                
                m_searchModel->appendRow(row);
            }
        }
    }
}

void MainWindow::onSearchResultActivated(const QModelIndex &index) {
    int row = index.row();
    QStandardItem *ptrItem = m_searchModel->item(row, 5);
    if (!ptrItem) return;
    
    Song *song = (Song *)ptrItem->data().value<void*>();
    if (!song) return;
    
    Album *album = library_find_album(m_library, song->artist, song->album);
    if (album) {
        setupQueueForAlbum(album, song);
        playSong(song);
    }
}

void MainWindow::onRecentAlbumClicked(Album *album) {
    if (!album) return;
    
    // Find index of the album in the library tab's tree model
    for (int i = 0; i < m_albumModel->rowCount(); ++i) {
        QString name = m_albumModel->item(i, 0)->text();
        QString artist = m_albumModel->item(i, 1)->text();
        if (name == QString::fromUtf8(album->name) && artist == QString::fromUtf8(album->artist)) {
            QModelIndex idx = m_albumModel->index(i, 0);
            m_albumTreeView->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_albumTreeView->scrollTo(idx);
            
            // Switch to Library tab (index 1, since Home is index 0)
            m_tabs->setCurrentIndex(1);
            break;
        }
    }
}
