#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

// All register fields are addresses, not device values.
// The live values remain in DeviceRepository.
struct ParamControl {
public:
    QString name;
    quint16 value{0};  // Optional setpoint register.
    quint16 real{0};   // Optional measured/read-only register.
    bool fixed{false};
};

struct LedMask {
public:
    quint16 code{0};
    quint16 mask{0};
    QString maskColor;
    QString defaultColor;
    QString message;
};

struct LedDesc {
public:
    QString name;
    QVector<LedMask> masks;
};

struct CardAlarm {
public:
    quint16 command{0};
    quint16 mask{0};
    QString message;
};

struct CardDesc {
public:
    quint16 channel{0};
    QString title;
    quint16 state{0};
    quint16 currentSet{0};
    quint16 current{0};
    quint16 voltageSet{0};
    quint16 voltage{0};
    quint16 runMask{0};
    quint16 modeMask{0};
    quint16 startCode{0};
    quint16 stopCode{0};
    quint16 ccCode{0};
    quint16 cvCode{0};
    CardAlarm overcurrent;
    CardAlarm overvoltage;
};

struct LimitControl {
public:
    QString name;
    quint16 code{0};
    double minValue{0};
    std::optional<quint16> maxCode;
};

// This is the separate Limits menu section, not part of CardDesc.
struct LimitsDesc {
public:
    quint16 channel{0};
    QString title;
    QVector<LimitControl> controls;
};

struct DeviceWidgetDesc {
public:
    quint16 id{0};
    QString title;
    QString description;
    QVector<ParamControl> header;
    QVector<ParamControl> controls;
    QVector<LedDesc> leds;
    QVector<CardDesc> cards;
    QVector<LimitsDesc> limits;
};
