#include "DataThread.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QWaitCondition>
#include <QtTest>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <algorithm>

namespace {
struct Shared {
    QMutex mutex;
    QWaitCondition changed;
    QSemaphore readEntered, writeEntered;
    QByteArray incoming, output;
    QVector<QByteArray> fragments;
    QVector<int> readWaits, writeWaits;
    QThread* executionThread{nullptr};
    int created{0}, destroyed{0}, closed{0}, sourceDestroyed{0};
    int affinityErrors{0}, bypasses{0}, reads{0}, writes{0}, closeDelegations{0};
    int writeChunk{65536};
    qint64 pending{0};
    bool buffered{false}, holdWrite{false}, failWrite{false}, zeroWrite{false};
    bool failRead{false}, faultOnWait{false}, fault{false};
    bool failOpen{false}, throwOpen{false};
    QString error;
    void inject(const QByteArray& bytes) { QMutexLocker lock(&mutex); incoming += bytes; changed.wakeAll(); }
    void releaseWrite() { QMutexLocker lock(&mutex); holdWrite = false; changed.wakeAll(); }
};

class FakeDevice : public QIODevice {
public:
    explicit FakeDevice(Shared& state) : s(state) {
        QIODevice::open(ReadWrite | Unbuffered);
    }
    ~FakeDevice() override {
        QMutexLocker lock(&s.mutex);
        if (thread() != QThread::currentThread()) ++s.affinityErrors;
        ++s.destroyed;
    }
    bool delegated{false};
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override {
        check(); QMutexLocker lock(&s.mutex);
        return s.incoming.size() + QIODevice::bytesAvailable();
    }
    qint64 bytesToWrite() const override { check(); QMutexLocker lock(&s.mutex); return s.pending; }
    void close() override {
        check(); QIODevice::close();
        QMutexLocker lock(&s.mutex); ++s.closed; s.pending = 0;
    }
    bool waitForReadyRead(int ms) override {
        check(); QMutexLocker lock(&s.mutex); s.readWaits.append(ms);
        if (!readNotified) { s.readEntered.release(); readNotified = true; }
        if (s.faultOnWait) { s.fault = true; s.error = QStringLiteral("injected wait fault"); return false; }
        if (s.incoming.isEmpty() && !s.fragments.isEmpty()) s.incoming += s.fragments.takeFirst();
        if (s.incoming.isEmpty()) s.changed.wait(&s.mutex, static_cast<unsigned long>(ms));
        return !s.incoming.isEmpty();
    }
    bool waitForBytesWritten(int ms) override {
        check(); QMutexLocker lock(&s.mutex); s.writeWaits.append(ms);
        if (!writeNotified) { s.writeEntered.release(); writeNotified = true; }
        if (s.holdWrite) s.changed.wait(&s.mutex, static_cast<unsigned long>(ms));
        if (s.holdWrite) return false;
        const bool progressed = s.pending > 0;
        s.pending = 0;
        return progressed;
    }
protected:
    qint64 readData(char* bytes, qint64 maximum) override {
        check(); QMutexLocker lock(&s.mutex); ++s.reads;
        if (s.failRead) { s.error = QStringLiteral("injected read failure"); s.fault = true; return -1; }
        const qint64 count = qMin<qint64>(maximum, s.incoming.size());
        std::memcpy(bytes, s.incoming.constData(), size_t(count)); s.incoming.remove(0, count);
        return count;
    }
    qint64 writeData(const char* bytes, qint64 maximum) override {
        check(); QMutexLocker lock(&s.mutex); ++s.writes;
        if (s.failWrite) { s.error = QStringLiteral("injected write failure"); s.fault = true; return -1; }
        if (s.zeroWrite) return 0;
        const qint64 count = qMin<qint64>(maximum, s.writeChunk);
        s.output.append(bytes, count);
        if (s.buffered) s.pending += count;
        return count;
    }
private:
    Shared& s;
    bool readNotified{false}, writeNotified{false};
    void check() const {
        QMutexLocker lock(&s.mutex);
        if (thread() != QThread::currentThread()) ++s.affinityErrors;
        if (!delegated) ++s.bypasses;
    }
};

class FakeSource : public IDataSource {
public:
    explicit FakeSource(Shared& state) : s(state) {}
    ~FakeSource() override { QMutexLocker lock(&s.mutex); ++s.sourceDestroyed; }
    void init(const QVariantMap&) override {}
    QIODevice* createAndConnect() override {
        QMutexLocker lock(&s.mutex);
        s.executionThread = QThread::currentThread();
        if (s.throwOpen) throw std::runtime_error("injected creation exception");
        if (s.failOpen) { s.error = QStringLiteral("injected open failure"); return nullptr; }
        s.fault = false; s.error.clear(); s.pending = 0;
        port = new FakeDevice(s); ++s.created;
        return port;
    }
    void close() override {
        { QMutexLocker lock(&s.mutex); ++s.closeDelegations; }
        if (port && port->isOpen()) { Call call(port); port->close(); }
    }
    qint64 write(const QByteArray& bytes) override { Call call(port); return port->write(bytes); }
    qint64 read(char* bytes, qint64 maximum) override { Call call(port); return port->read(bytes, maximum); }
    qint64 bytesAvailable() const override { Call call(port); return port->bytesAvailable(); }
    qint64 bytesToWrite() const override { Call call(port); return port->bytesToWrite(); }
    bool waitForReadyRead(int ms) override { Call call(port); return port->waitForReadyRead(ms); }
    bool waitForBytesWritten(int ms) override { Call call(port); return port->waitForBytesWritten(ms); }
    bool hasError() const override { QMutexLocker lock(&s.mutex); return s.fault || !port || !port->isOpen(); }
    QString errorString() const override { QMutexLocker lock(&s.mutex); return s.error; }
private:
    Shared& s;
    QPointer<FakeDevice> port;
    struct Call {
        explicit Call(FakeDevice* device) : port(device) { port->delegated = true; }
        ~Call() { port->delegated = false; }
        FakeDevice* port;
    };
};

struct Fixture {
    Shared state;
    DataThread worker;
    QSignalSpy connected{&worker, &DataThread::connected};
    QSignalSpy reads{&worker, &DataThread::readyRead};
    QSignalSpy results{&worker, &DataThread::writeFinished};
    QSignalSpy errors{&worker, &DataThread::errorOccured};
    Fixture() {
        QScopedPointer<IDataSource> source(new FakeSource(state));
        if (!worker.configure(source)) qFatal("Fixture configure failed");
    }
    ~Fixture() { worker.stop(); worker.wait(); }
    void join() { worker.stop(); if (!worker.wait(2000)) qFatal("Worker did not join"); }
    QByteArray received() const { QByteArray bytes; for (const auto& item : reads) bytes += item[0].toByteArray(); return bytes; }
};
}

