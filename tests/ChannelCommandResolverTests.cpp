#include <iostream>
#include <stdexcept>

#include "domain/DeviceModel.h"
#include "domain/ChannelCommandResolver.h"

using namespace domain;

namespace {

void expectEqual(const QString& actual, const QString& expected, const char* message)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << message
                  << " | expected=" << expected.toStdString()
                  << " actual=" << actual.toStdString() << '\n';
        throw std::runtime_error("Test failed.");
    }
}

DeviceModel makeBaseModel(int channelCount, DeviceVariant variant)
{
    DeviceModel model;
    model.id = channelCount == 8 ? "pic8_usb" : "pic16_usb";
    model.name = channelCount == 8 ? "PIC-8 USB" : "PIC-16 USB";
    model.variant = variant;
    model.channelCount = channelCount;
    model.supportsZeroChannel = true;

    model.channelPattern.state.stateBaseHex = "0011";
    model.channelPattern.state.stepHex = "0010";

    model.channelPattern.current.valueBaseHex = "0012";
    model.channelPattern.current.maximumBaseHex = "0013";
    model.channelPattern.current.maximumLimitBaseHex = "0014";
    model.channelPattern.current.measuredBaseHex = "0015";
    model.channelPattern.current.protectionBaseHex = "0016";
    model.channelPattern.current.stepHex = "0010";

    model.channelPattern.voltage.valueBaseHex = "0017";
    model.channelPattern.voltage.maximumBaseHex = "0018";
    model.channelPattern.voltage.maximumLimitBaseHex = "0019";
    model.channelPattern.voltage.measuredBaseHex = "001A";
    model.channelPattern.voltage.protectionBaseHex = "001B";
    model.channelPattern.voltage.stepHex = "0010";

    return model;
}

void testPic8Pattern()
{
    DeviceModel model = makeBaseModel(8, DeviceVariant::Pic8);
    ChannelCommandResolver resolver(model);

    expectEqual(resolver.stateCode(1), "0011", "PIC-8 channel 1 state");
    expectEqual(resolver.currentValueCode(1), "0012", "PIC-8 channel 1 current value");
    expectEqual(resolver.voltageProtectionCode(1), "001B", "PIC-8 channel 1 voltage protection");

    expectEqual(resolver.stateCode(2), "0021", "PIC-8 channel 2 state");
    expectEqual(resolver.currentMeasuredCode(2), "0025", "PIC-8 channel 2 current measured");
    expectEqual(resolver.voltageMeasuredCode(2), "002A", "PIC-8 channel 2 voltage measured");

    expectEqual(resolver.stateCode(8), "0081", "PIC-8 channel 8 state");
    expectEqual(resolver.currentProtectionCode(8), "0086", "PIC-8 channel 8 current protection");
    expectEqual(resolver.voltageProtectionCode(8), "008B", "PIC-8 channel 8 voltage protection");
}

void testPic16ExtendedPattern()
{
    DeviceModel model = makeBaseModel(16, DeviceVariant::Pic16);
    ChannelCommandResolver resolver(model);

    expectEqual(resolver.stateCode(9),  "0091", "PIC-16 channel 9 state");
    expectEqual(resolver.currentValueCode(9), "0092", "PIC-16 channel 9 current value");
    expectEqual(resolver.voltageProtectionCode(9), "009B", "PIC-16 channel 9 voltage protection");

    expectEqual(resolver.stateCode(15), "00F1", "PIC-16 channel 15 state");
    expectEqual(resolver.currentValueCode(15), "00F2", "PIC-16 channel 15 current value");
    expectEqual(resolver.voltageProtectionCode(15), "00FB", "PIC-16 channel 15 voltage protection");

    expectEqual(resolver.stateCode(16), "0101", "PIC-16 channel 16 state");
    expectEqual(resolver.currentValueCode(16), "0102", "PIC-16 channel 16 current value");
    expectEqual(resolver.voltageProtectionCode(16), "010B", "PIC-16 channel 16 voltage protection");
}

void testOutOfRange()
{
    DeviceModel model = makeBaseModel(8, DeviceVariant::Pic8);
    ChannelCommandResolver resolver(model);

    bool thrown = false;
    try {
        resolver.stateCode(0);
    } catch (const std::out_of_range&) {
        thrown = true;
    }

    if (!thrown) {
        throw std::runtime_error("Expected out_of_range for channel 0.");
    }
}

} // namespace

int main()
{
    try {
        testPic8Pattern();
        testPic16ExtendedPattern();
        testOutOfRange();

        std::cout << "All ChannelCommandResolver tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}