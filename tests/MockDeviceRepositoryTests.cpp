#include <iostream>
#include <stdexcept>

#include "domain/DeviceModel.h"
#include "domain/DeviceState.h"
#include "infrastructure/MockDeviceRepository.h"

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

void testInitialization()
{
    MockDeviceRepository repo(makeModel(8, DeviceVariant::Pic8));
    DeviceState state = repo.loadState();

    expect(state.channels.size() == 8, "PIC-8 repository initializes 8 channels");
    expect(!state.zeroChannel.enabled, "Zero channel starts disabled");
    expect(state.zeroChannel.mode == ChannelMode::CC, "Zero channel starts in CC");
}

void testZeroChannelAffectsFollowersOnly()
{
    MockDeviceRepository repo(makeModel(8, DeviceVariant::Pic8));

    repo.followZeroChannel(2);
    repo.followZeroChannel(4);

    repo.setZeroChannelMode(ChannelMode::CV);
    repo.startZeroChannel();

    DeviceState state = repo.loadState();

    expect(state.channels[1].followZero, "Channel 2 follows zero");
    expect(state.channels[3].followZero, "Channel 4 follows zero");

    expect(state.channels[1].enabled, "Channel 2 enabled by zero channel");
    expect(state.channels[3].enabled, "Channel 4 enabled by zero channel");

    expect(state.channels[1].mode == ChannelMode::CV, "Channel 2 mode copied from zero");
    expect(state.channels[3].mode == ChannelMode::CV, "Channel 4 mode copied from zero");

    expect(!state.channels[0].enabled, "Channel 1 unaffected");
    expect(!state.channels[2].enabled, "Channel 3 unaffected");
}

void testZeroChannelDoesNotOverwriteSetpoints()
{
    MockDeviceRepository repo(makeModel(8, DeviceVariant::Pic8));

    repo.setChannelCurrentValue(3, 120);
    repo.setChannelVoltageValue(3, 356);
    repo.followZeroChannel(3);

    repo.startZeroChannel();
    repo.setZeroChannelMode(ChannelMode::CV);

    DeviceState state = repo.loadState();

    expect(state.channels[2].setCurrentRaw == 120, "Zero channel must not overwrite current setpoint");
    expect(state.channels[2].setVoltageRaw == 356, "Zero channel must not overwrite voltage setpoint");
}

void testPic16Initialization()
{
    MockDeviceRepository repo(makeModel(16, DeviceVariant::Pic16));
    DeviceState state = repo.loadState();

    expect(state.channels.size() == 16, "PIC-16 repository initializes 16 channels");
}

} // namespace

int main()
{
    try {
        testInitialization();
        testZeroChannelAffectsFollowersOnly();
        testZeroChannelDoesNotOverwriteSetpoints();
        testPic16Initialization();

        std::cout << "All MockDeviceRepository tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}