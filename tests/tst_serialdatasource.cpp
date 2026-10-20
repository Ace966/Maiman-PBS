#include "IDataSource.h"
#include "serialdatasource.h"
#include "DataThread.h"

#include <QThread>
#include <QtTest>
#include <limits>
#include <memory>
#include <type_traits>
#include <cstring>

static_assert(std::is_abstract_v<IDataSource>);
static_assert(std::has_virtual_destructor_v<IDataSource>);
static_assert(!std::is_copy_constructible_v<SerialDataSource>);
static_assert(!std::is_copy_assignable_v<SerialDataSource>);

namespace {

QVariantMap settings(const QString& port = QStringLiteral("PBS_FAKE_A"),
                     const QVariant& baud = 115200) {
    return {{QStringLiteral("comport"), port}, {QStringLiteral("baudrate"), baud}};
}

struct PortState {
    int factoryCalls{0};
    int constructed{0};
    int destroyed{0};
    int openCalls{0};
    int closeCalls{0};
    bool failOpen{false};
    bool failBaud{false};
    bool nullFactory{false};
    QString openedName;
    qint32 openedBaud{0};
    QByteArray incoming, outgoing;
    int readCalls{0}, writeCalls{0}, readWaits{0}, writeWaits{0};
    int lastReadWait{-1}, lastWriteWait{-1};
    bool ioMode{false}, failRead{false}, failWrite{false};
    qint64 pending{0};
    QThread* creationThread{nullptr};
    int affinityErrors{0};
};

class FakeSerialPort : public QSerialPort {
public:
    explicit FakeSerialPort(PortState& state) : m_state(state) {
        m_state.creationThread = QThread::currentThread();
        ++m_state.constructed;
    }
    ~FakeSerialPort() override {
        checkThread();
        if (isOpen()) close();
        ++m_state.destroyed;
    }
    bool open(OpenMode mode) override {
        checkThread();
        ++m_state.openCalls;
        m_state.openedName = portName();
        m_state.openedBaud = baudRate();
        if (m_state.failOpen) {
            setErrorString(QStringLiteral("injected open failure"));
            return false;
        }
        // Only QIODevice's in-memory state changes; no serial backend is opened.
        return QIODevice::open(m_state.ioMode ? mode | QIODevice::Unbuffered : mode);
    }
    void close() override {
        checkThread();
        ++m_state.closeCalls;
        QIODevice::close();
    }
    void injectBaudFailure() {
        setErrorString(QStringLiteral("injected baud failure"));
    }

    qint64 bytesAvailable() const override { checkThread(); return m_state.incoming.size() + QIODevice::bytesAvailable(); }
    qint64 bytesToWrite() const override { checkThread(); return m_state.pending; }
    bool waitForReadyRead(int milliseconds) override {
        checkThread();
        ++m_state.readWaits; m_state.lastReadWait = milliseconds;
        return !m_state.incoming.isEmpty();
    }
    bool waitForBytesWritten(int milliseconds) override {
        checkThread();
        ++m_state.writeWaits; m_state.lastWriteWait = milliseconds;
        const bool progressed = m_state.pending > 0;
        m_state.pending = 0;
        return progressed;
    }

protected:
    qint64 readData(char* data, qint64 maximum) override {
        checkThread();
        ++m_state.readCalls;
        if (m_state.failRead) { setErrorString(QStringLiteral("injected read failure")); return -1; }
        const qint64 count = qMin<qint64>(maximum, m_state.incoming.size());
        std::memcpy(data, m_state.incoming.constData(), size_t(count));
        m_state.incoming.remove(0, count);
        return count;
    }
    qint64 writeData(const char* data, qint64 count) override {
        checkThread();
        ++m_state.writeCalls;
        if (m_state.failWrite) { setErrorString(QStringLiteral("injected write failure")); return -1; }
        m_state.outgoing.append(data, count);
        return count;
    }

private:
    void checkThread() const {
        if (thread() != QThread::currentThread()) ++m_state.affinityErrors;
    }
    PortState& m_state;
};

class FakeSerialDataSource : public SerialDataSource {
public:
    explicit FakeSerialDataSource(PortState& state) : m_state(state) {}

protected:
    std::unique_ptr<QSerialPort> createSerialPort() override {
        ++m_state.factoryCalls;
        if (m_state.nullFactory) return {};
        return std::make_unique<FakeSerialPort>(m_state);
    }
    bool configureBaudRate(QSerialPort& port, qint32 baud) override {
        if (m_state.failBaud) {
            static_cast<FakeSerialPort&>(port).injectBaudFailure();
            return false;
        }
        return SerialDataSource::configureBaudRate(port, baud);
    }

private:
    PortState& m_state;
};

class DefaultPortProbe : public SerialDataSource {
public:
    using SerialDataSource::createSerialPort;
};

} // namespace

