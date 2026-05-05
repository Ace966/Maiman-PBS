#pragma once

#include <QString>

#include "domain/DeviceModel.h"

namespace infrastructure {

class XmlDeviceModelLoader
{
public:
    domain::DeviceModel loadFromFile(const QString& filePath,
                                     const QString& modelId) const;
};

} // namespace infrastructure