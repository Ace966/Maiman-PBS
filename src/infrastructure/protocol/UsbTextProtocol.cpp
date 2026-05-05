#include "UsbTextProtocol.h"

#include <stdexcept>
#include <string>

namespace infrastructure::protocol {

QByteArray UsbTextProtocol::makeReadCommand(const QString& parameterCodeHex) const
{
    const QString code = normalizeHex4(parameterCodeHex, "parameter code");
    const QString command = "J" + code + "\r";

    return command.toLatin1();
}

QByteArray UsbTextProtocol::makeWriteCommand(const QString& parameterCodeHex, int value) const
{
    const QString code = normalizeHex4(parameterCodeHex, "parameter code");
    const QString valueHex = intToHex4(value);

    const QString command = "P" + code + " " + valueHex + "\r";

    return command.toLatin1();
}

UsbTextResponse UsbTextProtocol::parseResponse(const QByteArray& responseBytes) const
{
    const QString rawText = QString::fromLatin1(responseBytes).trimmed();

    if (rawText.isEmpty()) {
        throw std::runtime_error("Empty USB response.");
    }

    UsbTextResponse response;
    response.rawText = rawText;

    if (rawText.startsWith("K")) {
        if (rawText.size() != 10) {
            throw std::runtime_error(("Invalid K-response length: " + rawText).toStdString());
        }

        if (rawText.at(5) != ' ') {
            throw std::runtime_error(("Invalid K-response format: " + rawText).toStdString());
        }

        const QString parameterCode = rawText.mid(1, 4);
        const QString valueHex = rawText.mid(6, 4);

        response.isError = false;
        response.parameterCodeHex = normalizeHex4(parameterCode, "response parameter code");
        response.value = hexToInt(valueHex, "response value");

        return response;
    }

    if (rawText.startsWith("E")) {
        if (rawText.size() != 5) {
            throw std::runtime_error(("Invalid E-response length: " + rawText).toStdString());
        }

        const QString errorCode = rawText.mid(1, 4);

        response.isError = true;
        response.errorCodeHex = normalizeHex4(errorCode, "error code");

        return response;
    }

    throw std::runtime_error(("Unknown USB response prefix: " + rawText).toStdString());
}

QString UsbTextProtocol::normalizeHex4(const QString& value, const char* fieldName) const
{
    const QString normalized = value.trimmed().toUpper();

    if (!isHex4(normalized)) {
        throw std::invalid_argument(
            (std::string("Invalid ") + fieldName + ": " + normalized.toStdString()).c_str()
            );
    }

    return normalized;
}

QString UsbTextProtocol::intToHex4(int value) const
{
    if (value < 0 || value > 0xFFFF) {
        throw std::out_of_range("USB protocol value must be in range 0x0000 - 0xFFFF.");
    }

    return QString("%1").arg(value, 4, 16, QChar('0')).toUpper();
}

int UsbTextProtocol::hexToInt(const QString& value, const char* fieldName) const
{
    const QString normalized = normalizeHex4(value, fieldName);

    bool ok = false;
    const int parsedValue = normalized.toInt(&ok, 16);

    if (!ok) {
        throw std::invalid_argument(
            (std::string("Cannot parse ") + fieldName + ": " + normalized.toStdString()).c_str()
            );
    }

    return parsedValue;
}

bool UsbTextProtocol::isHex4(const QString& value) const
{
    if (value.size() != 4) {
        return false;
    }

    for (const QChar& ch : value) {
        const bool isDigit = ch >= '0' && ch <= '9';
        const bool isUpperHex = ch >= 'A' && ch <= 'F';
        const bool isLowerHex = ch >= 'a' && ch <= 'f';

        if (!isDigit && !isUpperHex && !isLowerHex) {
            return false;
        }
    }

    return true;
}

} // namespace infrastructure::protocol