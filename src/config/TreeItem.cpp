#include "TreeItem.h"

#include <utility>

TreeItem::TreeItem(QString name, QString value, std::vector<TreeItem> children)
    : m_name(std::move(name)), m_value(std::move(value)), m_children(std::move(children)) {}
