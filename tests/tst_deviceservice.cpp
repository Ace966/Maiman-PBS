#include "DeviceService.h"
#include "FakeCommunicationSource.h"

#include <QPointer>
#include <QScopedPointer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QtTest>

namespace {
using State = DeviceService::State;
using Outcome = CommunicationWorker::Outcome;
using Result = CommunicationWorker::Result;
using TestCommunication::FakeSource;
using SourceState = TestCommunication::State;

DeviceConfiguration configuration() {
    DeviceConfiguration result(0x2222, QStringLiteral("Selected device"));
    if (!result.addParameter(0x0017, {QStringLiteral("V"), 0.01})
        || !result.addParameter(0x0015, {QStringLiteral("mA"), 0.1, 1, true})
        || !result.addParameter(0x0021, {{}, 0.0})
        || !result.addParameter(0x0801, {})) qFatal("Invalid service fixture");
    return result;
}

struct Fixture {
public:
    QSharedPointer<SourceState> source = QSharedPointer<SourceState>::create();
    CommunicationConnection connection{[state = source] { return new FakeSource(state); }};
    DeviceRepository repository{configuration()};
    DeviceService service{connection, repository};
    QSignalSpy requests{&connection, &CommunicationConnection::requestFinished};
    QSignalSpy connectionChanges{&connection, &CommunicationConnection::connectionChanged};
    QSignalSpy states{&service, &DeviceService::stateChanged};
    QSignalSpy changes{&repository, &DeviceRepository::propertyChanged};

    void open() {
        if (!service.connectDevice({"FAKE", 115200})) qFatal("Service fixture open rejected");
    }
    void input(const QByteArray& frame) {
        QMutexLocker lock(&source->mutex);
        source->input(frame + char(13));
        source->changed.wakeAll();
    }
    Result result(int index) const { return qvariant_cast<Result>(requests.at(index).at(0)); }
    void finish() {
        service.disconnectDevice();
        connection.shutdown();
        QCOMPARE(source->created, source->destroyed);
        QCOMPARE(source->affinityErrors, 0);
    }
};
}

class DeviceServiceTests : public QObject {
    Q_OBJECT
private slots:
    void pollingSchedule();
    void pollingManualTrafficAndWritePriority();
    void pollingFailure_data();
    void pollingFailure();
    void pollingListChanges();
    void pollingStaleReconnect();
    void pollingRejectedAdmission();
    void lifecycle();
    void readUpdatesRepository();
    void failedRead_data();
    void failedRead();
    void writesNeedReceivedRead();
    void protectionIsNotConnectionError();
    void repositoryOwnsConversionAndValidation();
    void openFailureAndRetry();
    void queuedReadCannotUpdateDisconnectedSession();
    void queuedFailureCannotPoisonReconnect_data();
    void queuedFailureCannotPoisonReconnect();
    void nestedEarlierResultObserver();
    void nestedEarlierConnectionObserver();
    void serviceDestructionDetachesCallbacks();
    void borrowedDependenciesAndExistingConnection();
};

void DeviceServiceTests::lifecycle() {
    Fixture f;
    QVERIFY(f.states.isValid());
    QVERIFY(f.changes.isValid());
    QCOMPARE(f.service.state(), State::Disconnected);
    QVERIFY(f.service.errorString().isEmpty());
    QVERIFY(!f.service.connectDevice({{}, 115200}));
    QVERIFY(f.states.isEmpty());
    f.open();
    QCOMPARE(f.service.state(), State::Connecting);
    QVERIFY(!f.service.disconnectDevice());
    QCOMPARE(f.service.state(), State::Connecting);
    QTRY_COMPARE(f.service.state(), State::Connected);
    QCOMPARE(f.connection.findChildren<CommunicationWorker*>().size(), 1);
    QVERIFY(f.source->bytes().isEmpty());
    QVERIFY(f.service.disconnectDevice());
    QCOMPARE(f.service.state(), State::Disconnected);
    QTRY_COMPARE(f.connectionChanges.size(), 2);
    QCOMPARE(f.states.size(), 3);
    QCOMPARE(f.states[0][0].value<State>(), State::Connecting);
    QCOMPARE(f.states[1][0].value<State>(), State::Connected);
    QCOMPARE(f.states[2][0].value<State>(), State::Disconnected);
    QVERIFY(f.service.errorString().isEmpty());
    QVERIFY(f.requests.isEmpty());
    f.finish();
}

