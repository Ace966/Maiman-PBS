#include "CommunicationConnection.h"
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QPointer>
#include <QSignalSpy>
#include <QSemaphore>
#include <QThread>
#include <QWaitCondition>
#include <QtTest>
#include <cstring>
#include <memory>
#include <limits>

using Outcome=CommunicationWorker::Outcome;
using Result=CommunicationWorker::Result;
namespace {
struct State {
    QMutex mutex;
    QWaitCondition changed;
    QElapsedTimer clock;
    QList<qint64> submissionNs;
    QHash<quint64,qint64> completionNs;
    State() { clock.start(); }
    QByteArray output, incoming, early;
    QString error;
    qint64 pending{0};
    int created{0},destroyed{0},closed{0},affinityErrors{0},attempts{0},readCalls{0},readWaits{0},writeWaits{0};
    bool failOpen{false},failWrite{false},failRead{false},buffered{false},autoDrain{false},stall{false},noise{false},opened{false};
    int maximum{100};
    QThread* owner{nullptr};
    QByteArray bytes() {QMutexLocker lock(&mutex);return output;}
    int attemptCount(){QMutexLocker lock(&mutex);return attempts;}
    int readCount(){QMutexLocker lock(&mutex);return readCalls;}
    int submissions() {
        QMutexLocker lock(&mutex);
        return submissionNs.size();
    }
    qint64 submitted(int i) {
        QMutexLocker lock(&mutex);
        return submissionNs.at(i);
    }
    qint64 completed(quint64 id) {
        QMutexLocker lock(&mutex);
        return completionNs.value(id, -1);
    }
    qint64 now() { return clock.nsecsElapsed(); }
    // Controllers call these under mutex; they never invoke the transport QObject.
    void input(const QByteArray& data){if(opened)incoming+=data;}
    void drain(){pending=0;}
    void fault(){if(opened)error="injected port error";}
};
class FakeSource:public IDataSource {
public:
    explicit FakeSource(std::shared_ptr<State> state):s(std::move(state)) {
        QMutexLocker lock(&s->mutex);++s->created;s->owner=QThread::currentThread();
        s->incoming.clear();s->error.clear();s->pending=0;s->opened=false;
    }
    ~FakeSource() override {check();QMutexLocker lock(&s->mutex);++s->destroyed;}
    bool open(const SerialSettings&) override {
        check();QMutexLocker lock(&s->mutex);if(parent())++s->affinityErrors;
        if(s->failOpen){s->error="open failed";return false;}s->opened=true;return true;
    }
    void close() override {check();QMutexLocker lock(&s->mutex);if(s->opened){s->opened=false;++s->closed;}s->changed.wakeAll();}
    qint64 write(const QByteArray& bytes) override {
        check();QMutexLocker lock(&s->mutex);++s->attempts;
        if(s->failWrite){s->error="write failed";return -1;}
        if(s->stall)return 0;
        const auto count=qMin<qsizetype>(s->maximum,bytes.size());
        if(count>0)s->submissionNs.append(s->clock.nsecsElapsed());
        s->output+=bytes.first(count);
        if(s->buffered)s->pending+=count;
        if(!s->early.isEmpty()){s->input(s->early);s->early.clear();}
        return count;
    }
    qint64 read(char* data,qint64 count) override {
        check();QMutexLocker lock(&s->mutex);++s->readCalls;
        if(s->failRead){s->error="read failed";return -1;}
        count=qMin<qint64>(count,s->incoming.size());std::memcpy(data,s->incoming.constData(),size_t(count));s->incoming.remove(0,count);return count;
    }
    qint64 bytesAvailable() const override {check();QMutexLocker lock(&s->mutex);return s->incoming.size();}
    qint64 bytesToWrite() const override {check();QMutexLocker lock(&s->mutex);return s->pending;}
    bool hasError() const override {check();QMutexLocker lock(&s->mutex);return !s->opened||!s->error.isEmpty();}
    QString errorString() const override {check();QMutexLocker lock(&s->mutex);return s->error;}
    bool waitForReadyRead(int timeoutMs) override {
        check();QMutexLocker lock(&s->mutex);++s->readWaits;
        if(timeoutMs<=0||timeoutMs>20)++s->affinityErrors;
        QDeadlineTimer deadline(timeoutMs);
        while(s->incoming.isEmpty()&&s->opened&&s->error.isEmpty()&&!deadline.hasExpired()) {
            if(s->noise){s->changed.wait(&s->mutex,qMin(5,timeoutMs));s->incoming+="x";break;}
            s->changed.wait(&s->mutex,deadline);
        }
        return !s->incoming.isEmpty();
    }
    bool waitForBytesWritten(int timeoutMs) override {
        check();QMutexLocker lock(&s->mutex);++s->writeWaits;
        if(timeoutMs<=0||timeoutMs>20)++s->affinityErrors;
        if(s->autoDrain){s->pending=0;return true;}
        QDeadlineTimer deadline(timeoutMs);
        while(s->pending&&s->opened&&s->error.isEmpty()&&!deadline.hasExpired())s->changed.wait(&s->mutex,deadline);
        return s->pending==0;
    }
private:
    std::shared_ptr<State> s;
    void check()const{if(thread()!=QThread::currentThread()){QMutexLocker lock(&s->mutex);++s->affinityErrors;}}
};
struct Fixture {
    std::shared_ptr<State> state=std::make_shared<State>();
    CommunicationConnection connection{[s=state]{return new FakeSource(s);}};
    QSignalSpy results{&connection,&CommunicationConnection::requestFinished};
    QSignalSpy changes{&connection,&CommunicationConnection::connectionChanged};
    Fixture(){
        auto* worker=connection.findChild<CommunicationWorker*>();
        QObject::connect(worker,&CommunicationWorker::requestFinished,&connection,[s=state](const Result& result){
            const auto now = s->clock.nsecsElapsed();
            QMutexLocker lock(&s->mutex);
            s->completionNs.insert(result.id, now);
            s->changed.wakeAll();
        },Qt::DirectConnection);
    }
    ~Fixture(){connection.shutdown();}
    void open(){if(!connection.connectPort({"FAKE",115200}))qFatal("Fixture connect rejected");}
    void post(std::function<void(State&)> fn) {QMutexLocker lock(&state->mutex);fn(*state);state->changed.wakeAll();}
    void input(const QByteArray& data){post([data](State& s){s.input(data);});}
    Result result(int i)const{return qvariant_cast<Result>(results.at(i).at(0));}
    void exactlyOnce()const {QSet<quint64> ids;for(const auto& row:results){const auto r=qvariant_cast<Result>(row[0]);QVERIFY(!ids.contains(r.id));ids.insert(r.id);}}
};
}
class CommunicationWorkerTests:public QObject {
    Q_OBJECT
private slots:
    void typedSettings_data();void typedSettings();
    void idleUsesSemaphore();void shutdownInterruptsLongWait();
    void timingConfiguration();
    void timingZero_data();
    void timingZero();
    void timingMinimum_data();
    void timingMinimum();
    void timingIdle();
    void timingPriority();
    void timingQueuedCancel();
    void timingLifecycle_data();
    void timingLifecycle();
    void timingTimeoutIndependence();
    void timingWakeupsNearExpiry();
    void readFragmented();void priorityAndFifo();void duplicateFramesCannotCompleteNext();
    void responseKinds_data();void responseKinds();void unexpectedAndMalformed();
    void standardWrite();void partialOutput_data();void partialOutput();void earlyResponse();
    void writeDoesNotConfirmEcho();void earlyResponseStillTimesOutIfNotDrained();
    void lateResponseRequiresReconnect();void staleCallerCallback();
    void stalledOutput();void progressAfterZeroWrite();void deadlineNotExtended();
    void queuedCancel();void activeCancel();void boundedBurstAndShutdown();void completedResultsRemainBounded();
    void openFailureAndRetry();void nullFactory();void transportFailure_data();void transportFailure();
    void disconnectActive();void reconnectDropsStaleCallbacks();void sessionParserReset_data();void sessionParserReset();
    void independentConnections();void destructionCompletesPending();void shutdownWhileOpening();
};
void CommunicationWorkerTests::typedSettings_data(){
    QTest::addColumn<SerialSettings>("settings");
    QTest::newRow("empty")<<SerialSettings{{},115200};QTest::newRow("blank")<<SerialSettings{" \t",115200};
    QTest::newRow("NUL")<<SerialSettings{QString("FAKE")+QChar(0)+"suffix",115200};
    QTest::newRow("zero-baud")<<SerialSettings{"FAKE",0};QTest::newRow("negative-baud")<<SerialSettings{"FAKE",-1};
}
void CommunicationWorkerTests::typedSettings(){QFETCH(SerialSettings,settings);Fixture f;
    QVERIFY(!f.connection.connectPort(settings));QVERIFY(!f.connection.lastSubmissionError().isEmpty());
    QCOMPARE(f.connection.read(1),quint64(0));f.connection.shutdown();QCOMPARE(f.state->created,0);}
