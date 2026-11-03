#pragma once
#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>

struct SerialSettings {
    QString comport;
    qint32 baudrate{115200};
};
Q_DECLARE_METATYPE(SerialSettings)

// All operations are prompt, nonthrowing and performed in this object's thread.
// The source owns its device; no device pointer or ownership escapes this API.
class IDataSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IDataSource() override = default;
    virtual bool open(const SerialSettings& settings) = 0;
    virtual void close() = 0;
    virtual qint64 write(const QByteArray& data) = 0;
    virtual qint64 read(char* data, qint64 maximum) = 0;
    virtual qint64 bytesAvailable() const = 0;
    virtual qint64 bytesToWrite() const = 0;
    virtual bool hasError() const = 0;
    virtual QString errorString() const = 0;
signals:
    void readyRead();
    void bytesWritten(qint64 count);
    void errorOccurred(const QString& error);
};
