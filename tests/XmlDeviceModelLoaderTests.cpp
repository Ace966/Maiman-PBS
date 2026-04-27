#include <iostream>
#include <stdexcept>

#include <QString>

#include "domain/ChannelCommandResolver.h"
#include "infrastructure/XmlDeviceModelLoader.h"

using namespace domain;
using namespace infrastructure;

namespace {

QString configPath()
{
    return QString(PROJECT_SOURCE_DIR) + "/config/pic_usb.xml";
}

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
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

void testLoadPic8Xml()
{
    XmlDeviceModelLoader loader;
    DeviceModel model = loader.loadFromFile(configPath(), "pic8_usb");

    expect(model.id == "pic8_usb", "PIC-8 model id");
    expect(model.name == "PIC-8 USB", "PIC-8 model name");
    expect(model.variant == DeviceVariant::Pic8, "PIC-8 variant");
    expect(model.channelCount == 8, "PIC-8 channel count");
    expect(model.supportsZeroChannel, "PIC-8 supports zero channel");

    expect(model.transport.type == "USB", "PIC-8 transport type");
    expect(model.transport.baudRate == 115200, "PIC-8 baud rate");
    expect(model.transport.dataBits == 8, "PIC-8 data bits");
    expect(model.transport.stopBits == 1, "PIC-8 stop bits");
    expect(model.transport.parity == "none", "PIC-8 parity");
    expect(model.transport.flowControl == "none", "PIC-8 flow control");

    expect(model.protocol.mode == "text-plain", "PIC-8 protocol mode");
    expect(model.protocol.setPrefix == 'P', "PIC-8 set prefix");
    expect(model.protocol.getPrefix == 'J', "PIC-8 get prefix");
    expect(model.protocol.responsePrefix == 'K', "PIC-8 response prefix");
    expect(model.protocol.errorPrefix == 'E', "PIC-8 error prefix");

    expectEqual(model.zeroChannel.stateCodeHex, "0001", "PIC-8 zero channel state code");
    expectEqual(model.zeroChannel.actions.startHex, "0008", "PIC-8 zero channel start");
    expectEqual(model.zeroChannel.actions.stopHex, "0010", "PIC-8 zero channel stop");
    expectEqual(model.zeroChannel.actions.selectCCHex, "0020", "PIC-8 zero channel select CC");
    expectEqual(model.zeroChannel.actions.selectCVHex, "0040", "PIC-8 zero channel select CV");

    expectEqual(model.globalParameters["serialNumber"].codeHex, "0701", "PIC-8 serial number code");
    expectEqual(model.globalParameters["firmwareVersion"].codeHex, "0710", "PIC-8 firmware version code");
    expectEqual(model.globalParameters["lockStatus"].codeHex, "0800", "PIC-8 lock status code");
    expectEqual(model.globalParameters["overcurrentStatus"].codeHex, "0801", "PIC-8 overcurrent status code");
    expectEqual(model.globalParameters["overvoltageStatus"].codeHex, "0802", "PIC-8 overvoltage status code");

    ChannelCommandResolver resolver(model);

    expectEqual(resolver.stateCode(1), "0011", "PIC-8 channel 1 state");
    expectEqual(resolver.currentValueCode(1), "0012", "PIC-8 channel 1 current value");
    expectEqual(resolver.voltageProtectionCode(8), "008B", "PIC-8 channel 8 voltage protection");
}

void testLoadPic16Xml()
{
    XmlDeviceModelLoader loader;
    DeviceModel model = loader.loadFromFile(configPath(), "pic16_usb");

    expect(model.id == "pic16_usb", "PIC-16 model id");
    expect(model.name == "PIC-16 USB", "PIC-16 model name");
    expect(model.variant == DeviceVariant::Pic16, "PIC-16 variant");
    expect(model.channelCount == 16, "PIC-16 channel count");
    expect(model.supportsZeroChannel, "PIC-16 supports zero channel");

    expect(model.transport.type == "USB", "PIC-16 transport type");
    expect(model.transport.baudRate == 115200, "PIC-16 baud rate");

    expectEqual(model.zeroChannel.stateCodeHex, "0001", "PIC-16 zero channel state code");
    expectEqual(model.zeroChannel.actions.startHex, "0008", "PIC-16 zero channel start");
    expectEqual(model.zeroChannel.actions.selectCVHex, "0040", "PIC-16 zero channel select CV");

    ChannelCommandResolver resolver(model);

    expectEqual(resolver.stateCode(1), "0011", "PIC-16 channel 1 state");
    expectEqual(resolver.currentValueCode(1), "0012", "PIC-16 channel 1 current value");

    expectEqual(resolver.stateCode(16), "0101", "PIC-16 channel 16 state");
    expectEqual(resolver.currentValueCode(16), "0102", "PIC-16 channel 16 current value");
    expectEqual(resolver.voltageProtectionCode(16), "010B", "PIC-16 channel 16 voltage protection");
}

void testMissingProfileThrows()
{
    XmlDeviceModelLoader loader;

    bool thrown = false;

    try {
        loader.loadFromFile(configPath(), "unknown_profile");
    } catch (const std::runtime_error&) {
        thrown = true;
    }

    expect(thrown, "Missing profile should throw runtime_error");
}

} // namespace

int main()
{
    try {
        testLoadPic8Xml();
        testLoadPic16Xml();
        testMissingProfileThrows();

        std::cout << "All XmlDeviceModelLoader tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}