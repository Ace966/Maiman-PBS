#include "serialdatasource.h"
#include "CommunicationConnection.h"
#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>
#include <cstring>
#include <limits>

namespace {
struct State {
    int created{0}, destroyed{0}, opened{0}, closed{0}, affinityErrors{0};
    bool failOpen{false}, failBaud{false}, nullFactory{false}, failRead{false}, failWrite{false};
    bool waitFault{false}, waitTimeout{false}, buffered{false};
    int readWaits{0}, writeWaits{0}, lastWait{0};
    qint64 pending{0};
    QByteArray readyOnWait;
    QString name;
    qint32 baud{0};
    QByteArray incoming, outgoing;
    QThread* owner{nullptr};
    QPointer<QSerialPort> observed;
};
class Port : public QSerialPort {
public:
    explicit Port(State& state) : s(state) { ++s.created; s.owner=QThread::currentThread(); s.observed=this; }
    ~Port() override { check(); ++s.destroyed; }
    bool open(OpenMode mode) override {
        check(); ++s.opened; s.name=portName(); s.baud=baudRate();
        if(s.failOpen) { setErrorString("injected open failure"); return false; }
        return QIODevice::open(mode|Unbuffered);
    }
    void close() override { check(); ++s.closed; QIODevice::close(); }
    qint64 bytesAvailable() const override { check(); return s.incoming.size()+QIODevice::bytesAvailable(); }
    qint64 bytesToWrite() const override { check(); return s.pending; }
    void input(const QByteArray& bytes) { s.incoming+=bytes; emit readyRead(); }
    void fault() { setErrorString("injected removal"); emit errorOccurred(ResourceError); }
    void configurationError() { setErrorString("injected baud failure"); }
    bool waitForReadyRead(int timeoutMs) override {
        check();++s.readWaits;s.lastWait=timeoutMs;
        if(s.waitFault){fault();return false;}
        if(s.waitTimeout){emit errorOccurred(TimeoutError);return false;}
        s.incoming+=s.readyOnWait;s.readyOnWait.clear();return !s.incoming.isEmpty();
    }
    bool waitForBytesWritten(int timeoutMs) override {
        check();++s.writeWaits;s.lastWait=timeoutMs;
        if(s.waitFault){fault();return false;}
        if(s.waitTimeout){emit errorOccurred(TimeoutError);return false;}
        s.pending=0;return true;
    }
protected:
    qint64 readData(char* data,qint64 n) override {
        check(); if(s.failRead) {setErrorString("read failed"); return -1;}
        n=qMin<qint64>(n,s.incoming.size()); std::memcpy(data,s.incoming.constData(),size_t(n)); s.incoming.remove(0,n); return n;
    }
    qint64 writeData(const char* data,qint64 n) override {
        check(); if(s.failWrite) {setErrorString("write failed"); return -1;}
        s.outgoing.append(data,n); if(s.buffered)s.pending+=n; return n;
    }
private:
    State& s;
    void check() const {if(thread()!=QThread::currentThread()) ++s.affinityErrors;}
};
class Source : public SerialDataSource {
public:
    explicit Source(State& state):s(state) {}
protected:
    QSerialPort* createSerialPort() override {
        if (s.nullFactory) return nullptr;
        return new Port(s);
    }
    bool configureBaudRate(QSerialPort& port,qint32 baud) override {
        if(s.failBaud) {static_cast<Port&>(port).configurationError(); return false;}
        return SerialDataSource::configureBaudRate(port,baud);
    }
private: State& s;
};
}
class SerialDataSourceTests : public QObject {
    Q_OBJECT
private slots:
    void lazyConstructionAndDefaultFactory();
    void settings_data();
    void settings();
    void ownershipAndRepeatedOpen();
    void failures_data();
    void failures();
    void blockingIo();
    void waitFailures_data();
    void waitFailures();
    void ioFailures_data();
    void ioFailures();
    void closedOperations();
    void independentInstances();
    void workerOwnershipAndAffinity();
};
void SerialDataSourceTests::lazyConstructionAndDefaultFactory() {
    State s; {Source source(s); QCOMPARE(s.created,0);}
    QCOMPARE(s.destroyed,0);
    struct Probe: SerialDataSource { using SerialDataSource::createSerialPort; } source;
    QScopedPointer<QSerialPort> port(source.createSerialPort()); QVERIFY(port); QVERIFY(!port->isOpen()); QVERIFY(!port->parent());
    QCOMPARE(port->thread(),QThread::currentThread());
}
void SerialDataSourceTests::settings_data() {
    QTest::addColumn<QString>("name"); QTest::addColumn<qint32>("baud");
    QTest::newRow("normal")<<QStringLiteral("FAKE_A")<<qint32(115200);
    QTest::newRow("minimum-positive")<<QStringLiteral("FAKE_A")<<qint32(1);
    QTest::newRow("max-typed")<<QStringLiteral("FAKE_A")<<std::numeric_limits<qint32>::max();
    QTest::newRow("verbatim-spaces")<<QStringLiteral("  FAKE_A  ")<<qint32(9600);
    QTest::newRow("verbatim-newline")<<QStringLiteral("FAKE\nA")<<qint32(9600);
}
void SerialDataSourceTests::settings() {
    QFETCH(QString,name); QFETCH(qint32,baud); State s; Source source(s);
    QVERIFY(source.open({name,baud})); QCOMPARE(s.name,name); QCOMPARE(s.baud,baud);
    QVERIFY(!s.observed->parent()); QVERIFY(!source.hasError());
    QCOMPARE(s.observed->dataBits(),QSerialPort::Data8); QCOMPARE(s.observed->parity(),QSerialPort::NoParity);
    QCOMPARE(s.observed->flowControl(),QSerialPort::NoFlowControl); QCOMPARE(s.observed->stopBits(),QSerialPort::OneStop);
    source.close(); QVERIFY(s.observed.isNull()); QCOMPARE(s.created,s.destroyed); QCOMPARE(s.closed,1);
}
void SerialDataSourceTests::ownershipAndRepeatedOpen() {
    State s;
    {Source source(s); QVERIFY(source.open({"FAKE_A",115200}));
     QVERIFY(!source.open({"FAKE_B",9600})); QCOMPARE(s.created,1); QVERIFY(!source.hasError());
     source.close(); source.close(); QVERIFY(s.observed.isNull());
     QVERIFY(source.open({"FAKE_B",9600})); QCOMPARE(s.name,QStringLiteral("FAKE_B"));}
    QCOMPARE(s.created,2); QCOMPARE(s.destroyed,2); QCOMPARE(s.closed,2);
}
void SerialDataSourceTests::failures_data() {
    QTest::addColumn<int>("which"); QTest::newRow("configure")<<0; QTest::newRow("open")<<1; QTest::newRow("factory")<<2;
}
void SerialDataSourceTests::failures() {
    QFETCH(int,which); State s; s.failBaud=which==0; s.failOpen=which==1; s.nullFactory=which==2;
    Source source(s);
    QVERIFY(!source.open({"FAKE_A",115200})); QVERIFY(source.hasError()); QVERIFY(!source.errorString().isEmpty());
    QCOMPARE(s.created,s.destroyed); QVERIFY(s.observed.isNull());
    s.failBaud=s.failOpen=s.nullFactory=false;
    QVERIFY(source.open({"FAKE_B",9600})); QVERIFY(!source.hasError()); source.close(); QCOMPARE(s.created,s.destroyed);
}
void SerialDataSourceTests::blockingIo() {
    State s;Source source(s);QVERIFY(source.open({"FAKE",115200}));
    auto* port=static_cast<Port*>(s.observed.data());port->input(QByteArray("a\0b",3));
    QVERIFY(source.waitForReadyRead(25));QCOMPARE(s.lastWait,25);QCOMPARE(source.bytesAvailable(),qint64(3));
    char data[8];QCOMPARE(source.read(data,8),qint64(3));QCOMPARE(QByteArray(data,3),QByteArray("a\0b",3));
    s.buffered=true;QCOMPARE(source.write("P0017 0384\r"),qint64(11));QCOMPARE(s.outgoing,QByteArray("P0017 0384\r"));
    QCOMPARE(source.bytesToWrite(),qint64(11));QVERIFY(source.waitForBytesWritten(30));QCOMPARE(s.lastWait,30);QCOMPARE(source.bytesToWrite(),qint64(0));
    port->fault();QVERIFY(source.hasError());QVERIFY(source.errorString().contains("injected removal"));
}
void SerialDataSourceTests::waitFailures_data() {
    QTest::addColumn<bool>("reading");QTest::addColumn<bool>("fault");
    QTest::newRow("read-timeout")<<true<<false;QTest::newRow("write-timeout")<<false<<false;
    QTest::newRow("read-error")<<true<<true;QTest::newRow("write-error")<<false<<true;
}
void SerialDataSourceTests::waitFailures() {
    QFETCH(bool,reading);QFETCH(bool,fault);State s;Source source(s);QVERIFY(source.open({"FAKE",115200}));
    s.waitFault=fault;s.waitTimeout=!fault;
    QVERIFY(!(reading?source.waitForReadyRead(17):source.waitForBytesWritten(17)));
    QCOMPARE(s.lastWait,17);QCOMPARE(source.hasError(),fault);
    if(fault)QVERIFY(source.errorString().contains("injected removal"));
    else {s.waitTimeout=false;QVERIFY(source.write("x")>0);QVERIFY(!source.hasError());}
}
void SerialDataSourceTests::ioFailures_data() {QTest::addColumn<bool>("reading"); QTest::newRow("read")<<true; QTest::newRow("write")<<false;}
void SerialDataSourceTests::ioFailures() {
    QFETCH(bool,reading); State s; Source source(s); QVERIFY(source.open({"FAKE",115200}));
    s.failRead=reading; s.failWrite=!reading; s.incoming="x"; char data[8];
    QCOMPARE(reading?source.read(data,8):source.write("x"),qint64(-1)); QVERIFY(source.hasError()); QVERIFY(!source.errorString().isEmpty());
}
void SerialDataSourceTests::closedOperations() {
    State s; Source source(s); char data[1]; source.close();
    QCOMPARE(source.read(data,1),qint64(-1)); QCOMPARE(source.write("x"),qint64(-1));
    QVERIFY(!source.waitForReadyRead(10));QVERIFY(!source.waitForBytesWritten(10));
    QCOMPARE(source.bytesAvailable(),qint64(0)); QCOMPARE(source.bytesToWrite(),qint64(0)); QVERIFY(source.hasError());
}
void SerialDataSourceTests::independentInstances() {
    State a,b; Source first(a),second(b); QVERIFY(first.open({"FAKE_A",9600})); QVERIFY(second.open({"FAKE_B",115200}));
    first.close(); QVERIFY(!second.hasError()); QCOMPARE(b.name,QStringLiteral("FAKE_B")); QCOMPARE(b.destroyed,0);
}
void SerialDataSourceTests::workerOwnershipAndAffinity() {
    State s;s.buffered=true;s.readyOnWait="K0017 0164\r";
    CommunicationConnection connection([&s]{return new Source(s);});
    QSignalSpy results(&connection,&CommunicationConnection::requestFinished);
    QVERIFY(connection.connectPort({"FAKE",115200})); QTRY_VERIFY(connection.isConnected());
    const auto id=connection.write(0x0017,900); QVERIFY(id); QTRY_COMPARE(results.size(),1);
    QCOMPARE(qvariant_cast<CommunicationWorker::Result>(results[0][0]).outcome,CommunicationWorker::Outcome::BytesSent);
    QVERIFY(connection.read(0x0017));QTRY_COMPARE(results.size(),2);
    QCOMPARE(qvariant_cast<CommunicationWorker::Result>(results[1][0]).value,quint16(356));
    connection.shutdown();QVERIFY(s.readWaits>0);QVERIFY(s.writeWaits>0); QCOMPARE(s.created,1); QCOMPARE(s.destroyed,1); QCOMPARE(s.affinityErrors,0);
    QVERIFY(s.observed.isNull()); QVERIFY(s.owner!=QThread::currentThread());
}
QTEST_GUILESS_MAIN(SerialDataSourceTests)
#include "tst_serialdatasource.moc"
