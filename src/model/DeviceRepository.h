#pragma once

#include "DeviceConfiguration.h"
#include <QObject>
#include <QVariant>

#include <optional>

// One device, sequential access coordinated by its owning application thread.
class DeviceRepository : public QObject {
    Q_OBJECT
public:
    explicit DeviceRepository(DeviceConfiguration configuration, QObject* parent = nullptr);
    const DeviceConfiguration& configuration() const { return m_configuration; }

    // Convert received raw data and store the processed value; this is not a device write.
    bool update(quint16 code, quint16 rawValue);
    QVariant value(quint16 code) const; // Unknown/unreceived: invalid QVariant, distinct from zero.
    // Numerical conversion only; does not change stored values or validate device operating limits.
    std::optional<quint16> toRaw(quint16 code, double value) const;
    void clear(); // Values only; configuration snapshot is retained.

signals:
    void propertyChanged(quint16 code, const QVariant& value);
    void cleared();

private:
    DeviceConfiguration m_configuration;
    QMap<quint16, QVariant> m_values;
};
