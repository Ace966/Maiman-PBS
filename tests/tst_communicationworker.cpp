#include "CommunicationConnection.h"
#include <QMutex>
#include <QPointer>
#include <QSignalSpy>
#include <QSemaphore>
#include <atomic>
#include <QThread>
#include <QTimer>
#include <QtTest>
#include <cstring>
#include <memory>

using Outcome=CommunicationWorker::Outcome;
using Result=CommunicationWorker::Result;
namespace {
class FakeSource;
struct State {
    QMutex mutex;
    QObject* mailbox{nullptr}; // Lives until this connection thread finishes.
    FakeSource* current{nullptr}; // Accessed only by worker/mailbox thread.
    QByteArray output;
    int created{0},destroyed{0},closed{0},affinityErrors{0},attempts{0},readCalls{0};
    bool failOpen{false},failWrite{false},failRead{false},buffered{false},autoDrain{false},stall{false};
    int maximum{100};
    QByteArray early;
    QThread* owner{nullptr};
    QByteArray bytes() {QMutexLocker lock(&mutex);return output;}
    int attemptCount(){QMutexLocker lock(&mutex);return attempts;}
    int readCount(){QMutexLocker lock(&mutex);return readCalls;}
};
class FakeSource:public IDataSource {
public:
    explicit FakeSource(std::shared_ptr<State> state):s(std::move(state)) {
        QMutexLocker lock(&s->mutex); ++s->created; s->current=this; s->owner=QThread::currentThread();
        if(!s->mailbox) {
            s->mailbox=new QObject;
            QObject::connect(QThread::currentThread(),&QThread::finished,s->mailbox,&QObject::deleteLater);
        }
        failOpen=s->failOpen;failWrite=s->failWrite;failRead=s->failRead;
        buffered=s->buffered;autoDrain=s->autoDrain;stall=s->stall;maximum=s->maximum;early=s->early;
    }
    ~FakeSource() override {check();QMutexLocker lock(&s->mutex);++s->destroyed;s->current=nullptr;}
    bool open(const SerialSettings&) override {check();if(!parent()) {QMutexLocker lock(&s->mutex);++s->affinityErrors;}
        if(failOpen){error="open failed";return false;}opened=true;return true;}
    void close() override {check();if(opened){opened=false;QMutexLocker lock(&s->mutex);++s->closed;}}
    qint64 write(const QByteArray& bytes) override {
        check();{QMutexLocker lock(&s->mutex);++s->attempts;}
        if(failWrite){error="write failed";return -1;}
        if(stall)return 0;
        const auto count=qMin<qsizetype>(maximum,bytes.size());
        {QMutexLocker lock(&s->mutex);s->output+=bytes.first(count);}
        if(buffered)pending+=count;
        if(!early.isEmpty()){const auto value=early;early.clear();input(value);}
        if(buffered&&autoDrain)QTimer::singleShot(0,this,[this]{drain();});
        return count;
    }
    qint64 read(char* data,qint64 count) override {
        check();{QMutexLocker lock(&s->mutex);++s->readCalls;}if(failRead){error="read failed";return -1;}
        count=qMin<qint64>(count,incoming.size());std::memcpy(data,incoming.constData(),size_t(count));incoming.remove(0,count);return count;
    }
    qint64 bytesAvailable() const override {check();return incoming.size();}
    qint64 bytesToWrite() const override {check();return pending;}
    bool hasError() const override {check();return !opened||!error.isEmpty();}
    QString errorString() const override {return error;}
    void input(const QByteArray& data){check();incoming+=data;emit readyRead();}
    void drain(){check();const auto count=pending;pending=0;emit bytesWritten(count);}
    void fault(){error="injected port error";emit errorOccurred(error);}
    bool failOpen,failWrite,failRead,buffered,autoDrain,stall;
    int maximum;
    QByteArray early;
private:
    std::shared_ptr<State> s;
    bool opened{false};qint64 pending{0};QByteArray incoming;QString error;
    void check()const{if(thread()!=QThread::currentThread()){QMutexLocker lock(&s->mutex);++s->affinityErrors;}}
};
struct Fixture {
    std::shared_ptr<State> state=std::make_shared<State>();
    CommunicationConnection connection{[s=state]{return new FakeSource(s);}};
    QSignalSpy results{&connection,&CommunicationConnection::requestFinished};
    QSignalSpy sent{&connection,&CommunicationConnection::bytesSent};
    QSignalSpy observed{&connection,&CommunicationConnection::responseReceived};
    QSignalSpy discarded{&connection,&CommunicationConnection::framesDiscarded};
    QSignalSpy changes{&connection,&CommunicationConnection::connectionChanged};
    ~Fixture(){connection.shutdown();}
    void open(){if(!connection.connectPort({"FAKE",115200}))qFatal("Fixture connect rejected");}
    void post(std::function<void(FakeSource&)> fn) {
        QObject* mailbox;{QMutexLocker lock(&state->mutex);mailbox=state->mailbox;}
        if(!mailbox)qFatal("Source mailbox not created");
        QMetaObject::invokeMethod(mailbox,[s=state,fn=std::move(fn)]{if(s->current)fn(*s->current);},Qt::QueuedConnection);
    }
    void input(const QByteArray& data){post([data](FakeSource& s){s.input(data);});}
    Result result(int i)const{return qvariant_cast<Result>(results.at(i).at(0));}
    void exactlyOnce()const {QSet<quint64> ids;for(const auto& row:results){const auto r=qvariant_cast<Result>(row[0]);QVERIFY(!ids.contains(r.id));ids.insert(r.id);}}
};
}
class CommunicationWorkerTests:public QObject {
    Q_OBJECT
private slots:
    void typedSettings_data();void typedSettings();
    void readFragmented();void priorityAndFifo();void duplicateFramesCannotCompleteNext();
    void responseKinds_data();void responseKinds();void unexpectedAndMalformed();
    void standardWrite();void partialOutput_data();void partialOutput();void earlyResponse();
    void writeDoesNotConfirmEcho();void earlyResponseStillTimesOutIfNotDrained();
    void lateResponseRequiresReconnect();void staleCallerCallback();
    void stalledOutput();void progressAfterZeroWrite();void deadlineNotExtended();
    void queuedCancel();void activeCancel();void boundedBurstAndShutdown();
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
    f.connection.read(0x17);QTRY_COMPARE(f.sent.size(),1);f.connection.read(0x17);
    f.input("K0017 0001\rK0017 0002\r");QTRY_COMPARE(f.sent.size(),2);QCOMPARE(f.results.size(),1);
    f.input("K0017 0003\r");QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(1).value,quint16(3));f.exactlyOnce();}
