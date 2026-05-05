#pragma once

#include <QString>

#include "DeviceModel.h"

namespace domain {

class ChannelCommandResolver
{
public:
    explicit ChannelCommandResolver(const DeviceModel& model);

    QString stateCode(int channelIndex) const;

    QString currentValueCode(int channelIndex) const;
    QString currentMaximumCode(int channelIndex) const;
    QString currentMaximumLimitCode(int channelIndex) const;
    QString currentMeasuredCode(int channelIndex) const;
    QString currentProtectionCode(int channelIndex) const;

    QString voltageValueCode(int channelIndex) const;
    QString voltageMaximumCode(int channelIndex) const;
    QString voltageMaximumLimitCode(int channelIndex) const;
    QString voltageMeasuredCode(int channelIndex) const;
    QString voltageProtectionCode(int channelIndex) const;

private:
    QString resolve(const QString& baseHex, const QString& stepHex, int channelIndex) const;
    int hexToInt(const QString& hex) const;
    QString intToHex4(int value) const;
    void validateChannelIndex(int channelIndex) const;

    const DeviceModel& model_;
};

} // namespace domain