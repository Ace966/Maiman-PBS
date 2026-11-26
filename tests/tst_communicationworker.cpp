#include "CommunicationConnection.h"
#include "FakeCommunicationSource.h"
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QPointer>
#include <QSignalSpy>
#include <QSemaphore>
#include <QThread>
#include <QtTest>
#include <QScopedPointer>
#include <QSharedPointer>
#include <limits>

using Outcome=CommunicationWorker::Outcome;
using Result=CommunicationWorker::Result;
namespace {
using TestCommunication::State;
using TestCommunication::FakeSource;
struct Fixture {
    QSharedPointer<State> state=QSharedPointer<State>::create();
    CommunicationConnection connection{[s=state]{return new FakeSource(s);}};
    QSignalSpy results{&connection,&CommunicationConnection::requestFinished};
    QSignalSpy changes{&connection,&CommunicationConnection::connectionChanged};
    int accepted{0};
    Fixture(){
        auto* worker=connection.findChild<CommunicationWorker*>();
        QObject::connect(worker,&CommunicationWorker::requestFinished,&connection,[s=state](const Result&){
            const auto now = s->clock.nsecsElapsed();
            QMutexLocker lock(&s->mutex);
            s->completionNs.append(now);
            s->changed.wakeAll();
        },Qt::DirectConnection);
    }
    ~Fixture(){connection.shutdown();}
    void open(){if(!connection.connectPort({"FAKE",115200}))qFatal("Fixture connect rejected");}
    void post(std::function<void(State&)> fn) {QMutexLocker lock(&state->mutex);fn(*state);state->changed.wakeAll();}
    void input(const QByteArray& data){post([data](State& s){s.input(data);});}
    Result result(int i)const{return qvariant_cast<Result>(results.at(i).at(0));}
    bool read(quint16 parameter, int timeoutMs = 1000) {
        const bool ok = connection.read(parameter, timeoutMs);
        accepted += ok;
        return ok;
    }
    bool write(quint16 parameter, quint16 value, int timeoutMs = 1000) {
        const bool ok = connection.write(parameter, value, timeoutMs);
        accepted += ok;
        return ok;
    }
    void exactlyOnce() {
        connection.shutdown();
        QCOMPARE(results.size(), accepted);
        QCOMPARE(connection.outstanding(), 0);
        for (const auto& row : results) {
            const auto result = qvariant_cast<Result>(row[0]);
            if (result.outcome == Outcome::Cancelled) {
                QVERIFY(result.error.isEmpty());
                QCOMPARE(result.errorCode, quint16(0));
            }
        }
    }
};
}
class CommunicationWorkerTests:public QObject {
    Q_OBJECT
private slots:
    void submissionRejections_data();
    void submissionRejections();
    void typedSettings_data();void typedSettings();
    void idleUsesSemaphore();void shutdownInterruptsLongWait();
    void timingConfiguration();
    void timingZero_data();
    void timingZero();
    void timingMinimum_data();
    void timingMinimum();
    void timingIdle();
    void timingPriority();
    
    void timingLifecycle_data();
    void timingLifecycle();
    void timingTimeoutIndependence();
    void timingWakeupsNearExpiry();
    void readFragmented();void priorityAndFifo();void duplicateFramesCannotCompleteNext();
    void responseKinds_data();void responseKinds();void unexpectedAndMalformed();
    void standardWrite();void partialOutput_data();void partialOutput();void earlyResponse();
    void writeDoesNotConfirmEcho();void earlyResponseStillTimesOutIfNotDrained();
    void lateResponseRequiresReconnect();void staleCallerCallback_data();void staleCallerCallback();
    void serializedLifecycle();void creditsAfterFailure_data();void creditsAfterFailure();
    void shutdownFromFailureNotification();
    void lifecycleOutcomes_data();void lifecycleOutcomes();
    void reconnectFromResult_data();void reconnectFromResult();
    void stalledOutput();void progressAfterZeroWrite();void deadlineNotExtended();
    void boundedBurstAndShutdown();void completedResultsRemainBounded();
    void openFailureAndRetry();void nullFactory();void transportFailure_data();void transportFailure();
    void disconnectActive();void reconnectDropsStaleCallbacks();void reconnectParserReset_data();void reconnectParserReset();
    void independentConnections();void destructionCompletesPending();void shutdownWhileOpening();
};
void CommunicationWorkerTests::typedSettings_data(){
    QTest::addColumn<SerialSettings>("settings");
    QTest::newRow("empty")<<SerialSettings{{},115200};QTest::newRow("blank")<<SerialSettings{" \t",115200};
    QTest::newRow("NUL")<<SerialSettings{QString("FAKE")+QChar(0)+"suffix",115200};
    QTest::newRow("zero-baud")<<SerialSettings{"FAKE",0};QTest::newRow("negative-baud")<<SerialSettings{"FAKE",-1};
}
void CommunicationWorkerTests::typedSettings() {
    QFETCH(SerialSettings, settings);
    Fixture f;
    QVERIFY(!f.connection.connectPort(settings));
    QVERIFY(!f.read(1));
    QVERIFY(!f.write(2, 3));
    QCOMPARE(f.connection.outstanding(), 0);
    f.exactlyOnce();
    QCOMPARE(f.state->created, 0);
    QCOMPARE(f.state->attemptCount(), 0);
}

void CommunicationWorkerTests::readFragmented(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.read(0x0017));QTRY_COMPARE(f.state->bytes(),QByteArray("J0017\r"));
    f.input("K0017");f.input(" 01");f.input("64\r");QTRY_COMPARE(f.results.size(),1);
    QCOMPARE(f.result(0).kind,CommunicationWorker::Kind::Read);QCOMPARE(f.result(0).parameter,quint16(0x17));
    QCOMPARE(f.result(0).outcome,Outcome::ReadResponse);QCOMPARE(f.result(0).value,quint16(356));
    f.connection.shutdown();QCOMPARE(f.state->affinityErrors,0);QCOMPARE(f.state->created,f.state->destroyed);
    QVERIFY(f.state->owner!=QThread::currentThread());f.exactlyOnce();}