void CommunicationWorkerTests::readFragmented(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    const auto id=f.connection.read(0x0017);QVERIFY(id);QTRY_COMPARE(f.state->bytes(),QByteArray("J0017\r"));
    f.input("K0017");f.input(" 01");f.input("64\r");QTRY_COMPARE(f.results.size(),1);
    QCOMPARE(f.result(0).id,id);QCOMPARE(f.result(0).outcome,Outcome::ReadResponse);QCOMPARE(f.result(0).value,quint16(356));
    f.connection.shutdown();QCOMPARE(f.state->affinityErrors,0);QCOMPARE(f.state->created,f.state->destroyed);
    QVERIFY(f.state->owner!=QThread::currentThread());f.exactlyOnce();}
void CommunicationWorkerTests::priorityAndFifo(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    const auto active=f.connection.read(0x1000);QTRY_COMPARE(f.state->bytes(),QByteArray("J1000\r"));
    const auto r1=f.connection.read(0x1001),w1=f.connection.write(0x2001,1),r2=f.connection.read(0x1002),w2=f.connection.write(0x2002,2);
    QVERIFY(r1&&w1&&r2&&w2);QCOMPARE(f.state->bytes(),QByteArray("J1000\r"));
    f.input("K1000 0001\r");QTRY_COMPARE(f.state->bytes(),QByteArray("J1000\rP2001 0001\rP2002 0002\rJ1001\r"));
    f.input("K1001 0002\r");QTRY_VERIFY(f.state->bytes().endsWith("J1002\r"));f.input("K1002 0003\r");QTRY_COMPARE(f.results.size(),5);
    const QList<quint64> expected{active,w1,w2,r1,r2};for(int i=0;i<5;++i)QCOMPARE(f.result(i).id,expected[i]);f.exactlyOnce();}