class DataThreadTests : public QObject {
    Q_OBJECT
private slots:
    void outputAndAffinity();
    void idleRawInputAndFragments();
    void chunkingDoesNotDiscardOrStarveOutput();
    void partialAndBufferedWrite();
    void fifoExactlyOnce();
    void concurrentProducers();
    void limitsAndCancellation();
    void stopIdleAndRestart();
    void stopDuringWrite();
    void stopFromReadSignal();
    void destructionWithPending();
    void failures_data();
    void failures();
    void nullSource();
    void writeDeadline();
    void invalidTimeoutDoesNotChangeBudget();
    void reconfigureWhileRunning();
    void independentWorkers();
    void emptyWrite();
};

void DataThreadTests::outputAndAffinity() {
    Fixture f; QVERIFY(f.worker.write("hello"));
    QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000); f.join();
    QCOMPARE(f.results[0][0].toByteArray(), QByteArray("hello")); QVERIFY(f.results[0][1].toBool());
    QCOMPARE(f.state.output, QByteArray("hello")); QCOMPARE(f.state.created, 1);
    QCOMPARE(f.state.closed, 1); QCOMPARE(f.state.destroyed, 1); QCOMPARE(f.state.closeDelegations, 1);
    QCOMPARE(f.state.affinityErrors, 0); QCOMPARE(f.state.bypasses, 0);
    QVERIFY(f.state.executionThread != QThread::currentThread());
}