class SerialDataSourceTests : public QObject {
    Q_OBJECT

private slots:
    void polymorphicDestruction();
    void constructionDestructionDoesNotAllocate();
    void defaultFactoryCreatesOwnedUnopenedPort();
    void uninitializedSourceDoesNotAllocate();
    void validConfiguration_data();
    void validConfiguration();
    void portNameIsPassedVerbatim_data();
    void portNameIsPassedVerbatim();
    void invalidConfiguration_data();
    void invalidConfiguration();
    void reinitializationReplacesAndInvalidatesSettings();
    void callerOwnsDeviceAfterSourceDestruction();
    void repeatedCreationRequiresCallerCleanup();
    void reconfigureDoesNotTouchActiveDevice();
    void independentInstancesUseDifferentSettings();
    void configurationFailureCleansUp();
    void failedOpenCleansUpAndCanRetry();
    void nullFactoryResultIsReported();
    void createsDeviceInCallingThread();
    void operationsWithoutDeviceAreSafe();
    void delegatedReadWriteAndWaits();
    void delegatedFailures();
    void closeDoesNotTakeOwnership();
    void backendErrorIsReported();
    void workerUsesOperationalSerialSource();
};

void SerialDataSourceTests::polymorphicDestruction() {
    struct DestructionProbe : SerialDataSource {
        explicit DestructionProbe(bool& destroyed) : flag(destroyed) {}
        ~DestructionProbe() override { flag = true; }
        void init(const QVariantMap&) override {}
        QIODevice* createAndConnect() override { return nullptr; }
        bool& flag;
    };
    bool destroyed = false;
    {
        std::unique_ptr<IDataSource> source = std::make_unique<DestructionProbe>(destroyed);
        QVERIFY(!destroyed);
    }
    QVERIFY(destroyed);
}

void SerialDataSourceTests::constructionDestructionDoesNotAllocate() {
    PortState state;
    {
        SerialDataSource defaultSource;
        FakeSerialDataSource fakeSource(state);
        fakeSource.init(settings());
        QVERIFY(fakeSource.errorString().isEmpty());
    }
    QCOMPARE(state.factoryCalls, 0);
    QCOMPARE(state.constructed, 0);
    QCOMPARE(state.openCalls, 0);
}

void SerialDataSourceTests::defaultFactoryCreatesOwnedUnopenedPort() {
    DefaultPortProbe source;
    QPointer<QSerialPort> observer;
    {
        auto port = source.createSerialPort();
        QVERIFY(port);
        observer = port.get();
        QVERIFY(!port->isOpen());
        QVERIFY(port->parent() == nullptr);
        QCOMPARE(port->thread(), QThread::currentThread());
    }
    QVERIFY(observer.isNull());
}

void SerialDataSourceTests::uninitializedSourceDoesNotAllocate() {
    PortState state;
    FakeSerialDataSource source(state);
    QVERIFY(source.createAndConnect() == nullptr);
    QVERIFY(source.errorString().contains(QStringLiteral("initialized")));
    QCOMPARE(state.factoryCalls, 0);
}

void SerialDataSourceTests::validConfiguration_data() {
    QTest::addColumn<QVariant>("baud");
    QTest::addColumn<qint32>("expected");
    QTest::newRow("integer") << QVariant(115200) << qint32(115200);
    QTest::newRow("minimum-positive") << QVariant(1) << qint32(1);
    QTest::newRow("maximum-qint32") << QVariant(std::numeric_limits<qint32>::max())
                                    << std::numeric_limits<qint32>::max();
}

