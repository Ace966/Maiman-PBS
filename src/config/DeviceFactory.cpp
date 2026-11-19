#include "DeviceFactory.h"
#include "XmlConfigLoader.h"

#include <utility>

QSharedPointer<DeviceRepository> DeviceFactory::create(
    const QString& filename, quint16 modelId, QString* error) {
    auto configuration = XmlConfigLoader::loadModel(filename, modelId, error);
    if (!configuration) return {};
    return QSharedPointer<DeviceRepository>::create(std::move(*configuration));
}