void CommunicationWorkerTests::priorityAndFifo(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.read(0x1000));QTRY_COMPARE(f.state->bytes(),QByteArray("J1000\r"));
    const bool r1=f.read(0x1001),w1=f.write(0x2001,1),r2=f.read(0x1002),w2=f.write(0x2002,2);
    QVERIFY(r1&&w1&&r2&&w2);QCOMPARE(f.state->bytes(),QByteArray("J1000\r"));
    f.input("K1000 0001\r");QTRY_COMPARE(f.state->bytes(),QByteArray("J1000\rP2001 0001\rP2002 0002\rJ1001\r"));
    f.input("K1001 0002\r");QTRY_VERIFY(f.state->bytes().endsWith("J1002\r"));f.input("K1002 0003\r");QTRY_COMPARE(f.results.size(),5);
    const QList<quint16> expected{0x1000,0x2001,0x2002,0x1001,0x1002};
    for(int i=0;i<5;++i) {
        QCOMPARE(f.result(i).parameter,expected[i]);
        QCOMPARE(f.result(i).kind, i==1 || i==2 ? CommunicationWorker::Kind::Write : CommunicationWorker::Kind::Read);
    }
    f.exactlyOnce();}
void CommunicationWorkerTests::duplicateFramesCannotCompleteNext(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    f.read(0x17);QTRY_COMPARE(f.state->submissions(),1);f.read(0x17);
    f.input("K0017 0001\rK0017 0002\r");QTRY_COMPARE(f.state->submissions(),2);QTRY_COMPARE(f.results.size(),1);
    f.input("K0017 0003\r");QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(1).value,quint16(3));f.exactlyOnce();}
void CommunicationWorkerTests::responseKinds_data(){QTest::addColumn<QByteArray>("bytes");QTest::addColumn<int>("outcome");QTest::addColumn<bool>("writing");
    QTest::newRow("read-device-error")<<QByteArray("EFFFF\r")<<int(Outcome::DeviceError)<<false;
    QTest::newRow("read-nonexistent")<<QByteArray("K0000 0000\r")<<int(Outcome::NonexistentParameter)<<false;
    QTest::newRow("write-early-error")<<QByteArray("EFFFF\r")<<int(Outcome::DeviceError)<<true;
    QTest::newRow("write-early-nonexistent")<<QByteArray("K0000 0000\r")<<int(Outcome::NonexistentParameter)<<true;}
void CommunicationWorkerTests::responseKinds(){QFETCH(QByteArray,bytes);QFETCH(int,outcome);QFETCH(bool,writing);Fixture f;
    if(writing){f.state->buffered=true;f.state->early=bytes;}f.open();QTRY_VERIFY(f.connection.isConnected());
    if(writing){f.write(0x17,0xabcd);QTRY_VERIFY(f.state->readCount()>0);QCOMPARE(f.results.size(),0);f.post([](State& s){s.drain();});}
    else{f.read(0x17);QTRY_COMPARE(f.state->submissions(),1);f.input(bytes);}
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(int(f.result(0).outcome),outcome);QCOMPARE(f.result(0).parameter,quint16(0x17));
    QCOMPARE(f.result(0).value,quint16(writing?0xabcd:0));
    if(outcome==int(Outcome::DeviceError))QCOMPARE(f.result(0).errorCode,quint16(0xffff));QVERIFY(f.connection.isConnected());}
void CommunicationWorkerTests::unexpectedAndMalformed(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.read(0x17);QTRY_COMPARE(f.state->submissions(),1);
    f.input("K0021 FFFF\rbad\r"+QByteArray(20,'x')+"\r");QTRY_VERIFY(f.state->readCount()>0);QCOMPARE(f.results.size(),0);
    f.input("K0017 FFFF\r");QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).value,quint16(0xffff));}
void CommunicationWorkerTests::standardWrite(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());QVERIFY(f.write(0x17,900));
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::BytesSent);QCOMPARE(f.state->bytes(),QByteArray("P0017 0384\r"));
    QCOMPARE(f.result(0).kind,CommunicationWorker::Kind::Write);QCOMPARE(f.result(0).parameter,quint16(0x17));QCOMPARE(f.result(0).value,quint16(900));
    QCOMPARE(f.state->submissions(),1);QCOMPARE(f.connection.outstanding(),0);}
void CommunicationWorkerTests::partialOutput_data(){QTest::addColumn<bool>("buffered");QTest::newRow("buffered")<<true;QTest::newRow("unbuffered")<<false;}
void CommunicationWorkerTests::partialOutput(){QFETCH(bool,buffered);Fixture f;f.state->buffered=buffered;f.state->autoDrain=buffered;f.state->maximum=2;
    f.open();QTRY_VERIFY(f.connection.isConnected());f.write(0x17,900);QTRY_COMPARE(f.results.size(),1);
    QCOMPARE(f.result(0).outcome,Outcome::BytesSent);QCOMPARE(f.state->bytes(),QByteArray("P0017 0384\r"));f.connection.shutdown();QCOMPARE(f.state->attempts,6);}
void CommunicationWorkerTests::earlyResponse(){Fixture f;f.state->buffered=true;f.state->early="K0017 0164\r";f.open();QTRY_VERIFY(f.connection.isConnected());
    f.read(0x17);QTRY_VERIFY(f.state->readCount()>0);{QMutexLocker lock(&f.state->mutex);QVERIFY(f.state->pending>0);}QCOMPARE(f.results.size(),0);
    f.post([](State& s){s.drain();});QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::ReadResponse);QCOMPARE(f.result(0).value,quint16(356));}
void CommunicationWorkerTests::writeDoesNotConfirmEcho(){Fixture f;f.state->buffered=true;f.state->early="K0017 0384\r";f.open();QTRY_VERIFY(f.connection.isConnected());
    f.write(0x17,900);QTRY_VERIFY(f.state->readCount()>0);QCOMPARE(f.results.size(),0);f.post([](State& s){s.drain();});
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::BytesSent);}
void CommunicationWorkerTests::earlyResponseStillTimesOutIfNotDrained(){Fixture f;f.state->buffered=true;f.state->early="K0017 0164\r";f.open();QTRY_VERIFY(f.connection.isConnected());
    f.read(0x17,80);QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::Timeout);QVERIFY(!f.connection.isConnected());}
void CommunicationWorkerTests::stalledOutput(){Fixture f;f.state->stall=true;f.open();QTRY_VERIFY(f.connection.isConnected());f.write(0x17,1,50);
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::Timeout);f.connection.shutdown();QVERIFY(f.state->attempts>=1&&f.state->attempts<=8);}
void CommunicationWorkerTests::progressAfterZeroWrite(){Fixture f;f.state->stall=true;f.open();QTRY_VERIFY(f.connection.isConnected());f.write(0x17,1);QTRY_VERIFY(f.state->attemptCount()>0);
    f.post([](State& s){s.stall=false;s.drain();});QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::BytesSent);}
