#pragma once

#include <QString>
#include <vector>

// XML attributes are leaf children, before the element's child elements.
class TreeItem {
public:
    explicit TreeItem(QString name, QString value = {}, std::vector<TreeItem> children = {});

    const QString& name() const { return m_name; }
    const QString& value() const { return m_value; }
    const std::vector<TreeItem>& children() const { return m_children; }

private:
    QString m_name;
    QString m_value;
    std::vector<TreeItem> m_children;
};
