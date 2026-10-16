#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace PICTextProtocol {

enum class Kind { ParameterValue, DeviceError, NonexistentParameter, Malformed };

struct Response {
    Kind kind{Kind::Malformed};
    quint16 parameter{0}; // ParameterValue or NonexistentParameter only.
    quint16 value{0};
    quint16 errorCode{0}; // DeviceError only; unknown codes are preserved.
};

QByteArray encodeRead(quint16 parameter);
QByteArray encodeWrite(quint16 parameter, quint16 value);

// Exactly one CR-terminated standard frame. No buffering or request correlation.
// Hex fields accept ASCII 0-9/A-F/a-f; prefixes are the documented uppercase bytes.
Response decode(const QByteArray& frame);

} // namespace PICTextProtocol
