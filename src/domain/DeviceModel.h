#pragma once

#include <QChar>
#include <QMap>
#include <QString>

namespace domain {

enum class DeviceVariant {
    Pic8,
    Pic16,
    Unknown
};

struct TransportSettings {
    QString type;
    int baudRate{115200};
    int dataBits{8};
    int stopBits{1};
    QString parity{"none"};
    QString flowControl{"none"};
    QString commandTerminator{"\\r"};
};

struct ProtocolSettings {
    QString mode{"text-plain"};
    QChar setPrefix{'P'};
    QChar getPrefix{'J'};
    QChar responsePrefix{'K'};
    QChar errorPrefix{'E'};
};

struct ParameterDefinition {
    QString key;
    QString codeHex;
    QString access;
};

struct StateActionCodes {
    QString startHex;
    QString stopHex;
    QString selectCCHex;
    QString selectCVHex;
    QString followZeroHex;
    QString ignoreZeroHex;
};

struct ZeroChannelActionCodes {
    QString saveHex;
    QString clearHex;
    QString startHex;
    QString stopHex;
    QString selectCCHex;
    QString selectCVHex;
};

struct ZeroChannelDefinition {
    QString stateCodeHex;
    QString access;
    ZeroChannelActionCodes actions;
};

struct ChannelCurrentPattern {
    QString valueBaseHex;
    QString maximumBaseHex;
    QString maximumLimitBaseHex;
    QString measuredBaseHex;
    QString protectionBaseHex;
    QString stepHex;
    double scale{0.1};
    QString unit{"mA"};
};

struct ChannelVoltagePattern {
    QString valueBaseHex;
    QString maximumBaseHex;
    QString maximumLimitBaseHex;
    QString measuredBaseHex;
    QString protectionBaseHex;
    QString stepHex;
    double scale{0.01};
    QString unit{"V"};
};

struct ChannelStatePattern {
    QString stateBaseHex;
    QString stepHex;
};

struct ChannelPattern {
    ChannelStatePattern state;
    ChannelCurrentPattern current;
    ChannelVoltagePattern voltage;
    StateActionCodes stateActions;
};

struct DeviceModel {
    QString id;
    QString name;
    DeviceVariant variant{DeviceVariant::Unknown};
    int channelCount{0};
    bool supportsZeroChannel{false};

    TransportSettings transport;
    ProtocolSettings protocol;

    QMap<QString, ParameterDefinition> globalParameters;
    ZeroChannelDefinition zeroChannel;
    ChannelPattern channelPattern;
};

} // namespace domain