void DeviceServiceTests::readUpdatesRepository() {
    Fixture f;
    bool correctThread = true;
    connect(&f.repository, &DeviceRepository::propertyChanged, &f.repository,
        [&](quint16, const QVariant&) { correctThread &= QThread::currentThread() == f.repository.thread(); });
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    QVERIFY(f.connection.read(0x0017, 5000));
    QTRY_COMPARE(f.source->submissions(), 1);
    f.input("K0017 0164");
    QTRY_COMPARE(f.requests.size(), 1);
    QCOMPARE(f.result(0).outcome, Outcome::ReadResponse);
    QCOMPARE(f.result(0).parameter, quint16(0x0017));
    QCOMPARE(f.result(0).value, quint16(356));
    QCOMPARE(f.repository.value(0x0017).metaType().id(), int(QMetaType::Double));
    QCOMPARE(f.repository.value(0x0017).toDouble(), 3.56);
    QCOMPARE(f.changes.size(), 1);
    QVERIFY(f.connection.read(0x0017, 5000));
    QTRY_COMPARE(f.source->submissions(), 2);
    f.input("K0017 0164");
    QTRY_COMPARE(f.requests.size(), 2);
    QCOMPARE(f.changes.size(), 1);
    QVERIFY(correctThread);
    QVERIFY(f.source->owner != f.service.thread());
    f.finish();
    QCOMPARE(f.repository.value(0x0017).toDouble(), 3.56);
}

void DeviceServiceTests::failedRead_data() {
    QTest::addColumn<int>("outcome");
    QTest::addColumn<QByteArray>("frame");
    QTest::addColumn<int>("state");
    QTest::newRow("device-error") << int(Outcome::DeviceError) << QByteArray("EFFFF") << int(State::Connected);
    QTest::newRow("nonexistent") << int(Outcome::NonexistentParameter) << QByteArray("K0000 0000") << int(State::Connected);
    QTest::newRow("timeout") << int(Outcome::Timeout) << QByteArray() << int(State::Error);
    QTest::newRow("transport") << int(Outcome::TransportError) << QByteArray("K0017 0164") << int(State::Error);
}

void DeviceServiceTests::failedRead() {
    QFETCH(int, outcome);
    QFETCH(QByteArray, frame);
    QFETCH(int, state);
    Fixture f;
    QVERIFY(f.repository.update(0x0017, 100));
    const auto saved = f.repository.value(0x0017);
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    if (outcome == int(Outcome::TransportError)) {
        QMutexLocker lock(&f.source->mutex);
        f.source->failRead = true;
    }
    const int timeout = outcome == int(Outcome::Timeout) ? 80 : 5000;
    QVERIFY(f.connection.read(0x0017, timeout));
    QTRY_COMPARE(f.source->submissions(), 1);
    if (!frame.isEmpty()) f.input(frame);
    QTRY_COMPARE(f.requests.size(), 1);
    QCOMPARE(f.result(0).outcome, Outcome(outcome));
    QCOMPARE(f.service.state(), State(state));
    QCOMPARE(f.repository.value(0x0017), saved);
    QCOMPARE(f.changes.size(), 1);
    if (outcome == int(Outcome::TransportError))
        QCOMPARE(f.service.errorString(), QStringLiteral("read failed"));
    else
        QVERIFY(f.service.errorString().isEmpty());
    f.finish();
}