void CommunicationWorkerTests::deadlineNotExtended(){Fixture f;f.state->noise=true;f.open();QTRY_VERIFY(f.connection.isConnected());
    QElapsedTimer elapsed;elapsed.start();QVERIFY(f.read(0x17,150));QTRY_COMPARE(f.results.size(),1);
    QVERIFY(f.state->readCount()>0);QCOMPARE(f.result(0).outcome,Outcome::Timeout);QVERIFY(!f.connection.isConnected());QVERIFY(elapsed.elapsed()<1000);
    QCOMPARE(f.read(0x17),false);f.connection.shutdown();f.exactlyOnce();}




void CommunicationWorkerTests::boundedBurstAndShutdown() {
    Fixture f;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    for (int i = 0; i < 8; ++i) {
        QVERIFY(!f.read(0xEEEE, 0));
        QVERIFY(!f.write(0xEEEE, 0xFFFF, -1));
    }
    QCOMPARE(f.connection.outstanding(), 0);
    QVERIFY(f.results.isEmpty());
    QVERIFY(f.read(1, 10000));
    QTRY_COMPARE(f.state->submissions(), 1);
    for (int i = 0; i < 5000; ++i) f.read(1, 10000);
    for (int i = 0; i < 5000; ++i) f.write(2, 3, 10000);
    QCOMPARE(f.accepted, 64);
    QCOMPARE(f.connection.outstanding(), 64);
    QCOMPARE(f.state->bytes(), QByteArray("J0001\r"));
    QCOMPARE(f.state->submissions(), 1);
    f.connection.shutdown();
    QCOMPARE(f.results.size(), 64);
    QCOMPARE(f.connection.outstanding(), 0);
    int reads = 0, writes = 0;
    for (const auto& row : f.results) {
        const auto result = qvariant_cast<Result>(row[0]);
        QCOMPARE(result.outcome, Outcome::Cancelled);
        if (result.kind == CommunicationWorker::Kind::Read) {
            ++reads;
            QCOMPARE(result.parameter, quint16(1));
        } else {
            ++writes;
            QCOMPARE(result.parameter, quint16(2));
            QCOMPARE(result.value, quint16(3));
        }
    }
    QCOMPARE(reads, 32);
    QCOMPARE(writes, 32);
    f.exactlyOnce();
    auto* thread = f.connection.findChild<QThread*>();
    QVERIFY(thread);
    QVERIFY(!thread->isRunning());
}

void CommunicationWorkerTests::openFailureAndRetry() {
    Fixture f;
    f.state->failOpen = true;
    f.open();
    QTRY_COMPARE(f.changes.size(), 1);
    QVERIFY(!f.connection.isConnected());
    QCOMPARE(f.changes[0][1].toString(), QStringLiteral("open failed"));
    QVERIFY(!f.read(1));
    {
        QMutexLocker lock(&f.state->mutex);
        f.state->failOpen = false;
    }
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.write(1, 2));
    QTRY_COMPARE(f.results.size(), 1);
    f.connection.shutdown();
    QCOMPARE(f.state->created, 2);
    QCOMPARE(f.state->destroyed, 2);
    QCOMPARE(f.state->affinityErrors, 0);
    f.exactlyOnce();
}

void CommunicationWorkerTests::nullFactory(){CommunicationConnection c([]()->IDataSource*{return nullptr;});QSignalSpy changes(&c,&CommunicationConnection::connectionChanged);
    QVERIFY(c.connectPort({"FAKE",115200}));QTRY_COMPARE(changes.size(),1);QVERIFY(!c.isConnected());QVERIFY(!changes[0][1].toString().isEmpty());}
void CommunicationWorkerTests::transportFailure_data() {
    QTest::addColumn<int>("which");
    QTest::addColumn<QString>("diagnostic");
    QTest::newRow("write") << 0 << QStringLiteral("write failed");
    QTest::newRow("read") << 1 << QStringLiteral("read failed");
    QTest::newRow("blocking-wait-error") << 2 << QStringLiteral("injected port error");
}

void CommunicationWorkerTests::transportFailure() {
    QFETCH(int, which);
    QFETCH(QString, diagnostic);
    Fixture f;
    f.state->failWrite = which == 0;
    f.state->failRead = which == 1;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.read(1));
    if (which) {
        QTRY_COMPARE(f.state->submissions(), 1);
        if (which == 1) f.input("K0001 0002\r");
        else f.post([](State& state) { state.fault(); });
    }
    QTRY_COMPARE(f.results.size(), 1);
    QCOMPARE(f.result(0).outcome, Outcome::TransportError);
    QCOMPARE(f.result(0).error, diagnostic);
    QVERIFY(!f.connection.isConnected());
    f.exactlyOnce();
}

void CommunicationWorkerTests::disconnectActive(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.read(1);QTRY_COMPARE(f.state->submissions(),1);f.write(2,3);
    QVERIFY(f.connection.disconnectPort());QTRY_COMPARE(f.results.size(),2);QVERIFY(!f.connection.isConnected());f.exactlyOnce();}
void CommunicationWorkerTests::reconnectDropsStaleCallbacks(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.read(1);QTRY_COMPARE(f.state->submissions(),1);
    // Hold only fake state while posting replacement: old data/fault cannot run in the new source.
    {QMutexLocker lock(&f.state->mutex);QVERIFY(f.connection.connectPort({"FAKE_NEW",9600}));
     f.state->input("K0001 9999\r");f.state->fault();f.state->changed.wakeAll();}
    QTRY_VERIFY(f.connection.isConnected());QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::Cancelled);
    const bool accepted=f.read(2);QVERIFY(accepted);QTRY_COMPARE(f.state->submissions(),2);f.input("K0002 0007\r");
    QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(1).parameter,quint16(2));QCOMPARE(f.result(1).value,quint16(7));QVERIFY(f.connection.isConnected());f.exactlyOnce();}
