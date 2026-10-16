#include "PICTextProtocol.h"

#include <QtTest>

using PICTextProtocol::Kind;

class PICTextProtocolTests : public QObject {
    Q_OBJECT
private slots:
    void encodeRead_data();
    void encodeRead();
    void encodeWrite_data();
    void encodeWrite();
    void decode_data();
    void decode();
    void malformed_data();
    void malformed();
};

void PICTextProtocolTests::encodeRead_data() {
    QTest::addColumn<quint16>("parameter");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("voltage-example") << quint16(0x0017) << QByteArray("J0017\r");
    QTest::newRow("state-example") << quint16(0x0021) << QByteArray("J0021\r");
    QTest::newRow("zero") << quint16(0) << QByteArray("J0000\r");
    QTest::newRow("maximum") << quint16(0xffff) << QByteArray("JFFFF\r");
    QTest::newRow("uppercase") << quint16(0xabcd) << QByteArray("JABCD\r");
}

void PICTextProtocolTests::encodeRead() {
    QFETCH(quint16, parameter);
    QFETCH(QByteArray, expected);
    QCOMPARE(PICTextProtocol::encodeRead(parameter), expected);
}

void PICTextProtocolTests::encodeWrite_data() {
    QTest::addColumn<quint16>("parameter");
    QTest::addColumn<quint16>("value");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("voltage-example") << quint16(0x0017) << quint16(0x0384) << QByteArray("P0017 0384\r");
    QTest::newRow("state-example") << quint16(0x0021) << quint16(0x0600) << QByteArray("P0021 0600\r");
    QTest::newRow("zero") << quint16(0) << quint16(0) << QByteArray("P0000 0000\r");
    QTest::newRow("maximum") << quint16(0xffff) << quint16(0xffff) << QByteArray("PFFFF FFFF\r");
    QTest::newRow("uppercase-padding") << quint16(0xabcd) << quint16(0x000a) << QByteArray("PABCD 000A\r");
}

void PICTextProtocolTests::encodeWrite() {
    QFETCH(quint16, parameter);
    QFETCH(quint16, value);
    QFETCH(QByteArray, expected);
    QCOMPARE(PICTextProtocol::encodeWrite(parameter, value), expected);
}

void PICTextProtocolTests::decode_data() {
    QTest::addColumn<QByteArray>("frame");
    QTest::addColumn<int>("kind");
    QTest::addColumn<quint16>("parameter");
    QTest::addColumn<quint16>("value");
    QTest::addColumn<quint16>("errorCode");
    // Preserve the documented wire value 0164: it is 356 decimal, not 0356h.
    QTest::newRow("voltage-example") << QByteArray("K0017 0164\r") << int(Kind::ParameterValue)
        << quint16(0x0017) << quint16(356) << quint16(0);
    QTest::newRow("state-example") << QByteArray("K0021 000C\r") << int(Kind::ParameterValue)
        << quint16(0x0021) << quint16(12) << quint16(0);
    QTest::newRow("terminal-state-example") << QByteArray("K0021 001C\r") << int(Kind::ParameterValue)
        << quint16(0x0021) << quint16(28) << quint16(0);
    QTest::newRow("maximum") << QByteArray("KFFFF FFFF\r") << int(Kind::ParameterValue)
        << quint16(0xffff) << quint16(0xffff) << quint16(0);
    QTest::newRow("mixed-case-hex") << QByteArray("KaBcD eFaB\r") << int(Kind::ParameterValue)
        << quint16(0xabcd) << quint16(0xefab) << quint16(0);
    QTest::newRow("zero-parameter-nonzero-value") << QByteArray("K0000 0001\r") << int(Kind::ParameterValue)
        << quint16(0) << quint16(1) << quint16(0);
    QTest::newRow("zero-value") << QByteArray("K0017 0000\r") << int(Kind::ParameterValue)
        << quint16(0x0017) << quint16(0) << quint16(0);
    QTest::newRow("nonexistent-parameter") << QByteArray("K0000 0000\r") << int(Kind::NonexistentParameter)
        << quint16(0) << quint16(0) << quint16(0);
    QTest::newRow("E0000") << QByteArray("E0000\r") << int(Kind::DeviceError)
        << quint16(0) << quint16(0) << quint16(0);
    QTest::newRow("E0001") << QByteArray("E0001\r") << int(Kind::DeviceError)
        << quint16(0) << quint16(0) << quint16(1);
    QTest::newRow("E0002") << QByteArray("E0002\r") << int(Kind::DeviceError)
        << quint16(0) << quint16(0) << quint16(2);
    QTest::newRow("unknown-error-code") << QByteArray("Effff\r") << int(Kind::DeviceError)
        << quint16(0) << quint16(0) << quint16(0xffff);
}

