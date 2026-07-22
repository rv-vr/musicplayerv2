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
#include <QRandomGenerator>
#include <QListWidget>
#include <QFrame>
#include <QPainter>
#include <QCryptographicHash>
#include <QDir>
#include <QPropertyAnimation>
#include <QEasingCurve>

static QIcon recolorIcon(const QString &themeIcon, const QColor &color, int size = 28) {
    QPixmap base(size, size);
    base.fill(Qt::transparent);
    QPainter p(&base);
    QIcon::fromTheme(themeIcon).paint(&p, QRect(0, 0, size, size));
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(base.rect(), color);
    p.end();
    return QIcon(base);
}

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
            if (ext == "mp3" || ext == "m4a" || ext == "aac" || ext == "flac") {
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
    setFixedSize(115, 165);
    setFrameShape(QFrame::StyledPanel);
    setObjectName("albumCard");
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(3);
    
    QLabel *coverLbl = new QLabel(this);
    coverLbl->setFixedSize(103, 103);
    coverLbl->setAlignment(Qt::AlignCenter);
    
    bool loaded = false;
    if (!album->cover_path.empty() && QFile::exists(QString::fromStdString(album->cover_path))) {
        QPixmap pm(QString::fromStdString(album->cover_path));
        if (!pm.isNull()) {
            coverLbl->setPixmap(pm.scaled(103, 103, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            loaded = true;
        }
    }
    if (!loaded && !album->songs.isEmpty()) {
        Song *first = album->songs.first();
        char *cov_path = resolve_cover_art(first->filepath.c_str());
        if (cov_path && QFile::exists(cov_path)) {
            album->cover_path = cov_path;
            QPixmap pm(QString::fromUtf8(cov_path));
            if (!pm.isNull()) {
                coverLbl->setPixmap(pm.scaled(103, 103, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                loaded = true;
            }
        }
        free(cov_path);
    }
    if (!loaded) {
        QPixmap fallback(103, 103);
        fallback.fill(QColor("#e5e7eb"));
        coverLbl->setPixmap(fallback);
    }
    layout->addWidget(coverLbl);
    
    QLabel *titleLbl = new QLabel(QString::fromStdString(album->name), this);
    titleLbl->setStyleSheet("font-size: 11px; font-weight: 600; color: #1a1a1a;");
    titleLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFontMetrics fm(titleLbl->font());
    QString elidedTitle = fm.elidedText(titleLbl->text(), Qt::ElideRight, 103);
    titleLbl->setText(elidedTitle);
    layout->addWidget(titleLbl);
    
    QLabel *artistLbl = new QLabel(QString::fromStdString(album->artist), this);
    artistLbl->setStyleSheet("font-size: 10px; color: #6b7280;");
    artistLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QString elidedArtist = fm.elidedText(artistLbl->text(), Qt::ElideRight, 103);
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
    library_scan(m_lib, m_path, m_counter, m_totalCounter);
    emit finished(m_lib);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_playStream(0),
      m_isPlaying(false),
      m_isMuted(false),
      m_savedVolume(0.8),
      m_library(library_new()),
      m_config(config_load()),
      m_currentQueueIndex(-1),
      m_currentLyrics(nullptr),
      m_activeLyricIndex(-1),
      m_scanThread(nullptr),
      m_scanIsRunning(false),
      m_importThread(nullptr),
      m_searchEdit(nullptr),
      m_searchResultsTreeView(nullptr),
      m_searchModel(nullptr),
      m_recentAlbumsWidget(nullptr),
      m_recentAlbumsLayout(nullptr)
{
    // Initialize BASS
    bool bassOk = BASS_Init(-1, 44100, 0, NULL, NULL);
    if (!bassOk) {
        qWarning() << "BASS_Init error:" << BASS_ErrorGetCode();
    }
    
    // Load Audio Plugins (AAC, FLAC)
    if (bassOk) {
        QString appDir = QCoreApplication::applicationDirPath();
        const std::vector<QString> plugins = {"libbass_aac.so", "libbassflac.so"};
        for (const auto &pluginName : plugins) {
            QString pluginPath = appDir + "/lib/" + pluginName;
            if (!QFile::exists(pluginPath)) {
                pluginPath = appDir + "/../lib/" + pluginName;
            }
            if (BASS_PluginLoad(pluginPath.toUtf8().constData(), 0) == 0) {
                qWarning() << "Warning: BASS plugin not loaded at" << pluginPath;
            }
        }
    }
    
    // Load cached DB upfront for instant startup (<2ms)
    library_load_cached(m_library.get());

    // Setup UI
    setupUI();
    
    // Apply Stylesheet & Setup Live QSS Hot Reloading
    applyStyle();
    m_styleWatcher = new QFileSystemWatcher(this);
    QStringList styleWatchPaths = {
        QCoreApplication::applicationDirPath() + "/style.qss",
        QCoreApplication::applicationDirPath() + "/../style.qss"
    };
    for (const QString &sp : styleWatchPaths) {
        if (QFile::exists(sp)) {
            m_styleWatcher->addPath(sp);
        }
    }
    connect(m_styleWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        applyStyle();
        if (!m_styleWatcher->files().contains(path) && QFile::exists(path)) {
            m_styleWatcher->addPath(path);
        }
    });

    // Populate UI from Cache Immediately
    refreshRecentAlbums();
    populateArtistList();
    
    // Position Update Timer
    m_positionTimer = new QTimer(this);
    connect(m_positionTimer, &QTimer::timeout, this, &MainWindow::onPositionTimer);
    m_positionTimer->start(100);
    
    // Background Scan
    if (!m_config->library_path.empty()) {
        startAsyncScan(QString::fromStdString(m_config->library_path));
    }
}

MainWindow::~MainWindow() {
    if (m_scanThread && m_scanThread->isRunning()) {
        m_scanThread->quit();
        m_scanThread->wait(3000);
    }

    if (m_importThread && m_importThread->isRunning()) {
        m_importThread->requestInterruption();
        m_importThread->quit();
        m_importThread->wait(3000);
    }

    if (m_playStream) {
        BASS_StreamFree(m_playStream);
    }
    BASS_Free();
}

void MainWindow::setupUI() {
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // -------------------------------------------------------------
    // TOP PLAYER BAR (Apple Music Style)
    // -------------------------------------------------------------
    m_topPlayerBar = new QWidget(m_centralWidget);
    m_topPlayerBar->setObjectName("topPlayerBar");
    m_topPlayerBar->installEventFilter(this);
    
    m_ambientBackgroundLbl = new QLabel(m_topPlayerBar);
    m_ambientBackgroundLbl->setScaledContents(true);
    m_ambientBackgroundLbl->lower();
    updateAmbientBackground(QString());
    
    QVBoxLayout *topBarLayout = new QVBoxLayout(m_topPlayerBar);
    topBarLayout->setContentsMargins(16, 10, 16, 8);
    topBarLayout->setSpacing(8);
    
    // Top Row: [ Left: Cover + Title/Artist ]  [ Center: Playback Controls ]  [ Right: Volume ]
    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(12);
    
    // --- LEFT SECTION: Cover + Info ---
    QWidget *leftSection = new QWidget(m_topPlayerBar);
    leftSection->setObjectName("topLeftSection");
    QHBoxLayout *leftLayout = new QHBoxLayout(leftSection);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);
    
    m_albumCoverImg = new QLabel(leftSection);
    m_albumCoverImg->setObjectName("topAlbumCover");
    m_albumCoverImg->setFixedSize(48, 48);
    m_albumCoverImg->setAlignment(Qt::AlignCenter);
    
    QPixmap fallback(48, 48);
    fallback.fill(QColor("#e5e7eb"));
    m_albumCoverImg->setPixmap(fallback);
    leftLayout->addWidget(m_albumCoverImg);
    
    QVBoxLayout *infoStack = new QVBoxLayout();
    infoStack->setSpacing(2);
    infoStack->setContentsMargins(0, 0, 0, 0);
    infoStack->setAlignment(Qt::AlignVCenter);
    
    m_trackTitleLbl = new QLabel("Not Playing", leftSection);
    m_trackTitleLbl->setObjectName("topTrackTitle");
    infoStack->addWidget(m_trackTitleLbl);
    
    m_trackArtistLbl = new QLabel("Select a track to play", leftSection);
    m_trackArtistLbl->setObjectName("topTrackArtist");
    infoStack->addWidget(m_trackArtistLbl);
    
    leftLayout->addLayout(infoStack);
    leftLayout->addStretch();
    
    topRow->addWidget(leftSection, 1);
    
    // --- CENTER SECTION: Playback Controls ---
    QWidget *centerSection = new QWidget(m_topPlayerBar);
    centerSection->setObjectName("topCenterSection");
    QHBoxLayout *ctrlRow = new QHBoxLayout(centerSection);
    ctrlRow->setContentsMargins(0, 0, 0, 0);
    ctrlRow->setSpacing(8);
    ctrlRow->setAlignment(Qt::AlignCenter);
    
    QColor whiteIcon(60, 127, 177);
    
    auto mkTopBtn = [&](const QString &icon, bool chk) -> QPushButton* {
        QPushButton *b = new QPushButton(centerSection);
        b->setProperty("class", "topBtn");
        b->setCheckable(chk);
        b->setIcon(recolorIcon(icon, whiteIcon, 20));
        b->setFixedSize(32, 32);
        return b;
    };
    
    m_shuffleBtn = mkTopBtn("media-playlist-shuffle", true);
    m_shuffleBtn->setChecked(m_config->shuffle);
    connect(m_shuffleBtn, &QPushButton::clicked, this, &MainWindow::onShuffleToggled);
    ctrlRow->addWidget(m_shuffleBtn);
    
    m_prevBtn = mkTopBtn("media-skip-backward", false);
    connect(m_prevBtn, &QPushButton::clicked, this, &MainWindow::onPrevClicked);
    ctrlRow->addWidget(m_prevBtn);
    
    m_playPauseBtn = new QPushButton(centerSection);
    m_playPauseBtn->setObjectName("topPlayBtn");
    m_playPauseBtn->setIcon(recolorIcon("media-playback-start", QColor("#ffffff"), 24));
    m_playPauseBtn->setFixedSize(38, 38);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &MainWindow::onPlayPauseClicked);
    ctrlRow->addWidget(m_playPauseBtn);
    
    m_nextBtn = mkTopBtn("media-skip-forward", false);
    connect(m_nextBtn, &QPushButton::clicked, this, &MainWindow::onNextClicked);
    ctrlRow->addWidget(m_nextBtn);
    
    m_repeatBtn = mkTopBtn("media-playlist-repeat", true);
    m_repeatBtn->setChecked(m_config->repeat_mode);
    connect(m_repeatBtn, &QPushButton::clicked, this, &MainWindow::onRepeatToggled);
    ctrlRow->addWidget(m_repeatBtn);
    
    topRow->addWidget(centerSection, 1);
    
    // --- RIGHT SECTION: Volume ---
    QWidget *rightSection = new QWidget(m_topPlayerBar);
    rightSection->setObjectName("topRightSection");
    QHBoxLayout *rightLayout = new QHBoxLayout(rightSection);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);
    rightLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    
    m_muteBtn = new QPushButton(rightSection);
    m_muteBtn->setProperty("class", "topBtn");
    m_muteBtn->setIcon(recolorIcon("audio-volume-high", QColor("#6b7280"), 18));
    m_muteBtn->setFixedSize(32, 32);
    connect(m_muteBtn, &QPushButton::clicked, this, &MainWindow::onMuteClicked);
    rightLayout->addWidget(m_muteBtn);
    
    m_volumeScale = new QSlider(Qt::Horizontal, rightSection);
    m_volumeScale->setObjectName("topVolume");
    m_volumeScale->setRange(0, 100);
    m_volumeScale->setValue(m_config->volume * 100);
    m_volumeScale->setFixedWidth(100);
    connect(m_volumeScale, &QSlider::valueChanged, this, &MainWindow::onVolumeChanged);
    rightLayout->addWidget(m_volumeScale);
    
    topRow->addWidget(rightSection, 1);
    
    topBarLayout->addLayout(topRow);
    
    // --- BOTTOM ROW: Seekbar + Time Displays ---
    QHBoxLayout *seekRow = new QHBoxLayout();
    seekRow->setContentsMargins(0, 4, 0, 0);
    seekRow->setSpacing(10);
    
    m_timeLbl = new QLabel("00:00", m_topPlayerBar);
    m_timeLbl->setObjectName("topTime");
    m_timeLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_timeLbl->setFixedWidth(42);
    seekRow->addWidget(m_timeLbl);
    
    m_seekScale = new QSlider(Qt::Horizontal, m_topPlayerBar);
    m_seekScale->setObjectName("topSeek");
    m_seekScale->setRange(0, 1000);
    connect(m_seekScale, &QSlider::sliderMoved, this, &MainWindow::onSeekChanged);
    seekRow->addWidget(m_seekScale, 1);
    
    m_totalTimeLbl = new QLabel("00:00", m_topPlayerBar);
    m_totalTimeLbl->setObjectName("topTotalTime");
    m_totalTimeLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_totalTimeLbl->setFixedWidth(42);
    seekRow->addWidget(m_totalTimeLbl);
    
    topBarLayout->addLayout(seekRow);
    mainLayout->addWidget(m_topPlayerBar);
    
    // -------------------------------------------------------------
    // TAB WIDGET
    // -------------------------------------------------------------
    m_tabs = new QTabWidget(m_centralWidget);
    m_tabs->setObjectName("mainTabs");
    
    // TAB 0: Artists
    setupArtistsTab();
    
    // TAB 1: Home
    setupHomeTab();
    
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
    QWidget *lyricsTab = new QWidget(m_tabs);
    QVBoxLayout *lyricsTabLayout = new QVBoxLayout(lyricsTab);
    lyricsTabLayout->setContentsMargins(15, 15, 15, 15);
    lyricsTabLayout->setSpacing(10);

    // Fixed Pinned Glassmorphism Active Header
    m_pinnedActiveLyricLabel = new QLabel("♪ Select a song to view lyrics", lyricsTab);
    m_pinnedActiveLyricLabel->setAlignment(Qt::AlignCenter);
    m_pinnedActiveLyricLabel->setWordWrap(true);
    m_pinnedActiveLyricLabel->setStyleSheet(
        "font-family: 'Inter', 'Noto Sans KR', 'NanumGothic', sans-serif; "
        "font-size: 22px; font-weight: 700; color: #3c7fb1; "
        "background-color: rgba(60, 127, 177, 0.15); "
        "border: 1px solid rgba(60, 127, 177, 0.3); "
        "border-radius: 10px; padding: 14px 20px;"
    );
    lyricsTabLayout->addWidget(m_pinnedActiveLyricLabel);

    // Scrollable Full Lyrics View below Header
    m_lyricsScroll = new QScrollArea(lyricsTab);
    m_lyricsScroll->setWidgetResizable(true);
    m_lyricsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_lyricsContainer = new QWidget(m_lyricsScroll);
    m_lyricsContainer->setStyleSheet("background-color: transparent;");
    QVBoxLayout *lyricsLayout = new QVBoxLayout(m_lyricsContainer);
    lyricsLayout->setContentsMargins(0, 0, 0, 0);
    lyricsLayout->setSpacing(12);
    lyricsLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_lyricsScroll->setWidget(m_lyricsContainer);
    
    lyricsTabLayout->addWidget(m_lyricsScroll);
    m_tabs->addTab(lyricsTab, "Lyrics");
    
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
    m_importDestLbl = new QLabel(m_config->import_dest_path.empty() ? "Not configured" : QString::fromStdString(m_config->import_dest_path), importTab);
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
    m_settingsLibEdit->setText(QString::fromStdString(m_config->library_path));
    settingsGrid->addWidget(m_settingsLibEdit, 0, 1);
    m_settingsLibBtn = new QPushButton("Browse", settingsTab);
    connect(m_settingsLibBtn, &QPushButton::clicked, this, &MainWindow::onSettingsLibBrowse);
    settingsGrid->addWidget(m_settingsLibBtn, 0, 2);
    
    settingsGrid->addWidget(new QLabel("Import Destination Folder:", settingsTab), 1, 0, Qt::AlignRight);
    m_settingsDestEdit = new QLineEdit(settingsTab);
    m_settingsDestEdit->setText(QString::fromStdString(m_config->import_dest_path));
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
    
    mainLayout->addWidget(m_tabs, 1);
    
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

// -------------------------------------------------------------
// Artists Tab
// -------------------------------------------------------------
void MainWindow::setupArtistsTab() {
    QWidget *artistsTab = new QWidget(m_tabs);
    QHBoxLayout *artistsLayout = new QHBoxLayout(artistsTab);
    artistsLayout->setContentsMargins(0, 0, 0, 0);
    artistsLayout->setSpacing(0);
    
    m_artistList = new QListWidget(artistsTab);
    m_artistList->setObjectName("artistList");
    m_artistList->setFixedWidth(220);
    connect(m_artistList, &QListWidget::currentTextChanged, this, [this](const QString &text) {
        m_selectedArtist = text;
        m_selectedAlbum = nullptr;
        populateArtistAlbumGrid(text);
    });
    artistsLayout->addWidget(m_artistList);
    
    QFrame *divider = new QFrame(artistsTab);
    divider->setFrameShape(QFrame::VLine);
    divider->setObjectName("artistDivider");
    artistsLayout->addWidget(divider);
    
    m_artistContentPanel = new QWidget(artistsTab);
    m_artistContentLayout = new QVBoxLayout(m_artistContentPanel);
    m_artistContentLayout->setContentsMargins(16, 16, 16, 16);
    m_artistContentLayout->setSpacing(0);
    
    m_artistBackBtn = new QPushButton("<  Back to Albums", m_artistContentPanel);
    m_artistBackBtn->setObjectName("artistBackBtn");
    m_artistBackBtn->hide();
    connect(m_artistBackBtn, &QPushButton::clicked, this, [this]() {
        if (!m_selectedArtist.isEmpty()) {
            populateArtistAlbumGrid(m_selectedArtist);
        }
    });
    m_artistContentLayout->addWidget(m_artistBackBtn);
    
    artistsLayout->addWidget(m_artistContentPanel, 1);
    m_tabs->addTab(artistsTab, "Artists");
}

void MainWindow::populateArtistList() {
    if (!m_library) return;
    m_artistList->blockSignals(true);
    m_artistList->clear();
    QStringList artists = library_get_artists(m_library.get());
    m_artistList->addItems(artists);
    m_artistList->blockSignals(false);
}

void MainWindow::populateArtistAlbumGrid(const QString &artist) {
    if (!m_library || artist.isEmpty()) return;
    
    QLayoutItem *child;
    while ((child = m_artistContentLayout->takeAt(1)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    m_artistBackBtn->hide();
    
    QList<Album*> albums = library_get_albums_by_artist(m_library.get(), artist);
    if (albums.isEmpty()) {
        QLabel *empty = new QLabel("No albums found for this artist.", m_artistContentPanel);
        empty->setObjectName("artistEmpty");
        m_artistContentLayout->addWidget(empty);
        m_artistContentLayout->addStretch();
        return;
    }
    
    QWidget *gridWidget = new QWidget(m_artistContentPanel);
    QGridLayout *grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(12);
    grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    
    for (int i = 0; i < albums.size(); ++i) {
        AlbumCard *card = new AlbumCard(albums[i], gridWidget);
        connect(card, &AlbumCard::clicked, this, [this](Album *album) {
            populateArtistTrackList(album);
        });
        int row = i / 4;
        int col = i % 4;
        grid->addWidget(card, row, col);
    }
    
    m_artistContentLayout->addWidget(gridWidget);
    m_artistContentLayout->addStretch();
}

void MainWindow::populateArtistTrackList(Album *album) {
    if (!album) return;
    m_selectedAlbum = album;
    
    QLayoutItem *child;
    while ((child = m_artistContentLayout->takeAt(1)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    
    m_artistBackBtn->show();
    
    QLabel *albumTitle = new QLabel(QString::fromStdString(album->name), m_artistContentPanel);
    albumTitle->setObjectName("artistAlbumTitle");
    m_artistContentLayout->addWidget(albumTitle);
    
    QListWidget *trackList = new QListWidget(m_artistContentPanel);
    trackList->setObjectName("artistTrackList");
    
    int trackIdx = 0;
    for (Song *song : album->songs) {
        trackIdx++;
        QString trackNo;
        if (song->disc_no > 1)
            trackNo = QString("%1-%2").arg(song->disc_no).arg(song->track_no, 2, 10, QChar('0'));
        else if (song->track_no > 0)
            trackNo = QString("%1").arg(song->track_no, 2, 10, QChar('0'));
        else
            trackNo = QString("%1").arg(trackIdx, 2, 10, QChar('0'));
        
        int min = (int)song->duration / 60;
        int sec = (int)song->duration % 60;
        QString dur = QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
        
        QString text = QString("  %1   %2   %3").arg(trackNo, -4).arg(QString::fromStdString(song->title), -50).arg(dur);
        QListWidgetItem *item = new QListWidgetItem(text, trackList);
        item->setData(Qt::UserRole, QVariant::fromValue((void*)song));
    }
    
    connect(trackList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        Song *song = (Song *)item->data(Qt::UserRole).value<void*>();
        if (song) {
            Album *album = library_find_album(m_library.get(), song->artist.c_str(), song->album.c_str());
            if (album) {
                setupQueueForAlbum(album, song);
                playSong(song);
            }
        }
    });
    
    m_artistContentLayout->addWidget(trackList, 1);
    
    QPushButton *playAllBtn = new QPushButton("▶  Play All", m_artistContentPanel);
    playAllBtn->setObjectName("artistPlayAll");
    connect(playAllBtn, &QPushButton::clicked, this, [this, album]() {
        if (!album->songs.isEmpty()) {
            setupQueueForAlbum(album, album->songs.first());
            playSong(album->songs.first());
        }
    });
    m_artistContentLayout->addWidget(playAllBtn);
}

void MainWindow::applyStyle() {
    QString qss;
    QStringList paths = {
        QCoreApplication::applicationDirPath() + "/style.qss",
        QCoreApplication::applicationDirPath() + "/../style.qss"
    };
    for (const QString &p : paths) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly)) {
            qss = f.readAll();
            f.close();
            break;
        }
    }
    if (!qss.isEmpty()) {
        setStyleSheet(qss);
    }
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

    m_playStream = BASS_StreamCreateFile(FALSE, song->filepath.c_str(), 0, 0, 0);
    if (!m_playStream && BASS_ErrorGetCode() == BASS_ERROR_INIT) {
        QMessageBox::critical(this, "Playback Error", "BASS not initialized. Check audio device.");
        return;
    }
    if (m_playStream) {
        BASS_ChannelSetAttribute(m_playStream, BASS_ATTRIB_VOL, m_isMuted ? 0.0 : m_config->volume);
        if (BASS_ChannelPlay(m_playStream, FALSE)) {
            m_isPlaying = true;
            m_playPauseBtn->setIcon(recolorIcon("media-playback-pause", QColor("#ffffff"), 24));
            
            // Set metadata labels
            m_trackTitleLbl->setText(QString::fromStdString(song->title));
            m_trackArtistLbl->setText(QString::fromStdString(song->artist));
            
            updateAlbumCover(QString::fromStdString(song->filepath));
            loadSongLyrics(QString::fromStdString(song->filepath));
            
            // Refresh queue selection highlight
            refreshQueueList();
        }
    } else {
        QMessageBox::critical(this, "Playback Error", "Failed to play: " + QString::fromStdString(song->filepath));
    }
}

void MainWindow::onPlayPauseClicked() {
    if (m_playStream) {
        if (m_isPlaying) {
            BASS_ChannelPause(m_playStream);
            m_isPlaying = false;
            m_playPauseBtn->setIcon(recolorIcon("media-playback-start", QColor("#ffffff"), 24));
        } else {
            if (BASS_ChannelPlay(m_playStream, FALSE)) {
                m_isPlaying = true;
                m_playPauseBtn->setIcon(recolorIcon("media-playback-pause", QColor("#ffffff"), 24));
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
    config_save(m_config.get());
}

void MainWindow::onRepeatToggled() {
    m_config->repeat_mode = m_repeatBtn->isChecked();
    config_save(m_config.get());
}

void MainWindow::onMuteClicked() {
    m_isMuted = !m_isMuted;
    if (m_isMuted) {
        m_muteBtn->setIcon(recolorIcon("audio-volume-muted", QColor("#6b7280")));
        if (m_playStream) {
            BASS_ChannelSetAttribute(m_playStream, BASS_ATTRIB_VOL, 0.0);
        }
    } else {
        m_muteBtn->setIcon(recolorIcon("audio-volume-high", QColor("#6b7280")));
        if (m_playStream) {
            BASS_ChannelSetAttribute(m_playStream, BASS_ATTRIB_VOL, m_config->volume);
        }
    }
}

void MainWindow::onVolumeChanged(int value) {
    m_config->volume = (double)value / 100.0;
    config_save(m_config.get());
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

    if (!m_playStream) {
        m_timeLbl->setText("00:00");
        m_totalTimeLbl->setText("00:00");
        return;
    }
    
    QWORD pos = BASS_ChannelGetPosition(m_playStream, BASS_POS_BYTE);
    QWORD len = BASS_ChannelGetLength(m_playStream, BASS_POS_BYTE);
    
    double secPos = BASS_ChannelBytes2Seconds(m_playStream, pos);
    double secLen = BASS_ChannelBytes2Seconds(m_playStream, len);
    
    if (secPos >= secLen && secLen > 0.0) {
        if (m_currentQueueIndex + 1 >= m_queue.size() && !m_config->repeat_mode) {
            BASS_StreamFree(m_playStream);
            m_playStream = 0;
            m_isPlaying = false;
            m_playPauseBtn->setIcon(recolorIcon("media-playback-start", QColor("#ffffff"), 24));
            m_timeLbl->setText("00:00");
            m_totalTimeLbl->setText("00:00");
            m_seekScale->blockSignals(true);
            m_seekScale->setValue(0);
            m_seekScale->blockSignals(false);
        } else {
            onNextClicked();
        }
        return;
    }
    
    // Update Slider
    if (secLen > 0.0) {
        m_seekScale->blockSignals(true);
        m_seekScale->setValue((int)((secPos / secLen) * 1000));
        m_seekScale->blockSignals(false);
    }
    
    // Update Time Labels
    int curMin = (int)secPos / 60;
    int curSec = (int)secPos % 60;
    int totMin = (int)secLen / 60;
    int totSec = (int)secLen % 60;
    m_timeLbl->setText(QString("%1:%2")
                       .arg(curMin, 2, 10, QChar('0'))
                       .arg(curSec, 2, 10, QChar('0')));
    m_totalTimeLbl->setText(QString("%1:%2")
                            .arg(totMin, 2, 10, QChar('0'))
                            .arg(totSec, 2, 10, QChar('0')));
                        
    // Update Lyrics highlight
    updateLyricsDisplay(secPos);
}

// -------------------------------------------------------------
// UI List Updates & Actions
// -------------------------------------------------------------

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
        
        for (int i = m_queue.size() - 1; i > 0; --i) {
            int j = QRandomGenerator::global()->bounded(i + 1);
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
                m_queueModel->item(i, col)->setBackground(QBrush(QColor("#3c7fb1")));
                m_queueModel->item(i, col)->setForeground(QBrush(QColor("#ffffff")));
            }
        }
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

static QList<QColor> extractProminentColors(const QImage &img) {
    QList<QColor> colors;
    if (img.isNull()) {
        colors.append(QColor("#38bdf8"));
        colors.append(QColor("#818cf8"));
        return colors;
    }

    QImage small = img.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QMap<quint32, int> histogram;
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            QColor c = small.pixelColor(x, y);
            int r = (c.red() >> 4) << 4;
            int g = (c.green() >> 4) << 4;
            int b = (c.blue() >> 4) << 4;
            quint32 key = (r << 16) | (g << 8) | b;
            histogram[key]++;
        }
    }

    QVector<QPair<float, QColor>> candidates;
    for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it) {
        quint32 k = it.key();
        QColor col((k >> 16) & 0xFF, (k >> 8) & 0xFF, k & 0xFF);
        float h, s, v;
        col.getHsvF(&h, &s, &v);
        if (v < 0.15f || (s < 0.15f && (v > 0.85f || v < 0.25f))) continue;
        
        float score = (float)it.value() * (0.5f + s * 1.5f);
        candidates.append({score, col});
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });

    for (const auto &cand : candidates) {
        bool distinct = true;
        for (const QColor &existing : colors) {
            int dr = abs(cand.second.red() - existing.red());
            int dg = abs(cand.second.green() - existing.green());
            int db = abs(cand.second.blue() - existing.blue());
            if (dr + dg + db < 100) {
                distinct = false;
                break;
            }
        }
        if (distinct) {
            colors.append(cand.second);
            if (colors.size() >= 3) break;
        }
    }

    if (colors.isEmpty()) colors.append(QColor("#38bdf8"));
    if (colors.size() == 1) colors.append(colors.first().lighter(130));

    return colors;
}

