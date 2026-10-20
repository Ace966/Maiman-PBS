#pragma once

#include "DeviceConfiguration.h"

#include <QString>
#include <optional>

namespace YamlConfigLoader {
// One PIC definition using USB ASCII register IDs; no transport/runtime state.
// Failure returns nullopt and one diagnostic. Success clears the supplied error.
std::optional<DeviceConfiguration> loadFile(const QString& filename, QString* error = nullptr);

// Select a model from the shared catalog. IDs are numeric hexadecimal identities
std::optional<DeviceConfiguration> loadModel(
    const QString& filename, quint16 modelId, QString* error = nullptr);
}