void CommunicationWorkerTests::duplicateFramesCannotCompleteNext(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.read(0x17);QTRY_COMPARE(f.state->submissions(),1);f.connection.read(0x17);
    f.input("K0017 0001\rK0017 0002\r");QTRY_COMPARE(f.state->submissions(),2);QTRY_COMPARE(f.results.size(),1);
    f.input("K0017 0003\r");QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(1).value,quint16(3));f.exactlyOnce();}
void CommunicationWorkerTests::responseKinds_data(){QTest::addColumn<QByteArray>("bytes");QTest::addColumn<int>("outcome");QTest::addColumn<bool>("writing");
    QTest::newRow("read-device-error")<<QByteArray("EFFFF\r")<<int(Outcome::DeviceError)<<false;
    QTest::newRow("read-nonexistent")<<QByteArray("K0000 0000\r")<<int(Outcome::NonexistentParameter)<<false;
    QTest::newRow("write-early-error")<<QByteArray("EFFFF\r")<<int(Outcome::DeviceError)<<true;
    QTest::newRow("write-early-nonexistent")<<QByteArray("K0000 0000\r")<<int(Outcome::NonexistentParameter)<<true;}
void CommunicationWorkerTests::responseKinds(){QFETCH(QByteArray,bytes);QFETCH(int,outcome);QFETCH(bool,writing);Fixture f;
    if(writing){f.state->buffered=true;f.state->early=bytes;}f.open();QTRY_VERIFY(f.connection.isConnected());
    if(writing){f.connection.write(0x17,0xabcd);QTRY_VERIFY(f.state->readCount()>0);QCOMPARE(f.results.size(),0);f.post([](State& s){s.drain();});}
    else{f.connection.read(0x17);QTRY_COMPARE(f.state->submissions(),1);f.input(bytes);}
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(int(f.result(0).outcome),outcome);QCOMPARE(f.result(0).parameter,quint16(0x17));
    QCOMPARE(f.result(0).value,quint16(writing?0xabcd:0));
    if(outcome==int(Outcome::DeviceError))QCOMPARE(f.result(0).errorCode,quint16(0xffff));QVERIFY(f.connection.isConnected());}
void CommunicationWorkerTests::unexpectedAndMalformed(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.read(0x17);QTRY_COMPARE(f.state->submissions(),1);
    f.input("K0021 FFFF\rbad\r"+QByteArray(20,'x')+"\r");QTRY_VERIFY(f.state->readCount()>0);QCOMPARE(f.results.size(),0);
    f.input("K0017 FFFF\r");QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).value,quint16(0xffff));}