void SerialDataSourceTests::validConfiguration() {
    QFETCH(QVariant, baud);
    QFETCH(qint32, expected);
    PortState state;
    FakeSerialDataSource source(state);
    source.init(settings(QStringLiteral("PBS_FAKE_A"), baud));
    QVERIFY(source.errorString().isEmpty());
    std::unique_ptr<QIODevice> device(source.createAndConnect());
    QVERIFY(device);
    auto* port = qobject_cast<QSerialPort*>(device.get());
    QVERIFY(port);
    QVERIFY(port->isOpen());
    QCOMPARE(port->openMode(), QIODevice::ReadWrite);
    QCOMPARE(port->portName(), QStringLiteral("PBS_FAKE_A"));
    QCOMPARE(port->baudRate(), expected);
    QVERIFY(port->parent() == nullptr);
    QCOMPARE(port->thread(), QThread::currentThread());
    QCOMPARE(state.factoryCalls, 1);
    QCOMPARE(state.openCalls, 1);
    QVERIFY(source.errorString().isEmpty());
    device.reset();
    QCOMPARE(state.constructed, state.destroyed);
    QCOMPARE(state.closeCalls, 1);
}

void SerialDataSourceTests::portNameIsPassedVerbatim_data() {
    QTest::addColumn<QString>("name");
    QTest::newRow("padded-name") << QStringLiteral("  PBS_FAKE_A  ");
    QTest::newRow("newline-name") << QStringLiteral("PBS\nFAKE");
    QTest::newRow("del-name") << (QStringLiteral("PBS") + QChar(0x7f) + QStringLiteral("FAKE"));
}

void SerialDataSourceTests::portNameIsPassedVerbatim() {
    QFETCH(QString, name);
    PortState state;
    FakeSerialDataSource source(state);
    source.init(settings(name));
    std::unique_ptr<QIODevice> device(source.createAndConnect());
    QVERIFY(device);
    // Fake open records the literal identifier, not platform name validity.
    QCOMPARE(state.openedName, name);
    device.reset();
    QCOMPARE(state.constructed, state.destroyed);
    QCOMPARE(state.closeCalls, 1);
}

void SerialDataSourceTests::invalidConfiguration_data() {
    QTest::addColumn<QVariantMap>("configuration");
    QTest::addColumn<QString>("errorField");
    const auto portError = QStringLiteral("comport");
    const auto baudError = QStringLiteral("baudrate");
    QTest::newRow("missing-port") << QVariantMap{{QStringLiteral("baudrate"), 115200}} << portError;
    QTest::newRow("empty-port") << settings(QString()) << portError;
    QTest::newRow("blank-port") << settings(QStringLiteral("   ")) << portError;
    QTest::newRow("non-string-port") << QVariantMap{{QStringLiteral("comport"), 42},
                                                    {QStringLiteral("baudrate"), 115200}} << portError;
    QTest::newRow("blank-tab-port") << settings(QStringLiteral("\t \r\n")) << portError;
    QTest::newRow("nul-port") << settings(QString(QChar(0))) << portError;
    QTest::newRow("embedded-nul-port")
        << settings(QStringLiteral("PBS_FAKE_A") + QChar(0) + QStringLiteral("suffix")) << portError;
    QTest::newRow("missing-baud") << QVariantMap{{QStringLiteral("comport"), QStringLiteral("PBS_FAKE_A")}} << baudError;
    QTest::newRow("zero-baud") << settings(QStringLiteral("PBS_FAKE_A"), 0) << baudError;
    QTest::newRow("negative-baud") << settings(QStringLiteral("PBS_FAKE_A"), -1) << baudError;
    QTest::newRow("bad-text-baud") << settings(QStringLiteral("PBS_FAKE_A"), QStringLiteral("fast")) << baudError;
    QTest::newRow("numeric-string-baud") << settings(QStringLiteral("PBS_FAKE_A"), QStringLiteral("9600")) << baudError;
    QTest::newRow("unsigned-baud") << settings(QStringLiteral("PBS_FAKE_A"), uint(57600)) << baudError;
    QTest::newRow("wide-integer-baud") << settings(QStringLiteral("PBS_FAKE_A"), qlonglong(9600)) << baudError;
    QTest::newRow("wide-unsigned-baud") << settings(QStringLiteral("PBS_FAKE_A"), qulonglong(9600)) << baudError;
    QTest::newRow("integral-double-baud") << settings(QStringLiteral("PBS_FAKE_A"), 19200.0) << baudError;
    QTest::newRow("integral-float-baud") << settings(QStringLiteral("PBS_FAKE_A"), 19200.0f) << baudError;
    QTest::newRow("bool-baud") << settings(QStringLiteral("PBS_FAKE_A"), true) << baudError;
    QTest::newRow("fractional-baud") << settings(QStringLiteral("PBS_FAKE_A"), 9600.5) << baudError;
    QTest::newRow("nan-baud") << settings(QStringLiteral("PBS_FAKE_A"), std::numeric_limits<double>::quiet_NaN()) << baudError;
    QTest::newRow("infinite-baud") << settings(QStringLiteral("PBS_FAKE_A"), std::numeric_limits<double>::infinity()) << baudError;
    QTest::newRow("double-overflow") << settings(QStringLiteral("PBS_FAKE_A"), 2147483648.0) << baudError;
    QTest::newRow("integer-overflow") << settings(QStringLiteral("PBS_FAKE_A"), qlonglong(2147483648LL)) << baudError;
    QTest::newRow("string-overflow") << settings(QStringLiteral("PBS_FAKE_A"), QStringLiteral("2147483648")) << baudError;
    QTest::newRow("unsigned-overflow") << settings(QStringLiteral("PBS_FAKE_A"), std::numeric_limits<qulonglong>::max()) << baudError;
}