void DeviceServiceTests::writesNeedReceivedRead() {
    Fixture f;
    f.source->early = QByteArray("K0017 0384") + char(13);
    QVERIFY(f.repository.update(0x0017, 100));
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    QVERIFY(f.connection.write(0x0017, 900));
    QTRY_COMPARE(f.requests.size(), 1);
    QCOMPARE(f.result(0).kind, CommunicationWorker::Kind::Write);
    QCOMPARE(f.result(0).outcome, Outcome::BytesSent);
    QCOMPARE(f.repository.value(0x0017).toDouble(), 1.0);
    QCOMPARE(f.changes.size(), 1);
    QVERIFY(f.connection.read(0x0017, 5000));
    QTRY_COMPARE(f.source->submissions(), 2);
    f.input("K0017 0384");
    QTRY_COMPARE(f.requests.size(), 2);
    QCOMPARE(f.repository.value(0x0017).toDouble(), 9.0);
    QCOMPARE(f.changes.size(), 2);
    f.finish();
}

void DeviceServiceTests::protectionIsNotConnectionError() {
    Fixture f;
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    QVERIFY(f.connection.read(0x0801, 5000));
    QTRY_COMPARE(f.source->submissions(), 1);
    f.input("K0801 0001");
    QTRY_COMPARE(f.requests.size(), 1);
    QCOMPARE(f.repository.value(0x0801).metaType().id(), int(QMetaType::UInt));
    QCOMPARE(f.repository.value(0x0801).toUInt(), uint(1));
    QCOMPARE(f.service.state(), State::Connected);
    QVERIFY(f.service.errorString().isEmpty());
    QCOMPARE(f.source->bytes(), PICTextProtocol::encodeRead(0x0801));
    f.finish();
}

void DeviceServiceTests::repositoryOwnsConversionAndValidation() {
    Fixture f;
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    QVERIFY(f.connection.read(0x0015, 5000));
    QTRY_COMPARE(f.source->submissions(), 1);
    f.input("K0015 FF9C");
    QTRY_COMPARE(f.requests.size(), 1);
    QCOMPARE(f.repository.value(0x0015).toDouble(), -10.0);
    QVERIFY(f.connection.read(0xF000, 5000));
    QTRY_COMPARE(f.source->submissions(), 2);
    f.input("KF000 0001");
    QTRY_COMPARE(f.requests.size(), 2);
    QVERIFY(!f.repository.value(0xF000).isValid());
    QVERIFY(f.connection.read(0x0021, 5000));
    QTRY_COMPARE(f.source->submissions(), 3);
    f.input("K0021 0001");
    QTRY_COMPARE(f.requests.size(), 3);
    QVERIFY(!f.repository.value(0x0021).isValid());
    QCOMPARE(f.changes.size(), 1);
    QCOMPARE(f.service.state(), State::Connected);
    f.finish();
}

void DeviceServiceTests::openFailureAndRetry() {
    Fixture f;
    f.source->failOpen = true;
    f.open();
    QCOMPARE(f.service.state(), State::Connecting);
    QTRY_COMPARE(f.service.state(), State::Error);
    QCOMPARE(f.service.errorString(), QStringLiteral("open failed"));
    QVERIFY(!f.repository.value(0x0017).isValid());
    {
        QMutexLocker lock(&f.source->mutex);
        f.source->failOpen = false;
    }
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    QVERIFY(f.service.errorString().isEmpty());
    f.finish();
    QCOMPARE(f.source->created, 2);
}

