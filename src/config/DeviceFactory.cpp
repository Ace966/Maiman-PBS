#include "DeviceFactory.h"
#include "XmlConfigLoader.h"

#include <utility>

std::unique_ptr<DeviceRepository> DeviceFactory::create(
    const QString& filename, quint16 modelId, QString* error) {
    auto configuration = XmlConfigLoader::loadModel(filename, modelId, error);
    if (!configuration) return nullptr;
    return std::make_unique<DeviceRepository>(std::move(*configuration));
}
