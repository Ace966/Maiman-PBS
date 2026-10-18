#include "PICTextProtocol.h"

#include <QtTest>

using PICTextProtocol::Kind;
using PICTextProtocol::FrameAssembler;

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
    QTest::addColumn<QByteArray>("frame");
    QTest::newRow("K-exactly-11-bytes") << QByteArray("K0017 0164\r");
    QTest::newRow("E-six-bytes") << QByteArray("E0001\r");
}

void PICTextProtocolTests::completeFrame() {
    QFETCH(QByteArray, frame);
    FrameAssembler assembler;
    const auto result = assembler.append(frame);
    QCOMPARE(result.frames, QList<QByteArray>{frame});
    QCOMPARE(result.discardedFrames, qsizetype(0));
    QVERIFY(PICTextProtocol::decode(result.frames[0]).kind != Kind::Malformed);
}

void PICTextProtocolTests::splitFrame() {
    const QByteArray frame("K0017 0164\r");
    for (qsizetype split = 1; split < frame.size(); ++split) {
        FrameAssembler assembler;
        const auto first = assembler.append(frame.first(split));
        QVERIFY(first.frames.isEmpty()); QCOMPARE(first.discardedFrames, qsizetype(0));
        const auto last = assembler.append(frame.sliced(split));
        QCOMPARE(last.frames, QList<QByteArray>{frame});
        QCOMPARE(last.discardedFrames, qsizetype(0));
    }
    FrameAssembler assembler;
    QList<QByteArray> frames;
    for (char byte : frame) {
        const auto result = assembler.append(QByteArray(1, byte));
        frames += result.frames;
        QCOMPARE(result.discardedFrames, qsizetype(0));
    }
    QCOMPARE(frames, QList<QByteArray>{frame});
}

void PICTextProtocolTests::multipleFramesOneChunk() {
    FrameAssembler assembler;
    const QList<QByteArray> expected{"K0017 0164\r", "E0002\r", "K0000 0000\r"};
    const auto result = assembler.append("K0017 0164\rE0002\rK0000 0000\r");
    QCOMPARE(result.frames, expected); QCOMPARE(result.discardedFrames, qsizetype(0));
    const auto value = PICTextProtocol::decode(result.frames[0]);
    QCOMPARE(value.kind, Kind::ParameterValue); QCOMPARE(value.parameter, quint16(0x0017));
    QCOMPARE(value.value, quint16(356));
    const auto error = PICTextProtocol::decode(result.frames[1]);
    QCOMPARE(error.kind, Kind::DeviceError); QCOMPARE(error.errorCode, quint16(2));
    QCOMPARE(PICTextProtocol::decode(result.frames[2]).kind, Kind::NonexistentParameter);
}

void PICTextProtocolTests::multipleChunks() {
    FrameAssembler assembler;
    const auto first = assembler.append("K0017");
    QVERIFY(first.frames.isEmpty()); QCOMPARE(first.discardedFrames, qsizetype(0));
    const auto second = assembler.append(" 0164\rK0021");
    QCOMPARE(second.frames, QList<QByteArray>{"K0017 0164\r"});
    const auto third = assembler.append(" 000C\r");
    QCOMPARE(third.frames, QList<QByteArray>{"K0021 000C\r"});
    QCOMPARE(second.discardedFrames + third.discardedFrames, qsizetype(0));
    QCOMPARE(PICTextProtocol::decode(third.frames[0]).value, quint16(12));
}

