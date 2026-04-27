#pragma once

#include "application/IDeviceRepository.h"

namespace infrastructure {

class MockDeviceRepository : public application::IDeviceRepository
{
public:
    explicit MockDeviceRepository(const domain::DeviceModel& model);

    domain::DeviceModel loadModel() const override;
    domain::DeviceState loadState() const override;

    void startChannel(int channelIndex) override;
    void stopChannel(int channelIndex) override;

    void setChannelMode(int channelIndex, domain::ChannelMode mode) override;
    void setChannelCurrentValue(int channelIndex, int rawValue) override;
    void setChannelVoltageValue(int channelIndex, int rawValue) override;

    void followZeroChannel(int channelIndex) override;
    void ignoreZeroChannel(int channelIndex) override;

    void startZeroChannel() override;
    void stopZeroChannel() override;
    void setZeroChannelMode(domain::ChannelMode mode) override;

private:
    domain::ChannelState& findChannel(int channelIndex);
    const domain::ChannelState& findChannel(int channelIndex) const;

    void applyZeroChannelToFollowers();
    void updateProtectionSummary();

    domain::DeviceModel model_;
    domain::DeviceState state_;
};

} // namespace infrastructure