void MainWindow::updateAmbientBackground(const QString &coverPath) {
    if (!m_ambientBackgroundLbl || !m_topPlayerBar) return;

    QString cacheDir = QDir::homePath() + "/.cache/musicplayerv2/blurs";
    QDir().mkpath(cacheDir);

    QString cacheKey;
    if (coverPath.isEmpty() || !QFile::exists(coverPath)) {
        cacheKey = cacheDir + "/top_ambient_v4.png";
    } else {
        QByteArray hash = QCryptographicHash::hash(coverPath.toUtf8(), QCryptographicHash::Md5).toHex();
        cacheKey = cacheDir + "/" + QString(hash) + "_v4.png";
    }

    QPixmap blurPixmap;
    if (QFile::exists(cacheKey)) {
        blurPixmap.load(cacheKey);
    } else {
        QImage srcImg;
        if (!coverPath.isEmpty() && QFile::exists(coverPath)) {
            srcImg.load(coverPath);
        }
        
        QList<QColor> prominent = extractProminentColors(srcImg);
        QColor colorA = prominent.size() > 0 ? prominent.at(0) : QColor("#38bdf8");
        QColor colorB = prominent.size() > 1 ? prominent.at(1) : colorA.lighter(130);
        QColor colorC = prominent.size() > 2 ? prominent.at(2) : colorB.darker(130);

        QImage meshCanvas(1200, 200, QImage::Format_ARGB32_Premultiplied);
        meshCanvas.fill(QColor("#121215"));

        QPainter p(&meshCanvas);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        QRadialGradient radialA(300, 100, 500);
        QColor colA_soft = colorA;
        colA_soft.setAlpha(170);
        radialA.setColorAt(0.0, colA_soft);
        radialA.setColorAt(1.0, QColor(18, 18, 21, 0));
        p.fillRect(meshCanvas.rect(), radialA);

        QRadialGradient radialB(900, 100, 500);
        QColor colB_soft = colorB;
        colB_soft.setAlpha(150);
        radialB.setColorAt(0.0, colB_soft);
        radialB.setColorAt(1.0, QColor(18, 18, 21, 0));
        p.fillRect(meshCanvas.rect(), radialB);

        QRadialGradient radialC(600, 50, 350);
        QColor colC_soft = colorC;
        colC_soft.setAlpha(130);
        radialC.setColorAt(0.0, colC_soft);
        radialC.setColorAt(1.0, QColor(18, 18, 21, 0));
        p.fillRect(meshCanvas.rect(), radialC);

        QLinearGradient darkOverlay(0, 0, 0, 200);
        darkOverlay.setColorAt(0.0, QColor(18, 18, 21, 90));
        darkOverlay.setColorAt(1.0, QColor(18, 18, 21, 190));
        p.fillRect(meshCanvas.rect(), darkOverlay);
        p.end();

        meshCanvas.save(cacheKey, "PNG");
        blurPixmap = QPixmap::fromImage(meshCanvas);
    }

    m_ambientBackgroundLbl->setPixmap(blurPixmap);
    if (m_topPlayerBar->size().width() > 0) {
        m_ambientBackgroundLbl->resize(m_topPlayerBar->size());
    }
}

