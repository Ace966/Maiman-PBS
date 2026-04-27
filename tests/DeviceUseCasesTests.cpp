#include <iostream>
#include <stdexcept>

#include "application/use_cases/DeviceUseCases.h"
#include "domain/DeviceModel.h"
#include "domain/DeviceState.h"
#include "infrastructure/MockDeviceRepository.h"

using namespace application;
using namespace domain;
using namespace infrastructure;

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        throw std::runtime_error("Test failed.");
    }
}

DeviceModel makeModel(int channelCount, DeviceVariant variant)
{
    DeviceModel model;
    model.id = channelCount == 8 ? "pic8_usb" : "pic16_usb";
    model.name = channelCount == 8 ? "PIC-8 USB" : "PIC-16 USB";
    model.variant = variant;
    model.channelCount = channelCount;
    model.supportsZeroChannel = true;
    return model;
}

void testLoadStateThroughUseCases()
{
    MockDeviceRepository repository(makeModel(8, DeviceVariant::Pic8));
    DeviceUseCases useCases(repository);

    DeviceState state = useCases.loadState();

    expect(state.channels.size() == 8, "Use cases load state with 8 channels");
    expect(!state.zeroChannel.enabled, "Zero channel starts disabled");
}

void testStartAndStopChannelThroughUseCases()
{
    MockDeviceRepository repository(makeModel(8, DeviceVariant::Pic8));
    DeviceUseCases useCases(repository);

    useCases.startChannel(1);

    DeviceState state = useCases.loadState();
    expect(state.channels[0].enabled, "Channel 1 started through use case");

    useCases.stopChannel(1);

    state = useCases.loadState();
    expect(!state.channels[0].enabled, "Channel 1 stopped through use case");
}

void testSetValuesThroughUseCases()
{
    MockDeviceRepository repository(makeModel(8, DeviceVariant::Pic8));
    DeviceUseCases useCases(repository);

    useCases.setChannelCurrentValue(3, 125);
    useCases.setChannelVoltageValue(3, 356);
    useCases.setChannelMode(3, ChannelMode::CV);

    DeviceState state = useCases.loadState();

    expect(state.channels[2].setCurrentRaw == 125, "Channel 3 current set through use case");
    expect(state.channels[2].setVoltageRaw == 356, "Channel 3 voltage set through use case");
    expect(state.channels[2].mode == ChannelMode::CV, "Channel 3 mode set through use case");
}

void testZeroChannelThroughUseCases()
{
    MockDeviceRepository repository(makeModel(8, DeviceVariant::Pic8));
    DeviceUseCases useCases(repository);

    useCases.followZeroChannel(2);
    useCases.setZeroChannelMode(ChannelMode::CV);
    useCases.startZeroChannel();

    DeviceState state = useCases.loadState();

    expect(state.zeroChannel.enabled, "Zero channel started through use case");
    expect(state.zeroChannel.mode == ChannelMode::CV, "Zero channel mode set through use case");

    expect(state.channels[1].followZero, "Channel 2 follows zero channel");
    expect(state.channels[1].enabled, "Channel 2 started by zero channel");
    expect(state.channels[1].mode == ChannelMode::CV, "Channel 2 mode copied from zero channel");
}

void testInvalidChannelThrows()
{
    MockDeviceRepository repository(makeModel(8, DeviceVariant::Pic8));
    DeviceUseCases useCases(repository);

    bool thrown = false;

    try {
        useCases.startChannel(0);
    } catch (const std::out_of_range&) {
        thrown = true;
    }

    expect(thrown, "Invalid channel index should throw");
}

void testInvalidRawValueThrows()
{
    MockDeviceRepository repository(makeModel(8, DeviceVariant::Pic8));
    DeviceUseCases useCases(repository);

    bool thrown = false;

    try {
        useCases.setChannelCurrentValue(1, -1);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }

    expect(thrown, "Negative raw current value should throw");
}

} // namespace

int main()
{
    try {
        testLoadStateThroughUseCases();
        testStartAndStopChannelThroughUseCases();
        testSetValuesThroughUseCases();
        testZeroChannelThroughUseCases();
        testInvalidChannelThrows();
        testInvalidRawValueThrows();

        std::cout << "All DeviceUseCases tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}