void DataThreadTests::idleRawInputAndFragments() {
    Fixture f; f.state.incoming = "old";
    f.state.fragments = {"a\r", QByteArray("\0b", 2), "tail"};
    f.worker.start();
    QTRY_COMPARE_WITH_TIMEOUT(f.received(), QByteArray("olda\r\0btail", 11), 2000);
    QVERIFY(f.worker.write("send")); QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000); f.join();
    QCOMPARE(f.state.output, QByteArray("send")); QCOMPARE(f.state.bypasses, 0);
    for (int ms : f.state.readWaits) QVERIFY(ms > 0 && ms <= DataThread::PollMSecs);
}

void DataThreadTests::chunkingDoesNotDiscardOrStarveOutput() {
    Fixture f; f.state.incoming = QByteArray(20000, 'x');
    QVERIFY(f.worker.write("out"));
    QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(f.received().size(), 20000, 2000); f.join();
    QCOMPARE(f.received(), QByteArray(20000, 'x')); QCOMPARE(f.state.output, QByteArray("out"));
    for (const auto& item : f.reads) QVERIFY(item[0].toByteArray().size() <= 4096);
}

void DataThreadTests::partialAndBufferedWrite() {
    Fixture f; f.state.writeChunk = 2; f.state.buffered = true;
    QVERIFY(f.worker.write("abcdef"));
    QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000); f.join();
    QVERIFY(f.results[0][1].toBool()); QCOMPARE(f.state.output, QByteArray("abcdef"));
    QCOMPARE(f.state.writes, 3); QCOMPARE(f.state.pending, qint64(0)); QVERIFY(!f.state.writeWaits.isEmpty());
    for (int ms : f.state.writeWaits) QVERIFY(ms > 0 && ms <= DataThread::PollMSecs);
}

void DataThreadTests::fifoExactlyOnce() {
    Fixture f; f.state.buffered = true; f.state.holdWrite = true;
    QVERIFY(f.worker.write("first")); QVERIFY(f.state.writeEntered.tryAcquire(1, 2000));
    QVERIFY(f.worker.write("second")); QVERIFY(f.worker.write("third")); f.state.releaseWrite();
    QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 3, 2000); f.join();
    QCOMPARE(f.results[0][0].toByteArray(), QByteArray("first"));
    QCOMPARE(f.results[1][0].toByteArray(), QByteArray("second"));
    QCOMPARE(f.results[2][0].toByteArray(), QByteArray("third"));
    for (const auto& item : f.results) QVERIFY(item[1].toBool());
    QCOMPARE(f.state.output, QByteArray("firstsecondthird")); QCOMPARE(f.state.writes, 3);
}

void DataThreadTests::concurrentProducers() {
    Fixture f; f.state.buffered = true; f.state.holdWrite = true;
    QVERIFY(f.worker.write("first")); QVERIFY(f.state.writeEntered.tryAcquire(1, 2000));
    int accepted[16]{}; std::vector<std::thread> producers;
    for (int i = 0; i < 16; ++i) producers.emplace_back([&, i] { accepted[i] = f.worker.write(QByteArray::number(i)); });
    for (auto& producer : producers) producer.join();
    for (int count : accepted) QCOMPARE(count, 1);
    f.state.releaseWrite(); QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 17, 2000); f.join();
    QList<QByteArray> actual; QByteArray output;
    for (const auto& item : f.results) { QVERIFY(item[1].toBool()); actual += item[0].toByteArray(); output += item[0].toByteArray(); }
    QCOMPARE(f.state.output, output); QCOMPARE(f.state.writes, 17);
    actual.removeFirst(); std::sort(actual.begin(), actual.end()); QList<QByteArray> expected;
    for (int i = 0; i < 16; ++i) expected += QByteArray::number(i);
    std::sort(expected.begin(), expected.end()); QCOMPARE(actual, expected);
}

