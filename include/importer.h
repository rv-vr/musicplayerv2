#ifndef IMPORTER_H
#define IMPORTER_H

#include <QString>
#include <QObject>

struct ImporterOptions {
    QString sourceDir;
    QString destDir;
    bool dryRun = false;
    bool removeSourceFlac = false;
    bool overwriteLyrics = false;
    bool deleteLrc = false;
};

class ImporterWorker : public QObject {
    Q_OBJECT
public:
    explicit ImporterWorker(const ImporterOptions &options, QObject *parent = nullptr);

public slots:
    void process();

signals:
    void logMessage(const QString &msg);
    void progressUpdated(int value, int maximum);
    void finished();

private:
    ImporterOptions m_options;
};

#endif // IMPORTER_H