void CommunicationWorkerTests::reconnectParserReset_data(){QTest::addColumn<QByteArray>("old");QTest::newRow("partial")<<QByteArray("K0001");QTest::newRow("discard")<<QByteArray(12,'x');}
void CommunicationWorkerTests::reconnectParserReset(){QFETCH(QByteArray,old);Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    f.read(1);QTRY_COMPARE(f.state->submissions(),1);f.input(old);QTRY_VERIFY(f.state->readCount()>0);
    QVERIFY(f.connection.connectPort({"FAKE_NEW",115200}));QTRY_VERIFY(f.connection.isConnected());f.read(2);QTRY_COMPARE(f.state->submissions(),2);
    f.input("K0002 0009\r");QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(0).outcome,Outcome::Cancelled);QCOMPARE(f.result(1).value,quint16(9));}
void CommunicationWorkerTests::independentConnections(){Fixture a,b;a.open();b.open();QTRY_VERIFY(a.connection.isConnected()&&b.connection.isConnected());
    a.read(1);b.read(2);QTRY_COMPARE(a.state->submissions(),1);QTRY_COMPARE(b.state->submissions(),1);
    a.input("K0001 0003\r");b.input("K0002 0004\r");QTRY_COMPARE(a.results.size(),1);QTRY_COMPARE(b.results.size(),1);
    QCOMPARE(a.result(0).value,quint16(3));QCOMPARE(b.result(0).value,quint16(4));a.connection.shutdown();b.connection.shutdown();
    QVERIFY(a.state->owner!=b.state->owner);QCOMPARE(a.state->affinityErrors+b.state->affinityErrors,0);}
void CommunicationWorkerTests::destructionCompletesPending() {
    auto state = QSharedPointer<State>::create();
    QList<Result> results;
    QObject consumer;
    QScopedPointer<CommunicationConnection> connection(new CommunicationConnection([state] {
        return new FakeSource(state);
    }));
    connect(connection.data(), &CommunicationConnection::requestFinished, &consumer,
        [&results](const Result& result) { results.append(result); });
    QPointer<QThread> thread = connection->findChild<QThread*>();
    QVERIFY(connection->connectPort({"FAKE", 115200}));
    QTRY_VERIFY(connection->isConnected());
    QVERIFY(connection->read(1));
    QVERIFY(connection->write(2, 3));
    connection.reset();
    QCOMPARE(results.size(), 2);
    QVERIFY(thread.isNull());
    QCOMPARE(state->created, state->destroyed);
    QCOMPARE(state->affinityErrors, 0);
}

void CommunicationWorkerTests::shutdownWhileOpening() {
    Fixture f;
    QSemaphore opened;
    auto* worker = f.connection.findChild<CommunicationWorker*>();
    QVERIFY(worker);
    const auto observer = connect(worker, &CommunicationWorker::connectionChanged, worker,
        [&opened](bool, const QString&) { opened.release(); }, Qt::DirectConnection);
    f.open();
    const bool seen = opened.tryAcquire(1, 2000);
    disconnect(observer);
    QVERIFY(seen); // Port opened in run(), but caller acknowledgement is still queued.
    QVERIFY(!f.connection.isConnected());
    f.connection.shutdown();
    QVERIFY(!f.connection.isConnected());
    QCOMPARE(f.changes.size(), 0);
    QCOMPARE(f.state->created, 1);
    QCOMPARE(f.state->created, f.state->destroyed);
    QVERIFY(!f.connection.connectPort({"FAKE", 115200}));
}
void CommunicationWorkerTests::lateResponseRequiresReconnect() {
    Fixture f;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.read(1, 100));
    QVERIFY(f.read(1));
    QTRY_COMPARE(f.state->submissions(), 1);
    QTRY_COMPARE(f.results.size(), 2);
    QCOMPARE(f.result(0).outcome, Outcome::Timeout);
    QCOMPARE(f.result(1).outcome, Outcome::Cancelled);
    QVERIFY(!f.connection.isConnected());
    QVERIFY(!f.read(1));
    f.input("K0001 1111\r");
    QTest::qWait(30);
    QCOMPARE(f.results.size(), 2);
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.read(1));
    QTRY_COMPARE(f.state->submissions(), 2);
    f.input("K0001 2222\r");
    QTRY_COMPARE(f.results.size(), 3);
    QCOMPARE(f.result(2).value, quint16(0x2222));
    f.exactlyOnce();
}

void CommunicationWorkerTests::staleCallerCallback_data() {
    QTest::addColumn<bool>("timeout");
    QTest::newRow("queued-transport-failure") << false;
    QTest::newRow("queued-timeout") << true;
}

void CommunicationWorkerTests::staleCallerCallback() {
    QFETCH(bool, timeout);
    Fixture f;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QSemaphore emitted;
    auto* worker = f.connection.findChild<CommunicationWorker*>();
    QVERIFY(worker);
    const auto observer = connect(worker, &CommunicationWorker::requestFinished, worker,
        [&emitted](const Result&) { emitted.release(); }, Qt::DirectConnection);
    if (!timeout) f.post([](State& s) { s.failWrite = true; });
    QVERIFY(f.read(1, timeout ? 60 : 10000));
    // Wait without caller event dispatch: the old terminal must stay queued at the facade.
    const bool seen = emitted.tryAcquire(1, 2000);
    disconnect(observer);
    QVERIFY(seen);
    f.post([](State& s) { s.failWrite = false; });
    QVERIFY(f.connection.connectPort({"FAKE_NEW", 115200}));
    bool oldResultDelivered = false, overlapRejected = false;
    connect(&f.connection, &CommunicationConnection::requestFinished, this,
        [&](const Result& result) {
            if (result.outcome == Outcome::Timeout || result.outcome == Outcome::TransportError) {
                oldResultDelivered = true;
                overlapRejected = !f.connection.disconnectPort();
                QVERIFY(!f.connection.isConnected());
            }
        });
    QTRY_VERIFY(f.connection.isConnected());
    QTRY_COMPARE(f.results.size(), 1);
    QVERIFY(oldResultDelivered);
    QVERIFY(overlapRejected); // Old failure cannot release the pending replacement's control gate.
    QCOMPARE(f.result(0).outcome, timeout ? Outcome::Timeout : Outcome::TransportError);
    QCOMPARE(f.result(0).parameter, quint16(1));
    QCOMPARE(f.changes.size(), 2); // Initial open and replacement; no stale disconnect.
    QVERIFY(f.changes[0][0].toBool());
    QVERIFY(f.changes[1][0].toBool());
    const int before = f.state->submissions();
    QVERIFY(f.read(2));
    QTRY_COMPARE(f.state->submissions(), before + 1);
    f.input("K0002 0003\r");
    QTRY_COMPARE(f.results.size(), 2);
    QCOMPARE(f.result(1).parameter, quint16(2));
    QCOMPARE(f.result(1).value, quint16(3));
    f.exactlyOnce();
}