void DeviceServiceTests::queuedReadCannotUpdateDisconnectedSession() {
    Fixture f;
    QVERIFY(f.repository.update(0x0017, 100));
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    QSemaphore emitted;
    auto* worker = f.connection.findChild<CommunicationWorker*>();
    QVERIFY(worker);
    const auto barrier = connect(worker, &CommunicationWorker::requestFinished, worker,
        [&emitted](const Result&) { emitted.release(); }, Qt::DirectConnection);
    QVERIFY(f.connection.read(0x0017, 5000));
    QTRY_COMPARE(f.source->submissions(), 1);
    f.input("K0017 0164");
    const bool ready = emitted.tryAcquire(1, 2000); // No caller event dispatch: result stays queued at facade.
    disconnect(barrier);
    QVERIFY(ready);
    QVERIFY(f.service.disconnectDevice());
    QCOMPARE(f.service.state(), State::Disconnected);
    QTRY_COMPARE(f.connectionChanges.size(), 2);
    QCOMPARE(f.requests.size(), 1);
    QCOMPARE(f.result(0).outcome, Outcome::ReadResponse);
    QCOMPARE(f.repository.value(0x0017).toDouble(), 1.0);
    QCOMPARE(f.changes.size(), 1);
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    QVERIFY(f.connection.read(0x0017, 5000));
    QTRY_COMPARE(f.source->submissions(), 2);
    f.input("K0017 01F4");
    QTRY_COMPARE(f.requests.size(), 2);
    QCOMPARE(f.repository.value(0x0017).toDouble(), 5.0);
    f.finish();
}

void DeviceServiceTests::queuedFailureCannotPoisonReconnect_data() {
    QTest::addColumn<bool>("timeout");
    QTest::newRow("timeout") << true;
    QTest::newRow("transport-error") << false;
}

void DeviceServiceTests::queuedFailureCannotPoisonReconnect() {
    QFETCH(bool, timeout);
    Fixture f;
    QVERIFY(f.repository.update(0x0017, 100));
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    QSemaphore emitted;
    auto* worker = f.connection.findChild<CommunicationWorker*>();
    QVERIFY(worker);
    const auto barrier = connect(worker, &CommunicationWorker::requestFinished, worker,
        [&emitted](const Result&) { emitted.release(); }, Qt::DirectConnection);
    QVERIFY(f.connection.read(0x0017, timeout ? 80 : 5000));
    if (!timeout) {
        QTRY_COMPARE(f.source->submissions(), 1);
        QMutexLocker lock(&f.source->mutex);
        f.source->fault();
        f.source->changed.wakeAll();
    }
    // Keep timeout delivery queued even if the caller thread is scheduled late.
    const bool ready = emitted.tryAcquire(1, 2000);
    disconnect(barrier);
    QVERIFY(ready);
    QVERIFY(f.service.connectDevice({"RECONNECTED", 9600}));
    QCOMPARE(f.service.state(), State::Connecting);
    QTRY_COMPARE(f.service.state(), State::Connected);
    QCOMPARE(f.requests.size(), 1);
    QCOMPARE(f.result(0).outcome, timeout ? Outcome::Timeout : Outcome::TransportError);
    QCOMPARE(f.repository.value(0x0017).toDouble(), 1.0);
    for (const auto& notification : f.states)
        QVERIFY(notification[0].value<State>() != State::Error);
    QVERIFY(f.connection.read(0x0017, 5000));
    QTRY_COMPARE(f.source->submissions(), 2);
    f.input("K0017 01F4");
    QTRY_COMPARE(f.requests.size(), 2);
    QCOMPARE(f.repository.value(0x0017).toDouble(), 5.0);
    f.finish();
}