void DataThreadTests::limitsAndCancellation() {
    Fixture f; f.state.buffered = true; f.state.holdWrite = true;
    QVERIFY(!f.worker.write(QByteArray(DataThread::MaximumWriteBytes + 1, 'x')));
    QVERIFY(f.worker.write("active")); QVERIFY(f.state.writeEntered.tryAcquire(1, 2000));
    for (int i = 0; i < DataThread::MaximumPendingWrites; ++i) QVERIFY(f.worker.write("pending"));
    QVERIFY(!f.worker.write("overflow")); f.join();
    QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), DataThread::MaximumPendingWrites + 1, 2000);
    for (const auto& item : f.results) QVERIFY(!item[1].toBool());
    QCOMPARE(f.state.output, QByteArray("active")); QCOMPARE(f.state.destroyed, 1);
}

void DataThreadTests::stopIdleAndRestart() {
    Fixture f; f.worker.start(); QVERIFY(f.state.readEntered.tryAcquire(1, 2000)); f.join();
    QCOMPARE(f.state.writes, 0); QVERIFY(!f.worker.write("stopped"));
    f.worker.start(); QVERIFY(f.worker.write("next"));
    QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000); f.join();
    QCOMPARE(f.state.created, 2); QCOMPARE(f.state.destroyed, 2); QVERIFY(f.results[0][1].toBool());
}

void DataThreadTests::stopDuringWrite() {
    Fixture f; f.state.buffered = true; f.state.holdWrite = true;
    QVERIFY(f.worker.write("active")); QVERIFY(f.state.writeEntered.tryAcquire(1, 2000));
    QVERIFY(f.worker.write("later")); f.state.inject("during\r");
    QTRY_COMPARE_WITH_TIMEOUT(f.received(), QByteArray("during\r"), 2000);
    f.join(); QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 2, 2000);
    for (const auto& item : f.results) QVERIFY(!item[1].toBool());
    QCOMPARE(f.state.output, QByteArray("active")); QCOMPARE(f.state.bypasses, 0);
}

void DataThreadTests::stopFromReadSignal() {
    Fixture f; f.state.incoming = "notice";
    connect(&f.worker, &DataThread::readyRead, &f.worker, [&] { f.worker.stop(); }, Qt::DirectConnection);
    QVERIFY(f.worker.write("must-not-send")); QVERIFY(f.worker.wait(2000));
    QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000);
    QVERIFY(!f.results[0][1].toBool()); QCOMPARE(f.state.writes, 0);
}

void DataThreadTests::destructionWithPending() {
    Shared state; state.buffered = true; state.holdWrite = true;
    auto worker = std::make_unique<DataThread>();
    QScopedPointer<IDataSource> source(new FakeSource(state)); QVERIFY(worker->configure(source));
    QSignalSpy results(worker.get(), &DataThread::writeFinished);
    QVERIFY(worker->write("active")); QVERIFY(state.writeEntered.tryAcquire(1, 2000));
    QVERIFY(worker->write("later")); worker.reset();
    QCOMPARE(results.count(), 2); for (const auto& item : results) QVERIFY(!item[1].toBool());
    QCOMPARE(state.closed, 1); QCOMPARE(state.destroyed, 1); QCOMPARE(state.sourceDestroyed, 1);
    QCOMPARE(state.affinityErrors, 0); QCOMPARE(state.bypasses, 0);
}

void DataThreadTests::failures_data() {
    QTest::addColumn<QString>("failure");
    for (const auto& name : {"open", "throw", "read", "write", "zero", "wait"}) QTest::newRow(name) << QString::fromLatin1(name);
}