void CommunicationWorkerTests::completedResultsRemainBounded() {
    Fixture f;
    bool callerAffinity = true;
    connect(&f.connection, &CommunicationConnection::requestFinished, this,
        [&](const Result&) { callerAffinity &= QThread::currentThread() == f.connection.thread(); });
    connect(&f.connection, &CommunicationConnection::connectionChanged, this,
        [&](bool, const QString&) { callerAffinity &= QThread::currentThread() == f.connection.thread(); });
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    for (int i = 0; i < CommunicationWorker::QueueCapacity; ++i)
        QVERIFY(f.write(1, quint16(i)));
    {
        QMutexLocker lock(&f.state->mutex);
        QDeadlineTimer deadline(3000);
        while (f.state->completionNs.size() < CommunicationWorker::QueueCapacity && !deadline.hasExpired())
            f.state->changed.wait(&f.state->mutex, deadline);
        QCOMPARE(f.state->completionNs.size(), CommunicationWorker::QueueCapacity);
    }
    // All writes have completed in run(), but their queued results still hold admission credits.
    QCOMPARE(f.results.size(), 0);
    QCOMPARE(f.connection.outstanding(), CommunicationWorker::QueueCapacity);
    QCOMPARE(f.write(1, 1), false);
    for (int i = 0; i < CommunicationWorker::QueueCapacity; ++i)
        QVERIFY(f.read(2, 10000));
    QCOMPARE(f.read(2), false);
    QCOMPARE(f.connection.outstanding(), 2 * CommunicationWorker::QueueCapacity);
    f.connection.shutdown();
    QCOMPARE(f.results.size(), 2 * CommunicationWorker::QueueCapacity);
    QCOMPARE(f.connection.outstanding(), 0);
    for (int i = 0; i < f.results.size(); ++i) {
        QCOMPARE(f.result(i).outcome, i < CommunicationWorker::QueueCapacity ? Outcome::BytesSent : Outcome::Cancelled);
        const bool writing = i < CommunicationWorker::QueueCapacity;
        QCOMPARE(f.result(i).kind, writing ? CommunicationWorker::Kind::Write : CommunicationWorker::Kind::Read);
        QCOMPARE(f.result(i).parameter, quint16(writing ? 1 : 2));
        QCOMPARE(f.result(i).value, quint16(writing ? i : 0));
    }
    QVERIFY(callerAffinity);
    f.exactlyOnce();
}

void CommunicationWorkerTests::idleUsesSemaphore(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());QTest::qWait(40);
    {QMutexLocker lock(&f.state->mutex);QCOMPARE(f.state->attempts,0);QCOMPARE(f.state->readWaits,0);QCOMPARE(f.state->writeWaits,0);}
    f.connection.shutdown();QCOMPARE(f.state->created,f.state->destroyed);
}
void CommunicationWorkerTests::shutdownInterruptsLongWait(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.read(1,10000));QTRY_COMPARE(f.state->submissions(),1);QElapsedTimer elapsed;elapsed.start();f.connection.shutdown();
    QVERIFY(elapsed.elapsed()<1000);QCOMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::Cancelled);f.exactlyOnce();
}

void CommunicationWorkerTests::timingConfiguration() {
    Fixture f;
    QVERIFY(!f.connection.setInterCommandDelayMs(-1));
    QCOMPARE(f.connection.outstanding(), 0);
    QVERIFY(f.results.isEmpty());
    QVERIFY(f.connection.setInterCommandDelayMs(std::numeric_limits<int>::max()));
    QVERIFY(f.connection.setInterCommandDelayMs(0));
    QCOMPARE(f.state->attemptCount(), 0);
    f.open();
    QVERIFY(!f.connection.setInterCommandDelayMs(1)); // Opening is still pending in the caller.
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(!f.connection.setInterCommandDelayMs(1));
    QVERIFY(f.connection.disconnectPort());
    QVERIFY(!f.connection.setInterCommandDelayMs(1));
    QTRY_COMPARE(f.changes.size(), 2);
    QVERIFY(f.connection.setInterCommandDelayMs(25));
    f.connection.shutdown();
    QVERIFY(!f.connection.setInterCommandDelayMs(0));
}

void CommunicationWorkerTests::timingZero_data() {
    QTest::addColumn<bool>("explicitZero");
    QTest::newRow("default") << false;
    QTest::newRow("explicit-zero") << true;
}

void CommunicationWorkerTests::timingZero() {
    QFETCH(bool, explicitZero);
    Fixture f;
    if (explicitZero)
        QVERIFY(f.connection.setInterCommandDelayMs(0));
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    const bool a = f.write(1, 1), b = f.write(2, 2);
    QVERIFY(a && b);
    QTRY_COMPARE(f.results.size(), 2);
    QCOMPARE(f.state->submissions(), 2);
    QCOMPARE(f.result(0).outcome, Outcome::BytesSent);
    QCOMPARE(f.result(1).outcome, Outcome::BytesSent);
    const auto gap = f.state->submitted(1) - f.state->completed(0);
    QVERIFY(gap >= 0);
    QVERIFY(gap < 200000000);
    f.exactlyOnce();
}

void CommunicationWorkerTests::timingMinimum_data() {
    QTest::addColumn<bool>("writing");
    QTest::newRow("read-completion") << false;
    QTest::newRow("write-drain") << true;
}