void CommunicationWorkerTests::standardWrite(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());const auto id=f.connection.write(0x17,900);QVERIFY(id);
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::BytesSent);QCOMPARE(f.state->bytes(),QByteArray("P0017 0384\r"));
    QCOMPARE(f.state->submissions(),1);QCOMPARE(f.connection.outstanding(),0);}
void CommunicationWorkerTests::partialOutput_data(){QTest::addColumn<bool>("buffered");QTest::newRow("buffered")<<true;QTest::newRow("unbuffered")<<false;}
void CommunicationWorkerTests::partialOutput(){QFETCH(bool,buffered);Fixture f;f.state->buffered=buffered;f.state->autoDrain=buffered;f.state->maximum=2;
    f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.write(0x17,900);QTRY_COMPARE(f.results.size(),1);
    QCOMPARE(f.result(0).outcome,Outcome::BytesSent);QCOMPARE(f.state->bytes(),QByteArray("P0017 0384\r"));f.connection.shutdown();QCOMPARE(f.state->attempts,6);}
void CommunicationWorkerTests::earlyResponse(){Fixture f;f.state->buffered=true;f.state->early="K0017 0164\r";f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.read(0x17);QTRY_VERIFY(f.state->readCount()>0);{QMutexLocker lock(&f.state->mutex);QVERIFY(f.state->pending>0);}QCOMPARE(f.results.size(),0);
    f.post([](State& s){s.drain();});QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::ReadResponse);QCOMPARE(f.result(0).value,quint16(356));}
void CommunicationWorkerTests::writeDoesNotConfirmEcho(){Fixture f;f.state->buffered=true;f.state->early="K0017 0384\r";f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.write(0x17,900);QTRY_VERIFY(f.state->readCount()>0);QCOMPARE(f.results.size(),0);f.post([](State& s){s.drain();});
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::BytesSent);}
void CommunicationWorkerTests::earlyResponseStillTimesOutIfNotDrained(){Fixture f;f.state->buffered=true;f.state->early="K0017 0164\r";f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.read(0x17,80);QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::Timeout);QVERIFY(!f.connection.isConnected());}
void CommunicationWorkerTests::stalledOutput(){Fixture f;f.state->stall=true;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.write(0x17,1,50);
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::Timeout);f.connection.shutdown();QVERIFY(f.state->attempts>=1&&f.state->attempts<=8);}
void CommunicationWorkerTests::progressAfterZeroWrite(){Fixture f;f.state->stall=true;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.write(0x17,1);QTRY_VERIFY(f.state->attemptCount()>0);
    f.post([](State& s){s.stall=false;s.drain();});QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::BytesSent);}
void CommunicationWorkerTests::deadlineNotExtended(){Fixture f;f.state->noise=true;f.open();QTRY_VERIFY(f.connection.isConnected());
    QElapsedTimer elapsed;elapsed.start();QVERIFY(f.connection.read(0x17,150));QTRY_COMPARE(f.results.size(),1);
    QVERIFY(f.state->readCount()>0);QCOMPARE(f.result(0).outcome,Outcome::Timeout);QVERIFY(!f.connection.isConnected());QVERIFY(elapsed.elapsed()<1000);
    QCOMPARE(f.connection.read(0x17),quint64(0));f.connection.shutdown();f.exactlyOnce();}
void CommunicationWorkerTests::queuedCancel(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.read(1);QTRY_COMPARE(f.state->submissions(),1);
    const auto id=f.connection.write(2,3);QVERIFY(f.connection.cancel(id));QVERIFY(!f.connection.cancel(id));QTRY_COMPARE(f.results.size(),1);
    QCOMPARE(f.result(0).id,id);QCOMPARE(f.result(0).outcome,Outcome::Cancelled);QCOMPARE(f.state->bytes(),QByteArray("J0001\r"));QVERIFY(f.connection.isConnected());
    f.input("K0001 0005\r");QTRY_COMPARE(f.results.size(),2);f.exactlyOnce();}
void CommunicationWorkerTests::activeCancel(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());const auto id=f.connection.read(1,10000);QTRY_COMPARE(f.state->submissions(),1);
    f.connection.write(2,3);QElapsedTimer elapsed;elapsed.start();QVERIFY(f.connection.cancel(id));QTRY_COMPARE(f.results.size(),2);QVERIFY(elapsed.elapsed()<1000);QVERIFY(!f.connection.isConnected());
    for(int i=0;i<2;++i)QCOMPARE(f.result(i).outcome,Outcome::Cancelled);f.exactlyOnce();}
