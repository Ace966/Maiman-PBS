#include "DeviceRepository.h"

#include <utility>

DeviceRepository::DeviceRepository(DeviceConfiguration configuration)
    : m_configuration(std::move(configuration)) {}

bool DeviceRepository::update(quint16 id, quint16 rawValue) {
    if (!m_configuration.parameters().contains(id)) return false;
    m_values.insert(id, rawValue);
    return true;
}

std::optional<quint16> DeviceRepository::value(quint16 id) const {
    const auto it = m_values.constFind(id);
    if (it == m_values.cend()) return std::nullopt;
    return it.value();
}

void DeviceRepository::clear() {
    m_values.clear();
}
