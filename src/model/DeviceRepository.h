#pragma once

#include "DeviceConfiguration.h"

// One device, sequential access coordinated by its owning application thread.
class DeviceRepository {
public:
    explicit DeviceRepository(DeviceConfiguration configuration);
    const DeviceConfiguration& configuration() const { return m_configuration; }

    // Record known raw data, not a device write; read-only measurements may update.
    bool update(quint16 id, quint16 rawValue);
    std::optional<quint16> value(quint16 id) const; // Unknown/unreceived: nullopt.
    void clear(); // Values only; configuration snapshot is retained.

private:
    DeviceConfiguration m_configuration;
    QMap<quint16, quint16> m_values;
};