void CommunicationWorkerTests::responseKinds_data(){QTest::addColumn<QByteArray>("bytes");QTest::addColumn<int>("outcome");QTest::addColumn<bool>("writing");
    QTest::newRow("read-device-error")<<QByteArray("EFFFF\r")<<int(Outcome::DeviceError)<<false;
    QTest::newRow("read-nonexistent")<<QByteArray("K0000 0000\r")<<int(Outcome::NonexistentParameter)<<false;
    QTest::newRow("write-early-error")<<QByteArray("EFFFF\r")<<int(Outcome::DeviceError)<<true;
    QTest::newRow("write-early-nonexistent")<<QByteArray("K0000 0000\r")<<int(Outcome::NonexistentParameter)<<true;}
void CommunicationWorkerTests::responseKinds(){QFETCH(QByteArray,bytes);QFETCH(int,outcome);QFETCH(bool,writing);Fixture f;
    if(writing){f.state->buffered=true;f.state->early=bytes;}f.open();QTRY_VERIFY(f.connection.isConnected());
    if(writing){f.connection.write(0x17,0xabcd);QTRY_COMPARE(f.observed.size(),1);QCOMPARE(f.results.size(),0);f.post([](FakeSource& s){s.drain();});}
    else{f.connection.read(0x17);QTRY_COMPARE(f.sent.size(),1);f.input(bytes);}
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(int(f.result(0).outcome),outcome);QCOMPARE(f.result(0).parameter,quint16(0x17));
    QCOMPARE(f.result(0).value,quint16(writing?0xabcd:0));
    if(outcome==int(Outcome::DeviceError))QCOMPARE(f.result(0).errorCode,quint16(0xffff));QVERIFY(f.connection.isConnected());}
