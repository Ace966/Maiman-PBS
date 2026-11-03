#pragma once

#include "DeviceConfiguration.h"

#include <QString>
#include <optional>

namespace XmlConfigLoader {
// Interpret a MultiBench-style DB tree. Model IDs are numeric identities,
// independent of device serial numbers and transport addresses.
// Validate the complete database; on failure return nullopt and one diagnostic.
std::optional<DeviceConfiguration> loadModel(
    const QString& filename, quint16 modelId, QString* error = nullptr);
}
