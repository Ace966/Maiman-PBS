#pragma once

#include <QPointer>
#include <QSerialPort>
#include <memory>

#include "IDataSource.h"

class SerialDataSource : public IDataSource {
    QPointer<QSerialPort> m_device;
    QString m_port{};
    int m_baud{0};
    bool m_validConfiguration{false};
    QString m_errorString{QStringLiteral("Serial port settings have not been initialized.")};

public:
    SerialDataSource();
    ~SerialDataSource() override = default;
    SerialDataSource(const SerialDataSource&) = delete;
    SerialDataSource& operator=(const SerialDataSource&) = delete;

    // comport is an explicit, nonblank QString identifier without embedded NUL;
    // it is passed verbatim. The caller supplies canonical naming/formatting.
    // baudrate is a positive QVariant Int (qint32); other types are not coerced.
    // Calls on one source must be sequential. Reconfiguration does not change
    // the caller's existing device; new settings apply after that device dies.
    void init(const QVariantMap& portSettings) override;
    QIODevice* createAndConnect() override;
    void close() override;
    qint64 write(const QByteArray& data) override;
    qint64 read(char* data, qint64 maximum) override;
    qint64 bytesAvailable() const override;
    qint64 bytesToWrite() const override;
    bool waitForReadyRead(int milliseconds) override;
    bool waitForBytesWritten(int milliseconds) override;
    bool hasError() const override;
    QString errorString() const override;

protected:
    // Overrides must return a new, closed, parentless port in the calling thread.
    // These hooks allow controlled creation/configuration failures in tests.
    virtual std::unique_ptr<QSerialPort> createSerialPort();
    virtual bool configureBaudRate(QSerialPort& port, qint32 baud);

private:
    bool requireOpen();
};
