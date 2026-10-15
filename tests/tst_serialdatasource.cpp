#include "IDataSource.h"
#include "serialdatasource.h"

#include <QThread>
#include <QtTest>
#include <limits>
#include <memory>
#include <type_traits>

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
};

class FakeSerialPort : public QSerialPort {
public:
    explicit FakeSerialPort(PortState& state) : m_state(state) {
        ++m_state.constructed;
    }
    ~FakeSerialPort() override {
        if (isOpen()) close();
        ++m_state.destroyed;
    }
    bool open(OpenMode mode) override {
        ++m_state.openCalls;
        m_state.openedName = portName();
        m_state.openedBaud = baudRate();
        if (m_state.failOpen) {
            setErrorString(QStringLiteral("injected open failure"));
            return false;
        }
        // Only QIODevice's in-memory state changes; no serial backend is opened.
        return QIODevice::open(mode);
    }
    void close() override {
        ++m_state.closeCalls;
        QIODevice::close();
    }
    void injectBaudFailure() {
        setErrorString(QStringLiteral("injected baud failure"));
    }

private:
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
};

void SerialDataSourceTests::polymorphicDestruction() {
    struct DestructionProbe : IDataSource {
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

QTEST_GUILESS_MAIN(SerialDataSourceTests)

#include "tst_serialdatasource.moc"
