#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

struct DeviceInfo {
public:
    QString title;
    quint16 code{0};
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
    QString title;
    quint16 code{0};
    double minValue{0}; // GUI v1 policy; no received values or inferred hardware limits.
    std::optional<quint16> maxCode;
};

struct LimitsDesc {
public:
    quint16 channel{0};
    QString title;
    LimitControl currentMaximum;
    LimitControl voltageMaximum;
    LimitControl currentProtection;
    LimitControl voltageProtection;
};

struct DeviceWidgetDesc {
public:
    quint16 id{0};
    QString title;
    QString description;
    QVector<DeviceInfo> info;
    QVector<CardDesc> cards;
    QVector<LimitsDesc> limits;
};
