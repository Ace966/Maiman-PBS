#pragma once
#include "IDataSource.h"
#include <QSerialPort>
#include <memory>

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
    bool hasError() const override;
    QString errorString() const override;
protected:
    virtual std::unique_ptr<QSerialPort> createSerialPort();
    virtual bool configureBaudRate(QSerialPort& port, qint32 baud);
private:
    QSerialPort* m_device{nullptr}; // Owned QObject child after successful open.
    QString m_error;
    bool m_failed{false};
};