void CommunicationWorkerTests::unexpectedAndMalformed(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.read(0x17);QTRY_COMPARE(f.sent.size(),1);
    f.input("K0021 FFFF\rbad\r"+QByteArray(20,'x')+"\r");QTRY_COMPARE(f.observed.size(),2);QTRY_COMPARE(f.discarded.size(),1);QCOMPARE(f.results.size(),0);
    f.input("K0017 FFFF\r");QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).value,quint16(0xffff));}
void CommunicationWorkerTests::standardWrite(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());const auto id=f.connection.write(0x17,900);QVERIFY(id);
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::BytesSent);QCOMPARE(f.state->bytes(),QByteArray("P0017 0384\r"));
    QCOMPARE(f.observed.size(),0);QCOMPARE(f.sent.size(),1);QCOMPARE(f.connection.outstanding(),0);}
void CommunicationWorkerTests::partialOutput_data(){QTest::addColumn<bool>("buffered");QTest::newRow("buffered")<<true;QTest::newRow("unbuffered")<<false;}
void CommunicationWorkerTests::partialOutput(){QFETCH(bool,buffered);Fixture f;f.state->buffered=buffered;f.state->autoDrain=buffered;f.state->maximum=2;
    f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.write(0x17,900);QTRY_COMPARE(f.results.size(),1);
    QCOMPARE(f.result(0).outcome,Outcome::BytesSent);QCOMPARE(f.state->bytes(),QByteArray("P0017 0384\r"));f.connection.shutdown();QCOMPARE(f.state->attempts,6);}
void CommunicationWorkerTests::earlyResponse(){Fixture f;f.state->buffered=true;f.state->early="K0017 0164\r";f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.read(0x17);QTRY_COMPARE(f.observed.size(),1);QCOMPARE(f.sent.size(),0);QCOMPARE(f.results.size(),0);
    f.post([](FakeSource& s){s.drain();});QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::ReadResponse);QCOMPARE(f.result(0).value,quint16(356));}
void CommunicationWorkerTests::writeDoesNotConfirmEcho(){Fixture f;f.state->buffered=true;f.state->early="K0017 0384\r";f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.write(0x17,900);QTRY_COMPARE(f.observed.size(),1);QCOMPARE(f.results.size(),0);f.post([](FakeSource& s){s.drain();});
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::BytesSent);}
void CommunicationWorkerTests::earlyResponseStillTimesOutIfNotDrained(){Fixture f;f.state->buffered=true;f.state->early="K0017 0164\r";f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.read(0x17,80);QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::Timeout);QVERIFY(!f.connection.isConnected());}
void CommunicationWorkerTests::stalledOutput(){Fixture f;f.state->stall=true;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.write(0x17,1,50);
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::Timeout);f.connection.shutdown();QCOMPARE(f.state->attempts,1);}
void CommunicationWorkerTests::progressAfterZeroWrite(){Fixture f;f.state->stall=true;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.write(0x17,1);QTRY_COMPARE(f.state->attemptCount(),1);
    f.post([](FakeSource& s){s.stall=false;s.drain();});QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::BytesSent);}
