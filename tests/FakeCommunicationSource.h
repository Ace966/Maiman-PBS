#pragma once

#include "IDataSource.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QSharedPointer>
#include <QThread>
#include <QWaitCondition>
#include <cstring>
#include <utility>

namespace TestCommunication {
struct State {
public:
    QMutex mutex;
    QWaitCondition changed;
    QElapsedTimer clock;
    QList<qint64> submissionNs;
    QList<qint64> completionNs;
    State() { clock.start(); }
    QByteArray output, incoming, early;
    QString error;
    qint64 pending{0};
    int created{0},destroyed{0},closed{0},affinityErrors{0},attempts{0},readCalls{0},readWaits{0},writeWaits{0};
    bool failOpen{false},failWrite{false},failRead{false},buffered{false},autoDrain{false},stall{false},noise{false},opened{false};
    int maximum{100};
    QThread* owner{nullptr};
    QByteArray bytes() {
        QMutexLocker lock(&mutex);
        return output;
    }
    int attemptCount() {
        QMutexLocker lock(&mutex);
        return attempts;
    }
    int readCount() {
        QMutexLocker lock(&mutex);
        return readCalls;
    }
    int submissions() {
        QMutexLocker lock(&mutex);
        return submissionNs.size();
    }
    qint64 submitted(int i) {
        QMutexLocker lock(&mutex);
        return submissionNs.at(i);
    }
    qint64 completed(int index) {
        QMutexLocker lock(&mutex);
        return completionNs.value(index, -1);
    }
    qint64 now() { return clock.nsecsElapsed(); }
    // Controllers call these under mutex; they never invoke the transport QObject.
    void input(const QByteArray& data) {
        if (opened) incoming += data;
    }
    void drain() { pending = 0; }
    void fault() {
        if (opened) error = "injected port error";
    }
};

class FakeSource : public IDataSource {
public:
    explicit FakeSource(QSharedPointer<State> state) : s(std::move(state)) {
        QMutexLocker lock(&s->mutex);
        ++s->created;
        s->owner = QThread::currentThread();
        s->incoming.clear();
        s->error.clear();
        s->pending = 0;
        s->opened = false;
    }
    ~FakeSource() override {
        check();
        QMutexLocker lock(&s->mutex);
        ++s->destroyed;
    }
    bool open(const SerialSettings&) override {
        check();
        QMutexLocker lock(&s->mutex);
        if (parent()) ++s->affinityErrors;
        if (s->failOpen) {
            s->error = "open failed";
            return false;
        }
        s->opened = true;
        return true;
    }
    void close() override {
        check();
        QMutexLocker lock(&s->mutex);
        if (s->opened) {
            s->opened = false;
            ++s->closed;
        }
        s->changed.wakeAll();
    }
    qint64 write(const QByteArray& bytes) override {
        check();
        QMutexLocker lock(&s->mutex);
        ++s->attempts;
        if (s->failWrite) {
            s->error = "write failed";
            return -1;
        }
        if (s->stall) return 0;
        const auto count = qMin<qsizetype>(s->maximum, bytes.size());
        if (count > 0) s->submissionNs.append(s->clock.nsecsElapsed());
        s->output += bytes.first(count);
        if (s->buffered) s->pending += count;
        if (!s->early.isEmpty()) {
            s->input(s->early);
            s->early.clear();
        }
        return count;
    }
    qint64 read(char* data, qint64 count) override {
        check();
        QMutexLocker lock(&s->mutex);
        ++s->readCalls;
        if (s->failRead) {
            s->error = "read failed";
            return -1;
        }
        count = qMin<qint64>(count, s->incoming.size());
        std::memcpy(data, s->incoming.constData(), size_t(count));
        s->incoming.remove(0, count);
        return count;
    }
    qint64 bytesAvailable() const override {
        check();
        QMutexLocker lock(&s->mutex);
        return s->incoming.size();
    }
    qint64 bytesToWrite() const override {
        check();
        QMutexLocker lock(&s->mutex);
        return s->pending;
    }
    bool hasError() const override {
        check();
        QMutexLocker lock(&s->mutex);
        return !s->opened || !s->error.isEmpty();
    }
    QString errorString() const override {
        check();
        QMutexLocker lock(&s->mutex);
        return s->error;
    }
    bool waitForReadyRead(int timeoutMs) override {
        check();
        QMutexLocker lock(&s->mutex);
        ++s->readWaits;
        if (timeoutMs <= 0 || timeoutMs > 20) ++s->affinityErrors;
        QDeadlineTimer deadline(timeoutMs);
        while (s->incoming.isEmpty() && s->opened && s->error.isEmpty() && !deadline.hasExpired()) {
            if (s->noise) {
                s->changed.wait(&s->mutex, qMin(5, timeoutMs));
                s->incoming += "x";
                break;
            }
            s->changed.wait(&s->mutex, deadline);
        }
        return !s->incoming.isEmpty();
    }
    bool waitForBytesWritten(int timeoutMs) override {
        check();
        QMutexLocker lock(&s->mutex);
        ++s->writeWaits;
        if (timeoutMs <= 0 || timeoutMs > 20) ++s->affinityErrors;
        if (s->autoDrain) {
            s->pending = 0;
            return true;
        }
        QDeadlineTimer deadline(timeoutMs);
        while (s->pending && s->opened && s->error.isEmpty() && !deadline.hasExpired())
            s->changed.wait(&s->mutex, deadline);
        return s->pending == 0;
    }
private:
    QSharedPointer<State> s;
    void check() const {
        if (thread() != QThread::currentThread()) {
            QMutexLocker lock(&s->mutex);
            ++s->affinityErrors;
        }
    }
};
}
