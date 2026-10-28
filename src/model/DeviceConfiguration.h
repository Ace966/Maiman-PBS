#pragma once

#include <QMap>
#include <QString>
#include <QtGlobal>
#include <optional>

enum class ParameterAccess { ReadOnly, WriteOnly, ReadWrite };

struct ParameterDefinition {
    QString name;
    std::optional<quint8> channel;
    QString unit;
    double scale{1.0}; // Physical value = raw * scale; metadata only.
    ParameterAccess access{ParameterAccess::ReadOnly};
    quint8 tolerance{0}; // decimal display precision, not accuracy.
};

// Definitions only; register IDs are unique map keys
class DeviceConfiguration {
public:
    // The loader supplies a valid identity, name and nonnegative physical count.
    DeviceConfiguration(quint16 modelId, const QString& modelName, int channelCount)
        : m_modelId(modelId), m_modelName(modelName), m_channelCount(channelCount) {}
    quint16 modelId() const { return m_modelId; }
    const QString& modelName() const { return m_modelName; }
    int channelCount() const { return m_channelCount; }
    const QMap<quint16, ParameterDefinition>& parameters() const { return m_parameters; }

    bool addParameter(quint16 id, const ParameterDefinition& definition) {
        if (m_parameters.contains(id)
            || (definition.channel && *definition.channel > channelCount())) return false;
        m_parameters.insert(id, definition);
        return true;
    }

private:
    quint16 m_modelId;
    QString m_modelName;
    int m_channelCount;
    QMap<quint16, ParameterDefinition> m_parameters;
};