void CommunicationWorkerTests::timingMinimum() {
    QFETCH(bool, writing);
    constexpr int delay = 120;
    Fixture f;
    QVERIFY(f.connection.setInterCommandDelayMs(delay));
    QVERIFY(!f.connection.setInterCommandDelayMs(-1));
    f.state->buffered = writing;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    const bool first = writing ? f.write(1, 1) : f.read(1);
    QVERIFY(first);
    QTRY_COMPARE(f.state->submissions(), 1);
    const bool next = f.write(2, 2);
    QVERIFY(next);
    QTest::qWait(delay + 50);
    QCOMPARE(f.results.size(), 0);
    QCOMPARE(f.state->submissions(), 1);
    if (writing)
        f.post([](State& s) {
            s.buffered = false;
            s.drain();
        });
    else
        f.input("K0001 002A\r");
    QTRY_COMPARE(f.results.size(), 2);
    QCOMPARE(f.state->submissions(), 2);
    QCOMPARE(f.result(0).parameter, quint16(1));
    QCOMPARE(f.result(0).outcome, writing ? Outcome::BytesSent : Outcome::ReadResponse);
    QCOMPARE(f.result(1).parameter, quint16(2));
    QCOMPARE(f.result(1).outcome, Outcome::BytesSent);
    const auto gap = f.state->submitted(1) - f.state->completed(0);
    QVERIFY2(gap >= qint64(delay - 10) * 1000000, qPrintable(QString::number(gap)));
    QVERIFY(gap < qint64(delay + 1000) * 1000000);
    f.exactlyOnce();
}

void CommunicationWorkerTests::timingIdle() {
    constexpr int delay = 300;
    Fixture f;
    QVERIFY(f.connection.setInterCommandDelayMs(delay));
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    const bool first = f.write(1, 1);
    QVERIFY(first);
    QTRY_COMPARE(f.results.size(), 1);
    QTest::qWait(delay + 100);
    const auto ready = f.state->now();
    QVERIFY(f.write(2, 2));
    QTRY_COMPARE(f.results.size(), 2);
    const auto submitted = f.state->submitted(1);
    QVERIFY(submitted - f.state->completed(0) >= qint64(delay) * 1000000);
    QVERIFY(submitted - ready < 150000000);
    f.exactlyOnce();
}

void CommunicationWorkerTests::timingPriority() {
    constexpr int delay = 180;
    Fixture f;
    QVERIFY(f.connection.setInterCommandDelayMs(delay));
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    const bool active = f.read(0x1000);
    QTRY_COMPARE(f.state->submissions(), 1);
    const bool r1 = f.read(0x1001), r2 = f.read(0x1002);
    QVERIFY(active && r1 && r2);
    f.input("K1000 0001\r");
    QTRY_COMPARE(f.results.size(), 1);
    QTest::qWait(20);
    QCOMPARE(f.state->submissions(), 1); // Reads remain queued during the gap.
    const bool w1 = f.write(0x2001, 1), w2 = f.write(0x2002, 2);
    QVERIFY(w1 && w2);
    QTRY_COMPARE(f.state->bytes(), QByteArray("J1000\rP2001 0001\rP2002 0002\rJ1001\r"));
    f.input("K1001 0002\r");
    QTRY_VERIFY(f.state->bytes().endsWith("J1002\r"));
    f.input("K1002 0003\r");
    QTRY_COMPARE(f.results.size(), 5);
    const QList<quint16> expected{0x1000, 0x2001, 0x2002, 0x1001, 0x1002};
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(f.result(i).parameter, expected[i]);
        if (i)
            QVERIFY(f.state->submitted(i) - f.state->completed(i - 1) >= qint64(delay - 10) * 1000000);
    }
    f.exactlyOnce();
}
void CommunicationWorkerTests::timingLifecycle_data() {
    QTest::addColumn<int>("operation");
    QTest::newRow("disconnect-reconnect") << 0;
    QTest::newRow("shutdown") << 1;
    QTest::newRow("replace-connection") << 2;
}

void CommunicationWorkerTests::timingLifecycle() {
    QFETCH(int, operation);
    Fixture f;
    QVERIFY(f.connection.setInterCommandDelayMs(5000));
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.write(1, 1));
    QTRY_COMPARE(f.results.size(), 1);
    const bool queued = f.read(2);
    QVERIFY(queued);
    QTest::qWait(40);
    QCOMPARE(f.state->submissions(), 1);
    QElapsedTimer elapsed;
    elapsed.start();
    if (operation == 1)
        f.connection.shutdown();
    else {
        QVERIFY(operation == 0 ? f.connection.disconnectPort() : f.connection.connectPort({"FAKE_NEW", 9600}));
        QTRY_COMPARE(f.changes.size(), 2);
    }
    QTRY_COMPARE(f.results.size(), 2);
    QVERIFY(elapsed.elapsed() < 1000);
    QCOMPARE(f.result(1).kind, CommunicationWorker::Kind::Read);
    QCOMPARE(f.result(1).parameter, quint16(2));
    QCOMPARE(f.result(1).outcome, Outcome::Cancelled);
    QCOMPARE(f.state->submissions(), 1);
    if (operation != 1) {
        if (operation == 0) {
            f.open();
            QTRY_VERIFY(f.connection.isConnected());
        }
        QVERIFY(f.connection.isConnected());
        const auto ready = f.state->now();
        const bool fresh = f.write(3, 3);
        QVERIFY(fresh);
        QTRY_COMPARE(f.results.size(), 3);
        QCOMPARE(f.result(2).parameter, quint16(3));
        QCOMPARE(f.result(2).outcome, Outcome::BytesSent);
        QVERIFY(f.state->submitted(1) - ready < 500000000); // Old five-second deadline cannot leak into a reopened connection.
    }
    f.exactlyOnce();
}

void CommunicationWorkerTests::timingTimeoutIndependence() {
    constexpr int delay = 180, timeout = 70;
    Fixture f;
    QVERIFY(f.connection.setInterCommandDelayMs(delay));
    f.state->buffered = true;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    const bool first = f.write(1, 1);
    QVERIFY(first);
    QTRY_COMPARE(f.state->submissions(), 1);
    const bool queued = f.read(2, timeout);
    QVERIFY(queued);
    QTest::qWait(120);
    QCOMPARE(f.results.size(), 0);
    f.post([](State& s) {
        s.buffered = false;
        s.drain();
    });
    QTRY_COMPARE(f.results.size(), 2);
    QCOMPARE(f.state->submissions(), 2);
    QCOMPARE(f.result(0).outcome, Outcome::BytesSent);
    QCOMPARE(f.result(1).parameter, quint16(2));
    QCOMPARE(f.result(1).outcome, Outcome::Timeout);
    QVERIFY(f.state->submitted(1) - f.state->completed(0) >= qint64(delay - 10) * 1000000);
    QTRY_VERIFY(f.state->completed(1) >= 0); // Terminal event delivery can precede the direct timestamp observer.
    const auto active = f.state->completed(1) - f.state->submitted(1);
    QVERIFY(active >= qint64(timeout - 10) * 1000000);
    QVERIFY(active < 500000000);
    QVERIFY(!f.connection.isConnected());
    f.exactlyOnce();
}

