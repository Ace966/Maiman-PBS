#pragma once

#include <QMap>
#include <QString>
#include <QtGlobal>
#include <optional>


struct ParameterDefinition {
    QString unit;
    double scale{1.0}; // Physical value = raw * scale; metadata only.
    quint8 tolerance{0}; // decimal display precision, not accuracy.
};

// Definitions only; register IDs are unique map keys, not duplicated in metadata.
class DeviceConfiguration {
public:
    // The loader supplies a valid identity and model name.
    DeviceConfiguration(quint16 modelId, const QString& modelName)
        : m_modelId(modelId), m_modelName(modelName) {}
    quint16 modelId() const { return m_modelId; }
    const QString& modelName() const { return m_modelName; }
    const QMap<quint16, ParameterDefinition>& parameters() const { return m_parameters; }

    bool addParameter(quint16 id, const ParameterDefinition& definition) {
        if (m_parameters.contains(id)) return false;
        m_parameters.insert(id, definition);
        return true;
    }

private:
    quint16 m_modelId;
    QString m_modelName;
    QMap<quint16, ParameterDefinition> m_parameters;
};
