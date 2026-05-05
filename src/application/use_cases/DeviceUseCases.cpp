#include "DeviceUseCases.h"

#include <stdexcept>
#include <string>

namespace application {

DeviceUseCases::DeviceUseCases(IDeviceRepository& repository)
    : repository_(repository)
{
}

domain::DeviceModel DeviceUseCases::loadModel() const
{
    return repository_.loadModel();
}

domain::DeviceState DeviceUseCases::loadState() const
{
    return repository_.loadState();
}

void DeviceUseCases::startChannel(int channelIndex)
{
    validateChannelIndex(channelIndex);
    repository_.startChannel(channelIndex);
}

void DeviceUseCases::stopChannel(int channelIndex)
{
    validateChannelIndex(channelIndex);
    repository_.stopChannel(channelIndex);
}

void DeviceUseCases::setChannelMode(int channelIndex, domain::ChannelMode mode)
{
    validateChannelIndex(channelIndex);
    repository_.setChannelMode(channelIndex, mode);
}

void DeviceUseCases::setChannelCurrentValue(int channelIndex, int rawValue)
{
    validateChannelIndex(channelIndex);
    validateRawValue(rawValue, "current");
    repository_.setChannelCurrentValue(channelIndex, rawValue);
}

void DeviceUseCases::setChannelVoltageValue(int channelIndex, int rawValue)
{
    validateChannelIndex(channelIndex);
    validateRawValue(rawValue, "voltage");
    repository_.setChannelVoltageValue(channelIndex, rawValue);
}

void DeviceUseCases::followZeroChannel(int channelIndex)
{
    validateZeroChannelSupported();
    validateChannelIndex(channelIndex);
    repository_.followZeroChannel(channelIndex);
}

void DeviceUseCases::ignoreZeroChannel(int channelIndex)
{
    validateZeroChannelSupported();
    validateChannelIndex(channelIndex);
    repository_.ignoreZeroChannel(channelIndex);
}

void DeviceUseCases::startZeroChannel()
{
    validateZeroChannelSupported();
    repository_.startZeroChannel();
}

void DeviceUseCases::stopZeroChannel()
{
    validateZeroChannelSupported();
    repository_.stopZeroChannel();
}

void DeviceUseCases::setZeroChannelMode(domain::ChannelMode mode)
{
    validateZeroChannelSupported();
    repository_.setZeroChannelMode(mode);
}

void DeviceUseCases::validateChannelIndex(int channelIndex) const
{
    const auto model = repository_.loadModel();

    if (channelIndex < 1 || channelIndex > model.channelCount) {
        throw std::out_of_range("Channel index is out of valid range.");
    }
}

void DeviceUseCases::validateRawValue(int rawValue, const char* valueName) const
{
    if (rawValue < 0) {
        throw std::invalid_argument(std::string("Invalid negative ") + valueName + " value.");
    }
}

void DeviceUseCases::validateZeroChannelSupported() const
{
    const auto model = repository_.loadModel();

    if (!model.supportsZeroChannel) {
        throw std::runtime_error("Zero channel is not supported by this device model.");
    }
}

} // namespace application