void DeviceServiceTests::nestedEarlierResultObserver() {
    auto source = QSharedPointer<SourceState>::create();
    CommunicationConnection connection([source] { return new FakeSource(source); });
    DeviceRepository repository(configuration());
    QPointer<DeviceService> active;
    bool replaced = false;
    bool newSessionReady = false;
    connect(&connection, &CommunicationConnection::requestFinished, &connection, [&](const Result& result) {
        if (!active || replaced || result.outcome != Outcome::ReadResponse) return;
        replaced = true;
        QSignalSpy next(active.data(), &DeviceService::stateChanged);
        const bool accepted = active->connectDevice({"NEW", 9600});
        newSessionReady = accepted && next.wait(2000) && active->state() == State::Connected;
    });
    DeviceService service(connection, repository);
    active = &service;
    QSignalSpy changed(&repository, &DeviceRepository::propertyChanged);
    QVERIFY(service.connectDevice({"OLD", 115200}));
    QTRY_COMPARE(service.state(), State::Connected);
    QVERIFY(connection.read(0x0017, 5000));
    QTRY_COMPARE(source->submissions(), 1);
    {
        QMutexLocker lock(&source->mutex);
        source->input(QByteArray("K0017 0164") + char(13));
        source->changed.wakeAll();
    }
    QTRY_VERIFY(newSessionReady);
    QCOMPARE(service.state(), State::Connected);
    QVERIFY(!repository.value(0x0017).isValid());
    QVERIFY(changed.isEmpty());
    QVERIFY(connection.read(0x0017, 5000));
    QTRY_COMPARE(source->submissions(), 2);
    {
        QMutexLocker lock(&source->mutex);
        source->input(QByteArray("K0017 01F4") + char(13));
        source->changed.wakeAll();
    }
    QTRY_COMPARE(changed.size(), 1);
    QCOMPARE(repository.value(0x0017).toDouble(), 5.0);
    QVERIFY(service.disconnectDevice());
    connection.shutdown();
    QCOMPARE(source->affinityErrors, 0);
}

void DeviceServiceTests::nestedEarlierConnectionObserver() {
    auto source = QSharedPointer<SourceState>::create();
    CommunicationConnection connection([source] { return new FakeSource(source); });
    DeviceRepository repository(configuration());
    QPointer<DeviceService> active;
    bool replaceOnClose = false;
    bool newSessionReady = false;
    connect(&connection, &CommunicationConnection::connectionChanged, &connection, [&](bool connected, const QString&) {
        if (!active || connected || !replaceOnClose) return;
        replaceOnClose = false;
        QSignalSpy next(active.data(), &DeviceService::stateChanged);
        const bool accepted = active->connectDevice({"NEW", 9600});
        newSessionReady = accepted && next.wait(2000) && active->state() == State::Connected;
    });
    DeviceService service(connection, repository);
    active = &service;
    QVERIFY(service.connectDevice({"OLD", 115200}));
    QTRY_COMPARE(service.state(), State::Connected);
    replaceOnClose = true;
    QVERIFY(service.disconnectDevice());
    QTRY_VERIFY(newSessionReady);
    QCOMPARE(service.state(), State::Connected);
    QVERIFY(connection.isConnected());
    QVERIFY(service.disconnectDevice());
    connection.shutdown();
    QCOMPARE(source->affinityErrors, 0);
}

void DeviceServiceTests::serviceDestructionDetachesCallbacks() {
    auto source = QSharedPointer<SourceState>::create();
    CommunicationConnection connection([source] { return new FakeSource(source); });
    DeviceRepository repository(configuration());
    QScopedPointer<DeviceService> service(new DeviceService(connection, repository));
    QSignalSpy changed(&repository, &DeviceRepository::propertyChanged);
    QVERIFY(service->connectDevice({"FAKE", 115200}));
    QTRY_COMPARE(service->state(), State::Connected);
    QSemaphore emitted;
    auto* worker = connection.findChild<CommunicationWorker*>();
    QVERIFY(worker);
    const auto barrier = connect(worker, &CommunicationWorker::requestFinished, worker,
        [&emitted](const Result&) { emitted.release(); }, Qt::DirectConnection);
    QVERIFY(connection.read(0x0017, 5000));
    QTRY_COMPARE(source->submissions(), 1);
    {
        QMutexLocker lock(&source->mutex);
        source->input(QByteArray("K0017 0164") + char(13));
        source->changed.wakeAll();
    }
    const bool ready = emitted.tryAcquire(1, 2000);
    disconnect(barrier);
    QVERIFY(ready);
    service.reset();
    QTRY_COMPARE(connection.outstanding(), 0);
    QVERIFY(!repository.value(0x0017).isValid());
    QVERIFY(changed.isEmpty());
    connection.shutdown();
    QCOMPARE(source->created, source->destroyed);
}

