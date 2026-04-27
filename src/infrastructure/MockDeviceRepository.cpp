#include "MockDeviceRepository.h"

#include <stdexcept>

namespace infrastructure {

MockDeviceRepository::MockDeviceRepository(const domain::DeviceModel& model)
    : model_(model)
{
    state_.serialNumber = "0000";
    state_.firmwareVersion = "0000";
    state_.zeroChannel.enabled = false;
    state_.zeroChannel.mode = domain::ChannelMode::CC;

    state_.channels.reserve(model_.channelCount);
    for (int i = 1; i <= model_.channelCount; ++i) {
        domain::ChannelState channel;
        channel.index = i;
        channel.enabled = false;
        channel.mode = domain::ChannelMode::CC;
        channel.followZero = false;
        state_.channels.push_back(channel);
    }

    updateProtectionSummary();
}

domain::DeviceModel MockDeviceRepository::loadModel() const
{
    return model_;
}

domain::DeviceState MockDeviceRepository::loadState() const
{
    return state_;
}

void MockDeviceRepository::startChannel(int channelIndex)
{
    auto& channel = findChannel(channelIndex);
    channel.enabled = true;
    updateProtectionSummary();
}

void MockDeviceRepository::stopChannel(int channelIndex)
{
    auto& channel = findChannel(channelIndex);
    channel.enabled = false;
    updateProtectionSummary();
}

void MockDeviceRepository::setChannelMode(int channelIndex, domain::ChannelMode mode)
{
    auto& channel = findChannel(channelIndex);
    channel.mode = mode;
}

void MockDeviceRepository::setChannelCurrentValue(int channelIndex, int rawValue)
{
    auto& channel = findChannel(channelIndex);
    channel.setCurrentRaw = rawValue;
    channel.measuredCurrentRaw = rawValue;
    channel.overcurrent = false;
    updateProtectionSummary();
}

void MockDeviceRepository::setChannelVoltageValue(int channelIndex, int rawValue)
{
    auto& channel = findChannel(channelIndex);
    channel.setVoltageRaw = rawValue;
    channel.measuredVoltageRaw = rawValue;
    channel.overvoltage = false;
    updateProtectionSummary();
}

void MockDeviceRepository::followZeroChannel(int channelIndex)
{
    auto& channel = findChannel(channelIndex);
    channel.followZero = true;
    channel.mode = state_.zeroChannel.mode;
    channel.enabled = state_.zeroChannel.enabled;
}

void MockDeviceRepository::ignoreZeroChannel(int channelIndex)
{
    auto& channel = findChannel(channelIndex);
    channel.followZero = false;
}

void MockDeviceRepository::startZeroChannel()
{
    state_.zeroChannel.enabled = true;
    applyZeroChannelToFollowers();
    updateProtectionSummary();
}

void MockDeviceRepository::stopZeroChannel()
{
    state_.zeroChannel.enabled = false;
    applyZeroChannelToFollowers();
    updateProtectionSummary();
}

void MockDeviceRepository::setZeroChannelMode(domain::ChannelMode mode)
{
    state_.zeroChannel.mode = mode;
    applyZeroChannelToFollowers();
}

domain::ChannelState& MockDeviceRepository::findChannel(int channelIndex)
{
    if (channelIndex < 1 || channelIndex > state_.channels.size()) {
        throw std::out_of_range("Channel index is out of range.");
    }
    return state_.channels[channelIndex - 1];
}

const domain::ChannelState& MockDeviceRepository::findChannel(int channelIndex) const
{
    if (channelIndex < 1 || channelIndex > state_.channels.size()) {
        throw std::out_of_range("Channel index is out of range.");
    }
    return state_.channels[channelIndex - 1];
}

void MockDeviceRepository::applyZeroChannelToFollowers()
{
    for (auto& channel : state_.channels) {
        if (!channel.followZero) {
            continue;
        }

        channel.enabled = state_.zeroChannel.enabled;
        channel.mode = state_.zeroChannel.mode;
    }
}

void MockDeviceRepository::updateProtectionSummary()
{
    bool anyFault = false;
    for (const auto& channel : state_.channels) {
        if (channel.overcurrent || channel.overvoltage) {
            anyFault = true;
            break;
        }
    }

    state_.protection.anyChannelFault = anyFault;
}

} // namespace infrastructure