void PICTextProtocolTests::decode() {
    QFETCH(QByteArray, frame);
    QFETCH(int, kind);
    QFETCH(quint16, parameter);
    QFETCH(quint16, value);
    QFETCH(quint16, errorCode);
    const auto result = PICTextProtocol::decode(frame);
    QCOMPARE(int(result.kind), kind);
    QCOMPARE(result.parameter, parameter);
    QCOMPARE(result.value, value);
    QCOMPARE(result.errorCode, errorCode);
}

void PICTextProtocolTests::malformed_data() {
    QTest::addColumn<QByteArray>("frame");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("wrong-prefix") << QByteArray("X0017 0164\r");
    QTest::newRow("lowercase-prefix") << QByteArray("k0017 0164\r");
    QTest::newRow("request-is-not-response") << QByteArray("J0017\r");
    QTest::newRow("parameter-short") << QByteArray("K017 0164\r");
    QTest::newRow("parameter-long") << QByteArray("K00017 0164\r");
    QTest::newRow("value-short") << QByteArray("K0017 164\r");
    QTest::newRow("value-long") << QByteArray("K0017 00164\r");
    QTest::newRow("error-short") << QByteArray("E001\r");
    QTest::newRow("error-long") << QByteArray("E00001\r");
    QTest::newRow("missing-space") << QByteArray("K00170164\r");
    QTest::newRow("double-space") << QByteArray("K0017  0164\r");
    QTest::newRow("tab-separator") << QByteArray("K0017\t0164\r");
    QTest::newRow("nonhex-parameter") << QByteArray("K00G7 0164\r");
    QTest::newRow("nonhex-value") << QByteArray("K0017 016Z\r");
    QTest::newRow("nonhex-error") << QByteArray("E000G\r");
    QTest::newRow("signed-field") << QByteArray("K0017 +164\r");
    QTest::newRow("space-in-field") << QByteArray("K0017  164\r");
    QTest::newRow("hex-prefix-in-field") << QByteArray("K0017 0x64\r");
    QTest::newRow("nul-in-field") << QByteArray("K0017 01\0" "4\r", 11);
    QTest::newRow("nonascii-field") << QByteArray("E00\x80" "0\r", 6);
    QTest::newRow("missing-CR") << QByteArray("K0017 0164");
    QTest::newRow("error-missing-CR") << QByteArray("E0001");
    QTest::newRow("LF-terminator") << QByteArray("K0017 0164\n");
    QTest::newRow("CRLF-terminator") << QByteArray("K0017 0164\r\n");
    QTest::newRow("fragment") << QByteArray("K0017");
    QTest::newRow("concatenated-values") << QByteArray("K0017 0164\rK0021 000C\r");
    QTest::newRow("concatenated-errors") << QByteArray("E0000\rE0001\r");
}

void PICTextProtocolTests::malformed() {
    QFETCH(QByteArray, frame);
    const auto result = PICTextProtocol::decode(frame);
    QCOMPARE(result.kind, Kind::Malformed);
    QCOMPARE(result.parameter, quint16(0));
    QCOMPARE(result.value, quint16(0));
    QCOMPARE(result.errorCode, quint16(0));
}

QTEST_GUILESS_MAIN(PICTextProtocolTests)
#include "tst_pictextprotocol.moc"
