#include "resumestate.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QDebug>

ResumeState::ResumeState(const QString &scanRoot)
    : m_scanRoot(QDir::cleanPath(scanRoot))
{
    m_stateFilePath = QDir(m_scanRoot).filePath("_DicomSender/state/sent_records.jsonl");
}

ResumeState::~ResumeState()
{
    if (m_stateFile.isOpen())
        m_stateFile.close();
    if (m_logFile.isOpen())
        m_logFile.close();
}

void ResumeState::load()
{
    m_sentRecords.clear();

    QFile file(m_stateFilePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        QJsonObject obj = doc.object();
        if (obj.value("status").toString() != "OK")
            continue;

        QString rp = obj.value("relative_path").toString();
        qint64 size = obj.value("size").toVariant().toLongLong();
        qint64 mtime = obj.value("last_modified_ms").toVariant().toLongLong();

        if (!rp.isEmpty())
            m_sentRecords[rp] = { size, mtime };
    }

    file.close();
    qDebug() << "ResumeState: loaded" << m_sentRecords.size() << "sent record(s) from" << m_stateFilePath;
}

bool ResumeState::initialize()
{
    QDir root(m_scanRoot);

    if (!root.mkpath("_DicomSender/state") || !root.mkpath("_DicomSender/logs"))
    {
        qDebug() << "ResumeState: failed to create _DicomSender directories under" << m_scanRoot;
        return false;
    }

    // State file: opened for append so each OK record is persisted immediately.
    m_stateFile.setFileName(m_stateFilePath);
    if (!m_stateFile.open(QIODevice::Append | QIODevice::Text))
    {
        qDebug() << "ResumeState: failed to open state file:" << m_stateFilePath;
        return false;
    }

    // Session log file: a new timestamped file is created for each send run.
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    m_logFilePath = root.filePath(QString("_DicomSender/logs/send_%1.log").arg(timestamp));
    m_logFile.setFileName(m_logFilePath);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qDebug() << "ResumeState: failed to open log file:" << m_logFilePath;
        // State file is already open; proceed without a session log.
    }

    return true;
}

bool ResumeState::isAlreadySent(const QString &absPath) const
{
    QString rp = relPath(absPath);
    auto it = m_sentRecords.constFind(rp);
    if (it == m_sentRecords.constEnd())
        return false;

    QFileInfo fi(absPath);
    return it->size == fi.size() &&
           it->lastModifiedMs == fi.lastModified().toMSecsSinceEpoch();
}

void ResumeState::recordSuccess(const QString &absPath, int index, int total, const QString &msg)
{
    QFileInfo fi(absPath);
    QString rp = relPath(absPath);
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);

    appendStateRecord(rp, fi.size(), fi.lastModified().toMSecsSinceEpoch(), "OK", timestamp);
    appendLogRecord(timestamp, index, total, "OK", rp, absPath, msg);
}

void ResumeState::recordFailure(const QString &absPath, int index, int total, const QString &msg)
{
    QString rp = relPath(absPath);
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);

    appendLogRecord(timestamp, index, total, "ERROR", rp, absPath, msg);
}

QString ResumeState::relPath(const QString &absPath) const
{
    QDir root(m_scanRoot);
    return root.relativeFilePath(absPath);
}

void ResumeState::appendStateRecord(const QString &relPath, qint64 size, qint64 mtime,
                                     const QString &status, const QString &timestamp)
{
    if (!m_stateFile.isOpen())
        return;

    QJsonObject obj;
    obj["status"] = status;
    obj["relative_path"] = relPath;
    obj["size"] = size;
    obj["last_modified_ms"] = mtime;
    obj["timestamp"] = timestamp;

    QTextStream out(&m_stateFile);
    out << QJsonDocument(obj).toJson(QJsonDocument::Compact) << "\n";
    out.flush();
    m_stateFile.flush();
}

void ResumeState::appendLogRecord(const QString &timestamp, int index, int total,
                                   const QString &status, const QString &relPath,
                                   const QString &absPath, const QString &msg)
{
    if (!m_logFile.isOpen())
        return;

    QString totalStr = QString("%1").arg(total);
    QString line = QString("[%1] [%2] [%3/%4] relative: %5 | source: %6 | %7")
                    .arg(timestamp)
                    .arg(status)
                    .arg(index, totalStr.length(), 10, QChar('0'))
                    .arg(total)
                    .arg(relPath)
                    .arg(absPath)
                    .arg(msg);

    QTextStream out(&m_logFile);
    out << line << "\n";
    out.flush();
    m_logFile.flush();
}