void SerialDataSourceTests::invalidConfiguration() {
    QFETCH(QVariantMap, configuration);
    QFETCH(QString, errorField);
    PortState state;
    FakeSerialDataSource source(state);
    source.init(configuration);
    const auto error = source.errorString();
    QVERIFY2(error.contains(errorField), qPrintable(error));
    QVERIFY(source.createAndConnect() == nullptr);
    QCOMPARE(source.errorString(), error);
    QCOMPARE(state.factoryCalls, 0);
    QCOMPARE(state.openCalls, 0);
}

void SerialDataSourceTests::reinitializationReplacesAndInvalidatesSettings() {
    PortState state;
    FakeSerialDataSource source(state);
    source.init(settings());
    source.init({{QStringLiteral("comport"), QStringLiteral("PBS_INVALID")}});
    QVERIFY(source.createAndConnect() == nullptr);
    QCOMPARE(state.factoryCalls, 0);
    source.init(settings(QStringLiteral("PBS_FAKE_B"), 9600));
    std::unique_ptr<QIODevice> device(source.createAndConnect());
    QVERIFY(device);
    QCOMPARE(state.openedName, QStringLiteral("PBS_FAKE_B"));
    QCOMPARE(state.openedBaud, qint32(9600));
    QVERIFY(source.errorString().isEmpty());
    device.reset();
    source.init(settings(QStringLiteral("PBS_FAKE_C"), 0));
    QVERIFY(source.createAndConnect() == nullptr);
    source.init(settings(QStringLiteral("PBS_FAKE_C"), 19200));
    device.reset(source.createAndConnect());
    QVERIFY(device);
    QCOMPARE(state.openedName, QStringLiteral("PBS_FAKE_C"));
    QCOMPARE(state.openedBaud, qint32(19200));
    QVERIFY(source.errorString().isEmpty());
    device.reset();
    QCOMPARE(state.constructed, state.destroyed);
}

void SerialDataSourceTests::callerOwnsDeviceAfterSourceDestruction() {
    PortState state;
    std::unique_ptr<QIODevice> device;
    QPointer<QIODevice> observer;
    {
        std::unique_ptr<IDataSource> source = std::make_unique<FakeSerialDataSource>(state);
        source->init(settings());
        device.reset(source->createAndConnect());
        QVERIFY(device);
        observer = device.get();
    }
    QVERIFY(observer);
    QVERIFY(device->isOpen());
    QCOMPARE(state.destroyed, 0);
    QCOMPARE(state.closeCalls, 0);
    device.reset();
    QVERIFY(observer.isNull());
    QCOMPARE(state.destroyed, 1);
    QCOMPARE(state.closeCalls, 1);
}

