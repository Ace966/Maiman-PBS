#include "GuiFactory.h"
#include "XmlParser.h"

#include <QFile>
#include <QMap>
#include <QSet>
#include <utility>

namespace {
const TreeItem* field(const TreeItem& item, const char* name, QString&) {
    for (const auto& child : item.children()) {
        if (child.name() == QLatin1String(name)) return &child;
    }
    return nullptr;
}

QString text(const TreeItem& item, const char* name, QString& error) {
    const auto* value = field(item, name, error);
    if (!value || value->value().trimmed().isEmpty()) {
        if (error.isEmpty()) error = QStringLiteral("%1: missing %2").arg(item.name(), QLatin1String(name));
        return {};
    }
    return value->value();
}

std::optional<quint64> number(const TreeItem& item, const char* name, int base, quint64 maximum, QString& error) {
    const auto value = text(item, name, error);
    if (!error.isEmpty()) return std::nullopt;
    bool ok = false;
    const auto parsed = value.toULongLong(&ok, base);
    if (!ok || parsed > maximum) {
        error = QStringLiteral("%1: invalid %2").arg(item.name(), QLatin1String(name));
        return std::nullopt;
    }
    return parsed;
}

std::optional<quint16> reg(const TreeItem& item, const char* name, quint64 base,
                           const DeviceConfiguration& configuration, QString& error) {
    const auto offset = number(item, name, 16, 0xffff, error);
    if (!offset) return std::nullopt;
    const quint64 address = base + *offset;
    if (address > 0xffff) {
        error = QStringLiteral("%1: register-address overflow in %2").arg(item.name(), QLatin1String(name));
        return std::nullopt;
    }
    if (!configuration.parameters().contains(quint16(address))) {
        error = QStringLiteral("%1: nonexistent register %2 (%3)")
            .arg(item.name()).arg(address, 4, 16, QLatin1Char('0')).arg(QLatin1String(name));
        return std::nullopt;
    }
    return quint16(address);
}

std::optional<CardDesc> card(const TreeItem& item, quint16 channel, quint64 base,
                             const DeviceConfiguration& configuration, QString& error) {
    CardDesc result;
    result.channel = channel;
    result.title = text(item, "title", error).replace(QStringLiteral("{n}"), QString::number(channel));
    for (const auto& mapping : {std::pair{"stateOffset", &result.state},
             std::pair{"currentSetOffset", &result.currentSet}, std::pair{"currentOffset", &result.current},
             std::pair{"voltageSetOffset", &result.voltageSet}, std::pair{"voltageOffset", &result.voltage}}) {
        const auto code = reg(item, mapping.first, base, configuration, error);
        if (!code) return std::nullopt;
        *mapping.second = *code;
    }
    for (const auto& mapping : {std::pair{"runMask", &result.runMask}, std::pair{"modeMask", &result.modeMask},
             std::pair{"start", &result.startCode}, std::pair{"stop", &result.stopCode},
             std::pair{"cc", &result.ccCode}, std::pair{"cv", &result.cvCode}}) {
        const auto value = number(item, mapping.first, 16, 0xffff, error);
        if (!value) return std::nullopt;
        *mapping.second = quint16(*value);
    }
    if (!result.runMask || !result.modeMask) {
        error = QStringLiteral("Card: state masks must be nonzero");
        return std::nullopt;
    }
    for (const auto& mapping : {std::pair{"Overcurrent", &result.overcurrent},
                                std::pair{"Overvoltage", &result.overvoltage}}) {
        const auto* alarm = field(item, mapping.first, error);
        if (!alarm) {
            if (error.isEmpty()) error = QStringLiteral("Card: missing %1").arg(QLatin1String(mapping.first));
            return std::nullopt;
        }
        const auto code = reg(*alarm, "command", 0, configuration, error);
        if (!code) return std::nullopt;
        mapping.second->command = *code;
        const auto maskText = text(*alarm, "mask", error);
        if (maskText == QLatin1String("{bit}")) {
            mapping.second->mask = quint16(quint32(1) << (channel - 1));
        } else {
            const auto mask = number(*alarm, "mask", 16, 0xffff, error);
            if (!mask) return std::nullopt;
            mapping.second->mask = quint16(*mask);
        }
        if (!mapping.second->mask) {
            error = QStringLiteral("%1: mask must be nonzero").arg(alarm->name());
            return std::nullopt;
        }
        mapping.second->message = text(*alarm, "message", error)
            .replace(QStringLiteral("{n}"), QString::number(channel));
    }
    if (!error.isEmpty()) return std::nullopt;
    return result;
}

std::optional<LimitsDesc> limits(const TreeItem& item, quint16 channel, quint64 base,
                                 const DeviceConfiguration& configuration, QString& error) {
    LimitsDesc result;
    result.channel = channel;
    result.title = text(item, "title", error).replace(QStringLiteral("{n}"), QString::number(channel));
    for (const auto& mapping : {std::pair{"CurrentMaximum", &result.currentMaximum},
             std::pair{"VoltageMaximum", &result.voltageMaximum},
             std::pair{"CurrentProtection", &result.currentProtection},
             std::pair{"VoltageProtection", &result.voltageProtection}}) {
        const auto* control = field(item, mapping.first, error);
        if (!control) {
            if (error.isEmpty()) error = QStringLiteral("Limits: missing %1").arg(QLatin1String(mapping.first));
            return std::nullopt;
        }
        mapping.second->title = text(*control, "title", error);
        const auto code = reg(*control, "offset", base, configuration, error);
        if (!code) return std::nullopt;
        mapping.second->code = *code;
        if (field(*control, "maxOffset", error)) {
            const auto maximum = reg(*control, "maxOffset", base, configuration, error);
            if (!maximum) return std::nullopt;
            mapping.second->maxCode = *maximum;
        }
    }
    if (!error.isEmpty()) return std::nullopt;
    return result;
}
}

