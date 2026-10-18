#include "DataThread.h"
#include "PICTextReceiver.h"

#include <QPointer>
#include <QSignalSpy>
#include <QWaitCondition>
#include <QtTest>
#include <cstring>
#include <memory>

using PICTextProtocol::Kind;
using PICTextProtocol::Response;

namespace {
// Adapted from the existing DataThread fake-provider/device fixture.
struct Shared {
    QMutex mutex;
    QWaitCondition changed;
    QSemaphore consumed;
    QQueue<QByteArray> chunks;
    QByteArray input, output;
    QThread* deviceThread{nullptr};
    int created{0}, destroyed{0}, closed{0}, affinityErrors{0};
    void inject(const QList<QByteArray>& bytes) {
        QMutexLocker lock(&mutex);
        for (const auto& chunk : bytes) chunks.enqueue(chunk);
        changed.wakeAll();
    }
};

class FakeDevice : public QIODevice {
public:
    explicit FakeDevice(Shared& state) : s(state) {
        { QMutexLocker lock(&s.mutex); s.deviceThread = QThread::currentThread(); ++s.created; s.input.clear(); }
        QIODevice::open(ReadWrite | Unbuffered);
    }
    ~FakeDevice() override { checkThread(); QMutexLocker lock(&s.mutex); ++s.destroyed; }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override {
        checkThread(); QMutexLocker lock(&s.mutex);
        return s.input.size() + QIODevice::bytesAvailable();
    }
    void close() override {
        checkThread();
        if (!isOpen()) return;
        QIODevice::close(); QMutexLocker lock(&s.mutex); ++s.closed;
    }
    bool waitForReadyRead(int ms) override {
        checkThread(); QMutexLocker lock(&s.mutex);
        if (s.input.isEmpty() && s.chunks.isEmpty()) s.changed.wait(&s.mutex, static_cast<unsigned long>(ms));
        if (s.input.isEmpty() && !s.chunks.isEmpty()) s.input = s.chunks.dequeue();
        return !s.input.isEmpty();
    }
protected:
    qint64 readData(char* data, qint64 maximum) override {
        checkThread(); QMutexLocker lock(&s.mutex);
        const qint64 count = qMin<qint64>(maximum, s.input.size());
        std::memcpy(data, s.input.constData(), size_t(count)); s.input.remove(0, count);
        if (count) s.consumed.release();
        return count;
    }
    qint64 writeData(const char* data, qint64 count) override {
        checkThread(); QMutexLocker lock(&s.mutex); s.output.append(data, count); return count;
    }
private:
    Shared& s;
    void checkThread() const {
        if (thread() != QThread::currentThread()) { QMutexLocker lock(&s.mutex); ++s.affinityErrors; }
    }
};

class FakeSource : public IDataSource {
public:
    explicit FakeSource(Shared& state) : s(state) {}
    void init(const QVariantMap&) override {}
    QIODevice* createAndConnect() override { port = new FakeDevice(s); return port; }
    void close() override { if (port) port->close(); }
    qint64 write(const QByteArray& bytes) override { return port->write(bytes); }
    qint64 read(char* data, qint64 maximum) override { return port->read(data, maximum); }
    qint64 bytesAvailable() const override { return port->bytesAvailable(); }
    qint64 bytesToWrite() const override { return 0; }
    bool waitForReadyRead(int ms) override { return port->waitForReadyRead(ms); }
    bool waitForBytesWritten(int) override { return false; } // Output is accepted synchronously.
    bool hasError() const override { return !port || !port->isOpen(); }
    QString errorString() const override { return {}; }
private:
    Shared& s;
    QPointer<FakeDevice> port;
};

struct Fixture {
    Shared state;
    DataThread worker;
    PICTextReceiver receiver{worker};
    QSignalSpy responses{&receiver, &PICTextReceiver::responseReceived};
    QSignalSpy discarded{&receiver, &PICTextReceiver::framesDiscarded};
    QSignalSpy raw{&worker, &DataThread::readyRead};
    QSignalSpy writes{&worker, &DataThread::writeFinished};
    Fixture() {
        QScopedPointer<IDataSource> source(new FakeSource(state));
        if (!worker.configure(source)) qFatal("Fixture configure failed");
    }
    ~Fixture() { worker.stop(); worker.wait(); }
    void join() { worker.stop(); if (!worker.wait(2000)) qFatal("Worker did not join"); }
    Response response(qsizetype index) const { return qvariant_cast<Response>(responses.at(index).at(0)); }
    void deliverQueuedInput() { QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall); }
};
}

