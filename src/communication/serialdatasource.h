#pragma once
#include "IDataSource.h"
#include <QSerialPort>
#include <QScopedPointer>

class SerialDataSource : public IDataSource {
public:
    using IDataSource::IDataSource;
    ~SerialDataSource() override;
    // Settings are typed and validated by CommunicationConnection.
    bool open(const SerialSettings& settings) override;
    void close() override;
    qint64 write(const QByteArray& data) override;
    qint64 read(char* data, qint64 maximum) override;
    qint64 bytesAvailable() const override;
    qint64 bytesToWrite() const override;
    bool waitForReadyRead(int timeoutMs) override;
    bool waitForBytesWritten(int timeoutMs) override;
    bool hasError() const override;
    QString errorString() const override;
protected:
    virtual QSerialPort* createSerialPort(); // New parentless port, adopted immediately by m_device.
    virtual bool configureBaudRate(QSerialPort& port, qint32 baud);
private:
    QScopedPointer<QSerialPort> m_device;
    QString m_error;
    bool m_failed{false};
};
