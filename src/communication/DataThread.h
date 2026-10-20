#pragma once

#include <QMutex>
#include <QQueue>
#include <QScopedPointer>
#include <QSemaphore>
#include <QThread>
#include "IDataSource.h"

class DataThread : public QThread {
    Q_OBJECT
public:
    explicit DataThread(QObject* parent = nullptr);
    // Destroy in the object's owning thread, never in a direct worker callback.
    ~DataThread() override;
    static constexpr int MaximumPendingWrites = 32;
    static constexpr int MaximumWriteBytes = 65536;
    static constexpr int PollMSecs = 10;

public slots:
    // Stopped/joined only; source ownership swaps. Do not use a transferred source.
    bool configure(QScopedPointer<IDataSource>& source);
    void start(QThread::Priority priority = QThread::InheritPriority);
    void setTimeout(qint64 milliseconds); // Active write/drain budget only.
    bool write(const QByteArray& message); // Acceptance, not remote execution.
    void stop();

signals:
    void connected();
    void readyRead(const QByteArray& bytes); // Raw chunks; no framing/correlation.
    // One result per accepted write, in FIFO order; false may follow partial I/O.
    void writeFinished(const QByteArray& message, bool written);
    void errorOccured(const QString& message);
    void readyToWrite(); // Previous raw output drained; no response implied.

private:
    mutable QMutex m_mtx;
    QQueue<QByteArray> m_pending;
    QSemaphore m_sem;
    QScopedPointer<IDataSource> m_dataSource;
    bool m_started{false};
    bool m_stopRequested{false};
    int m_timeout{500};

    void startLocked(QThread::Priority priority);
    bool stopping() const;
    bool readAvailable();
    bool writeAll(const QByteArray& message, int timeout);
    void run() override;
};