void CommunicationWorkerTests::deadlineNotExtended(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());
    f.post([](FakeSource& s){auto* timer=new QTimer(&s);timer->setInterval(5);QObject::connect(timer,&QTimer::timeout,&s,[&s]{s.input("x");});timer->start();});
    QVERIFY(f.connection.read(0x17,150));QTRY_COMPARE(f.results.size(),1);QVERIFY(f.state->readCount()>0);QCOMPARE(f.result(0).outcome,Outcome::Timeout);QVERIFY(!f.connection.isConnected());
    QCOMPARE(f.connection.read(0x17),quint64(0));f.connection.shutdown();f.exactlyOnce();}
void CommunicationWorkerTests::queuedCancel(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.read(1);QTRY_COMPARE(f.sent.size(),1);
    const auto id=f.connection.write(2,3);QVERIFY(f.connection.cancel(id));QVERIFY(!f.connection.cancel(id));QTRY_COMPARE(f.results.size(),1);
    QCOMPARE(f.result(0).id,id);QCOMPARE(f.result(0).outcome,Outcome::Cancelled);QCOMPARE(f.state->bytes(),QByteArray("J0001\r"));QVERIFY(f.connection.isConnected());
    f.input("K0001 0005\r");QTRY_COMPARE(f.results.size(),2);f.exactlyOnce();}
void CommunicationWorkerTests::activeCancel(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());const auto id=f.connection.read(1);QTRY_COMPARE(f.sent.size(),1);
    f.connection.write(2,3);QVERIFY(f.connection.cancel(id));QTRY_COMPARE(f.results.size(),2);QVERIFY(!f.connection.isConnected());
    for(int i=0;i<2;++i)QCOMPARE(f.result(i).outcome,Outcome::Cancelled);f.exactlyOnce();}
void CommunicationWorkerTests::boundedBurstAndShutdown(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());QList<quint64> ids;ids.append(f.connection.read(1,10000));QTRY_COMPARE(f.sent.size(),1);
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
void CommunicationWorkerTests::transportFailure_data(){QTest::addColumn<int>("which");QTest::newRow("write")<<0;QTest::newRow("read")<<1;QTest::newRow("error-signal")<<2;}
void CommunicationWorkerTests::transportFailure(){QFETCH(int,which);Fixture f;f.state->failWrite=which==0;f.state->failRead=which==1;f.open();QTRY_VERIFY(f.connection.isConnected());
    f.connection.read(1);if(which){QTRY_COMPARE(f.sent.size(),1);if(which==1)f.input("K0001 0002\r");else f.post([](FakeSource& s){s.fault();});}
    QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).outcome,Outcome::TransportError);QVERIFY(!f.connection.isConnected());f.exactlyOnce();}
void CommunicationWorkerTests::disconnectActive(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.read(1);QTRY_COMPARE(f.sent.size(),1);f.connection.write(2,3);
    QVERIFY(f.connection.disconnectPort());QTRY_COMPARE(f.results.size(),2);QVERIFY(!f.connection.isConnected());f.exactlyOnce();}
void CommunicationWorkerTests::reconnectDropsStaleCallbacks(){Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.connection.read(1);QTRY_COMPARE(f.sent.size(),1);
    // Source callbacks emitted here are queued behind the already posted replacement-open operation.
    auto gate=std::make_shared<QMutex>();gate->lock();
    f.post([gate](FakeSource& s){QMutexLocker lock(gate.get());s.input("K0001 9999\r");s.fault();s.drain();});
    QVERIFY(f.connection.connectPort({"FAKE_NEW",9600}));gate->unlock();QTRY_VERIFY(f.connection.isConnected());QTRY_COMPARE(f.results.size(),1);
    QCOMPARE(f.result(0).outcome,Outcome::Cancelled);const auto id=f.connection.read(2);QVERIFY(id);QTRY_COMPARE(f.sent.size(),2);
    f.input("K0002 0007\r");QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(1).id,id);QCOMPARE(f.result(1).value,quint16(7));QVERIFY(f.connection.isConnected());f.exactlyOnce();}
