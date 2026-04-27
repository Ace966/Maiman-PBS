#pragma once

#include "application/IDeviceRepository.h"

namespace application {

class DeviceUseCases
{
public:
    explicit DeviceUseCases(IDeviceRepository& repository);

    domain::DeviceModel loadModel() const;
    domain::DeviceState loadState() const;

    void startChannel(int channelIndex);
    void stopChannel(int channelIndex);

    void setChannelMode(int channelIndex, domain::ChannelMode mode);
    void setChannelCurrentValue(int channelIndex, int rawValue);
    void setChannelVoltageValue(int channelIndex, int rawValue);

    void followZeroChannel(int channelIndex);
    void ignoreZeroChannel(int channelIndex);

    void startZeroChannel();
    void stopZeroChannel();
    void setZeroChannelMode(domain::ChannelMode mode);

private:
    void validateChannelIndex(int channelIndex) const;
    void validateRawValue(int rawValue, const char* valueName) const;
    void validateZeroChannelSupported() const;

    IDeviceRepository& repository_;
};

} // namespace application