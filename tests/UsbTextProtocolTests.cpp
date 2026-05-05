#include <iostream>
#include <stdexcept>

#include "infrastructure/protocol/UsbTextProtocol.h"

using namespace infrastructure::protocol;

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        throw std::runtime_error("Test failed.");
    }
}

void expectEqual(const QByteArray& actual, const QByteArray& expected, const char* message)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << message
                  << " | expected=" << expected.toStdString()
                  << " actual=" << actual.toStdString() << '\n';
        throw std::runtime_error("Test failed.");
    }
}

void expectEqual(const QString& actual, const QString& expected, const char* message)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << message
                  << " | expected=" << expected.toStdString()
                  << " actual=" << actual.toStdString() << '\n';
        throw std::runtime_error("Test failed.");
    }
}

void expectEqual(int actual, int expected, const char* message)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << message
                  << " | expected=" << expected
                  << " actual=" << actual << '\n';
        throw std::runtime_error("Test failed.");
    }
}

void testMakeReadCommand()
{
    UsbTextProtocol protocol;

    expectEqual(protocol.makeReadCommand("0017"), QByteArray("J0017\r"),
                "Read command for channel 1 voltage value");

    expectEqual(protocol.makeReadCommand("0021"), QByteArray("J0021\r"),
                "Read command for channel 2 state");

    expectEqual(protocol.makeReadCommand("00a1"), QByteArray("J00A1\r"),
                "Read command should normalize hex code to uppercase");
}

void testMakeWriteCommand()
{
    UsbTextProtocol protocol;

    expectEqual(protocol.makeWriteCommand("0017", 0x0384), QByteArray("P0017 0384\r"),
                "Write command for channel 1 voltage value");

    expectEqual(protocol.makeWriteCommand("0021", 0x0600), QByteArray("P0021 0600\r"),
                "Write command for channel 2 follow zero channel");

    expectEqual(protocol.makeWriteCommand("0001", 0x0008), QByteArray("P0001 0008\r"),
                "Write command for zero channel start");
}

void testParseKResponse()
{
    UsbTextProtocol protocol;

    const UsbTextResponse response = protocol.parseResponse(QByteArray("K0017 0164\r"));

    expect(!response.isError, "K-response should not be marked as error");
    expectEqual(response.parameterCodeHex, "0017", "K-response parameter code");
    expectEqual(response.value, 0x0164, "K-response value");
}

void testParseKResponseWithLineFeed()
{
    UsbTextProtocol protocol;

    const UsbTextResponse response = protocol.parseResponse(QByteArray("K0021 000C\r\n"));

    expect(!response.isError, "K-response with CRLF should not be marked as error");
    expectEqual(response.parameterCodeHex, "0021", "K-response with CRLF parameter code");
    expectEqual(response.value, 0x000C, "K-response with CRLF value");
}

void testParseErrorResponse()
{
    UsbTextProtocol protocol;

    const UsbTextResponse response = protocol.parseResponse(QByteArray("E0001\r"));

    expect(response.isError, "E-response should be marked as error");
    expectEqual(response.errorCodeHex, "0001", "E-response error code");
}

void testInvalidReadCodeThrows()
{
    UsbTextProtocol protocol;

    bool thrown = false;

    try {
        protocol.makeReadCommand("17");
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "Invalid read command code should throw");
}

void testInvalidWriteValueThrows()
{
    UsbTextProtocol protocol;

    bool thrown = false;

    try {
        protocol.makeWriteCommand("0017", 0x10000);
    } catch (const std::out_of_range&) {
        thrown = true;
    }

    expect(thrown, "Write value above 0xFFFF should throw");
}

void testInvalidResponseThrows()
{
    UsbTextProtocol protocol;

    bool thrown = false;

    try {
        protocol.parseResponse(QByteArray("X0017 0164\r"));
    } catch (const std::runtime_error&) {
        thrown = true;
    }

    expect(thrown, "Unknown response prefix should throw");
}

} // namespace

int main()
{
    try {
        testMakeReadCommand();
        testMakeWriteCommand();
        testParseKResponse();
        testParseKResponseWithLineFeed();
        testParseErrorResponse();
        testInvalidReadCodeThrows();
        testInvalidWriteValueThrows();
        testInvalidResponseThrows();

        std::cout << "All UsbTextProtocol tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}