void SerialDataSourceTests::repeatedCreationRequiresCallerCleanup() {
    PortState state;
    FakeSerialDataSource source(state);
    source.init(settings());
    std::unique_ptr<QIODevice> device(source.createAndConnect());
    QVERIFY(device);
    QPointer<QIODevice> observer(device.get());
    QVERIFY(source.createAndConnect() == nullptr);
    QVERIFY(source.errorString().contains(QStringLiteral("caller")));
    QCOMPARE(state.factoryCalls, 1);
    QCOMPARE(state.closeCalls, 0);
    QVERIFY(observer && device->isOpen());
    device->close();
    QVERIFY(source.createAndConnect() == nullptr);
    QCOMPARE(state.factoryCalls, 1);
    QCOMPARE(state.closeCalls, 1);
    device.reset();
    QVERIFY(observer.isNull());
    device.reset(source.createAndConnect());
    QVERIFY(device);
    QCOMPARE(state.factoryCalls, 2);
    QVERIFY(source.errorString().isEmpty());
    device.reset();
    QCOMPARE(state.constructed, 2);
    QCOMPARE(state.destroyed, 2);
}

void SerialDataSourceTests::reconfigureDoesNotTouchActiveDevice() {
    PortState state;
    FakeSerialDataSource source(state);
    source.init(settings());
    std::unique_ptr<QIODevice> device(source.createAndConnect());
    QVERIFY(device);
    auto* port = qobject_cast<QSerialPort*>(device.get());
    source.init(settings(QStringLiteral("PBS_FAKE_B"), 9600));
    QCOMPARE(port->portName(), QStringLiteral("PBS_FAKE_A"));
    QCOMPARE(port->baudRate(), qint32(115200));
    QVERIFY(source.createAndConnect() == nullptr);
    QCOMPARE(state.closeCalls, 0);
    device.reset();
    device.reset(source.createAndConnect());
    QVERIFY(device);
    QCOMPARE(state.openedName, QStringLiteral("PBS_FAKE_B"));
    QCOMPARE(state.openedBaud, qint32(9600));
    source.init(settings(QStringLiteral("PBS_FAKE_C"), -1));
    QVERIFY(source.createAndConnect() == nullptr);
    QVERIFY(device->isOpen());
    QCOMPARE(qobject_cast<QSerialPort*>(device.get())->portName(), QStringLiteral("PBS_FAKE_B"));
    QCOMPARE(state.closeCalls, 1);
    device.reset();
    QCOMPARE(state.constructed, state.destroyed);
}

void SerialDataSourceTests::independentInstancesUseDifferentSettings() {
    PortState firstState;
    PortState secondState;
    FakeSerialDataSource first(firstState);
    FakeSerialDataSource second(secondState);
    first.init(settings(QStringLiteral("PBS_FAKE_A"), 9600));
    second.init(settings(QStringLiteral("PBS_FAKE_B"), 115200));
    std::unique_ptr<QIODevice> firstDevice(first.createAndConnect());
    std::unique_ptr<QIODevice> secondDevice(second.createAndConnect());
    QVERIFY(firstDevice && secondDevice);
    QVERIFY(firstDevice.get() != secondDevice.get());
    QCOMPARE(firstState.openedName, QStringLiteral("PBS_FAKE_A"));
    QCOMPARE(firstState.openedBaud, qint32(9600));
    QCOMPARE(secondState.openedName, QStringLiteral("PBS_FAKE_B"));
    QCOMPARE(secondState.openedBaud, qint32(115200));
    first.init(settings(QStringLiteral("PBS_BAD"), 0));
    QVERIFY(!first.errorString().isEmpty());
    QVERIFY(second.errorString().isEmpty());
    QVERIFY(firstDevice->isOpen() && secondDevice->isOpen());
    firstDevice.reset();
    secondDevice.reset();
    QCOMPARE(firstState.destroyed, 1);
    QCOMPARE(secondState.destroyed, 1);
}