class PICTextReceiverTests : public QObject {
    Q_OBJECT
private slots:
    void fragmentedInput();
    void twoResponsesOneChunk();
    void multipleChunksOrder();
    void forwardsResponseKind_data();
    void forwardsResponseKind();
    void trailingPartial();
    void sessionBoundary_data();
    void sessionBoundary();
    void manualReset();
    void encodedOutgoing_data();
    void encodedOutgoing();
    void receiverThreadAndQueuedResponse();
    void destructionWithPendingCallbacks();
    void discardNotification();
};

void PICTextReceiverTests::fragmentedInput() {
    Fixture f; f.state.inject({"K0017", " 01", "64\r"}); f.worker.start();
    QTRY_COMPARE_WITH_TIMEOUT(f.responses.count(), 1, 2000); f.join();
    QCOMPARE(f.response(0).kind, Kind::ParameterValue);
    QCOMPARE(f.response(0).parameter, quint16(0x0017)); QCOMPARE(f.response(0).value, quint16(356));
    QCOMPARE(f.discarded.count(), 0); QCOMPARE(f.state.affinityErrors, 0);
}

void PICTextReceiverTests::twoResponsesOneChunk() {
    Fixture f; f.state.inject({"K0017 0164\rK0021 000C\r"}); f.worker.start();
    QTRY_COMPARE_WITH_TIMEOUT(f.responses.count(), 2, 2000); f.join();
    QCOMPARE(f.response(0).parameter, quint16(0x0017)); QCOMPARE(f.response(1).parameter, quint16(0x0021));
}

void PICTextReceiverTests::multipleChunksOrder() {
    Fixture f; f.state.inject({"K0017 01", "64\rE0001\rK0021", " 000C\r"}); f.worker.start();
    QTRY_COMPARE_WITH_TIMEOUT(f.responses.count(), 3, 2000); f.join();
    QCOMPARE(f.response(0).parameter, quint16(0x0017));
    QCOMPARE(f.response(1).kind, Kind::DeviceError); QCOMPARE(f.response(1).errorCode, quint16(1));
    QCOMPARE(f.response(2).parameter, quint16(0x0021));
}

void PICTextReceiverTests::forwardsResponseKind_data() {
    QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<int>("kind");
    QTest::newRow("documented-device-error") << QByteArray("E0002\r") << int(Kind::DeviceError);
    QTest::newRow("malformed-complete-frame") << QByteArray("bad\r") << int(Kind::Malformed);
}

void PICTextReceiverTests::forwardsResponseKind() {
    QFETCH(QByteArray, bytes); QFETCH(int, kind);
    Fixture f; f.state.inject({bytes}); f.worker.start();
    QTRY_COMPARE_WITH_TIMEOUT(f.responses.count(), 1, 2000); f.join();
    QCOMPARE(int(f.response(0).kind), kind);
    if (kind == int(Kind::DeviceError)) QCOMPARE(f.response(0).errorCode, quint16(2));
}

void PICTextReceiverTests::trailingPartial() {
    Fixture f; f.state.inject({"K0017 0164\rE00"}); f.worker.start();
    QTRY_COMPARE_WITH_TIMEOUT(f.raw.count(), 1, 2000); f.deliverQueuedInput();
    QCOMPARE(f.responses.count(), 1); QCOMPARE(f.response(0).parameter, quint16(0x0017));
    f.state.inject({"01\r"});
    QTRY_COMPARE_WITH_TIMEOUT(f.responses.count(), 2, 2000); f.join();
    QCOMPARE(f.response(1).kind, Kind::DeviceError); QCOMPARE(f.response(1).errorCode, quint16(1));
}

void PICTextReceiverTests::sessionBoundary_data() {
    QTest::addColumn<QByteArray>("oldBytes"); QTest::addColumn<int>("discards");
    QTest::newRow("old-partial") << QByteArray("K0017") << 0;
    QTest::newRow("old-discard-state") << QByteArray(12, 'x') << 1;
}

void PICTextReceiverTests::sessionBoundary() {
    QFETCH(QByteArray, oldBytes); QFETCH(int, discards);
    Fixture f; f.state.inject({oldBytes}); f.worker.start();
    QVERIFY(f.state.consumed.tryAcquire(1, 2000)); f.join();
    // Deliberately do not process receiver events between stopped/joined sessions.
    f.state.inject({"K0021 000C\r"}); f.worker.start();
    QVERIFY(f.state.consumed.tryAcquire(1, 2000)); f.join();
    QCOMPARE(f.responses.count(), 0); f.deliverQueuedInput();
    QCOMPARE(f.responses.count(), 1); QCOMPARE(f.response(0).kind, Kind::ParameterValue);
    QCOMPARE(f.response(0).parameter, quint16(0x0021)); QCOMPARE(f.response(0).value, quint16(12));
    QCOMPARE(f.discarded.count(), discards); QCOMPARE(f.state.created, 2); QCOMPARE(f.state.destroyed, 2);
}

