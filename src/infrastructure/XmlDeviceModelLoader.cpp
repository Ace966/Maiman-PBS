#include "XmlDeviceModelLoader.h"

#include <QFile>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

#include <stdexcept>
#include <string>

namespace infrastructure {

namespace {

QString requireElementText(QXmlStreamReader& xml, const QString& elementName)
{
    const QString value = xml.readElementText().trimmed();

    if (value.isEmpty()) {
        throw std::runtime_error(("Empty required element: " + elementName).toStdString());
    }

    return value;
}

domain::DeviceVariant parseVariant(const QString& value)
{
    if (value == "PIC-8") {
        return domain::DeviceVariant::Pic8;
    }

    if (value == "PIC-16") {
        return domain::DeviceVariant::Pic16;
    }

    return domain::DeviceVariant::Unknown;
}

QChar parseSingleChar(const QString& value, const char* fieldName)
{
    if (value.size() != 1) {
        throw std::runtime_error(std::string("Expected single char for ") + fieldName);
    }

    return value.front();
}

void parseDeviceSection(QXmlStreamReader& xml, domain::DeviceModel& model)
{
    while (!xml.atEnd() && !(xml.isEndElement() && xml.name() == "Device")) {
        xml.readNext();

        if (!xml.isStartElement()) {
            continue;
        }

        if (xml.name() == "Variant") {
            model.variant = parseVariant(requireElementText(xml, "Variant"));
        } else if (xml.name() == "ChannelCount") {
            model.channelCount = requireElementText(xml, "ChannelCount").toInt();
        } else if (xml.name() == "SupportsZeroChannel") {
            model.supportsZeroChannel =
                (requireElementText(xml, "SupportsZeroChannel").toLower() == "true");
        }
    }
}

void parseTransportSection(QXmlStreamReader& xml, domain::DeviceModel& model)
{
    while (!xml.atEnd() && !(xml.isEndElement() && xml.name() == "Transport")) {
        xml.readNext();

        if (!xml.isStartElement()) {
            continue;
        }

        if (xml.name() == "Type") {
            model.transport.type = requireElementText(xml, "Type");
        } else if (xml.name() == "BaudRate") {
            model.transport.baudRate = requireElementText(xml, "BaudRate").toInt();
        } else if (xml.name() == "DataBits") {
            model.transport.dataBits = requireElementText(xml, "DataBits").toInt();
        } else if (xml.name() == "StopBits") {
            model.transport.stopBits = requireElementText(xml, "StopBits").toInt();
        } else if (xml.name() == "Parity") {
            model.transport.parity = requireElementText(xml, "Parity");
        } else if (xml.name() == "FlowControl") {
            model.transport.flowControl = requireElementText(xml, "FlowControl");
        } else if (xml.name() == "CommandTerminator") {
            model.transport.commandTerminator = requireElementText(xml, "CommandTerminator");
        }
    }
}

void parseProtocolSection(QXmlStreamReader& xml, domain::DeviceModel& model)
{
    while (!xml.atEnd() && !(xml.isEndElement() && xml.name() == "Protocol")) {
        xml.readNext();

        if (!xml.isStartElement()) {
            continue;
        }

        if (xml.name() == "Mode") {
            model.protocol.mode = requireElementText(xml, "Mode");
        } else if (xml.name() == "SetPrefix") {
            model.protocol.setPrefix =
                parseSingleChar(requireElementText(xml, "SetPrefix"), "SetPrefix");
        } else if (xml.name() == "GetPrefix") {
            model.protocol.getPrefix =
                parseSingleChar(requireElementText(xml, "GetPrefix"), "GetPrefix");
        } else if (xml.name() == "ResponsePrefix") {
            model.protocol.responsePrefix =
                parseSingleChar(requireElementText(xml, "ResponsePrefix"), "ResponsePrefix");
        } else if (xml.name() == "ErrorPrefix") {
            model.protocol.errorPrefix =
                parseSingleChar(requireElementText(xml, "ErrorPrefix"), "ErrorPrefix");
        }
    }
}

void parseGlobalParametersSection(QXmlStreamReader& xml, domain::DeviceModel& model)
{
    while (!xml.atEnd() && !(xml.isEndElement() && xml.name() == "GlobalParameters")) {
        xml.readNext();

        if (!xml.isStartElement()) {
            continue;
        }

        if (xml.name() == "Parameter") {
            const auto attrs = xml.attributes();

            domain::ParameterDefinition parameter;
            parameter.key = attrs.value("key").toString();
            parameter.codeHex = attrs.value("code").toString().toUpper();
            parameter.access = attrs.value("access").toString();

            model.globalParameters.insert(parameter.key, parameter);

            xml.skipCurrentElement();
        }
    }
}

void parseZeroChannelSection(QXmlStreamReader& xml, domain::DeviceModel& model)
{
    while (!xml.atEnd() && !(xml.isEndElement() && xml.name() == "ZeroChannel")) {
        xml.readNext();

        if (!xml.isStartElement()) {
            continue;
        }

        if (xml.name() == "State") {
            const auto attrs = xml.attributes();

            model.zeroChannel.stateCodeHex = attrs.value("code").toString().toUpper();
            model.zeroChannel.access = attrs.value("access").toString();

            xml.skipCurrentElement();
        } else if (xml.name() == "Actions") {
            const auto attrs = xml.attributes();

            model.zeroChannel.actions.saveHex = attrs.value("save").toString().toUpper();
            model.zeroChannel.actions.clearHex = attrs.value("clear").toString().toUpper();
            model.zeroChannel.actions.startHex = attrs.value("start").toString().toUpper();
            model.zeroChannel.actions.stopHex = attrs.value("stop").toString().toUpper();
            model.zeroChannel.actions.selectCCHex = attrs.value("selectCC").toString().toUpper();
            model.zeroChannel.actions.selectCVHex = attrs.value("selectCV").toString().toUpper();

            xml.skipCurrentElement();
        }
    }
}

void parseChannelPatternSection(QXmlStreamReader& xml, domain::DeviceModel& model)
{
    while (!xml.atEnd() && !(xml.isEndElement() && xml.name() == "ChannelPattern")) {
        xml.readNext();

        if (!xml.isStartElement()) {
            continue;
        }

        const auto attrs = xml.attributes();

        if (xml.name() == "StateBase") {
            model.channelPattern.state.stateBaseHex =
                attrs.value("code").toString().toUpper();
            model.channelPattern.state.stepHex =
                attrs.value("step").toString().toUpper();

            xml.skipCurrentElement();
        } else if (xml.name() == "CurrentBases") {
            model.channelPattern.current.valueBaseHex =
                attrs.value("value").toString().toUpper();
            model.channelPattern.current.maximumBaseHex =
                attrs.value("maximum").toString().toUpper();
            model.channelPattern.current.maximumLimitBaseHex =
                attrs.value("maximumLimit").toString().toUpper();
            model.channelPattern.current.measuredBaseHex =
                attrs.value("measured").toString().toUpper();
            model.channelPattern.current.protectionBaseHex =
                attrs.value("protection").toString().toUpper();
            model.channelPattern.current.stepHex =
                attrs.value("step").toString().toUpper();
            model.channelPattern.current.scale =
                attrs.value("scale").toString().toDouble();
            model.channelPattern.current.unit =
                attrs.value("unit").toString();

            xml.skipCurrentElement();
        } else if (xml.name() == "VoltageBases") {
            model.channelPattern.voltage.valueBaseHex =
                attrs.value("value").toString().toUpper();
            model.channelPattern.voltage.maximumBaseHex =
                attrs.value("maximum").toString().toUpper();
            model.channelPattern.voltage.maximumLimitBaseHex =
                attrs.value("maximumLimit").toString().toUpper();
            model.channelPattern.voltage.measuredBaseHex =
                attrs.value("measured").toString().toUpper();
            model.channelPattern.voltage.protectionBaseHex =
                attrs.value("protection").toString().toUpper();
            model.channelPattern.voltage.stepHex =
                attrs.value("step").toString().toUpper();
            model.channelPattern.voltage.scale =
                attrs.value("scale").toString().toDouble();
            model.channelPattern.voltage.unit =
                attrs.value("unit").toString();

            xml.skipCurrentElement();
        } else if (xml.name() == "StateActions") {
            model.channelPattern.stateActions.startHex =
                attrs.value("start").toString().toUpper();
            model.channelPattern.stateActions.stopHex =
                attrs.value("stop").toString().toUpper();
            model.channelPattern.stateActions.selectCCHex =
                attrs.value("selectCC").toString().toUpper();
            model.channelPattern.stateActions.selectCVHex =
                attrs.value("selectCV").toString().toUpper();
            model.channelPattern.stateActions.followZeroHex =
                attrs.value("followZero").toString().toUpper();
            model.channelPattern.stateActions.ignoreZeroHex =
                attrs.value("ignoreZero").toString().toUpper();

            xml.skipCurrentElement();
        }
    }
}

void parseSelectedDeviceProfile(QXmlStreamReader& xml, domain::DeviceModel& model)
{
    while (!xml.atEnd() && !(xml.isEndElement() && xml.name() == "DeviceProfile")) {
        xml.readNext();

        if (!xml.isStartElement()) {
            continue;
        }

        if (xml.name() == "Device") {
            parseDeviceSection(xml, model);
        } else if (xml.name() == "Transport") {
            parseTransportSection(xml, model);
        } else if (xml.name() == "Protocol") {
            parseProtocolSection(xml, model);
        } else if (xml.name() == "GlobalParameters") {
            parseGlobalParametersSection(xml, model);
        } else if (xml.name() == "ZeroChannel") {
            parseZeroChannelSection(xml, model);
        } else if (xml.name() == "ChannelPattern") {
            parseChannelPatternSection(xml, model);
        }
    }
}

} // namespace

domain::DeviceModel XmlDeviceModelLoader::loadFromFile(const QString& filePath,
                                                       const QString& modelId) const
{
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(("Cannot open XML file: " + filePath).toStdString());
    }

    QXmlStreamReader xml(&file);
    domain::DeviceModel model;
    bool profileFound = false;

    while (!xml.atEnd()) {
        xml.readNext();

        if (!xml.isStartElement()) {
            continue;
        }

        if (xml.name() != "DeviceProfile") {
            continue;
        }

        const auto attrs = xml.attributes();
        const QString currentId = attrs.value("id").toString();

        if (currentId != modelId) {
            xml.skipCurrentElement();
            continue;
        }

        profileFound = true;
        model.id = currentId;
        model.name = attrs.value("name").toString();

        parseSelectedDeviceProfile(xml, model);

        break;
    }

    if (xml.hasError()) {
        throw std::runtime_error(("XML parse error: " + xml.errorString()).toStdString());
    }

    if (!profileFound) {
        throw std::runtime_error(("Device profile not found: " + modelId).toStdString());
    }

    return model;
}

} // namespace infrastructure