void DeviceServiceTests::borrowedDependenciesAndExistingConnection() {
    auto source = QSharedPointer<SourceState>::create();
    CommunicationConnection connection([source] { return new FakeSource(source); });
    DeviceRepository repository(configuration());
    QVERIFY(repository.update(0x0017, 100));
    QVERIFY(connection.connectPort({"FAKE", 115200}));
    QTRY_VERIFY(connection.isConnected());
    {
        DeviceService service(connection, repository);
        QCOMPARE(service.state(), State::Connected);
        QCOMPARE(service.thread(), repository.thread());
        QCOMPARE(service.thread(), connection.thread());
        QCOMPARE(repository.value(0x0017).toDouble(), 1.0);
    }
    QVERIFY(connection.isConnected());
    QVERIFY(repository.value(0x0017).isValid());
    connection.shutdown();
    QCOMPARE(source->created, source->destroyed);
    QCOMPARE(source->affinityErrors, 0);
}

void DeviceServiceTests::pollingSchedule() {
    Fixture f;
    using Priority = DeviceService::PollPriority;
    QVERIFY(f.service.setPollingList({{0x0017, Priority::High}, {0x0801, Priority::Low},
                                      {0x0015, Priority::High}}));
    QTest::qWait(70);
    QCOMPARE(f.source->submissions(), 0);
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    const QList<quint16> order{0x0017, 0x0015, 0x0017, 0x0015, 0x0801,
                              0x0017, 0x0015, 0x0017, 0x0015, 0x0801};
    QByteArray wire;
    for (int i = 0; i < order.size(); ++i) {
        QTRY_COMPARE(f.source->submissions(), i + 1);
        QCOMPARE(f.connection.outstanding(), 1);
        wire += PICTextProtocol::encodeRead(order[i]);
        QCOMPARE(f.source->bytes(), wire);
        if (i == 0) {
            QTest::qWait(160); // Multiple timer periods cannot enqueue another polling Read.
            QCOMPARE(f.source->submissions(), 1);
            QCOMPARE(f.connection.outstanding(), 1);
        }
        f.input("K" + QByteArray::number(order[i], 16).rightJustified(4, '0').toUpper() + " 0164");
        QTRY_COMPARE(f.requests.size(), i + 1);
        QCOMPARE(f.result(i).outcome, Outcome::ReadResponse);
    }
    QVERIFY(f.service.setPollingList({}));
    QCOMPARE(order.count(0x0017), 4);
    QCOMPARE(order.count(0x0015), 4);
    QCOMPARE(order.count(0x0801), 2);
    QCOMPARE(f.repository.value(0x0017).toDouble(), 3.56);
    QCOMPARE(f.repository.value(0x0015).toDouble(), 35.6);
    QCOMPARE(f.repository.value(0x0801).toUInt(), uint(356));
    f.finish();
}

