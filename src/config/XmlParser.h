#pragma once

#include "TreeItem.h"

#include <QByteArray>
#include <optional>

class XmlParser {
public:
    static std::optional<TreeItem> parse(const QByteArray& xml, QString* error = nullptr);
};