void CommunicationWorkerTests::boundedBurstAndShutdown(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());QList<quint64> ids;ids.append(f.connection.read(1,10000));QTRY_COMPARE(f.state->submissions(),1);
    for(int i=0;i<5000;++i)if(auto id=f.connection.read(1,10000))ids.append(id);
    for(int i=0;i<5000;++i)if(auto id=f.connection.write(2,3,10000))ids.append(id);
    QCOMPARE(ids.size(),64);QCOMPARE(f.connection.outstanding(),64);int cancellations=0;
    for(int i=0;i<5000;++i)cancellations+=f.connection.cancel(ids.last());QCOMPARE(cancellations,1);
    f.connection.shutdown();QCOMPARE(f.results.size(),64);QCOMPARE(f.connection.outstanding(),0);f.exactlyOnce();
    for(const auto& row:f.results)QCOMPARE(qvariant_cast<Result>(row[0]).outcome,Outcome::Cancelled);
    auto* thread=f.connection.findChild<QThread*>();QVERIFY(thread);QVERIFY(!thread->isRunning());}
void CommunicationWorkerTests::openFailureAndRetry(){Fixture f;f.state->failOpen=true;f.open();QTRY_COMPARE(f.changes.size(),1);QVERIFY(!f.connection.isConnected());
    QCOMPARE(f.connection.read(1),quint64(0));{QMutexLocker lock(&f.state->mutex);f.state->failOpen=false;}
    f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.write(1,2);QTRY_COMPARE(f.results.size(),1);f.connection.shutdown();QCOMPARE(f.state->created,2);QCOMPARE(f.state->destroyed,2);}
void CommunicationWorkerTests::nullFactory(){CommunicationConnection c([]()->IDataSource*{return nullptr;});QSignalSpy changes(&c,&CommunicationConnection::connectionChanged);
    QVERIFY(c.connectPort({"FAKE",115200}));QTRY_COMPARE(changes.size(),1);QVERIFY(!c.isConnected());QVERIFY(!changes[0][1].toString().isEmpty());}
void CommunicationWorkerTests::transportFailure_data(){QTest::addColumn<int>("which");QTest::newRow("write")<<0;QTest::newRow("read")<<1;QTest::newRow("blocking-wait-error")<<2;}
void CommunicationWorkerTests::transportFailure(){QFETCH(int,which);Fixture f;f.state->failWrite=which==0;f.state->failRead=which==1;f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.read(1);if(which){QTRY_COMPARE(f.state->submissions(),1);if(which==1)f.input("K0001 0002\r");else f.post([](State& s){s.fault();});}
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::TransportError);QVERIFY(!f.connection.isConnected());f.exactlyOnce();}
void CommunicationWorkerTests::disconnectActive(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.read(1);QTRY_COMPARE(f.state->submissions(),1);f.connection.write(2,3);
    QVERIFY(f.connection.disconnectPort());QTRY_COMPARE(f.results.size(),2);QVERIFY(!f.connection.isConnected());f.exactlyOnce();}
void CommunicationWorkerTests::reconnectDropsStaleCallbacks(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.read(1);QTRY_COMPARE(f.state->submissions(),1);
    // Hold only fake state while posting replacement: old data/fault cannot run in the new source.
    {QMutexLocker lock(&f.state->mutex);QVERIFY(f.connection.connectPort({"FAKE_NEW",9600}));
     f.state->input("K0001 9999\r");f.state->fault();f.state->changed.wakeAll();}
    QTRY_VERIFY(f.connection.isConnected());QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::Cancelled);
    const auto id=f.connection.read(2);QVERIFY(id);QTRY_COMPARE(f.state->submissions(),2);f.input("K0002 0007\r");
    QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(1).id,id);QCOMPARE(f.result(1).value,quint16(7));QVERIFY(f.connection.isConnected());f.exactlyOnce();}
void CommunicationWorkerTests::sessionParserReset_data(){QTest::addColumn<QByteArray>("old");QTest::newRow("partial")<<QByteArray("K0001");QTest::newRow("discard")<<QByteArray(12,'x');}
void CommunicationWorkerTests::sessionParserReset(){QFETCH(QByteArray,old);Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.read(1);QTRY_COMPARE(f.state->submissions(),1);f.input(old);QTRY_VERIFY(f.state->readCount()>0);
    QVERIFY(f.connection.connectPort({"FAKE_NEW",115200}));QTRY_VERIFY(f.connection.isConnected());f.connection.read(2);QTRY_COMPARE(f.state->submissions(),2);
    f.input("K0002 0009\r");QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(0).outcome,Outcome::Cancelled);QCOMPARE(f.result(1).value,quint16(9));}
