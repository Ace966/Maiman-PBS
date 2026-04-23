#include "ChannelCommandResolver.h"

#include <stdexcept>

namespace domain {

ChannelCommandResolver::ChannelCommandResolver(const DeviceModel& model)
    : model_(model)
{
}

QString ChannelCommandResolver::stateCode(int channelIndex) const
{
    return resolve(model_.channelPattern.state.stateBaseHex,
                   model_.channelPattern.state.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::currentValueCode(int channelIndex) const
{
    return resolve(model_.channelPattern.current.valueBaseHex,
                   model_.channelPattern.current.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::currentMaximumCode(int channelIndex) const
{
    return resolve(model_.channelPattern.current.maximumBaseHex,
                   model_.channelPattern.current.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::currentMaximumLimitCode(int channelIndex) const
{
    return resolve(model_.channelPattern.current.maximumLimitBaseHex,
                   model_.channelPattern.current.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::currentMeasuredCode(int channelIndex) const
{
    return resolve(model_.channelPattern.current.measuredBaseHex,
                   model_.channelPattern.current.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::currentProtectionCode(int channelIndex) const
{
    return resolve(model_.channelPattern.current.protectionBaseHex,
                   model_.channelPattern.current.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::voltageValueCode(int channelIndex) const
{
    return resolve(model_.channelPattern.voltage.valueBaseHex,
                   model_.channelPattern.voltage.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::voltageMaximumCode(int channelIndex) const
{
    return resolve(model_.channelPattern.voltage.maximumBaseHex,
                   model_.channelPattern.voltage.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::voltageMaximumLimitCode(int channelIndex) const
{
    return resolve(model_.channelPattern.voltage.maximumLimitBaseHex,
                   model_.channelPattern.voltage.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::voltageMeasuredCode(int channelIndex) const
{
    return resolve(model_.channelPattern.voltage.measuredBaseHex,
                   model_.channelPattern.voltage.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::voltageProtectionCode(int channelIndex) const
{
    return resolve(model_.channelPattern.voltage.protectionBaseHex,
                   model_.channelPattern.voltage.stepHex,
                   channelIndex);
}

QString ChannelCommandResolver::resolve(const QString& baseHex, const QString& stepHex, int channelIndex) const
{
    validateChannelIndex(channelIndex);

    const int base = hexToInt(baseHex);
    const int step = hexToInt(stepHex);
    const int value = base + (channelIndex - 1) * step;

    return intToHex4(value);
}

int ChannelCommandResolver::hexToInt(const QString& hex) const
{
    bool ok = false;
    const int value = hex.toInt(&ok, 16);
    if (!ok) {
        throw std::runtime_error(("Invalid hex value: " + hex).toStdString());
    }
    return value;
}

QString ChannelCommandResolver::intToHex4(int value) const
{
    return QString("%1").arg(value, 4, 16, QChar('0')).toUpper();
}

void ChannelCommandResolver::validateChannelIndex(int channelIndex) const
{
    if (channelIndex < 1 || channelIndex > model_.channelCount) {
        throw std::out_of_range("Channel index is out of valid range.");
    }
}

} // namespace domain