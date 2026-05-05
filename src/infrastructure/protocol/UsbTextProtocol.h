#pragma once

#include <QByteArray>
#include <QString>

namespace infrastructure::protocol {

struct UsbTextResponse
{
    bool isError{false};

    QString parameterCodeHex;
    int value{0};

    QString errorCodeHex;
    QString rawText;
};

class UsbTextProtocol
{
public:
    QByteArray makeReadCommand(const QString& parameterCodeHex) const;
    QByteArray makeWriteCommand(const QString& parameterCodeHex, int value) const;

    UsbTextResponse parseResponse(const QByteArray& responseBytes) const;

private:
    QString normalizeHex4(const QString& value, const char* fieldName) const;
    QString intToHex4(int value) const;
    int hexToInt(const QString& value, const char* fieldName) const;

    bool isHex4(const QString& value) const;
};

} // namespace infrastructure::protocol