void CommunicationWorkerTests::timingWakeupsNearExpiry() {
    Fixture f;
    QVERIFY(f.connection.setInterCommandDelayMs(2));
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QList<quint16> acceptedParameters;
    for (int i = 0; i < 40; ++i) {
        if (f.write(quint16(i + 1), 1)) acceptedParameters.append(quint16(i + 1));
        QTest::qWait(1);
    }
    QVERIFY(acceptedParameters.size() >= 2);
    QTRY_COMPARE(f.results.size(), acceptedParameters.size());
    QCOMPARE(f.state->submissions(), acceptedParameters.size());
    for (qsizetype i = 0; i < acceptedParameters.size(); ++i) {
        QCOMPARE(f.result(int(i)).parameter, acceptedParameters[i]);
        QCOMPARE(f.result(int(i)).outcome, Outcome::BytesSent);
    }
    f.exactlyOnce();
}

void CommunicationWorkerTests::serializedLifecycle() {
    Fixture f;
    QVERIFY(!f.read(0xEEEE));
    QVERIFY(!f.write(0xEEEE, 0xFFFF));
    QCOMPARE(f.connection.outstanding(), 0);
    f.open();
    QVERIFY(!f.connection.disconnectPort());
    QVERIFY(!f.connection.connectPort({"OVERLAP", 9600}));
    QVERIFY(!f.read(0xEEEE));
    QVERIFY(!f.write(0xEEEE, 0xFFFF));
    QTRY_COMPARE(f.changes.size(), 1);
    QVERIFY(f.connection.isConnected());
    QVERIFY(f.results.isEmpty());
    QCOMPARE(f.state->attemptCount(), 0);
    QByteArray expected;
    for (int i = 0; i < 12; ++i) {
        const auto before = f.changes.size();
        QVERIFY(f.connection.disconnectPort());
        QVERIFY(!f.connection.connectPort({"OVERLAP", 9600}));
        QVERIFY(!f.connection.disconnectPort());
        QVERIFY(!f.connection.setInterCommandDelayMs(1));
        QVERIFY(!f.read(0xEEEE));
        QVERIFY(!f.write(0xEEEE, 0xFFFF));
        QTRY_COMPARE(f.changes.size(), before + 1);
        QVERIFY(!f.connection.isConnected());
        QVERIFY(f.changes.last()[1].toString().isEmpty());
        f.open();
        QVERIFY(!f.connection.disconnectPort());
        QVERIFY(!f.connection.connectPort({"OVERLAP", 9600}));
        QVERIFY(!f.read(0xEEEE));
        QVERIFY(!f.write(0xEEEE, 0xFFFF));
        QTRY_COMPARE(f.changes.size(), before + 2);
        QVERIFY(f.connection.isConnected());
        QVERIFY(f.write(quint16(i + 1), quint16(i + 10)));
        QTRY_COMPARE(f.results.size(), i + 1);
        QCOMPARE(f.result(i).parameter, quint16(i + 1));
        QCOMPARE(f.result(i).value, quint16(i + 10));
        QCOMPARE(f.result(i).outcome, Outcome::BytesSent);
        expected += PICTextProtocol::encodeWrite(quint16(i + 1), quint16(i + 10));
        QCOMPARE(f.state->bytes(), expected);
    }
    f.exactlyOnce();
    QVERIFY(!f.connection.connectPort({"AFTER_SHUTDOWN", 9600}));
    QVERIFY(!f.connection.disconnectPort());
    QVERIFY(!f.connection.setInterCommandDelayMs(0));
    QVERIFY(!f.read(0xEEEE));
    QVERIFY(!f.write(0xEEEE, 0xFFFF));
    QCOMPARE(f.results.size(), 12);
    QCOMPARE(f.connection.outstanding(), 0);
    QCOMPARE(f.state->bytes(), expected);
    QCOMPARE(f.state->created, f.state->destroyed);
    QCOMPARE(f.state->affinityErrors, 0);
}

void CommunicationWorkerTests::creditsAfterFailure_data() {
    QTest::addColumn<bool>("timeout");
    QTest::newRow("timeout") << true;
    QTest::newRow("transport-error") << false;
}

void CommunicationWorkerTests::creditsAfterFailure() {
    QFETCH(bool, timeout);
    Fixture f;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.read(1, timeout ? 250 : 10000));
    QTRY_COMPARE(f.state->submissions(), 1);
    {
        QMutexLocker lock(&f.state->mutex);
        for (int i = 0; i < 31; ++i) QVERIFY(f.read(1));
        for (int i = 0; i < 32; ++i) QVERIFY(f.write(2, 3));
        if (!timeout) f.state->fault();
        f.state->changed.wakeAll();
    }
    QCOMPARE(f.connection.outstanding(), 64);
    QVERIFY(!f.read(1));
    QVERIFY(!f.write(2, 3));
    QTRY_COMPARE(f.results.size(), 64);
    QCOMPARE(f.result(0).outcome, timeout ? Outcome::Timeout : Outcome::TransportError);
    QCOMPARE(f.result(0).kind, CommunicationWorker::Kind::Read);
    int reads = 0, writes = 0;
    for (int i = 0; i < 64; ++i) {
        const auto result = f.result(i);
        if (i) QCOMPARE(result.outcome, Outcome::Cancelled);
        if (result.kind == CommunicationWorker::Kind::Read) {
            ++reads;
            QCOMPARE(result.parameter, quint16(1));
        } else {
            ++writes;
            QCOMPARE(result.parameter, quint16(2));
            QCOMPARE(result.value, quint16(3));
        }
    }
    QCOMPARE(reads, 32);
    QCOMPARE(writes, 32);
    QCOMPARE(f.connection.outstanding(), 0);
    QVERIFY(!f.connection.isConnected());
    QTRY_COMPARE(f.changes.size(), 2);
    if (timeout) {
        QVERIFY(f.result(0).error.isEmpty());
        QVERIFY(f.changes[1][1].toString().isEmpty());
    } else {
        QCOMPARE(f.result(0).error, QStringLiteral("injected port error"));
        QCOMPARE(f.changes[1][1].toString(), f.result(0).error);
    }
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.write(3, 123));
    QTRY_COMPARE(f.results.size(), 65);
    QCOMPARE(f.result(64).parameter, quint16(3));
    QCOMPARE(f.result(64).value, quint16(123));
    QCOMPARE(f.result(64).outcome, Outcome::BytesSent);
    f.exactlyOnce();
}

