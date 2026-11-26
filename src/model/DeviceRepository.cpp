#include "DeviceRepository.h"

#include <utility>
#include <algorithm>
#include <cmath>
#include <limits>

DeviceRepository::DeviceRepository(DeviceConfiguration configuration, QObject* parent)
    : QObject(parent), m_configuration(std::move(configuration)) {}

bool DeviceRepository::update(quint16 code, quint16 rawValue) {
    const auto it = m_configuration.parameters().constFind(code);
    if (it == m_configuration.parameters().cend()) return false;
    const auto& parameter = it.value();
    if (!std::isfinite(parameter.scale) || parameter.scale == 0.0) return false;
    int interpreted = int(rawValue);
    if (parameter.isSigned && rawValue >= 0x8000) interpreted -= 0x10000;
    QVariant processed;
    if (parameter.scale == 1.0) {
        if (parameter.isSigned) processed = interpreted;
        else processed = QVariant::fromValue(uint(rawValue));
    } else {
        const double value = interpreted * parameter.scale;
        if (!std::isfinite(value)) return false;
        processed = value;
    }
    const auto previous = m_values.constFind(code);
    if (previous != m_values.cend() && previous.value().toDouble() == processed.toDouble()) return true;
    m_values.insert(code, processed);
    emit propertyChanged(code, processed);
    return true;
}

QVariant DeviceRepository::value(quint16 code) const {
    return m_values.value(code);
}

std::optional<quint16> DeviceRepository::toRaw(quint16 code, double value) const {
    const auto it = m_configuration.parameters().constFind(code);
    if (it == m_configuration.parameters().cend() || !std::isfinite(value)) return std::nullopt;
    const auto& parameter = it.value();
    if (!std::isfinite(parameter.scale) || parameter.scale == 0.0) return std::nullopt;
    const double raw = value / parameter.scale;
    if (!std::isfinite(raw)) return std::nullopt;
    const double minimum = parameter.isSigned ? -32768.0 : 0.0;
    const double maximum = parameter.isSigned ? 32767.0 : 65535.0;
    // Only boundary arithmetic noise is allowed before rounding, never a fractional raw step.
    constexpr double error = 4.0 * std::numeric_limits<double>::epsilon();
    if (raw < minimum - error * std::max(1.0, std::abs(minimum))
        || raw > maximum + error * std::max(1.0, std::abs(maximum))) return std::nullopt;
    const double rounded = std::round(raw);
    if (rounded < minimum || rounded > maximum) return std::nullopt;
    return static_cast<quint16>(static_cast<int>(rounded)); // Valid negative values encode modulo65536.
}

void DeviceRepository::clear() {
    if (m_values.isEmpty()) return;
    m_values.clear();
    emit cleared();
}