void CommunicationWorkerTests::independentConnections(){Fixture a,b;a.open();b.open();QTRY_VERIFY(a.connection.isConnected()&&b.connection.isConnected());
    a.connection.read(1);b.connection.read(2);QTRY_COMPARE(a.state->submissions(),1);QTRY_COMPARE(b.state->submissions(),1);
    a.input("K0001 0003\r");b.input("K0002 0004\r");QTRY_COMPARE(a.results.size(),1);QTRY_COMPARE(b.results.size(),1);
    QCOMPARE(a.result(0).value,quint16(3));QCOMPARE(b.result(0).value,quint16(4));a.connection.shutdown();b.connection.shutdown();
    QVERIFY(a.state->owner!=b.state->owner);QCOMPARE(a.state->affinityErrors+b.state->affinityErrors,0);}
void CommunicationWorkerTests::destructionCompletesPending(){auto state=std::make_shared<State>();QList<Result> results;QObject consumer;
    auto connection=std::make_unique<CommunicationConnection>([state]{return new FakeSource(state);});
    connect(connection.get(),&CommunicationConnection::requestFinished,&consumer,[&results](const Result& r){results.append(r);});
    QPointer<QThread> thread=connection->findChild<QThread*>();QVERIFY(connection->connectPort({"FAKE",115200}));QTRY_VERIFY(connection->isConnected());
    QVERIFY(connection->read(1));QVERIFY(connection->write(2,3));connection.reset();QCOMPARE(results.size(),2);QVERIFY(thread.isNull());QCOMPARE(state->created,state->destroyed);}
void CommunicationWorkerTests::shutdownWhileOpening(){Fixture f;f.open();f.connection.shutdown();QVERIFY(!f.connection.isConnected());QCOMPARE(f.state->created,f.state->destroyed);QVERIFY(!f.connection.connectPort({"FAKE",115200}));}
void CommunicationWorkerTests::lateResponseRequiresReconnect(){
    Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    const auto old=f.connection.read(1,100);const auto queued=f.connection.read(1);QVERIFY(queued);QTRY_COMPARE(f.state->submissions(),1);
    QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(0).id,old);QCOMPARE(f.result(0).outcome,Outcome::Timeout);
    QCOMPARE(f.result(1).id,queued);QCOMPARE(f.result(1).outcome,Outcome::Cancelled);QVERIFY(!f.connection.isConnected());
    QCOMPARE(f.connection.read(1),quint64(0));
    f.input("K0001 1111\r");QTest::qWait(30);QCOMPARE(f.results.size(),2);
    f.open();QTRY_VERIFY(f.connection.isConnected());const auto id=f.connection.read(1);QVERIFY(id>queued);QTRY_COMPARE(f.state->submissions(),2);
    f.input("K0001 2222\r");QTRY_COMPARE(f.results.size(),3);QCOMPARE(f.result(2).id,id);QCOMPARE(f.result(2).value,quint16(0x2222));f.exactlyOnce();
}
void CommunicationWorkerTests::staleCallerCallback() {
    Fixture f;
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QSemaphore emitted;
    auto* worker = f.connection.findChild<CommunicationWorker*>();
    QVERIFY(worker);
    const auto observer = connect(worker, &CommunicationWorker::connectionChanged, worker,
        [&emitted](bool connected, const QString&, quint64) {
            if (!connected) emitted.release();
        }, Qt::DirectConnection);
    const auto old = f.connection.read(1);
    QTRY_COMPARE(f.state->submissions(), 1);
    f.post([](State& s) { s.fault(); });
    // Do not dispatch caller events: the old failure must remain queued at the facade.
    const bool seen = emitted.tryAcquire(1, 2000);
    disconnect(observer);
    QVERIFY(seen);
    QVERIFY(f.connection.connectPort({"FAKE_NEW", 115200}));
    QTRY_VERIFY(f.connection.isConnected());
    QTRY_COMPARE(f.results.size(), 1);
    QCOMPARE(f.result(0).id, old);
    QCOMPARE(f.result(0).outcome, Outcome::TransportError);
    QCOMPARE(f.changes.size(), 2); // Initial open and replacement, no stale disconnect.
    QVERIFY(f.changes[0][0].toBool());
    QVERIFY(f.changes[1][0].toBool());
    const auto fresh = f.connection.read(2);
    QVERIFY(fresh > old);
    QTRY_COMPARE(f.state->submissions(), 2);
    f.input("K0002 0003\r");
    QTRY_COMPARE(f.results.size(), 2);
    QCOMPARE(f.result(1).id, fresh);
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
        QVERIFY(f.connection.write(1, quint16(i)));
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
    QCOMPARE(f.connection.write(1, 1), quint64(0));
    for (int i = 0; i < CommunicationWorker::QueueCapacity; ++i)
        QVERIFY(f.connection.read(2, 10000));
    QCOMPARE(f.connection.read(2), quint64(0));
    QCOMPARE(f.connection.outstanding(), 2 * CommunicationWorker::QueueCapacity);
    f.connection.shutdown();
    QCOMPARE(f.results.size(), 2 * CommunicationWorker::QueueCapacity);
    QCOMPARE(f.connection.outstanding(), 0);
    for (int i = 0; i < f.results.size(); ++i)
        QCOMPARE(f.result(i).outcome, i < CommunicationWorker::QueueCapacity ? Outcome::BytesSent : Outcome::Cancelled);
    QVERIFY(callerAffinity);
    f.exactlyOnce();
}

