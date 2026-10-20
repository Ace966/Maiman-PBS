#include "PICTextProtocol.h"

namespace {
QByteArray hexField(quint16 value) {
    return QByteArray::number(value, 16).rightJustified(4, '0').toUpper();
}

// Called only after frame length/field positions have been checked.
bool parseHex(const QByteArray& frame, qsizetype offset, quint16& value) {
    value = 0;
    for (qsizetype i = offset; i < offset + 4; ++i) {
        const char byte = frame[i];
        int digit;
        if (byte >= '0' && byte <= '9') digit = byte - '0';
        else if (byte >= 'A' && byte <= 'F') digit = byte - 'A' + 10;
        else if (byte >= 'a' && byte <= 'f') digit = byte - 'a' + 10;
        else return false;
        value = quint16((value << 4) | digit);
    }
    return true;
}
} // namespace

namespace PICTextProtocol {

QByteArray encodeRead(quint16 parameter) {
    return QByteArray("J") + hexField(parameter) + '\r';
}

QByteArray encodeWrite(quint16 parameter, quint16 value) {
    return QByteArray("P") + hexField(parameter) + ' ' + hexField(value) + '\r';
}

Response decode(const QByteArray& frame) {
    Response result;
    if (frame.size() == 11 && frame[0] == 'K' && frame[5] == ' ' && frame[10] == '\r') {
        if (!parseHex(frame, 1, result.parameter) || !parseHex(frame, 6, result.value)) return {};
        result.kind = result.parameter == 0 && result.value == 0
            ? Kind::NonexistentParameter : Kind::ParameterValue;
    } else if (frame.size() == 6 && frame[0] == 'E' && frame[5] == '\r') {
        if (!parseHex(frame, 1, result.errorCode)) return {};
        result.kind = Kind::DeviceError;
    }
    return result;
}

FrameAssembler::Result FrameAssembler::append(const QByteArray& bytes) {
    Result result;
    for (char byte : bytes) {
        if (m_discarding) {
            if (byte == '\r') m_discarding = false;
            continue;
        }
        // A CR in position 11 is valid; any twelfth byte makes the frame oversized.
        if (m_pending.size() == MaximumFrameBytes) {
            m_pending.clear();
            m_discarding = byte != '\r';
            ++result.discardedFrames;
            continue;
        }
        m_pending.append(byte);
        if (byte == '\r') {
            result.frames.append(m_pending);
            m_pending.clear();
        }
    }
    return result;
}

void FrameAssembler::reset() {
    m_pending.clear();
    m_discarding = false;
}

} // namespace PICTextProtocol
