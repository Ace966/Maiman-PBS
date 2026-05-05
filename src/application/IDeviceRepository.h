#pragma once

#include "domain/DeviceModel.h"
#include "domain/DeviceState.h"

namespace application {

class IDeviceRepository
{
public:
    virtual ~IDeviceRepository() = default;

    virtual domain::DeviceModel loadModel() const = 0;
    virtual domain::DeviceState loadState() const = 0;

    virtual void startChannel(int channelIndex) = 0;
    virtual void stopChannel(int channelIndex) = 0;

    virtual void setChannelMode(int channelIndex, domain::ChannelMode mode) = 0;
    virtual void setChannelCurrentValue(int channelIndex, int rawValue) = 0;
    virtual void setChannelVoltageValue(int channelIndex, int rawValue) = 0;

    virtual void followZeroChannel(int channelIndex) = 0;
    virtual void ignoreZeroChannel(int channelIndex) = 0;

    virtual void startZeroChannel() = 0;
    virtual void stopZeroChannel() = 0;
    virtual void setZeroChannelMode(domain::ChannelMode mode) = 0;
};

} // namespace application