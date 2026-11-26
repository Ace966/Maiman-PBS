#pragma once

#include "DeviceRepository.h"

#include <QString>
#include <QSharedPointer>

class DeviceFactory {
public:
    // Shared ownership without a QObject parent; access and release in the owning thread.
    static QSharedPointer<DeviceRepository> create(
        const QString& filename, quint16 modelId, QString* error = nullptr);
};
