#pragma once

#include <QString>
#include <QHash>
#include <QFile>

struct SentRecord {
    qint64 size;
    qint64 lastModifiedMs;
};

class ResumeState
{
public:
    explicit ResumeState(const QString &scanRoot);
    ~ResumeState();

    // Load existing successfully-sent records from state file.
    // Call this before initialize() so the read happens before the append handle is opened.
    void load();

    // Create _DicomSender/state and logs directories, open the state file for
    // append and a timestamped log file for writing.  Returns false if the
    // directories or files cannot be created (sending still continues without
    // persistent state).
    bool initialize();

    // Returns true if absPath was already successfully sent (relative path,
    // file size and last-modified timestamp all match a previous OK record).
    bool isAlreadySent(const QString &absPath) const;

    // Append an OK record to the state file and a line to the session log.
    void recordSuccess(const QString &absPath, int index, int total, const QString &msg);

    // Append an ERROR line to the session log (no state-file entry on failure).
    void recordFailure(const QString &absPath, int index, int total, const QString &msg);

private:
    QString relPath(const QString &absPath) const;
    void appendStateRecord(const QString &relPath, qint64 size, qint64 mtime,
                           const QString &status, const QString &timestamp);
    void appendLogRecord(const QString &timestamp, int index, int total,
                         const QString &status, const QString &relPath,
                         const QString &absPath, const QString &msg);

    QString m_scanRoot;
    QString m_stateFilePath;
    QString m_logFilePath;
    QHash<QString, SentRecord> m_sentRecords; // key = relative path

    QFile m_stateFile;  // kept open for append during a send session
    QFile m_logFile;    // kept open for write during a send session
};