void PICTextReceiverTests::manualReset() {
    Fixture f; f.state.inject({"K0017"}); f.worker.start();
    QTRY_COMPARE_WITH_TIMEOUT(f.raw.count(), 1, 2000); f.deliverQueuedInput();
    QCOMPARE(f.responses.count(), 0); f.receiver.reset();
    f.state.inject({"E0001\r"});
    QTRY_COMPARE_WITH_TIMEOUT(f.responses.count(), 1, 2000); f.join();
    QCOMPARE(f.response(0).kind, Kind::DeviceError);
}

void PICTextReceiverTests::encodedOutgoing_data() {
    QTest::addColumn<QByteArray>("command");
    QTest::newRow("J-command") << PICTextProtocol::encodeRead(0x0017);
    QTest::newRow("P-command") << PICTextProtocol::encodeWrite(0x0017, 0x0384);
}

void PICTextReceiverTests::encodedOutgoing() {
    QFETCH(QByteArray, command); Fixture f;
    QVERIFY(f.worker.write(command));
    QTRY_COMPARE_WITH_TIMEOUT(f.writes.count(), 1, 2000); f.join(); f.deliverQueuedInput();
    QCOMPARE(f.state.output, command); QVERIFY(f.writes.at(0).at(1).toBool());
    QCOMPARE(f.responses.count(), 0); // Output drain does not synthesize a PIC acknowledgement.
}

void PICTextReceiverTests::receiverThreadAndQueuedResponse() {
    Fixture f; QThread* emittedIn = nullptr; QList<Response> queued; QObject consumer;
    connect(&f.receiver, &PICTextReceiver::responseReceived, &f.receiver,
        [&](const Response&) { emittedIn = QThread::currentThread(); }, Qt::DirectConnection);
    connect(&f.receiver, &PICTextReceiver::responseReceived, &consumer,
        [&](const Response& response) { queued.append(response); }, Qt::QueuedConnection);
    f.state.inject({"K0017 0164\r"}); f.worker.start();
    QTRY_COMPARE_WITH_TIMEOUT(queued.size(), 1, 2000); f.join();
    QCOMPARE(emittedIn, f.receiver.thread()); QCOMPARE(emittedIn, QThread::currentThread());
    QCOMPARE(f.state.deviceThread, &f.worker); QCOMPARE(f.state.affinityErrors, 0);
    QCOMPARE(queued[0].kind, Kind::ParameterValue); QCOMPARE(queued[0].value, quint16(356));
}

void PICTextReceiverTests::destructionWithPendingCallbacks() {
    Shared state; DataThread worker;
    QScopedPointer<IDataSource> source(new FakeSource(state)); QVERIFY(worker.configure(source));
    auto owner = std::make_unique<QObject>();
    auto* receiver = new PICTextReceiver(worker, owner.get()); QPointer<PICTextReceiver> observer(receiver);
    QObject consumer; int responses = 0;
    connect(receiver, &PICTextReceiver::responseReceived, &consumer, [&](const Response&) { ++responses; });
    state.inject({"K0017 0164\r"}); worker.start(); QVERIFY(state.consumed.tryAcquire(1, 2000));
    owner.reset(); QVERIFY(observer.isNull()); // Delete in owner thread while raw callbacks are pending.
    state.inject({"E0001\r"}); QVERIFY(state.consumed.tryAcquire(1, 2000));
    worker.stop(); QVERIFY(worker.wait(2000)); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(responses, 0); QCOMPARE(state.created, 1); QCOMPARE(state.destroyed, 1);
    QCOMPARE(state.closed, 1); QCOMPARE(state.affinityErrors, 0);
}

void PICTextReceiverTests::discardNotification() {
    Fixture f; f.state.inject({QByteArray(12, 'x') + "\rE0000\r"}); f.worker.start();
    QTRY_COMPARE_WITH_TIMEOUT(f.responses.count(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(f.discarded.count(), 1, 2000); f.join();
    QCOMPARE(f.discarded.at(0).at(0).value<qsizetype>(), qsizetype(1));
    QCOMPARE(f.response(0).kind, Kind::DeviceError); QCOMPARE(f.response(0).errorCode, quint16(0));
}

QTEST_GUILESS_MAIN(PICTextReceiverTests)
#include "tst_pictextreceiver.moc"
