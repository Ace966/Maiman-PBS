#pragma once

#include <QMap>
#include <QString>
#include <QtGlobal>
#include <optional>

enum class PICModel { PIC8, PIC16 };
enum class ParameterAccess { ReadOnly, WriteOnly, ReadWrite };

struct ParameterDefinition {
    QString name;
    std::optional<quint8> channel; // Absent: device-wide; 0: documented channel zero.
    QString unit;
    double scale{1.0}; // Physical value = raw * scale; metadata only.
    ParameterAccess access{ParameterAccess::ReadOnly};
};

// Definitions only; register IDs are unique map keys, not duplicated in metadata.
class DeviceConfiguration {
public:
    explicit DeviceConfiguration(PICModel model) : m_model(model) {}
    PICModel model() const { return m_model; }
    int channelCount() const { return m_model == PICModel::PIC8 ? 8 : 16; }
    const QMap<quint16, ParameterDefinition>& parameters() const { return m_parameters; }

    bool addParameter(quint16 id, const ParameterDefinition& definition) {
        if (m_parameters.contains(id)
            || (definition.channel && *definition.channel > channelCount())) return false;
        m_parameters.insert(id, definition);
        return true;
    }

private:
    PICModel m_model;
    QMap<quint16, ParameterDefinition> m_parameters;
};
