#include <iostream>
#include <stdexcept>

#include "domain/ChannelCommandResolver.h"
#include "infrastructure/XmlDeviceModelLoader.h"

using namespace domain;
using namespace infrastructure::config;

namespace {

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
    DeviceModel model = loader.loadFromFile("config/devices/pic8_usb.xml");

    expect(model.id == "pic8_usb", "Model id");
    expect(model.name == "PIC-8 USB", "Model name");
    expect(model.variant == DeviceVariant::Pic8, "Variant PIC-8");
    expect(model.channelCount == 8, "Channel count");
    expect(model.supportsZeroChannel, "Supports zero channel");

    expect(model.transport.type == "USB", "Transport type");
    expect(model.transport.baudRate == 115200, "Baud rate");
    expect(model.protocol.setPrefix == 'P', "Set prefix");
    expect(model.protocol.getPrefix == 'J', "Get prefix");

    expectEqual(model.zeroChannel.stateCodeHex, "0001", "Zero channel state code");
    expectEqual(model.zeroChannel.actions.startHex, "0008", "Zero channel start");
    expectEqual(model.zeroChannel.actions.selectCVHex, "0040", "Zero channel select CV");

    expectEqual(model.globalParameters["serialNumber"].codeHex, "0701", "Serial number code");
    expectEqual(model.globalParameters["firmwareVersion"].codeHex, "0710", "Firmware version code");
    expectEqual(model.globalParameters["lockStatus"].codeHex, "0800", "Lock status code");

    ChannelCommandResolver resolver(model);

    expectEqual(resolver.stateCode(1), "0011", "Channel 1 state");
    expectEqual(resolver.currentValueCode(1), "0012", "Channel 1 current value");
    expectEqual(resolver.voltageProtectionCode(8), "008B", "Channel 8 voltage protection");
}

} // namespace

int main()
{
    try {
        testLoadPic8Xml();
        std::cout << "All XmlDeviceModelLoader tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}