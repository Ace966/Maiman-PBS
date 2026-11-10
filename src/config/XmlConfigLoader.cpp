#include "XmlConfigLoader.h"
#include "XmlParser.h"

#include <QFile>
#include <QSet>
#include <cmath>
#include <limits>
#include <utility>

namespace {
// MultiBench fields (including XML attributes) are named child TreeItems.
const TreeItem* child(const TreeItem& item, const char* name) {
    for (const auto& candidate : item.children())
        if (candidate.name() == QLatin1String(name)) return &candidate;
    return nullptr;
}

std::optional<quint64> number(const TreeItem& item, const char* name, int base,
                              quint64 maximum, QString& error) {
    const auto* field = child(item, name);
    bool ok = false;
    const auto value = field ? field->value().toULongLong(&ok, base) : 0;
    if (!ok || value > maximum) {
        error = QStringLiteral("%1: missing or invalid %2").arg(item.name(), QLatin1String(name));
        return std::nullopt;
    }
    return value;
}

bool command(const TreeItem& item, DeviceConfiguration& configuration, QString& error) {
    const auto code = number(item, "code", 16, 0xffff, error);
    if (!code) return false;
    if (*code == 0) {
        error = QStringLiteral("Command: code must be nonzero");
        return false;
    }
    ParameterDefinition definition;
    if (const auto* unit = child(item, "unit")) definition.unit = unit->value();
    if (const auto* field = child(item, "divider")) {
        bool ok = false;
        const auto divider = field->value().toDouble(&ok);
        if (!ok || !std::isfinite(divider) || divider == 0 || !std::isfinite(1.0 / divider)) {
            error = QStringLiteral("Command: divider must have a finite nonzero reciprocal");
            return false;
        }
        definition.scale = 1.0 / divider;
    }
    if (child(item, "tol")) {
        const auto tolerance = number(item, "tol", 10, std::numeric_limits<quint8>::max(), error);
        if (!tolerance) return false;
        definition.tolerance = quint8(*tolerance);
    }
    if (!configuration.addParameter(quint16(*code), definition)) {
        error = QStringLiteral("Command %1: duplicate code").arg(*code, 4, 16, QLatin1Char('0'));
        return false;
    }
    return true;
}

std::optional<DeviceConfiguration> device(const TreeItem& item, QString& error) {
    const auto id = number(item, "id", 16, 0xffff, error);
    if (!id) return std::nullopt;
    if (*id == 0) {
        error = QStringLiteral("Device: id must be nonzero");
        return std::nullopt;
    }
    const auto* name = child(item, "name");
    if (!name || name->value().isEmpty()) {
        error = QStringLiteral("Device: missing or empty name");
        return std::nullopt;
    }
    DeviceConfiguration result(quint16(*id), name->value());
    bool hasCommands = false;
    for (const auto& list : item.children()) {
        if (list.name() != QLatin1String("Commands")) continue;
        if (hasCommands) {
            error = QStringLiteral("Device: duplicate Commands element");
            return std::nullopt;
        }
        hasCommands = true;
        for (const auto& entry : list.children())
            if (entry.name() == QLatin1String("Command") && !command(entry, result, error))
                return std::nullopt;
    }
    if (result.parameters().isEmpty()) {
        error = QStringLiteral("Device: Commands must contain at least one Command");
        return std::nullopt;
    }
    return result;
}
}

std::optional<DeviceConfiguration> XmlConfigLoader::loadModel(
    const QString& filename, quint16 modelId, QString* error) {
    if (error) error->clear();
    QString diagnostic;
    // Keep diagnostics available internally even when the caller does not request them.
    const auto fail = [&]() -> std::optional<DeviceConfiguration> {
        if (error) *error = QStringLiteral("%1: %2").arg(filename, diagnostic);
        return std::nullopt;
    };
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        diagnostic = file.errorString();
        return fail();
    }
    const auto data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        diagnostic = file.errorString();
        return fail();
    }
    const auto tree = XmlParser::parse(data, &diagnostic);
    if (!tree) return fail();
    if (tree->name() != QLatin1String("DB")) {
        diagnostic = QStringLiteral("Expected DB root");
        return fail();
    }
    std::optional<DeviceConfiguration> selected;
    QSet<quint16> ids;
    for (const auto& entry : tree->children()) {
        if (entry.name() != QLatin1String("Device")) continue;
        auto configuration = device(entry, diagnostic);
        if (!configuration) return fail();
        if (ids.contains(configuration->modelId())) {
            diagnostic = QStringLiteral("DB: duplicate Device id");
            return fail();
        }
        ids.insert(configuration->modelId());
        if (configuration->modelId() == modelId) selected = std::move(configuration);
    }
    if (!selected) {
        diagnostic = QStringLiteral("unknown model ID %1").arg(modelId, 4, 16, QLatin1Char('0'));
        return fail();
    }
    return selected;
}
