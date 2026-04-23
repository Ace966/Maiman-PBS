#pragma once

#include <QString>

#include "domain/DeviceModel.h"

namespace infrastructure::config {

class XmlDeviceModelLoader
{
public:
    domain::DeviceModel loadFromFile(const QString& filePath) const;
};

} // namespace infrastructure::config