void CommunicationWorkerTests::idleUsesSemaphore(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());QTest::qWait(40);
    {QMutexLocker lock(&f.state->mutex);QCOMPARE(f.state->attempts,0);QCOMPARE(f.state->readWaits,0);QCOMPARE(f.state->writeWaits,0);}
    f.connection.shutdown();QCOMPARE(f.state->created,f.state->destroyed);
}
void CommunicationWorkerTests::shutdownInterruptsLongWait(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    const auto id=f.connection.read(1,10000);QTRY_COMPARE(f.state->submissions(),1);QElapsedTimer elapsed;elapsed.start();f.connection.shutdown();
    QVERIFY(elapsed.elapsed()<1000);QCOMPARE(f.results.size(),1);QCOMPARE(f.result(0).id,id);QCOMPARE(f.result(0).outcome,Outcome::Cancelled);f.exactlyOnce();
}

void CommunicationWorkerTests::timingConfiguration() {
    Fixture f;
    QVERIFY(!f.connection.setInterCommandDelayMs(-1));
    QVERIFY(!f.connection.lastSubmissionError().isEmpty());
    QVERIFY(f.connection.setInterCommandDelayMs(std::numeric_limits<int>::max()));
    QVERIFY(f.connection.setInterCommandDelayMs(0));
    QVERIFY(f.connection.lastSubmissionError().isEmpty());
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
    const auto a = f.connection.write(1, 1), b = f.connection.write(2, 2);
    QVERIFY(a && b);
    QTRY_COMPARE(f.results.size(), 2);
    QCOMPARE(f.state->submissions(), 2);
    QCOMPARE(f.result(0).outcome, Outcome::BytesSent);
    QCOMPARE(f.result(1).outcome, Outcome::BytesSent);
    const auto gap = f.state->submitted(1) - f.state->completed(a);
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
    const auto first = writing ? f.connection.write(1, 1) : f.connection.read(1);
    QVERIFY(first);
    QTRY_COMPARE(f.state->submissions(), 1);
    const auto next = f.connection.write(2, 2);
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
    QCOMPARE(f.result(0).id, first);
    QCOMPARE(f.result(0).outcome, writing ? Outcome::BytesSent : Outcome::ReadResponse);
    QCOMPARE(f.result(1).id, next);
    QCOMPARE(f.result(1).outcome, Outcome::BytesSent);
    const auto gap = f.state->submitted(1) - f.state->completed(first);
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
    const auto first = f.connection.write(1, 1);
    QVERIFY(first);
    QTRY_COMPARE(f.results.size(), 1);
    QTest::qWait(delay + 100);
    const auto ready = f.state->now();
    QVERIFY(f.connection.write(2, 2));
    QTRY_COMPARE(f.results.size(), 2);
    const auto submitted = f.state->submitted(1);
    QVERIFY(submitted - f.state->completed(first) >= qint64(delay) * 1000000);
    QVERIFY(submitted - ready < 150000000);
    f.exactlyOnce();
}

