#include "XmlParser.h"

#include <QXmlStreamReader>
#include <utility>

namespace {
TreeItem readElement(QXmlStreamReader& xml) {
    const QString name = xml.name().toString();
    QString text;
    std::vector<TreeItem> children;
    for (const auto& attribute : xml.attributes())
        children.emplace_back(attribute.name().toString(), attribute.value().toString());

    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement)
            children.push_back(readElement(xml));
        else if (token == QXmlStreamReader::Characters)
            text += xml.text();
        else if (token == QXmlStreamReader::EndElement)
            break;
    }
    return TreeItem(name, std::move(text), std::move(children));
}
}

std::optional<TreeItem> XmlParser::parse(const QByteArray& data, QString* error) {
    if (error) error->clear();
    QXmlStreamReader xml(data);
    std::optional<TreeItem> root;
    if (xml.readNextStartElement()) root = readElement(xml);
    // A valid root does not make a malformed document tail acceptable.
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError()) {
        if (error) *error = QStringLiteral("XML at line %1, column %2: %3")
            .arg(xml.lineNumber()).arg(xml.columnNumber()).arg(xml.errorString());
        return std::nullopt;
    }
    return root;
}