void PICTextProtocolTests::emptyInput() {
    FrameAssembler assembler;
    auto result = assembler.append({});
    QVERIFY(result.frames.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    QVERIFY(assembler.append("E00").frames.isEmpty());
    result = assembler.append({});
    QVERIFY(result.frames.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    QCOMPARE(assembler.append("01\r").frames, QList<QByteArray>{"E0001\r"});
}

void PICTextProtocolTests::incompleteTrailingFrame() {
    FrameAssembler assembler;
    const auto first = assembler.append("K0017 0164\rE00");
    QCOMPARE(first.frames, QList<QByteArray>{"K0017 0164\r"});
    const auto next = assembler.append("01\r");
    QCOMPARE(next.frames, QList<QByteArray>{"E0001\r"});
    QCOMPARE(first.discardedFrames + next.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::resetIncompleteFrame() {
    FrameAssembler assembler;
    QVERIFY(assembler.append("K0017").frames.isEmpty());
    assembler.reset(); assembler.reset();
    const auto result = assembler.append("E0000\r");
    QCOMPARE(result.frames, QList<QByteArray>{"E0000\r"});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::oversizedDiscardAndRecovery() {
    FrameAssembler assembler;
    auto result = assembler.append(QByteArray(11, 'x'));
    QVERIFY(result.frames.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    result = assembler.append("x"); // First byte beyond the total 11-byte limit.
    QVERIFY(result.frames.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(1));
    result = assembler.append({});
    QVERIFY(result.frames.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    result = assembler.append("K0017 0164"); // Must not retain a valid-looking discarded tail.
    QVERIFY(result.frames.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    result = assembler.append("\rE0001\r");
    QCOMPARE(result.frames, QList<QByteArray>{"E0001\r"});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::oversizedSameChunk_data() {
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<qsizetype>("discarded");
    QTest::addColumn<QList<QByteArray>>("expected");
    QTest::newRow("twelfth-byte-is-CR") << (QByteArray(11, 'x') + "\rE0001\r") << qsizetype(1)
        << QList<QByteArray>{"E0001\r"};
    QTest::newRow("discard-until-CR") << (QByteArray(12, 'x') + "\rK0017 0164\r") << qsizetype(1)
        << QList<QByteArray>{"K0017 0164\r"};
    QTest::newRow("two-discarded-frames") << (QByteArray(12, 'x') + "\rE0000\r" + QByteArray(20, 'y') + "\rE0002\r")
        << qsizetype(2) << QList<QByteArray>{"E0000\r", "E0002\r"};
}

void PICTextProtocolTests::oversizedSameChunk() {
    QFETCH(QByteArray, input); QFETCH(qsizetype, discarded); QFETCH(QList<QByteArray>, expected);
    FrameAssembler assembler;
    const auto result = assembler.append(input);
    QCOMPARE(result.frames, expected); QCOMPARE(result.discardedFrames, discarded);
}

void PICTextProtocolTests::resetDiscardState() {
    FrameAssembler assembler;
    QCOMPARE(assembler.append(QByteArray(12, 'x')).discardedFrames, qsizetype(1));
    assembler.reset(); // A new stream need not finish the old oversized frame.
    const auto result = assembler.append("K0021 000C\r");
    QCOMPARE(result.frames, QList<QByteArray>{"K0021 000C\r"});
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
    FrameAssembler assembler;
    const auto result = assembler.append(frame);
    QCOMPARE(result.frames, QList<QByteArray>{frame});
    QCOMPARE(result.discardedFrames, qsizetype(0));
    QCOMPARE(PICTextProtocol::decode(result.frames[0]).kind, Kind::Malformed);
}

void PICTextProtocolTests::largeDiscardedInput() {
    FrameAssembler assembler;
    auto result = assembler.append(QByteArray(1024 * 1024, 'x'));
    QVERIFY(result.frames.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(1));
    result = assembler.append(QByteArray(1024 * 1024, 'y'));
    QVERIFY(result.frames.isEmpty()); QCOMPARE(result.discardedFrames, qsizetype(0));
    result = assembler.append("\rE0001\r");
    QCOMPARE(result.frames, QList<QByteArray>{"E0001\r"});
    QCOMPARE(result.discardedFrames, qsizetype(0));
}

void PICTextProtocolTests::chunkBoundaryIndependence() {
    const QByteArray stream = QByteArray("K0017 0164\r") + QByteArray(15, 'x') + "\rE0001\rE0";
    const QList<QByteArray> expected{"K0017 0164\r", "E0001\r"};
    for (qsizetype split = 0; split <= stream.size(); ++split) {
        FrameAssembler assembler;
        const auto first = assembler.append(stream.first(split));
        const auto second = assembler.append(stream.sliced(split));
        QCOMPARE(first.frames + second.frames, expected);
        QCOMPARE(first.discardedFrames + second.discardedFrames, qsizetype(1));
        QCOMPARE(assembler.append("002\r").frames, QList<QByteArray>{"E0002\r"});
    }
}

QTEST_GUILESS_MAIN(PICTextProtocolTests)
#include "tst_pictextprotocol.moc"