void CommunicationWorkerTests::shutdownFromFailureNotification() {
    Fixture f;
    int resultsAtShutdownReturn = -1;
    connect(&f.connection, &CommunicationConnection::connectionChanged, this,
        [&](bool connected, const QString&) {
            if (connected) return;
            f.connection.shutdown();
            resultsAtShutdownReturn = int(f.results.size());
        });
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.read(1, 10000));
    QTRY_COMPARE(f.state->submissions(), 1);
    {
        QMutexLocker lock(&f.state->mutex);
        QVERIFY(f.write(2, 3));
        QVERIFY(f.read(4));
        f.state->fault();
        f.state->changed.wakeAll();
    }
    QTRY_VERIFY(resultsAtShutdownReturn >= 0);
    QCOMPARE(resultsAtShutdownReturn, 3);
    QCOMPARE(f.result(0).outcome, Outcome::TransportError);
    QCOMPARE(f.result(1).outcome, Outcome::Cancelled);
    QCOMPARE(f.result(2).outcome, Outcome::Cancelled);
    f.exactlyOnce();
}

void CommunicationWorkerTests::lifecycleOutcomes_data() {
    QTest::addColumn<int>("operation");
    QTest::newRow("disconnect") << 0;
    QTest::newRow("shutdown") << 1;
    QTest::newRow("replacement") << 2;
}

void CommunicationWorkerTests::lifecycleOutcomes() {
    QFETCH(int, operation);
    Fixture f;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.read(0x10, 10000));
    QTRY_COMPARE(f.state->submissions(), 1);
    QVERIFY(f.write(0x20, 0x1234));
    QVERIFY(f.read(0x30));
    if (operation == 1) f.connection.shutdown();
    else {
        QVERIFY(operation == 0 ? f.connection.disconnectPort() : f.connection.connectPort({"NEW", 9600}));
        QTRY_COMPARE(f.changes.size(), 2);
        QCOMPARE(f.changes[1][0].toBool(), operation == 2);
        QVERIFY(f.changes[1][1].toString().isEmpty());
    }
    QTRY_COMPARE(f.results.size(), 3);
    const QList<quint16> parameters{0x10, 0x20, 0x30};
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(f.result(i).parameter, parameters[i]);
        QCOMPARE(f.result(i).outcome, Outcome::Cancelled);
        QVERIFY(f.result(i).error.isEmpty());
        QCOMPARE(f.result(i).errorCode, quint16(0));
    }
    QCOMPARE(f.result(1).value, quint16(0x1234));
    QCOMPARE(f.connection.outstanding(), 0);
    f.exactlyOnce();
    QCOMPARE(f.state->affinityErrors, 0);
    QCOMPARE(f.state->created, f.state->destroyed);
}

void CommunicationWorkerTests::reconnectFromResult_data() {
    QTest::addColumn<bool>("dispatchNested");
    QTest::newRow("control-pending-after-callback") << false;
    QTest::newRow("control-acknowledged-in-callback") << true;
}

void CommunicationWorkerTests::reconnectFromResult() {
    QFETCH(bool, dispatchNested);
    Fixture f;
    f.state->failWrite = true;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    bool reconnectAccepted = false;
    connect(&f.connection, &CommunicationConnection::requestFinished, this,
        [&](const Result& result) {
            if (result.outcome != Outcome::TransportError) return;
            QVERIFY(!result.error.isEmpty());
            QVERIFY(!f.connection.isConnected());
            f.post([](State& s) { s.failWrite = false; });
            reconnectAccepted = f.connection.connectPort({"NEW", 9600});
            QVERIFY(reconnectAccepted);
            if (dispatchNested) QTRY_VERIFY(f.connection.isConnected());
        });
    QVERIFY(f.read(1));
    QTRY_VERIFY(reconnectAccepted && f.connection.isConnected());
    QCOMPARE(f.results.size(), 1);
    QCOMPARE(f.changes.size(), 2); // Open then replacement: no old failure after the callback.
    QVERIFY(f.changes[0][0].toBool());
    QVERIFY(f.changes[1][0].toBool());
    QVERIFY(f.write(2, 123));
    QTRY_COMPARE(f.results.size(), 2);
    QCOMPARE(f.result(1).outcome, Outcome::BytesSent);
    QCOMPARE(f.result(1).parameter, quint16(2));
    QCOMPARE(f.result(1).value, quint16(123));
    f.exactlyOnce();
}

void CommunicationWorkerTests::submissionRejections_data() {
    QTest::addColumn<bool>("writing");
    QTest::addColumn<int>("timeout");
    QTest::newRow("read-zero-timeout") << false << 0;
    QTest::newRow("read-negative-timeout") << false << -1;
    QTest::newRow("write-zero-timeout") << true << 0;
    QTest::newRow("write-negative-timeout") << true << -1;
}

void CommunicationWorkerTests::submissionRejections() {
    QFETCH(bool, writing);
    QFETCH(int, timeout);
    Fixture f;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    const bool admitted = writing ? f.write(0xEEEE, 0xFFFF, timeout) : f.read(0xEEEE, timeout);
    QVERIFY(!admitted);
    QCOMPARE(f.connection.outstanding(), 0);
    QVERIFY(f.results.isEmpty());
    // A successful marker completion observes Worker processing without relying on a sleep.
    QVERIFY(f.write(0x4444, 0x1234));
    QTRY_COMPARE(f.results.size(), 1);
    QCOMPARE(f.result(0).kind, CommunicationWorker::Kind::Write);
    QCOMPARE(f.result(0).parameter, quint16(0x4444));
    QCOMPARE(f.result(0).value, quint16(0x1234));
    QCOMPARE(f.result(0).outcome, Outcome::BytesSent);
    QCOMPARE(f.state->bytes(), QByteArray("P4444 1234\r"));
    QCOMPARE(f.state->submissions(), 1);
    f.exactlyOnce();
    QCOMPARE(f.state->affinityErrors, 0);
}

QTEST_GUILESS_MAIN(CommunicationWorkerTests)
#include "tst_communicationworker.moc"
