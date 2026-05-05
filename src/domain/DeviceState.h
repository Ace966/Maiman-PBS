#pragma once

#include <QString>
#include <QVector>

namespace domain {

enum class ChannelMode {
    CC,
    CV
};

struct ChannelState {
    int index{0};

    bool enabled{false};
    ChannelMode mode{ChannelMode::CC};
    bool followZero{false};

    int setCurrentRaw{0};
    int measuredCurrentRaw{0};

    int setVoltageRaw{0};
    int measuredVoltageRaw{0};

    bool overcurrent{false};
    bool overvoltage{false};
};

struct ZeroChannelState {
    bool enabled{false};
    ChannelMode mode{ChannelMode::CC};
};

struct ProtectionState {
    bool overheatWarning{false};
    bool overheat{false};
    bool anyChannelFault{false};
};

struct DeviceState {
    QString serialNumber;
    QString firmwareVersion;

    ZeroChannelState zeroChannel;
    ProtectionState protection;

    QVector<ChannelState> channels;
};

} // namespace domain