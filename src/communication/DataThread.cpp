#include "DataThread.h"

#include <QElapsedTimer>
#include <algorithm>
#include <exception>
#include <limits>

DataThread::DataThread(QObject* parent) : QThread(parent) {}

DataThread::~DataThread() {
    stop();
    wait();
}

bool DataThread::configure(QScopedPointer<IDataSource>& source) {
    {
        QMutexLocker lock(&m_mtx);
        if (!m_started && !isRunning()) {
            m_dataSource.swap(source);
            return true;
        }
    }
    emit errorOccured(QStringLiteral("Configure requires a stopped, joined worker."));
    return false;
}

void DataThread::startLocked(QThread::Priority priority) {
    while (m_sem.tryAcquire()) {}
    m_stopRequested = false;
    m_started = true;
    QThread::start(priority);
}

void DataThread::start(QThread::Priority priority) {
    QMutexLocker lock(&m_mtx);
    if (!m_started && !isRunning()) startLocked(priority);
}

void DataThread::setTimeout(qint64 milliseconds) {
    if (milliseconds <= 0 || milliseconds > std::numeric_limits<int>::max()) {
        emit errorOccured(QStringLiteral("Write timeout must be a positive int duration."));
        return;
    }
    QMutexLocker lock(&m_mtx);
    m_timeout = int(milliseconds);
}

bool DataThread::write(const QByteArray& message) {
    {
        QMutexLocker lock(&m_mtx);
        if (!m_stopRequested && message.size() <= MaximumWriteBytes
            && m_pending.size() < MaximumPendingWrites) {
            m_pending.enqueue(message);
            if (!m_started && !isRunning()) startLocked(QThread::LowPriority);
            m_sem.release();
            return true;
        }
    }
    emit errorOccured(QStringLiteral("Write rejected: stopped worker or pending-output limit."));
    return false;
}

void DataThread::stop() {
    QMutexLocker lock(&m_mtx);
    if (!m_stopRequested) {
        m_stopRequested = true;
        m_sem.release();
    }
}

bool DataThread::stopping() const {
    QMutexLocker lock(&m_mtx);
    return m_stopRequested;
}

bool DataThread::readAvailable() {
    if (m_dataSource->hasError()) {
        emit errorOccured(m_dataSource->errorString());
        return false;
    }
    if (!stopping() && m_dataSource->bytesAvailable() > 0) {
        char bytes[4096];
        const qint64 count = m_dataSource->read(bytes, sizeof(bytes));
        if (count <= 0) {
            emit errorOccured(QStringLiteral("Read failed or made no progress: %1").arg(m_dataSource->errorString()));
            return false;
        }
        emit readyRead(QByteArray(bytes, count));
    }
    return true;
}

bool DataThread::writeAll(const QByteArray& message, int timeout) {
    QElapsedTimer timer;
    timer.start();
    qint64 offset = 0;
    while (offset < message.size() || m_dataSource->bytesToWrite() > 0) {
        if (stopping()) return false;
        const int remaining = int(std::clamp<qint64>(timeout - timer.elapsed(), 0, PollMSecs));
        if (!remaining) {
            emit errorOccured(QStringLiteral("Total write deadline expired."));
            return false;
        }
        if (!readAvailable() || stopping()) return false;
        if (offset < message.size()) {
            const qint64 count = m_dataSource->write(message.mid(offset));
            if (count < 0 || (!count && m_dataSource->bytesToWrite() == 0)) {
                emit errorOccured(QStringLiteral("Write failed or made no progress: %1").arg(m_dataSource->errorString()));
                return false;
            }
            offset += count;
        }
        if (m_dataSource->bytesToWrite() > 0) {
            const int wait = int(std::clamp<qint64>(timeout - timer.elapsed(), 0, PollMSecs));
            if (!wait) {
                emit errorOccured(QStringLiteral("Total write deadline expired."));
                return false;
            }
            m_dataSource->waitForBytesWritten(wait);
        }
    }
    if (m_dataSource->hasError()) {
        emit errorOccured(m_dataSource->errorString());
        return false;
    }
    if (timer.elapsed() >= timeout) {
        emit errorOccured(QStringLiteral("Total write deadline expired."));
        return false;
    }
    return !stopping();
}

void DataThread::run() {
    QScopedPointer<QIODevice> device; // Lifetime only; all port operations delegated.
    QString startupError;
    try {
        if (!stopping()) {
            if (!m_dataSource) startupError = QStringLiteral("No IDataSource configured.");
            else {
                device.reset(m_dataSource->createAndConnect());
                if (!device) {
                    startupError = m_dataSource->errorString();
                    if (startupError.isEmpty()) startupError = QStringLiteral("Port creation/open failed.");
                }
            }
        }
    } catch (const std::exception& error) {
        startupError = QStringLiteral("Port creation failed: %1").arg(QString::fromUtf8(error.what()));
    } catch (...) {
        startupError = QStringLiteral("Port creation failed.");
    }
    if (!startupError.isEmpty()) emit errorOccured(startupError);
    if (device && !stopping()) {
        emit connected();
        while (!stopping()) {
            if (!readAvailable()) break;
            const int wait = m_dataSource->bytesAvailable() > 0 ? 0 : PollMSecs;
            if (m_sem.tryAcquire(1, wait)) {
                QByteArray message;
                int timeout;
                {
                    QMutexLocker lock(&m_mtx);
                    if (m_stopRequested) break;
                    if (m_pending.isEmpty()) continue;
                    message = m_pending.dequeue();
                    timeout = m_timeout;
                }
                const bool written = writeAll(message, timeout);
                if (!written) stop();
                emit writeFinished(message, written);
                if (stopping()) break;
                emit readyToWrite();
            } else if (!stopping() && m_dataSource->bytesAvailable() == 0) {
                m_dataSource->waitForReadyRead(PollMSecs);
            }
        }
    }
    stop();
    QQueue<QByteArray> cancelled;
    {
        QMutexLocker lock(&m_mtx);
        cancelled.swap(m_pending);
    }
    for (const auto& message : cancelled) emit writeFinished(message, false);
    if (device) m_dataSource->close();
    device.reset();
    QMutexLocker lock(&m_mtx);
    while (m_sem.tryAcquire()) {}
    m_started = false;
}