void DataThreadTests::failures() {
    QFETCH(QString, failure); Fixture f;
    f.state.failOpen = failure == "open"; f.state.throwOpen = failure == "throw";
    f.state.failRead = failure == "read"; if (f.state.failRead) f.state.incoming = "input";
    f.state.failWrite = failure == "write"; f.state.zeroWrite = failure == "zero";
    f.state.faultOnWait = failure == "wait";
    QVERIFY(f.worker.write("out"));
    if (failure == "wait") QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000);
    QVERIFY(f.worker.wait(2000)); QTRY_VERIFY_WITH_TIMEOUT(!f.errors.isEmpty(), 2000);
    QCOMPARE(f.results.count(), 1);
    QCOMPARE(f.results[0][1].toBool(), failure == "wait");
    QCOMPARE(f.state.created, f.state.destroyed); QCOMPARE(f.state.affinityErrors, 0); QCOMPARE(f.state.bypasses, 0);
    QVERIFY(!f.worker.write("after-fault"));
}

void DataThreadTests::nullSource() {
    DataThread worker; QSignalSpy result(&worker, &DataThread::writeFinished), error(&worker, &DataThread::errorOccured);
    QVERIFY(worker.write("out")); QVERIFY(worker.wait(2000));
    QCOMPARE(result.count(), 1); QVERIFY(!result[0][1].toBool()); QVERIFY(!error.isEmpty());
}

void DataThreadTests::writeDeadline() {
    Fixture f; f.state.buffered = true; f.state.holdWrite = true; f.worker.setTimeout(30);
    QElapsedTimer timer; timer.start(); QVERIFY(f.worker.write("out"));
    QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000); QVERIFY(f.worker.wait(2000));
    QVERIFY(timer.elapsed() >= 30); QVERIFY(!f.results[0][1].toBool()); QVERIFY(!f.errors.isEmpty());
    QVERIFY(f.errors[0][0].toString().contains(QStringLiteral("deadline")));
    for (int ms : f.state.writeWaits) QVERIFY(ms > 0 && ms <= DataThread::PollMSecs);
}

void DataThreadTests::invalidTimeoutDoesNotChangeBudget() {
    Fixture f; f.worker.setTimeout(30); f.worker.setTimeout(0); f.worker.setTimeout(-1);
    f.state.buffered = true; f.state.holdWrite = true;
    QVERIFY(f.worker.write("out")); QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000);
    QVERIFY(f.worker.wait(2000)); QVERIFY(!f.results[0][1].toBool()); QCOMPARE(f.errors.count(), 3);
}

void DataThreadTests::reconfigureWhileRunning() {
    Shared other; Fixture f; QScopedPointer<IDataSource> source(new FakeSource(other));
    f.worker.start(); QVERIFY(f.state.readEntered.tryAcquire(1, 2000));
    QVERIFY(!f.worker.configure(source)); QVERIFY(source); f.join();
    QVERIFY(f.worker.configure(source)); QVERIFY(source); // Original source returned to caller.
}

void DataThreadTests::independentWorkers() {
    Fixture a, b; a.state.incoming = "left"; b.state.incoming = "right";
    QVERIFY(a.worker.write("A")); QVERIFY(b.worker.write("B"));
    QTRY_COMPARE_WITH_TIMEOUT(a.results.count(), 1, 2000); QTRY_COMPARE_WITH_TIMEOUT(b.results.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(a.received(), QByteArray("left"), 2000); QTRY_COMPARE_WITH_TIMEOUT(b.received(), QByteArray("right"), 2000);
    a.join(); b.join(); QCOMPARE(a.state.output, QByteArray("A")); QCOMPARE(b.state.output, QByteArray("B"));
    QVERIFY(a.state.executionThread != b.state.executionThread);
    QCOMPARE(a.state.affinityErrors + b.state.affinityErrors, 0); QCOMPARE(a.state.bypasses + b.state.bypasses, 0);
}

void DataThreadTests::emptyWrite() {
    Fixture f; QVERIFY(f.worker.write({})); QTRY_COMPARE_WITH_TIMEOUT(f.results.count(), 1, 2000); f.join();
    QVERIFY(f.results[0][1].toBool()); QCOMPARE(f.state.writes, 0);
}

QTEST_GUILESS_MAIN(DataThreadTests)
#include "tst_datathread.moc"