void MainWindow::updateAlbumCover(const QString &song_path) {
    if (song_path.isEmpty()) {
        QPixmap fallback(48, 48);
        fallback.fill(QColor("#27272a"));
        m_albumCoverImg->setPixmap(fallback);
        updateAmbientBackground(QString());
        return;
    }

    char *cover_path = resolve_cover_art(song_path.toUtf8().constData());
    if (cover_path && QFile::exists(cover_path)) {
        QString coverStr = QString::fromUtf8(cover_path);
        QPixmap pm(coverStr);
        m_albumCoverImg->setPixmap(pm.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        updateAmbientBackground(coverStr);
    } else {
        QPixmap fallback(48, 48);
        fallback.fill(QColor("#27272a"));
        m_albumCoverImg->setPixmap(fallback);
        updateAmbientBackground(QString());
    }

    free(cover_path);
}

void MainWindow::loadSongLyrics(const QString &song_path) {
    qDeleteAll(m_lyricLabels);
    m_lyricLabels.clear();
    m_activeLyricIndex = -1;
    
    QVBoxLayout *vbox = static_cast<QVBoxLayout*>(m_lyricsContainer->layout());
    if (vbox) {
        QLayoutItem *child;
        while ((child = vbox->takeAt(0)) != nullptr) {
            if (child->widget()) delete child->widget();
            delete child;
        }
        vbox->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        vbox->setContentsMargins(20, 0, 20, 0);
        vbox->setSpacing(12);
    }
    
    m_currentLyrics.reset();
    
    int viewportW = (m_lyricsScroll && m_lyricsScroll->viewport()) ? m_lyricsScroll->viewport()->width() : 400;
    int pad = (m_lyricsScroll && m_lyricsScroll->viewport() && m_lyricsScroll->viewport()->height() > 0) ? m_lyricsScroll->viewport()->height() / 2 : 250;
    
    if (m_lyricsContainer) {
        m_lyricsContainer->setMinimumWidth(viewportW);
    }

    if (vbox) vbox->addSpacing(pad);

    if (song_path.isEmpty()) {
        QLabel *lbl = new QLabel("Lyrics Not Loaded", m_lyricsContainer);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setWordWrap(true);
        lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        lbl->setStyleSheet("font-family: 'Inter', 'Noto Sans KR', 'NanumGothic', sans-serif; font-size: 16px; color: #9ca3af; font-weight: 600; padding: 12px 0;");
        if (vbox) vbox->addWidget(lbl);
        m_lyricLabels.append(lbl);
    } else {
        char *lrc_path = resolve_lyrics(song_path.toUtf8().constData());
        if (lrc_path && QFile::exists(lrc_path)) {
            m_currentLyrics.reset(lyrics_load(lrc_path));
        }
        free(lrc_path);
        
        if (m_currentLyrics && !m_currentLyrics->lines.isEmpty()) {
            for (const LyricLine &line : m_currentLyrics->lines) {
                QLabel *lbl = new QLabel(QString::fromUtf8(line.text), m_lyricsContainer);
                lbl->setAlignment(Qt::AlignCenter);
                lbl->setWordWrap(true);
                lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                lbl->setProperty("class", "lyric-line");
                lbl->setStyleSheet("font-family: 'Inter', 'Noto Sans KR', 'NanumGothic', sans-serif; font-size: 16px; color: #9ca3af; padding: 12px 0; font-weight: 600;");
                if (vbox) vbox->addWidget(lbl);
                m_lyricLabels.append(lbl);
            }
        } else {
            QLabel *lbl = new QLabel("Instrumental / No Lyrics Found", m_lyricsContainer);
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setWordWrap(true);
            lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            lbl->setStyleSheet("font-family: 'Inter', 'Noto Sans KR', 'NanumGothic', sans-serif; font-size: 16px; color: #9ca3af; font-weight: 600; padding: 20px;");
            if (vbox) vbox->addWidget(lbl);
            m_lyricLabels.append(lbl);
        }
    }

    if (vbox) {
        vbox->addSpacing(pad);
        vbox->activate();
    }
    if (m_lyricsContainer) {
        m_lyricsContainer->adjustSize();
       if (m_pinnedActiveLyricLabel) {
        if (m_currentLyrics && !m_currentLyrics->lines.isEmpty()) {
            m_pinnedActiveLyricLabel->setText(QString::fromUtf8(m_currentLyrics->lines.first().text));
        } else {
            m_pinnedActiveLyricLabel->setText("♪ Instrumental / No Lyrics Found");
        }
    }
    }

    m_lyricLineTargets.clear();
    int viewportHalf = m_lyricsScroll->viewport()->height() > 0 ? m_lyricsScroll->viewport()->height() / 2 : 250;
    for (QLabel *lbl : m_lyricLabels) {
        int targetY = lbl->y() + (lbl->height() / 2) - viewportHalf;
        m_lyricLineTargets.append(targetY);
    }
}

void MainWindow::updateLyricsDisplay(double position) {
    if (!m_currentLyrics || m_currentLyrics->lines.isEmpty() || m_lyricLabels.isEmpty()) return;
    
    int index = lyrics_find_index(m_currentLyrics.get(), position);
    if (index == m_activeLyricIndex) return;
    
    // Reset old highlighted lyric
    if (m_activeLyricIndex >= 0 && m_activeLyricIndex < m_lyricLabels.size()) {
        m_lyricLabels.at(m_activeLyricIndex)->setStyleSheet("font-family: 'Inter', 'Noto Sans KR', 'NanumGothic', sans-serif; font-size: 16px; color: #9ca3af; padding: 12px 0; font-weight: 600;");
    }
    
    m_activeLyricIndex = index;
    
    if (m_activeLyricIndex >= 0 && m_activeLyricIndex < m_lyricLabels.size()) {
        // Live update the fixed pinned top header label
        if (m_pinnedActiveLyricLabel && m_currentLyrics && m_activeLyricIndex < m_currentLyrics->lines.size()) {
            m_pinnedActiveLyricLabel->setText(QString::fromUtf8(m_currentLyrics->lines.at(m_activeLyricIndex).text));
        }

        QLabel *activeLabel = m_lyricLabels.at(m_activeLyricIndex);
        activeLabel->setStyleSheet("font-family: 'Inter', 'Noto Sans KR', 'NanumGothic', sans-serif; font-size: 18px; color: #3c7fb1; padding: 12px 0; font-weight: 700;");
        
        QScrollBar *vBar = m_lyricsScroll->verticalScrollBar();
        int viewportH = m_lyricsScroll->viewport()->height();
        int targetY = activeLabel->y() + (activeLabel->height() / 2) - (viewportH / 2);
        targetY = qBound(vBar->minimum(), targetY, vBar->maximum());

        QPropertyAnimation *anim = new QPropertyAnimation(vBar, "value", m_lyricsScroll);
        anim->setDuration(350);
        anim->setStartValue(vBar->value());
        anim->setEndValue(targetY);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
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
    ScanWorker *worker = new ScanWorker(path, nullptr, &m_scanScannedCount, &m_scanTotalCount);
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
    
    if (temp_lib) {
        library_free(temp_lib);
    }

    m_selectedAlbum = nullptr;
    m_selectedArtist.clear();
    m_queue.clear();
    m_currentQueueIndex = -1;

    if (m_queueModel) m_queueModel->removeRows(0, m_queueModel->rowCount());
    if (m_searchModel) m_searchModel->removeRows(0, m_searchModel->rowCount());

    if (m_recentAlbumsLayout) {
        QLayoutItem *child;
        while ((child = m_recentAlbumsLayout->takeAt(0)) != nullptr) {
            if (child->widget()) {
                delete child->widget();
            }
            delete child;
        }
    }

    if (m_artistContentLayout) {
        QLayoutItem *child;
        while ((child = m_artistContentLayout->takeAt(1)) != nullptr) {
            if (child->widget()) {
                delete child->widget();
            }
            delete child;
        }
    }

    library_load_cached(m_library.get());
    
    refreshRecentAlbums();
    populateArtistList();
    
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
    
    m_config->library_path = lib.toStdString();
    m_config->import_dest_path = dest.toStdString();
    
    config_save(m_config.get());
    
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
    
    if (m_config->import_dest_path.empty()) {
        QMessageBox::warning(this, "Settings Error", "Import Destination Folder is not set in Settings.");
        return;
    }
    
    m_importLogView->clear();
    m_importProgress->setRange(0, 100);
    m_importProgress->setValue(0);
    
    m_importStartBtn->setEnabled(false);
    m_importStopBtn->setEnabled(true);

    ImporterOptions opts;
    opts.sourceDir = src;
    opts.destDir = QString::fromStdString(m_config->import_dest_path);
    opts.dryRun = m_importDryRunChk->isChecked();
    opts.removeSourceFlac = m_importRemoveChk->isChecked();
    opts.overwriteLyrics = !m_importSkipChk->isChecked();
    opts.deleteLrc = m_importReduceChk->isChecked();

    m_importThread = new QThread(this);
    ImporterWorker *worker = new ImporterWorker(opts);
    worker->moveToThread(m_importThread);

    connect(m_importThread, &QThread::started, worker, &ImporterWorker::process);
    connect(worker, &ImporterWorker::logMessage, this, [this](const QString &msg) {
        m_importLogView->appendPlainText(msg);
        m_importLogView->verticalScrollBar()->setValue(m_importLogView->verticalScrollBar()->maximum());
    });
    connect(worker, &ImporterWorker::progressUpdated, this, [this](int value, int maximum) {
        m_importProgress->setRange(0, maximum);
        m_importProgress->setValue(value);
    });
    connect(worker, &ImporterWorker::finished, this, [this, worker]() {
        m_importThread->quit();
        m_importThread->wait();
        worker->deleteLater();
        m_importThread->deleteLater();
        m_importThread = nullptr;

        onImportFinished(0, QProcess::NormalExit);
    });

    m_importThread->start();
}

void MainWindow::onImportStop() {
    if (m_importThread && m_importThread->isRunning()) {
        m_importThread->requestInterruption();
        m_importThread->quit();
        m_importThread->wait();
        m_importThread = nullptr;
        m_importStartBtn->setEnabled(true);
        m_importStopBtn->setEnabled(false);
        m_importLogView->appendPlainText("\n*** Import Cancelled ***");
    }
}

void MainWindow::onImportFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_importStartBtn->setEnabled(true);
    m_importStopBtn->setEnabled(false);
    
    m_importLogView->appendPlainText("\n*** Done. Library Re-scanned. ***");
    
    // Rescan library
    if (!m_config->library_path.empty()) {
        startAsyncScan(QString::fromStdString(m_config->library_path));
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
        "    background-color: #ffffff;"
        "    border: 1px solid #d1d5db;"
        "    border-radius: 8px;"
        "    padding: 10px 14px;"
        "    color: #1a1a1a;"
        "    font-size: 14px;"
        "}"
        "QLineEdit:focus {"
        "    border: 1px solid #3c7fb1;"
        "}"
    );
    homeLayout->addWidget(m_searchEdit);
    
    // Recently Added container
    m_recentAlbumsWidget = new QWidget(homeTab);
    QVBoxLayout *recentLayout = new QVBoxLayout(m_recentAlbumsWidget);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(10);
    
    QLabel *recentHeader = new QLabel("Recently Added Albums", m_recentAlbumsWidget);
    recentHeader->setStyleSheet("font-size: 16px; font-weight: 700; color: #3c7fb1;");
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
            delete child->widget();
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
    QList<Album*> recent = library_get_recent_albums(m_library.get(), limit);
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

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    if (m_ambientBackgroundLbl && m_topPlayerBar) {
        m_ambientBackgroundLbl->setGeometry(m_topPlayerBar->rect());
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_topPlayerBar && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        if (m_ambientBackgroundLbl && m_topPlayerBar) {
            m_ambientBackgroundLbl->setGeometry(m_topPlayerBar->rect());
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_ambientBackgroundLbl && m_topPlayerBar) {
        m_ambientBackgroundLbl->setGeometry(m_topPlayerBar->rect());
    }
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
            
            QString title = QString::fromStdString(song->title);
            QString artist = QString::fromStdString(song->artist);
            QString albumName = QString::fromStdString(song->album);
            
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
    
    Album *album = library_find_album(m_library.get(), song->artist.c_str(), song->album.c_str());
    if (album) {
        setupQueueForAlbum(album, song);
        playSong(song);
    }
}

void MainWindow::onRecentAlbumClicked(Album *album) {
    if (!album) return;
    
    m_tabs->setCurrentIndex(0);
    
    QString artistName = QString::fromStdString(album->artist);
    m_artistList->blockSignals(true);
    QList<QListWidgetItem*> items = m_artistList->findItems(artistName, Qt::MatchExactly);
    if (!items.isEmpty()) {
        m_artistList->setCurrentItem(items.first());
    } else {
        m_artistList->clearSelection();
    }
    m_artistList->blockSignals(false);
    
    m_selectedArtist = artistName;
    m_selectedAlbum = album;
    populateArtistTrackList(album);
}
