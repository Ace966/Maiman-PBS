#pragma once

#include "DeviceRepository.h"

#include <QString>
#include <memory>

namespace DeviceFactory {
// Sole ownership, no QObject parent; runtime values remain unreceived.
std::unique_ptr<DeviceRepository> create(
    const QString& filename, quint16 modelId, QString* error = nullptr);
}