std::optional<DeviceWidgetDesc> GuiFactory::create(
    const QString& filename, const DeviceConfiguration& configuration, QString* error) {
    if (error) error->clear();
    QString diagnostic;
    const auto fail = [&]() -> std::optional<DeviceWidgetDesc> {
        if (error) *error = QStringLiteral("%1: %2").arg(filename, diagnostic);
        return std::nullopt;
    };
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        diagnostic = file.errorString();
        return fail();
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        diagnostic = file.errorString();
        return fail();
    }
    const auto tree = XmlParser::parse(bytes, &diagnostic);
    if (!tree) return fail();
    if (tree->name() != QLatin1String("GUI")) {
        diagnostic = QStringLiteral("Expected GUI root");
        return fail();
    }
    const auto* definitions = field(*tree, "Templates", diagnostic);
    if (!definitions) {
        if (diagnostic.isEmpty()) diagnostic = QStringLiteral("GUI: missing Templates");
        return fail();
    }
    QMap<QString, const TreeItem*> templates;
    for (const auto& item : definitions->children()) {
        if (item.name() != QLatin1String("Template")) continue;
        const auto name = text(item, "name", diagnostic);
        if (!diagnostic.isEmpty()) return fail();
        if (templates.contains(name)) {
            diagnostic = QStringLiteral("Duplicate template %1").arg(name);
            return fail();
        }
        templates.insert(name, &item);
    }
    const TreeItem* selected = nullptr;
    QSet<quint16> ids;
    for (const auto& item : tree->children()) {
        if (item.name() != QLatin1String("Device")) continue;
        const auto id = number(item, "id", 16, 0xffff, diagnostic);
        if (!id) return fail();
        if (*id == 0 || ids.contains(quint16(*id))) {
            diagnostic = QStringLiteral("Invalid or duplicate Device id");
            return fail();
        }
        ids.insert(quint16(*id));
        if (*id == configuration.modelId()) selected = &item;
    }
    if (!selected) {
        diagnostic = QStringLiteral("Unknown GUI model %1").arg(configuration.modelId(), 4, 16, QLatin1Char('0'));
        return fail();
    }
    DeviceWidgetDesc result;
    result.id = configuration.modelId();
    result.title = configuration.modelName();
    if (const auto* description = field(*selected, "Description", diagnostic))
        result.description = description->value().trimmed();
    QSet<quint16> channels;
    for (const auto& item : selected->children()) {
        if (item.name() == QLatin1String("Info")) {
            const auto title = text(item, "title", diagnostic);
            const auto code = reg(item, "code", 0, configuration, diagnostic);
            if (!code) return fail();
            result.info.append(DeviceInfo{title, *code});
        } else if (item.name() == QLatin1String("Use")) {
            const auto name = text(item, "template", diagnostic);
            const auto count = number(item, "count", 10, 16, diagnostic);
            const auto first = number(item, "first", 10, 16, diagnostic);
            const auto base = number(item, "base", 16, 0xffff, diagnostic);
            const auto stride = number(item, "stride", 16, 0xffff, diagnostic);
            if (!diagnostic.isEmpty()) return fail();
            if (!*count || !*first || !*stride || *first + *count - 1 > 16) {
                diagnostic = QStringLiteral("Use: count/first must describe channels 1..16 and stride must be positive");
                return fail();
            }
            if (!templates.contains(name)) {
                diagnostic = QStringLiteral("Unknown template %1").arg(name);
                return fail();
            }
            const auto* cardItem = field(*templates.value(name), "Card", diagnostic);
            const auto* limitsItem = field(*templates.value(name), "Limits", diagnostic);
            if (!cardItem || !limitsItem) {
                if (diagnostic.isEmpty()) diagnostic = QStringLiteral("Template requires one Card and one Limits");
                return fail();
            }
            for (quint64 index = 0; index < *count; ++index) {
                const quint16 channel = quint16(*first + index);
                if (channels.contains(channel)) {
                    diagnostic = QStringLiteral("Duplicate channel %1").arg(channel);
                    return fail();
                }
                const quint64 address = *base + index * *stride;
                auto channelCard = card(*cardItem, channel, address, configuration, diagnostic);
                if (!channelCard) return fail();
                auto channelLimits = limits(*limitsItem, channel, address, configuration, diagnostic);
                if (!channelLimits) return fail();
                channels.insert(channel);
                result.cards.append(std::move(*channelCard));
                result.limits.append(std::move(*channelLimits));
            }
        }
    }
    if (!diagnostic.isEmpty()) return fail();
    if (result.cards.isEmpty()) {
        diagnostic = QStringLiteral("Device requires physical channel descriptions");
        return fail();
    }
    return result;
}