void SerialDataSourceTests::configurationFailureCleansUp() {
    PortState state;
    state.failBaud = true;
    FakeSerialDataSource source(state);
    source.init(settings());
    QVERIFY(source.createAndConnect() == nullptr);
    QVERIFY(source.errorString().contains(QStringLiteral("injected baud failure")));
    QVERIFY(source.errorString().contains(QStringLiteral("115200")));
    QVERIFY(source.errorString().contains(QStringLiteral("PBS_FAKE_A")));
    QCOMPARE(state.constructed, 1);
    QCOMPARE(state.destroyed, 1);
    QCOMPARE(state.openCalls, 0);
    state.failBaud = false;
    std::unique_ptr<QIODevice> device(source.createAndConnect());
    QVERIFY(device);
    QVERIFY(source.errorString().isEmpty());
    device.reset();
    QCOMPARE(state.constructed, state.destroyed);
}

void SerialDataSourceTests::failedOpenCleansUpAndCanRetry() {
    PortState state;
    state.failOpen = true;
    FakeSerialDataSource source(state);
    source.init(settings());
    QVERIFY(source.createAndConnect() == nullptr);
    QVERIFY(source.errorString().contains(QStringLiteral("injected open failure")));
    QVERIFY(source.errorString().contains(QStringLiteral("PBS_FAKE_A")));
    QCOMPARE(state.constructed, 1);
    QCOMPARE(state.destroyed, 1);
    QCOMPARE(state.openCalls, 1);
    QCOMPARE(state.closeCalls, 0);
    state.failOpen = false;
    std::unique_ptr<QIODevice> device(source.createAndConnect());
    QVERIFY(device);
    QVERIFY(source.errorString().isEmpty());
    device.reset();
    QCOMPARE(state.constructed, 2);
    QCOMPARE(state.destroyed, 2);
}

void SerialDataSourceTests::nullFactoryResultIsReported() {
    PortState state;
    state.nullFactory = true;
    FakeSerialDataSource source(state);
    source.init(settings());
    QVERIFY(source.createAndConnect() == nullptr);
    QVERIFY(source.errorString().contains(QStringLiteral("no device")));
    QCOMPARE(state.factoryCalls, 1);
    QCOMPARE(state.constructed, 0);
    QCOMPARE(state.openCalls, 0);
}

void SerialDataSourceTests::createsDeviceInCallingThread() {
    PortState state;
    FakeSerialDataSource source(state);
    source.init(settings());
    QThread* mainThread = QThread::currentThread();
    bool correctThread = false;
    bool opened = false;
    std::unique_ptr<QThread> worker(QThread::create([&]() {
        std::unique_ptr<QIODevice> device(source.createAndConnect());
        opened = device && device->isOpen();
        correctThread = device && device->thread() == QThread::currentThread()
                        && device->thread() != mainThread && device->parent() == nullptr;
    }));
    worker->start();
    // Join before assertions so no source/state is destroyed while worker runs.
    worker->wait();
    QVERIFY(opened);
    QVERIFY(correctThread);
    QCOMPARE(state.constructed, 1);
    QCOMPARE(state.destroyed, 1);
    QCOMPARE(state.closeCalls, 1);
}

void SerialDataSourceTests::operationsWithoutDeviceAreSafe() {
    SerialDataSource source;
    source.init(settings());
    char byte;
    QCOMPARE(source.read(&byte, 1), qint64(-1));
    QCOMPARE(source.write("x"), qint64(-1));
    QVERIFY(!source.waitForReadyRead(1)); QVERIFY(!source.waitForBytesWritten(1));
    QCOMPARE(source.bytesAvailable(), qint64(0)); QCOMPARE(source.bytesToWrite(), qint64(0));
    QVERIFY(source.hasError()); QVERIFY(source.errorString().contains(QStringLiteral("not open")));
    source.close();
}

