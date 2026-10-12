#pragma once

#include <QIODevice>
#include <QVariant>

class IDataSource
{
public:
    IDataSource() {}
    virtual ~IDataSource() {}
    virtual void init(const QVariantMap& portSettings) = 0;

    // On success, the caller owns a parentless device created in the calling
    // thread and must destroy it in that thread. Failure returns nullptr.
    virtual QIODevice* createAndConnect() = 0;
};
