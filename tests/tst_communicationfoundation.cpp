#include <QCoreApplication>
#include <QSerialPort>
#include <QtTest>

class CommunicationFoundationTests : public QObject
{
    Q_OBJECT

private slots:
    void serialPortCanBeConfiguredWithoutOpening();
};

void CommunicationFoundationTests::serialPortCanBeConfiguredWithoutOpening()
{
    QVERIFY(QCoreApplication::instance() != nullptr);

    QSerialPort port;
    QVERIFY(!port.isOpen());
    QCOMPARE(port.openMode(), QIODevice::NotOpen);

    port.setPortName(QStringLiteral("PBS_MILESTONE1_UNOPENED"));
    QVERIFY(port.setBaudRate(QSerialPort::Baud115200));
    QVERIFY(port.setDataBits(QSerialPort::Data8));
    QVERIFY(port.setParity(QSerialPort::NoParity));
    QVERIFY(port.setStopBits(QSerialPort::OneStop));
    QVERIFY(port.setFlowControl(QSerialPort::NoFlowControl));

    QCOMPARE(port.portName(), QStringLiteral("PBS_MILESTONE1_UNOPENED"));
    QCOMPARE(port.baudRate(), qint32(115200));
    QCOMPARE(port.dataBits(), QSerialPort::Data8);
    QCOMPARE(port.parity(), QSerialPort::NoParity);
    QCOMPARE(port.stopBits(), QSerialPort::OneStop);
    QCOMPARE(port.flowControl(), QSerialPort::NoFlowControl);
    QCOMPARE(port.error(), QSerialPort::NoError);
    QCOMPARE(port.openMode(), QIODevice::NotOpen);
    QVERIFY(!port.isOpen());
}

QTEST_GUILESS_MAIN(CommunicationFoundationTests)

#include "tst_communicationfoundation.moc"
