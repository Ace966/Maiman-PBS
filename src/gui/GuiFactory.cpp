#include "GuiFactory.h"
#include "XmlParser.h"

#include <QFile>
#include <QSet>
#include <utility>

namespace {
const TreeItem* field(const TreeItem& item, const char* name) {
    for (const auto& child : item.children()) {
        if (child.name() == QLatin1String(name)) return &child;
    }
    return nullptr;
}

QString text(const TreeItem& item, const char* name, QString& error) {
    const auto* value = field(item, name);
    if (!value || value->value().trimmed().isEmpty()) {
        if (error.isEmpty())
            error = QStringLiteral("%1: missing %2").arg(item.name(), QLatin1String(name));
        return {};
    }
    return value->value().trimmed();
}

std::optional<quint16> number(const TreeItem& item, const char* name, int base, QString& error) {
    const auto value = text(item, name, error);
    if (!error.isEmpty()) return std::nullopt;
    bool ok = false;
    const quint32 parsed = value.toUInt(&ok, base);
    if (!ok || parsed > 0xFFFF) {
        error = QStringLiteral("%1: invalid %2").arg(item.name(), QLatin1String(name));
        return std::nullopt;
    }
    return quint16(parsed);
}

std::optional<quint16> reg(const TreeItem& item, const char* name,
                           const DeviceConfiguration& config, QString& error) {
    const auto code = number(item, name, 16, error);
    if (!code) return std::nullopt;
    if (!config.parameters().contains(*code)) {
        error = QStringLiteral("%1: nonexistent register %2 (%3)")
            .arg(item.name())
            .arg(*code, 4, 16, QLatin1Char('0'))
            .arg(QLatin1String(name));
        return std::nullopt;
    }
    return code;
}

std::optional<ParamControl> parseParam(const TreeItem& item,
                                      const DeviceConfiguration& config, QString& error) {
    ParamControl result;
    result.name = item.value().trimmed();
    if (result.name.isEmpty()) {
        error = QStringLiteral("Param: missing label");
        return std::nullopt;
    }
    if (field(item, "value")) {
        const auto code = reg(item, "value", config, error);
        if (!code) return std::nullopt;
        result.value = *code;
    }
    if (field(item, "real")) {
        const auto code = reg(item, "real", config, error);
        if (!code) return std::nullopt;
        result.real = *code;
    }
    if (!result.value && !result.real) {
        error = QStringLiteral("Param: missing value/real register");
        return std::nullopt;
    }
    result.fixed = field(item, "fixed") != nullptr;
    return result;
}

bool parseParams(const TreeItem& section, QVector<ParamControl>& output,
                 const DeviceConfiguration& config, QString& error) {
    for (const auto& item : section.children()) {
        if (item.name() != QLatin1String("Param")) continue;
        auto control = parseParam(item, config, error);
        if (!control) return false;
        output.append(std::move(*control));
    }
    return true;
}

std::optional<LedDesc> parseLed(const TreeItem& item,
                                const DeviceConfiguration& config, QString& error) {
    LedDesc result;
    result.name = text(item, "label", error);
    if (!error.isEmpty()) return std::nullopt;
    for (const auto& child : item.children()) {
        if (child.name() != QLatin1String("LedMask")) continue;
        LedMask mask;
        const auto code = reg(child, "code", config, error);
        const auto bitMask = number(child, "mask", 16, error);
        if (!code || !bitMask || !*bitMask) {
            if (error.isEmpty()) error = QStringLiteral("LedMask: mask must be nonzero");
            return std::nullopt;
        }
        mask.code = *code;
        mask.mask = *bitMask;
        if (const auto* color = field(child, "maskColor")) mask.maskColor = color->value();
        if (const auto* color = field(child, "defaultColor")) mask.defaultColor = color->value();
        mask.message = child.value().trimmed();
        result.masks.append(std::move(mask));
    }
    if (result.masks.isEmpty()) {
        error = QStringLiteral("Led: no LedMask entries");
        return std::nullopt;
    }
    return result;
}

std::optional<CardDesc> parseCard(const TreeItem& item,
                                  const DeviceConfiguration& config, QString& error) {
    CardDesc result;
    const auto channel = number(item, "channel", 10, error);
    if (!channel || *channel == 0 || *channel > 16) {
        if (error.isEmpty()) error = QStringLiteral("Card: invalid channel");
        return std::nullopt;
    }
    result.channel = *channel;
    result.title = text(item, "title", error);
    if (!error.isEmpty()) return std::nullopt;
    for (const auto& mapping : {std::pair{"state", &result.state},
                                std::pair{"currentSet", &result.currentSet},
                                std::pair{"current", &result.current},
                                std::pair{"voltageSet", &result.voltageSet},
                                std::pair{"voltage", &result.voltage}}) {
        const auto code = reg(item, mapping.first, config, error);
        if (!code) return std::nullopt;
        *mapping.second = *code;
    }
    for (const auto& mapping : {std::pair{"runMask", &result.runMask},
                                std::pair{"modeMask", &result.modeMask},
                                std::pair{"start", &result.startCode},
                                std::pair{"stop", &result.stopCode},
                                std::pair{"cc", &result.ccCode},
                                std::pair{"cv", &result.cvCode}}) {
        const auto value = number(item, mapping.first, 16, error);
        if (!value) return std::nullopt;
        *mapping.second = *value;
    }
    if (!result.runMask || !result.modeMask) {
        error = QStringLiteral("Card: state masks must be nonzero");
        return std::nullopt;
    }
    for (const auto& mapping : {std::pair{"Overcurrent", &result.overcurrent},
                                std::pair{"Overvoltage", &result.overvoltage}}) {
        const auto* child = field(item, mapping.first);
        if (!child) {
            error = QStringLiteral("Card: missing %1").arg(QLatin1String(mapping.first));
            return std::nullopt;
        }
        const auto code = reg(*child, "command", config, error);
        const auto mask = number(*child, "mask", 16, error);
        if (!code || !mask || !*mask) {
            if (error.isEmpty()) error = QStringLiteral("Card: alarm mask must be nonzero");
            return std::nullopt;
        }
        mapping.second->command = *code;
        mapping.second->mask = *mask;
        mapping.second->message = text(*child, "message", error);
    }
    if (!error.isEmpty()) return std::nullopt;
    return result;
}

std::optional<LimitsDesc> parseLimitsChannel(const TreeItem& item,
                                             const DeviceConfiguration& config, QString& error) {
    LimitsDesc result;
    const auto channel = number(item, "channel", 10, error);
    if (!channel || *channel == 0 || *channel > 16) {
        if (error.isEmpty()) error = QStringLiteral("Limits: invalid channel");
        return std::nullopt;
    }
    result.channel = *channel;
    result.title = text(item, "title", error);
    if (!error.isEmpty()) return std::nullopt;
    for (const auto& child : item.children()) {
        if (child.name() != QLatin1String("Limit")) continue;
        LimitControl control;
        control.name = child.value().trimmed();
        if (control.name.isEmpty()) {
            error = QStringLiteral("Limit: missing label");
            return std::nullopt;
        }
        const auto code = reg(child, "code", config, error);
        if (!code) return std::nullopt;
        control.code = *code;
        if (field(child, "maxCode")) {
            const auto maximum = reg(child, "maxCode", config, error);
            if (!maximum) return std::nullopt;
            control.maxCode = *maximum;
        }
        result.controls.append(std::move(control));
    }
    if (result.controls.isEmpty()) {
        error = QStringLiteral("Limits: no controls");
        return std::nullopt;
    }
    return result;
}
} // namespace

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
    const auto tree = XmlParser::parse(file.readAll(), &diagnostic);
    if (!tree) return fail();
    if (tree->name() != QLatin1String("GUI")) {
        diagnostic = QStringLiteral("Expected GUI root");
        return fail();
    }

    const TreeItem* selected = nullptr;
    for (const auto& item : tree->children()) {
        if (item.name() != QLatin1String("Device")) continue;
        const auto id = number(item, "id", 16, diagnostic);
        if (!id) return fail();
        if (*id == configuration.modelId()) selected = &item;
    }
    if (!selected) {
        diagnostic = QStringLiteral("Unknown GUI model %1")
            .arg(configuration.modelId(), 4, 16, QLatin1Char('0'));
        return fail();
    }

    DeviceWidgetDesc result;
    result.id = configuration.modelId();
    result.title = configuration.modelName();
    if (const auto* content = field(*selected, "Content")) {
        if (const auto* description = field(*content, "Description"))
            result.description = description->value().trimmed();
    }
    if (const auto* header = field(*selected, "Header")) {
        if (!parseParams(*header, result.header, configuration, diagnostic)) return fail();
    }
    if (const auto* controls = field(*selected, "ParamControls")) {
        if (!parseParams(*controls, result.controls, configuration, diagnostic)) return fail();
    }
    if (const auto* leds = field(*selected, "Leds")) {
        for (const auto& led : leds->children()) {
            if (led.name() != QLatin1String("Led")) continue;
            auto description = parseLed(led, configuration, diagnostic);
            if (!description) return fail();
            result.leds.append(std::move(*description));
        }
    }

    const auto* cards = field(*selected, "Cards");
    if (!cards) {
        diagnostic = QStringLiteral("Device: missing Cards");
        return fail();
    }
    QSet<quint16> channels;
    for (const auto& item : cards->children()) {
        if (item.name() != QLatin1String("Card")) continue;
        auto description = parseCard(item, configuration, diagnostic);
        if (!description) return fail();
        if (channels.contains(description->channel)) {
            diagnostic = QStringLiteral("Duplicate channel %1").arg(description->channel);
            return fail();
        }
        channels.insert(description->channel);
        result.cards.append(std::move(*description));
    }
    if (result.cards.isEmpty()) {
        diagnostic = QStringLiteral("Device: no physical cards");
        return fail();
    }

    if (const auto* limits = field(*selected, "Limits")) {
        QSet<quint16> limitChannels;
        for (const auto& item : limits->children()) {
            if (item.name() != QLatin1String("Channel")) continue;
            auto description = parseLimitsChannel(item, configuration, diagnostic);
            if (!description) return fail();
            if (!channels.contains(description->channel) || limitChannels.contains(description->channel)) {
                diagnostic = QStringLiteral("Limits: unmatched or duplicate channel %1").arg(description->channel);
                return fail();
            }
            limitChannels.insert(description->channel);
            result.limits.append(std::move(*description));
        }
    }
    return result;
}