void CommunicationWorkerTests::timingPriority() {
    constexpr int delay = 180;
    Fixture f;
    QVERIFY(f.connection.setInterCommandDelayMs(delay));
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    const auto active = f.connection.read(0x1000);
    QTRY_COMPARE(f.state->submissions(), 1);
    const auto r1 = f.connection.read(0x1001), r2 = f.connection.read(0x1002);
    QVERIFY(active && r1 && r2);
    f.input("K1000 0001\r");
    QTRY_COMPARE(f.results.size(), 1);
    QTest::qWait(20);
    QCOMPARE(f.state->submissions(), 1); // Reads remain queued during the gap.
    const auto w1 = f.connection.write(0x2001, 1), w2 = f.connection.write(0x2002, 2);
    QVERIFY(w1 && w2);
    QTRY_COMPARE(f.state->bytes(), QByteArray("J1000\rP2001 0001\rP2002 0002\rJ1001\r"));
    f.input("K1001 0002\r");
    QTRY_VERIFY(f.state->bytes().endsWith("J1002\r"));
    f.input("K1002 0003\r");
    QTRY_COMPARE(f.results.size(), 5);
    const QList<quint64> expected{active, w1, w2, r1, r2};
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(f.result(i).id, expected[i]);
        if (i)
            QVERIFY(f.state->submitted(i) - f.state->completed(expected[i - 1]) >= qint64(delay - 10) * 1000000);
    }
    f.exactlyOnce();
}

void CommunicationWorkerTests::timingQueuedCancel() {
    Fixture f;
    QVERIFY(f.connection.setInterCommandDelayMs(5000));
    f.open();
    QTRY_VERIFY(f.connection.isConnected());
    QVERIFY(f.connection.write(1, 1));
    QTRY_COMPARE(f.results.size(), 1);
    const auto queued = f.connection.read(2);
    QVERIFY(queued);
    QTest::qWait(40);
    QCOMPARE(f.state->submissions(), 1);
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(f.connection.cancel(queued));
    QTRY_COMPARE(f.results.size(), 2);
    QVERIFY(elapsed.elapsed() < 1000);
    QCOMPARE(f.result(1).id, queued);
    QCOMPARE(f.result(1).outcome, Outcome::Cancelled);
    QVERIFY(f.connection.isConnected());
    QCOMPARE(f.state->submissions(), 1);
    QCOMPARE(f.connection.outstanding(), 0);
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
    QVERIFY(f.connection.write(1, 1));
    QTRY_COMPARE(f.results.size(), 1);
    const auto queued = f.connection.read(2);
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
    QCOMPARE(f.result(1).id, queued);
    QCOMPARE(f.result(1).outcome, Outcome::Cancelled);
    QCOMPARE(f.state->submissions(), 1);
    if (operation != 1) {
        if (operation == 0) {
            f.open();
            QTRY_VERIFY(f.connection.isConnected());
        }
        QVERIFY(f.connection.isConnected());
        const auto ready = f.state->now();
        const auto fresh = f.connection.write(3, 3);
        QVERIFY(fresh > queued);
        QTRY_COMPARE(f.results.size(), 3);
        QCOMPARE(f.result(2).id, fresh);
        QCOMPARE(f.result(2).outcome, Outcome::BytesSent);
        QVERIFY(f.state->submitted(1) - ready < 500000000); // Old five-second deadline cannot leak into a new session.
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
    const auto first = f.connection.write(1, 1);
    QVERIFY(first);
    QTRY_COMPARE(f.state->submissions(), 1);
    const auto queued = f.connection.read(2, timeout);
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
    QCOMPARE(f.result(1).id, queued);
    QCOMPARE(f.result(1).outcome, Outcome::Timeout);
    QVERIFY(f.state->submitted(1) - f.state->completed(first) >= qint64(delay - 10) * 1000000);
    QTRY_VERIFY(f.state->completed(queued) >= 0); // Terminal event delivery can precede the direct timestamp observer.
    const auto active = f.state->completed(queued) - f.state->submitted(1);
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
    QList<quint64> accepted;
    for (int i = 0; i < 40; ++i) {
        if (const auto id = f.connection.write(quint16(i + 1), 1))
            accepted.append(id);
        QTest::qWait(1);
    }
    QVERIFY(accepted.size() >= 2);
    QTRY_COMPARE(f.results.size(), accepted.size());
    QCOMPARE(f.state->submissions(), accepted.size());
    for (qsizetype i = 0; i < accepted.size(); ++i) {
        QCOMPARE(f.result(int(i)).id, accepted[i]);
        QCOMPARE(f.result(int(i)).outcome, Outcome::BytesSent);
    }
    f.exactlyOnce();
}

QTEST_GUILESS_MAIN(CommunicationWorkerTests)
#include "tst_communicationworker.moc"
