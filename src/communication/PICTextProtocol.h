#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QtGlobal>

// One standard-mode stream; sequential access by its owner. No transport or
// request tracking. Encode/decode can also be used without a stream instance.
class PICTextProtocol {
public:

    enum class Kind { ParameterValue, DeviceError, NonexistentParameter, Malformed };

    struct Response {
        Kind kind{Kind::Malformed};
        quint16 parameter{0}; // ParameterValue or NonexistentParameter only.
        quint16 value{0};
        quint16 errorCode{0}; // DeviceError only; unknown codes are preserved.
    };

    static QByteArray encodeRead(quint16 parameter);
    static QByteArray encodeWrite(quint16 parameter, quint16 value);

    // Exactly one CR-terminated standard frame. No buffering or request correlation.
    // Hex fields accept ASCII 0-9/A-F/a-f; prefixes are the documented uppercase bytes.
    static Response decode(const QByteArray& frame);

    static constexpr qsizetype MaximumFrameBytes = 11; // Includes terminating CR.
    struct Result {
        QList<Response> responses; // One decoded result per bounded CR frame, in order.
        qsizetype discardedFrames{0}; // Newly detected oversized frames in this call.
    };

    Result append(const QByteArray& bytes);
    void reset(); // Clears incomplete data and discard-until-CR state.

private:
    static QByteArray hexField(quint16 value);
    static bool parseHex(const QByteArray& frame, qsizetype offset, quint16& value);
    QByteArray m_pending;
    bool m_discarding{false};
};

Q_DECLARE_METATYPE(PICTextProtocol::Response)
