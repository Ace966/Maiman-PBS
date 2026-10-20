#include "YamlConfigLoader.h"

#include <QFile>
#include <QSet>
#include <yaml-cpp/yaml.h>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void mapping(const YAML::Node& node) {
    require(node.IsMap(), "expected a mapping");
    QSet<QString> keys;
    for (const auto& entry : node) {
        require(entry.first.IsScalar(), "mapping keys must be strings");
        const auto key = QString::fromStdString(entry.first.as<std::string>());
        require(!keys.contains(key), "duplicate mapping key");
        keys.insert(key);
    }
}

bool stringTag(const YAML::Node& node) {
    return node.Tag() == "!" || node.Tag() == "tag:yaml.org,2002:str";
}

QString text(const YAML::Node& node) {
    require(node.IsScalar(), "expected a string");
    if (!stringTag(node)) {
        require(node.Tag() == "?", "expected a string");
        bool boolean; double number;
        require(!YAML::convert<bool>::decode(node, boolean)
                    && !YAML::convert<double>::decode(node, number),
                "expected a string; quote numeric or boolean text");
    }
    return QString::fromStdString(node.as<std::string>());
}

void numeric(const YAML::Node& node) {
    require(node.IsScalar() && !stringTag(node), "expected a number");
}

quint16 hexadecimalId(const YAML::Node& node) {
    require(node.IsScalar() && stringTag(node), "expected a quoted four-digit uppercase hex ID");
    const auto value = text(node);
    require(value.size() == 4, "expected a four-digit uppercase hex ID");
    for (const auto digit : value)
        require((digit >= QLatin1Char('0') && digit <= QLatin1Char('9'))
                    || (digit >= QLatin1Char('A') && digit <= QLatin1Char('F')),
                "expected a four-digit uppercase hex ID");
    return value.toUShort(nullptr, 16);
}

YAML::Node readRoot(const QString& filename, QString& field) {
    field = QStringLiteral("file");
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(file.errorString().toStdString());
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        throw std::runtime_error(file.errorString().toStdString());
    field = QStringLiteral("YAML");
    const auto documents = YAML::LoadAll(bytes.toStdString());
    require(documents.size() == 1, "expected exactly one YAML document");
    const auto root = documents.front();
    mapping(root);
    return root;
}

PICModel picModel(const YAML::Node& node) {
    const auto model = text(node);
    require(model == QStringLiteral("PIC-8") || model == QStringLiteral("PIC-16"),
            "expected PIC-8 or PIC-16");
    return model == QStringLiteral("PIC-8") ? PICModel::PIC8 : PICModel::PIC16;
}

void appendParameters(const YAML::Node& parameters, DeviceConfiguration& configuration,
                      QString& field, const QString& listName) {
    field = listName;
    require(parameters.IsSequence(), "expected a parameter list");
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const auto parameter = parameters[i];
        const auto location = listName + QStringLiteral("[%1]").arg(i);
        field = location;
        mapping(parameter);
        field = location + QStringLiteral(".id");
        const auto id = hexadecimalId(parameter["id"]);
        const auto idText = text(parameter["id"]);
        ParameterDefinition definition;
        field = location + QStringLiteral(".name");
        definition.name = text(parameter["name"]);
        require(!definition.name.isEmpty(), "name must not be empty");
        if (parameter["channel"]) {
            field = location + QStringLiteral(".channel");
            numeric(parameter["channel"]);
            const auto channel = parameter["channel"].as<int>();
            require(channel >= 0 && channel <= std::numeric_limits<quint8>::max(),
                    "channel cannot be represented as an unsigned byte");
            definition.channel = quint8(channel);
        }
        if (parameter["unit"]) {
            field = location + QStringLiteral(".unit");
            definition.unit = text(parameter["unit"]);
        }
        if (parameter["scale"]) {
            field = location + QStringLiteral(".scale");
            numeric(parameter["scale"]);
            definition.scale = parameter["scale"].as<double>();
            require(std::isfinite(definition.scale), "scale must be finite");
        }
        field = location + QStringLiteral(".access");
        const auto access = text(parameter["access"]);
        if (access == QStringLiteral("r")) definition.access = ParameterAccess::ReadOnly;
        else if (access == QStringLiteral("w")) definition.access = ParameterAccess::WriteOnly;
        else if (access == QStringLiteral("rw")) definition.access = ParameterAccess::ReadWrite;
        else throw std::runtime_error("expected r, w or rw");
        field = location + QStringLiteral(" (ID %1)").arg(idText);
        require(configuration.addParameter(id, definition), "duplicate ID or channel outside the selected model");
    }
}
}

std::optional<DeviceConfiguration> YamlConfigLoader::loadFile(const QString& filename, QString* error) {
    if (error) error->clear();
    QString field;
    try {
        const auto root = readRoot(filename, field);
        field = QStringLiteral("device");
        const auto device = root["device"];
        mapping(device);
        field = QStringLiteral("device.model");
        DeviceConfiguration configuration(picModel(device["model"]));
        appendParameters(root["parameters"], configuration, field, QStringLiteral("parameters"));
        return configuration;
    } catch (const std::exception& exception) {
        if (error) *error = field + QStringLiteral(": ") + QString::fromUtf8(exception.what());
        return std::nullopt;
    }
}

std::optional<DeviceConfiguration> YamlConfigLoader::loadModel(
    const QString& filename, quint16 modelId, QString* error) {
    if (error) error->clear();
    QString field;
    try {
        const auto root = readRoot(filename, field);
        field = QStringLiteral("models");
        const auto models = root["models"];
        mapping(models);
        for (const auto& entry : models) hexadecimalId(entry.first);
        const auto key = QStringLiteral("%1").arg(modelId, 4, 16, QLatin1Char('0')).toUpper();
        const auto location = QStringLiteral("models[%1]").arg(key);
        field = location;
        const auto selected = models[key.toStdString()];
        require(selected.IsDefined(), "unsupported model ID");
        mapping(selected);
        field = location + QStringLiteral(".model");
        DeviceConfiguration configuration(picModel(selected["model"]));
        appendParameters(root["common"], configuration, field, QStringLiteral("common"));
        appendParameters(selected["parameters"], configuration, field, location + QStringLiteral(".parameters"));
        return configuration;
    } catch (const std::exception& exception) {
        if (error) *error = field + QStringLiteral(": ") + QString::fromUtf8(exception.what());
        return std::nullopt;
    }
}