void SerialDataSourceTests::delegatedReadWriteAndWaits() {
    PortState state; state.ioMode = true; state.incoming = QByteArray("a\r\0b", 4);
    FakeSerialDataSource source(state); source.init(settings());
    std::unique_ptr<QIODevice> device(source.createAndConnect()); QVERIFY(device);
    IDataSource& port = source;
    QVERIFY(!port.hasError());
    QCOMPARE(port.bytesAvailable(), qint64(4)); QVERIFY(port.waitForReadyRead(7));
    char bytes[8]; const auto count = port.read(bytes, sizeof(bytes));
    QCOMPARE(QByteArray(bytes, count), QByteArray("a\r\0b", 4));
    QCOMPARE(state.readCalls, 1); QCOMPARE(state.lastReadWait, 7);
    QVERIFY(!port.waitForReadyRead(2)); QVERIFY(!port.hasError());
    QCOMPARE(port.write(QByteArray("x\0y", 3)), qint64(3));
    QCOMPARE(state.outgoing, QByteArray("x\0y", 3)); QCOMPARE(state.writeCalls, 1);
    state.pending = 3; QCOMPARE(port.bytesToWrite(), qint64(3));
    QVERIFY(port.waitForBytesWritten(9)); QCOMPARE(state.lastWriteWait, 9);
    QCOMPARE(port.bytesToWrite(), qint64(0)); QVERIFY(port.errorString().isEmpty());
}

void SerialDataSourceTests::delegatedFailures() {
    PortState state; state.ioMode = true; state.incoming = "x";
    FakeSerialDataSource source(state); source.init(settings());
    std::unique_ptr<QIODevice> device(source.createAndConnect()); QVERIFY(device);
    state.failRead = true; char byte;
    QCOMPARE(source.read(&byte, 1), qint64(-1));
    QVERIFY(source.errorString().contains(QStringLiteral("injected read failure")));
    state.failWrite = true;
    QCOMPARE(source.write("x"), qint64(-1));
    QVERIFY(source.errorString().contains(QStringLiteral("injected write failure")));
}

void SerialDataSourceTests::closeDoesNotTakeOwnership() {
    PortState state;
    FakeSerialDataSource source(state); source.init(settings());
    std::unique_ptr<QIODevice> device(source.createAndConnect()); QVERIFY(device);
    source.close(); source.close();
    QCOMPARE(state.closeCalls, 1); QCOMPARE(state.destroyed, 0);
    QVERIFY(source.hasError());
    QVERIFY(source.createAndConnect() == nullptr);
    device.reset(); QCOMPARE(state.destroyed, 1);
    device.reset(source.createAndConnect()); QVERIFY(device);
    QVERIFY(!source.hasError());
}

void SerialDataSourceTests::backendErrorIsReported() {
    PortState state;
    FakeSerialDataSource source(state); source.init(settings());
    std::unique_ptr<QIODevice> device(source.createAndConnect()); QVERIFY(device);
    source.close();
    auto* port = qobject_cast<QSerialPort*>(device.get());
    QVERIFY(!port->clear()); // Closed-object guard sets NotOpenError; no backend open.
    QVERIFY(port->error() != QSerialPort::NoError);
    QVERIFY(source.hasError());
    QVERIFY(source.errorString().contains(QStringLiteral("Qt serial error")));
    QVERIFY(source.errorString().contains(QString::number(int(port->error()))));
}

void SerialDataSourceTests::workerUsesOperationalSerialSource() {
    PortState state;
    state.ioMode = true;
    state.incoming = QByteArray("raw\r\0input", 10);
    DataThread worker;
    QSignalSpy reads(&worker, &DataThread::readyRead);
    QSignalSpy results(&worker, &DataThread::writeFinished);
    QSignalSpy errors(&worker, &DataThread::errorOccured);
    QScopedPointer<IDataSource> source(new FakeSerialDataSource(state));
    source->init(settings());
    QVERIFY(worker.configure(source));
    QVERIFY(worker.write(QByteArray("out\0put", 7)));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(reads.size(), 1, 2000);
    worker.stop();
    QVERIFY(worker.wait(2000));
    QVERIFY(results.at(0).at(1).toBool());
    QCOMPARE(reads.at(0).at(0).toByteArray(), QByteArray("raw\r\0input", 10));
    QCOMPARE(state.outgoing, QByteArray("out\0put", 7));
    QCOMPARE(errors.size(), 0);
    QCOMPARE(state.creationThread, &worker);
    QCOMPARE(state.affinityErrors, 0);
    QCOMPARE(state.constructed, 1);
    QCOMPARE(state.destroyed, 1);
    QCOMPARE(state.closeCalls, 1);
}

QTEST_GUILESS_MAIN(SerialDataSourceTests)

#include "tst_serialdatasource.moc"
