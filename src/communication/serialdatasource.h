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

    // Calls on one source must be sequential. Reconfiguration does not change
    // the caller's existing device; new settings apply after that device dies.
    void init(const QVariantMap& portSettings) override;
    QIODevice* createAndConnect() override;
    QString errorString() const;

protected:
    // Overrides must return a new, closed, parentless port in the calling thread.
    // These hooks allow controlled creation/configuration failures in tests.
    virtual std::unique_ptr<QSerialPort> createSerialPort();
    virtual bool configureBaudRate(QSerialPort& port, qint32 baud);
};
