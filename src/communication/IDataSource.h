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

    // Operations below are nonthrowing and run sequentially in the device's
    // creation thread. Reads/writes/inspection/close must return promptly. Waits must
    // return within the supplied nonnegative duration. Reads/writes follow
    // QIODevice count semantics; polling timeout alone is not a backend error.
    virtual void close() = 0;
    virtual qint64 write(const QByteArray& data) = 0;
    virtual qint64 read(char* data, qint64 maximum) = 0;
    virtual qint64 bytesAvailable() const = 0;
    virtual qint64 bytesToWrite() const = 0;
    virtual bool waitForReadyRead(int milliseconds) = 0;
    virtual bool waitForBytesWritten(int milliseconds) = 0;
    virtual bool hasError() const = 0;
    virtual QString errorString() const = 0;
};
