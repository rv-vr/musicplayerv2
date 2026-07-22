#include "importer.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QDebug>

#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>

static QString sanitizePathPart(const QString &name) {
    if (name.isEmpty()) return "Unknown";
    static QRegularExpression invalidChars(R"([<>:"/\\|?*])");
    QString clean = name;
    clean.replace(invalidChars, "-");
    return clean.trimmed();
}

static QString sanitizeFilename(const QString &name) {
    QFileInfo fi(name);
    QString stem = fi.completeBaseName();
    QString suffix = fi.suffix();
    static QRegularExpression invalidChars(R"([<>:"/\\|?*])");
    stem.replace(invalidChars, "_");
    stem = stem.trimmed();
    if (suffix.isEmpty()) return stem;
    return stem + "." + suffix;
}

ImporterWorker::ImporterWorker(const ImporterOptions &options, QObject *parent)
    : QObject(parent), m_options(options) {}

void ImporterWorker::process() {
    emit logMessage("Starting import process...");
    
    QDir srcDir(m_options.sourceDir);
    if (!srcDir.exists()) {
        emit logMessage("Error: Source directory does not exist.");
        emit finished();
        return;
    }

    QDir destDir(m_options.destDir);
    if (!destDir.exists()) {
        destDir.mkpath(m_options.destDir);
    }

    // Locate all FLAC files
    QStringList flacFiles;
    QDirIterator it(m_options.sourceDir, QStringList() << "*.flac", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        flacFiles.append(it.next());
    }

    if (flacFiles.isEmpty()) {
        emit logMessage("No .flac files found in source directory.");
        emit finished();
        return;
    }

    emit logMessage(QString("Found %1 FLAC file(s) to process.").arg(flacFiles.size()));
    emit progressUpdated(0, flacFiles.size());

    QString qaacExe = QDir::homePath() + "/.wine/drive_c/qaac/qaac64.exe";
    const QString explicitPattern = " [Explicit]";

    for (int i = 0; i < flacFiles.size(); ++i) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            emit logMessage("Import cancelled by user.");
            break;
        }
        const QString &flacPath = flacFiles[i];
        QFileInfo fi(flacPath);
        QString stem = fi.completeBaseName();

        emit logMessage(QString("[%1/%2] Processing %3...").arg(i + 1).arg(flacFiles.size()).arg(fi.fileName()));

        // 1. Merge LRC lyrics if present
        QString lrcPath = fi.absolutePath() + "/" + stem + ".lrc";
        if (!QFile::exists(lrcPath)) {
            lrcPath = flacPath + ".lrc";
        }

        TagLib::FLAC::File flacFile(flacPath.toUtf8().constData());
        if (flacFile.isValid()) {
            TagLib::Ogg::XiphComment *comment = flacFile.xiphComment(true);
            bool tagChanged = false;

            if (QFile::exists(lrcPath) && comment) {
                bool hasLyrics = comment->fieldListMap().contains("UNSYNCEDLYRICS") ||
                                 comment->fieldListMap().contains("LYRICS");
                if (m_options.overwriteLyrics || !hasLyrics) {
                    QFile lrcFile(lrcPath);
                    if (lrcFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        QString lyricsText = QString::fromUtf8(lrcFile.readAll());
                        comment->addField("UNSYNCEDLYRICS", TagLib::String(lyricsText.toUtf8().constData(), TagLib::String::UTF8), true);
                        tagChanged = true;
                        emit logMessage(QString("  └─ Embedded lyrics from %1").arg(QFileInfo(lrcPath).fileName()));
                    }
                }
                if (m_options.deleteLrc && !m_options.dryRun) {
                    QFile::remove(lrcPath);
                    emit logMessage(QString("  └─ Deleted %1").arg(QFileInfo(lrcPath).fileName()));
                }
            }

            // 2. Remove [Explicit] from Title and Album metadata
            if (comment) {
                if (comment->fieldListMap().contains("TITLE")) {
                    TagLib::String title = comment->fieldListMap()["TITLE"].front();
                    QString qTitle = QString::fromUtf8(title.to8Bit(true).c_str());
                    if (qTitle.contains(explicitPattern)) {
                        qTitle.remove(explicitPattern);
                        comment->addField("TITLE", TagLib::String(qTitle.toUtf8().constData(), TagLib::String::UTF8), true);
                        tagChanged = true;
                    }
                }
                if (comment->fieldListMap().contains("ALBUM")) {
                    TagLib::String album = comment->fieldListMap()["ALBUM"].front();
                    QString qAlbum = QString::fromUtf8(album.to8Bit(true).c_str());
                    if (qAlbum.contains(explicitPattern)) {
                        qAlbum.remove(explicitPattern);
                        comment->addField("ALBUM", TagLib::String(qAlbum.toUtf8().constData(), TagLib::String::UTF8), true);
                        tagChanged = true;
                    }
                }
            }

            if (tagChanged && !m_options.dryRun) {
                flacFile.save();
            }

            // Extract artist and album for target directory structure
            QString artist = "Unknown Artist";
            QString album = "Unknown Album";
            if (comment) {
                if (comment->fieldListMap().contains("ALBUMARTIST")) {
                    artist = QString::fromUtf8(comment->fieldListMap()["ALBUMARTIST"].front().to8Bit(true).c_str());
                } else if (comment->fieldListMap().contains("ARTIST")) {
                    artist = QString::fromUtf8(comment->fieldListMap()["ARTIST"].front().to8Bit(true).c_str());
                }
                if (comment->fieldListMap().contains("ALBUM")) {
                    album = QString::fromUtf8(comment->fieldListMap()["ALBUM"].front().to8Bit(true).c_str());
                }
            }

            QString cleanArtist = sanitizePathPart(artist);
            QString cleanAlbum = sanitizePathPart(album);

            QString outStem = stem;
            outStem.remove(explicitPattern);
            QString outFileName = sanitizeFilename(outStem + ".m4a");

            QString targetSubDir = m_options.destDir + "/" + cleanArtist + "/" + cleanAlbum;
            QString targetFilePath = targetSubDir + "/" + outFileName;

            if (m_options.dryRun) {
                emit logMessage(QString("  [DRY RUN] Would convert -> %1/%2/%3").arg(cleanArtist, cleanAlbum, outFileName));
                if (m_options.removeSourceFlac) {
                    emit logMessage("  [DRY RUN] Would delete source FLAC");
                }
            } else {
                QDir().mkpath(targetSubDir);

                // Execute QAAC with nu774 recommended flags: -q 2 -v 0 -o <output>
                QProcess proc;
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                env.insert("WINEDEBUG", "-all");
                proc.setProcessEnvironment(env);

                QStringList args;
                args << qaacExe;
                args << "-q" << "2";
                args << "-v" << "0";
                args << "--copy-artwork";
                args << "-o" << targetFilePath;
                args << flacPath;

                proc.start("wine", args);
                if (proc.waitForFinished(120000) && proc.exitCode() == 0 && QFile::exists(targetFilePath)) {
                    emit logMessage(QString("  └─ Converted to %1/%2/%3").arg(cleanArtist, cleanAlbum, outFileName));
                    if (m_options.removeSourceFlac) {
                        QFile::remove(flacPath);
                        emit logMessage("  └─ Deleted source FLAC.");
                    }
                } else {
                    emit logMessage(QString("  └─ Error converting %1 via QAAC.").arg(fi.fileName()));
                }
            }
        } else {
            emit logMessage(QString("  └─ Invalid FLAC file %1").arg(fi.fileName()));
        }

        emit progressUpdated(i + 1, flacFiles.size());
    }

    emit logMessage("\n*** Import & Transcode Complete ***");
    emit finished();
}