void DeviceServiceTests::pollingManualTrafficAndWritePriority() {
    Fixture f;
    using Priority = DeviceService::PollPriority;
    QVERIFY(f.service.setPollingList({{0x0017, Priority::High}, {0x0801, Priority::Low}}));
    f.open();
    QTRY_COMPARE(f.source->submissions(), 1);
    QVERIFY(f.connection.read(0x0017, 5000)); // Same register as the active polling request.
    QVERIFY(f.connection.write(0x0801, 9));
    f.input("K0017 0164");
    QTRY_COMPARE(f.source->submissions(), 3);
    QTRY_COMPARE(f.requests.size(), 2);
    QCOMPARE(f.result(1).outcome, Outcome::BytesSent);
    QCOMPARE(f.source->bytes(), PICTextProtocol::encodeRead(0x0017)
        + PICTextProtocol::encodeWrite(0x0801, 9) + PICTextProtocol::encodeRead(0x0017));
    QVERIFY(!f.repository.value(0x0801).isValid());
    QTest::qWait(160);
    QCOMPARE(f.source->submissions(), 3); // No poll can be confused with the outstanding manual Read.
    QCOMPARE(f.connection.outstanding(), 1);
    f.input("K0017 01F4");
    QTRY_COMPARE(f.requests.size(), 3);
    QCOMPARE(f.repository.value(0x0017).toDouble(), 5.0);
    QTRY_COMPARE(f.source->submissions(), 4);
    QVERIFY(f.source->bytes().endsWith(PICTextProtocol::encodeRead(0x0017))); // Second High pass.
    QVERIFY(f.service.setPollingList({}));
    f.input("K0017 01F4");
    QTRY_COMPARE(f.requests.size(), 4);
    f.finish();
}

void DeviceServiceTests::pollingFailure_data() {
    QTest::addColumn<int>("outcome");
    QTest::addColumn<QByteArray>("frame");
    QTest::newRow("device-error") << int(Outcome::DeviceError) << QByteArray("EFFFF");
    QTest::newRow("nonexistent") << int(Outcome::NonexistentParameter) << QByteArray("K0000 0000");
    QTest::newRow("timeout") << int(Outcome::Timeout) << QByteArray();
    QTest::newRow("transport") << int(Outcome::TransportError) << QByteArray();
}

void DeviceServiceTests::pollingFailure() {
    QFETCH(int, outcome);
    QFETCH(QByteArray, frame);
    Fixture f;
    QVERIFY(f.repository.update(0x0017, 100));
    QVERIFY(f.service.setPollingList({{0x0017, DeviceService::PollPriority::Low}}));
    f.open();
    QTRY_COMPARE(f.source->submissions(), 1);
    if (outcome == int(Outcome::TransportError)) {
        QMutexLocker lock(&f.source->mutex);
        f.source->fault();
        f.source->changed.wakeAll();
    } else if (!frame.isEmpty()) {
        f.input(frame);
    }
    QTRY_COMPARE(f.requests.size(), 1);
    QCOMPARE(f.result(0).outcome, Outcome(outcome));
    QCOMPARE(f.repository.value(0x0017).toDouble(), 1.0);
    QCOMPARE(f.changes.size(), 1);
    if (outcome == int(Outcome::Timeout) || outcome == int(Outcome::TransportError)) {
        QCOMPARE(f.service.state(), State::Error);
        QTest::qWait(160);
        QCOMPARE(f.source->submissions(), 1);
    } else {
        QCOMPARE(f.service.state(), State::Connected);
        QTRY_COMPARE(f.source->submissions(), 2); // Request-level failure releases the single poll slot.
    }
    f.finish();
}

void DeviceServiceTests::pollingListChanges() {
    Fixture f;
    using Priority = DeviceService::PollPriority;
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    QVERIFY(f.service.setPollingList({}));
    QTest::qWait(120);
    QCOMPARE(f.source->submissions(), 0);
    QVERIFY(!f.service.setPollingList({{0xF000, Priority::High}}));
    QVERIFY(!f.service.setPollingList({{0x0017, Priority(99)}}));
    QVERIFY(!f.service.setPollingList({{0x0017, Priority::High}, {0x0017, Priority::Low}}));
    QTest::qWait(70);
    QCOMPARE(f.source->submissions(), 0);
    QVERIFY(f.service.setPollingList({{0x0017, Priority::High}}));
    QVERIFY(!f.service.setPollingList({{0xF000, Priority::Low}})); // Invalid replacement is atomic.
    QTRY_COMPARE(f.source->submissions(), 1);
    QCOMPARE(f.source->bytes(), PICTextProtocol::encodeRead(0x0017));
    QVERIFY(f.service.setPollingList({{0x0801, Priority::Low}}));
    QTest::qWait(120);
    QCOMPARE(f.source->submissions(), 1); // Replacing the list cannot release the admitted poll slot.
    f.input("K0017 0164");
    QTRY_COMPARE(f.requests.size(), 1);
    QTRY_COMPARE(f.source->submissions(), 2);
    QVERIFY(f.source->bytes().endsWith(PICTextProtocol::encodeRead(0x0801)));
    QVERIFY(f.service.setPollingList({}));
    f.input("K0801 0001");
    QTRY_COMPARE(f.requests.size(), 2);
    QCOMPARE(f.repository.value(0x0801).toUInt(), uint(1));
    QTest::qWait(120);
    QCOMPARE(f.source->submissions(), 2);
    f.finish();
}

