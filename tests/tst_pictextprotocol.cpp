#include "PICTextProtocol.h"

#include <QtTest>

using Kind = PICTextProtocol::Kind;
using Response = PICTextProtocol::Response;

namespace {
Response parameterResponse(quint16 parameter, quint16 value) {
    return {Kind::ParameterValue, parameter, value};
}
Response errorResponse(quint16 code) { return {Kind::DeviceError, 0, 0, code}; }
void compareResponses(const QList<Response>& actual, const QList<Response>& expected) {
    QCOMPARE(actual.size(), expected.size());
    for (qsizetype i = 0; i < expected.size(); ++i) {
        QCOMPARE(actual[i].kind, expected[i].kind);
        QCOMPARE(actual[i].parameter, expected[i].parameter);
        QCOMPARE(actual[i].value, expected[i].value);
        QCOMPARE(actual[i].errorCode, expected[i].errorCode);
    }
}
}

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
    void completeFrame_data();
    void completeFrame();
    void splitFrame();
    void multipleFramesOneChunk();
    void multipleChunks();
    void emptyInput();
    void incompleteTrailingFrame();
    void resetIncompleteFrame();
    void oversizedDiscardAndRecovery();
    void oversizedSameChunk_data();
    void oversizedSameChunk();
    void resetDiscardState();
    void rawPayload_data();
    void rawPayload();
    void largeDiscardedInput();
    void chunkBoundaryIndependence();
    void independentInstances();
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

void PICTextProtocolTests::completeFrame_data() {
    QTest::addColumn<QByteArray>("frame"); QTest::addColumn<Response>("expected");
    QTest::newRow("K-exactly-11-bytes") << QByteArray("K0017 0164\r") << parameterResponse(0x0017, 356);
    QTest::newRow("E-six-bytes") << QByteArray("E0001\r") << errorResponse(1);
}