void CommunicationWorkerTests::sessionParserReset_data(){QTest::addColumn<QByteArray>("old");QTest::newRow("partial")<<QByteArray("K0001");QTest::newRow("discard")<<QByteArray(12,'x');}
void CommunicationWorkerTests::sessionParserReset(){QFETCH(QByteArray,old);Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());f.input(old);QTRY_VERIFY(f.state->readCount()>0);
    QVERIFY(f.connection.connectPort({"FAKE_NEW",115200}));QTRY_VERIFY(f.connection.isConnected());f.connection.read(2);QTRY_COMPARE(f.sent.size(),1);
    f.input("K0002 0009\r");QTRY_COMPARE(f.results.size(),1);QCOMPARE(f.result(0).value,quint16(9));}
void CommunicationWorkerTests::independentConnections(){Fixture a,b;a.open();b.open();QTRY_VERIFY(a.connection.isConnected()&&b.connection.isConnected());
    a.connection.read(1);b.connection.read(2);QTRY_COMPARE(a.sent.size(),1);QTRY_COMPARE(b.sent.size(),1);
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
    const auto old=f.connection.read(1,100);const auto queued=f.connection.read(1);QVERIFY(queued);QTRY_COMPARE(f.sent.size(),1);
    QTRY_COMPARE(f.results.size(),2);QCOMPARE(f.result(0).id,old);QCOMPARE(f.result(0).outcome,Outcome::Timeout);
    QCOMPARE(f.result(1).id,queued);QCOMPARE(f.result(1).outcome,Outcome::Cancelled);QVERIFY(!f.connection.isConnected());
    QCOMPARE(f.connection.read(1),quint64(0));
    auto delivered=std::make_shared<std::atomic_bool>(false);
    QObject* mailbox;{QMutexLocker lock(&f.state->mutex);mailbox=f.state->mailbox;}
    QMetaObject::invokeMethod(mailbox,[s=f.state,delivered]{if(s->current)s->current->input("K0001 1111\r");delivered->store(true);},Qt::QueuedConnection);
    QTRY_VERIFY(delivered->load());QCOMPARE(f.observed.size(),0);QCOMPARE(f.results.size(),2);
    f.open();QTRY_VERIFY(f.connection.isConnected());const auto id=f.connection.read(1);QVERIFY(id>queued);QTRY_COMPARE(f.sent.size(),2);
    f.input("K0001 2222\r");QTRY_COMPARE(f.results.size(),3);QCOMPARE(f.result(2).id,id);QCOMPARE(f.result(2).value,quint16(0x2222));f.exactlyOnce();
}
void CommunicationWorkerTests::staleCallerCallback(){
    Fixture f;f.open();QTRY_VERIFY(f.connection.isConnected());auto emitted=std::make_shared<QSemaphore>();
    f.post([emitted](FakeSource& source){auto* worker=qobject_cast<CommunicationWorker*>(source.parent());
        QObject::connect(worker,&CommunicationWorker::responseReceived,worker,[emitted](const PICTextProtocol::Response&,quint64){emitted->release();},Qt::DirectConnection);
        source.input("K7777 0001\r");});
    QVERIFY(emitted->tryAcquire(1,2000)); // Test-only synchronization; do not dispatch caller callbacks yet.
    QVERIFY(f.connection.connectPort({"FAKE_NEW",115200}));QTRY_VERIFY(f.connection.isConnected());QCOMPARE(f.observed.size(),0);
    f.input("K2222 0002\r");QTRY_COMPARE(f.observed.size(),1);
    QCOMPARE(qvariant_cast<PICTextProtocol::Response>(f.observed[0][0]).parameter,quint16(0x2222));
}
QTEST_GUILESS_MAIN(CommunicationWorkerTests)
#include "tst_communicationworker.moc"