void DeviceServiceTests::pollingStaleReconnect() {
    Fixture f;
    using Priority = DeviceService::PollPriority;
    QVERIFY(f.service.setPollingList({{0x0017, Priority::High}}));
    f.open();
    QTRY_COMPARE(f.source->submissions(), 1);
    QSemaphore emitted;
    auto* worker = f.connection.findChild<CommunicationWorker*>();
    QVERIFY(worker);
    const auto barrier = connect(worker, &CommunicationWorker::requestFinished, worker,
        [&emitted](const Result&) { emitted.release(); }, Qt::DirectConnection);
    f.input("K0017 0164");
    const bool ready = emitted.tryAcquire(1, 2000); // Leave old success queued at the facade.
    disconnect(barrier);
    QVERIFY(ready);
    QVERIFY(f.service.disconnectDevice());
    QTRY_COMPARE(f.connectionChanges.size(), 2);
    QCOMPARE(f.requests.size(), 1);
    QVERIFY(!f.repository.value(0x0017).isValid());
    QTest::qWait(120);
    QCOMPARE(f.source->submissions(), 1);
    QVERIFY(f.service.setPollingList({{0x0801, Priority::Low}}));
    f.open();
    QTRY_COMPARE(f.source->submissions(), 2);
    QCOMPARE(f.service.state(), State::Connected);
    QVERIFY(f.source->bytes().endsWith(PICTextProtocol::encodeRead(0x0801)));
    QVERIFY(f.service.setPollingList({}));
    f.input("K0801 0001");
    QTRY_COMPARE(f.requests.size(), 2);
    QVERIFY(!f.repository.value(0x0017).isValid());
    QCOMPARE(f.repository.value(0x0801).toUInt(), uint(1));
    f.finish();
}

void DeviceServiceTests::pollingRejectedAdmission() {
    Fixture f;
    f.open();
    QTRY_COMPARE(f.service.state(), State::Connected);
    // Final shutdown deliberately bypasses lifecycle only to force real facade Read rejection.
    // Normal application teardown disconnects through DeviceService first.
    f.connection.shutdown();
    QVERIFY(!f.connection.read(0x0017));
    auto* timer = f.service.findChild<QTimer*>();
    QVERIFY(timer);
    QSignalSpy wakeups(timer, &QTimer::timeout);
    QVERIFY(f.service.setPollingList({{0x0017, DeviceService::PollPriority::High}}));
    QTRY_COMPARE(wakeups.size(), 1);
    QTest::qWait(120);
    QCOMPARE(wakeups.size(), 1); // Closed transport cannot cause repeated admission attempts.
    QVERIFY(!timer->isActive());
    QCOMPARE(f.connection.outstanding(), 0);
    QVERIFY(f.requests.isEmpty());
    QCOMPARE(f.source->submissions(), 0);
    QVERIFY(f.service.setPollingList({}));
    const int stopped = wakeups.size();
    QTest::qWait(70);
    QCOMPARE(wakeups.size(), stopped);
    f.finish();
}

QTEST_GUILESS_MAIN(DeviceServiceTests)
#include "tst_deviceservice.moc"
