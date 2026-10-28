#include "XmlConfigLoader.h"

#include <QFile>
#include <QSet>
#include <QXmlStreamReader>
#include <cmath>
#include <limits>
#include <utility>

namespace {
std::optional<quint64> number(QXmlStreamReader& xml, const QXmlStreamAttributes& attributes,
                              const char* name, int base, quint64 maximum) {
    bool ok = false;
    const auto value = attributes.value(QLatin1String(name)).toString().toULongLong(&ok, base);
    if (!ok || value > maximum) {
        xml.raiseError(QStringLiteral("%1: missing or invalid %2").arg(xml.name().toString(), QLatin1String(name)));
        return std::nullopt;
    }
    return value;
}

bool command(QXmlStreamReader& xml, DeviceConfiguration& configuration) {
    const auto attributes = xml.attributes();
    const auto code = number(xml, attributes, "code", 16, 0xffff);
    if (!code) return false;
    if (*code == 0) {
        xml.raiseError(QStringLiteral("Command: code must be nonzero"));
        return false;
    }
    ParameterDefinition definition;
    definition.name = attributes.value(QLatin1String("pbsName")).toString();
    if (definition.name.isEmpty()) {
        xml.raiseError(QStringLiteral("Command: missing or empty pbsName"));
        return false;
    }
    const auto access = attributes.value(QLatin1String("pbsAccess"));
    if (access == QLatin1String("r")) definition.access = ParameterAccess::ReadOnly;
    else if (access == QLatin1String("w")) definition.access = ParameterAccess::WriteOnly;
    else if (access == QLatin1String("rw")) definition.access = ParameterAccess::ReadWrite;
    else {
        xml.raiseError(QStringLiteral("Command: pbsAccess must be r, w or rw"));
        return false;
    }
    if (attributes.hasAttribute(QLatin1String("pbsChannel"))) {
        const auto channel = number(xml, attributes, "pbsChannel", 10, std::numeric_limits<quint8>::max());
        if (!channel) return false;
        definition.channel = quint8(*channel);
    }
    definition.unit = attributes.value(QLatin1String("unit")).toString();
    definition.unit.replace(QStringLiteral("(deg)"), QString(QChar(0x00b0)));
    if (attributes.hasAttribute(QLatin1String("divider"))) {
        bool ok = false;
        const auto divider = attributes.value(QLatin1String("divider")).toDouble(&ok);
        if (!ok || !std::isfinite(divider) || divider == 0 || !std::isfinite(1.0 / divider)) {
            xml.raiseError(QStringLiteral("Command: divider must have a finite nonzero reciprocal"));
            return false;
        }
        definition.scale = 1.0 / divider;
    }
    if (attributes.hasAttribute(QLatin1String("tol"))) {
        const auto tolerance = number(xml, attributes, "tol", 10, std::numeric_limits<quint8>::max());
        if (!tolerance) return false;
        definition.tolerance = quint8(*tolerance);
    }
    if (!configuration.addParameter(quint16(*code), definition)) {
        xml.raiseError(QStringLiteral("Command %1: duplicate code or pbsChannel outside model")
                           .arg(*code, 4, 16, QLatin1Char('0')));
        return false;
    }
    xml.skipCurrentElement();
    return !xml.hasError();
}

std::optional<DeviceConfiguration> device(QXmlStreamReader& xml) {
    const auto attributes = xml.attributes();
    const auto id = number(xml, attributes, "id", 16, 0xffff);
    if (!id) return std::nullopt;
    if (*id == 0) {
        xml.raiseError(QStringLiteral("Device: id must be nonzero"));
        return std::nullopt;
    }
    const auto name = attributes.value(QLatin1String("name")).toString();
    if (name.isEmpty()) {
        xml.raiseError(QStringLiteral("Device: missing or empty name"));
        return std::nullopt;
    }
    const auto count = number(xml, attributes, "pbsChannelCount", 10, std::numeric_limits<int>::max());
    if (!count) return std::nullopt;
    DeviceConfiguration result(quint16(*id), name, int(*count));
    bool hasCommands = false;
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("Commands")) {
            xml.skipCurrentElement();
            continue;
        }
        if (hasCommands) {
            xml.raiseError(QStringLiteral("Device: duplicate Commands element"));
            return std::nullopt;
        }
        hasCommands = true;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("Command")) {
                if (!command(xml, result)) return std::nullopt;
            } else xml.skipCurrentElement();
        }
    }
    if (!hasCommands || result.parameters().isEmpty())
        xml.raiseError(QStringLiteral("Device: Commands must contain at least one Command"));
    if (xml.hasError()) return std::nullopt;
    return result;
}
}

std::optional<DeviceConfiguration> XmlConfigLoader::loadModel(
    const QString& filename, quint16 modelId, QString* error) {
    if (error) error->clear();
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("%1: %2").arg(filename, file.errorString());
        return std::nullopt;
    }
    QXmlStreamReader xml(&file);
    std::optional<DeviceConfiguration> selected;
    QSet<quint16> ids;
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("DB"))
        xml.raiseError(QStringLiteral("Expected DB root"));
    else if (xml.attributes().value(QLatin1String("pbsMetadataVersion")) != QLatin1String("1"))
        xml.raiseError(QStringLiteral("DB: pbsMetadataVersion must be 1"));
    else {
        while (xml.readNextStartElement()) {
            if (xml.name() != QLatin1String("Device")) {
                xml.skipCurrentElement();
                continue;
            }
            auto configuration = device(xml);
            if (!configuration) break;
            if (ids.contains(configuration->modelId())) {
                xml.raiseError(QStringLiteral("DB: duplicate Device id"));
                break;
            }
            ids.insert(configuration->modelId());
            if (configuration->modelId() == modelId) selected = std::move(configuration);
        }
    }
    // Consume the document tail as well: no partial success for trailing XML errors.
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError() || file.error() != QFileDevice::NoError) {
        if (error) *error = QStringLiteral("%1:%2:%3: %4").arg(filename).arg(xml.lineNumber())
                               .arg(xml.columnNumber()).arg(xml.hasError() ? xml.errorString() : file.errorString());
        return std::nullopt;
    }
    if (!selected && error)
        *error = QStringLiteral("%1: unknown model ID %2").arg(filename).arg(modelId, 4, 16, QLatin1Char('0'));
    return selected;
}