void PICTextProtocolTests::completeFrame() {
    QFETCH(QByteArray, frame); QFETCH(Response, expected);
    PICTextProtocol protocol;
    const auto result = protocol.append(frame);
    compareResponses(result.responses, {expected});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::splitFrame() {
    const QByteArray frame("K0017 0164\r");
    for (qsizetype split = 1; split < frame.size(); ++split) {
        PICTextProtocol protocol;
        const auto first = protocol.append(frame.first(split));
        QVERIFY(first.responses.isEmpty()); QCOMPARE(first.discardedFrames, qsizetype(0));
        const auto last = protocol.append(frame.sliced(split));
        compareResponses(last.responses, {parameterResponse(0x0017, 356)});
        QCOMPARE(last.discardedFrames, qsizetype(0));
    }
    PICTextProtocol protocol;
    QList<Response> responses;
    for (char byte : frame) {
        const auto result = protocol.append(QByteArray(1, byte));
        responses += result.responses;
        QCOMPARE(result.discardedFrames, qsizetype(0));
    }
    compareResponses(responses, {parameterResponse(0x0017, 356)});
}

void PICTextProtocolTests::multipleFramesOneChunk() {
    PICTextProtocol protocol;
    const auto result = protocol.append("K0017 0164\rE0002\rK0000 0000\r");
    compareResponses(result.responses, {parameterResponse(0x0017, 356), errorResponse(2), {Kind::NonexistentParameter}});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::multipleChunks() {
    PICTextProtocol protocol;
    const auto first = protocol.append("K0017");
    QVERIFY(first.responses.isEmpty()); QCOMPARE(first.discardedFrames, qsizetype(0));
    const auto second = protocol.append(" 0164\rK0021");
    compareResponses(second.responses, {parameterResponse(0x0017, 356)});
    const auto third = protocol.append(" 000C\r");
    compareResponses(third.responses, {parameterResponse(0x0021, 12)});
    QCOMPARE(second.discardedFrames + third.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::emptyInput() {
    PICTextProtocol protocol;
    auto result = protocol.append({});
    QVERIFY(result.responses.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    QVERIFY(protocol.append("E00").responses.isEmpty());
    result = protocol.append({});
    QVERIFY(result.responses.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    compareResponses(protocol.append("01\r").responses, {errorResponse(1)});
}

void PICTextProtocolTests::incompleteTrailingFrame() {
    PICTextProtocol protocol;
    const auto first = protocol.append("K0017 0164\rE00");
    compareResponses(first.responses, {parameterResponse(0x0017, 356)});
    const auto next = protocol.append("01\r");
    compareResponses(next.responses, {errorResponse(1)});
    QCOMPARE(first.discardedFrames + next.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::resetIncompleteFrame() {
    PICTextProtocol protocol;
    QVERIFY(protocol.append("K0017").responses.isEmpty());
    protocol.reset(); protocol.reset();
    const auto result = protocol.append("E0000\r");
    compareResponses(result.responses, {errorResponse(0)});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::oversizedDiscardAndRecovery() {
    PICTextProtocol protocol;
    auto result = protocol.append(QByteArray(11, 'x'));
    QVERIFY(result.responses.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    result = protocol.append("x");
    QVERIFY(result.responses.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(1));
    result = protocol.append({});
    QVERIFY(result.responses.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    result = protocol.append("K0017 0164"); // Do not parse valid-looking discarded tails.
    QVERIFY(result.responses.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    result = protocol.append("\rE0001\r");
    compareResponses(result.responses, {errorResponse(1)});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::oversizedSameChunk_data() {
    QTest::addColumn<QByteArray>("input"); QTest::addColumn<qsizetype>("discarded");
    QTest::addColumn<QList<Response>>("expected");
    QTest::newRow("twelfth-byte-is-CR") << (QByteArray(11, 'x') + "\rE0001\r") << qsizetype(1)
        << QList<Response>{errorResponse(1)};
    QTest::newRow("discard-until-CR") << (QByteArray(12, 'x') + "\rK0017 0164\r") << qsizetype(1)
        << QList<Response>{parameterResponse(0x0017, 356)};
    QTest::newRow("two-discarded-frames") << (QByteArray(12, 'x') + "\rE0000\r" + QByteArray(20, 'y') + "\rE0002\r")
        << qsizetype(2) << QList<Response>{errorResponse(0), errorResponse(2)};
}

void PICTextProtocolTests::oversizedSameChunk() {
    QFETCH(QByteArray, input); QFETCH(qsizetype, discarded); QFETCH(QList<Response>, expected);
    PICTextProtocol protocol;
    const auto result = protocol.append(input);
    compareResponses(result.responses, expected); QCOMPARE(result.discardedFrames, discarded);
}

void PICTextProtocolTests::resetDiscardState() {
    PICTextProtocol protocol;
    QCOMPARE(protocol.append(QByteArray(12, 'x')).discardedFrames, qsizetype(1));
    protocol.reset();
    const auto result = protocol.append("K0021 000C\r");
    compareResponses(result.responses, {parameterResponse(0x0021, 12)});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::rawPayload_data() {
    QTest::addColumn<QByteArray>("frame");
    QTest::newRow("case-and-spaces") << QByteArray("k0017 0164\r");
    QTest::newRow("NUL-and-LF-are-payload") << QByteArray("x\0 \naBc\r", 8);
    QTest::newRow("CR-only") << QByteArray("\r");
}

void PICTextProtocolTests::rawPayload() {
    QFETCH(QByteArray, frame);
    PICTextProtocol protocol;
    const auto result = protocol.append(frame);
    compareResponses(result.responses, {{Kind::Malformed}});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::largeDiscardedInput() {
    PICTextProtocol protocol;
    auto result = protocol.append(QByteArray(1024 * 1024, 'x'));
    QVERIFY(result.responses.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(1));
    result = protocol.append(QByteArray(1024 * 1024, 'y'));
    QVERIFY(result.responses.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    result = protocol.append("\rE0001\r");
    compareResponses(result.responses, {errorResponse(1)});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::chunkBoundaryIndependence() {
    const QByteArray stream = QByteArray("K0017 0164\r") + QByteArray(15, 'x') + "\rE0001\rE0";
    const QList<Response> expected{parameterResponse(0x0017, 356), errorResponse(1)};
    for (qsizetype split = 0; split <= stream.size(); ++split) {
        PICTextProtocol protocol;
        const auto first = protocol.append(stream.first(split));
        const auto second = protocol.append(stream.sliced(split));
        compareResponses(first.responses + second.responses, expected);
        QCOMPARE(first.discardedFrames + second.discardedFrames, qsizetype(1));
        compareResponses(protocol.append("002\r").responses, {errorResponse(2)});
    }
}

void PICTextProtocolTests::independentInstances() {
    PICTextProtocol first, second;
    QVERIFY(first.append("K0017").responses.isEmpty());
    compareResponses(second.append("Effff\r").responses, {errorResponse(0xffff)});
    compareResponses(first.append(" 0164\r").responses, {parameterResponse(0x0017, 356)});
    QCOMPARE(first.append(QByteArray(12, 'x')).discardedFrames, qsizetype(1));
    compareResponses(second.append("KFFFF FFFF\r").responses, {parameterResponse(0xffff, 0xffff)});
    QVERIFY(second.append("E00").responses.isEmpty());
    first.reset();
    compareResponses(first.append("E0001\r").responses, {errorResponse(1)});
    compareResponses(second.append("02\r").responses, {errorResponse(2)});
}

QTEST_GUILESS_MAIN(PICTextProtocolTests)
#include "tst_pictextprotocol.moc"
