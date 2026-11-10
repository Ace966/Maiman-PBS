#include "serialdatasource.h"

SerialDataSource::~SerialDataSource() { close(); }

bool SerialDataSource::open(const SerialSettings& settings) {
    if (m_device) {
        m_error = QStringLiteral("Serial port is already open.");
        return false;
    }
    m_error.clear();
    m_failed = false;
    m_device.reset(createSerialPort());
    if (!m_device) {
        m_error = QStringLiteral("Serial port factory returned no device.");
        return false;
    }
    m_device->setPortName(settings.comport);
    const bool configured = configureBaudRate(*m_device, settings.baudrate)
        && m_device->setDataBits(QSerialPort::Data8) && m_device->setParity(QSerialPort::NoParity)
        && m_device->setStopBits(QSerialPort::OneStop) && m_device->setFlowControl(QSerialPort::NoFlowControl);
    if (!configured || !m_device->open(QIODevice::ReadWrite)) {
        m_error = QStringLiteral("Cannot %1 '%2': %3 (Qt serial error %4).")
            .arg(configured ? QStringLiteral("open") : QStringLiteral("configure"), settings.comport,
                 m_device->errorString()).arg(int(m_device->error()));
        m_device.reset();
        return false;
    }
    connect(m_device.data(), &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || error == QSerialPort::TimeoutError) return;
        m_error = QStringLiteral("%1 (Qt serial error %2).")
            .arg(m_device->errorString()).arg(int(error));
        m_failed = true;
    });
    return true;
}

void SerialDataSource::close() {
    if (!m_device) return;
    disconnect(m_device.data(), nullptr, this, nullptr);
    if (m_device->isOpen()) m_device->close();
    m_device.reset();
}
qint64 SerialDataSource::write(const QByteArray& data) {
    if (!m_device || !m_device->isOpen()) { m_error = QStringLiteral("Serial port is not open."); return -1; }
    const auto count = m_device->write(data);
    if (count < 0) { m_error = m_device->errorString(); m_failed = true; }
    return count;
}
qint64 SerialDataSource::read(char* data, qint64 maximum) {
    if (!m_device || !m_device->isOpen()) { m_error = QStringLiteral("Serial port is not open."); return -1; }
    const auto count = m_device->read(data, maximum);
    if (count < 0) { m_error = m_device->errorString(); m_failed = true; }
    return count;
}
qint64 SerialDataSource::bytesAvailable() const { return m_device ? m_device->bytesAvailable() : 0; }
qint64 SerialDataSource::bytesToWrite() const { return m_device ? m_device->bytesToWrite() : 0; }
bool SerialDataSource::waitForReadyRead(int timeoutMs) {
    if (!m_device || !m_device->isOpen()) { m_error = QStringLiteral("Serial port is not open."); return false; }
    return m_device->waitForReadyRead(timeoutMs);
}
bool SerialDataSource::waitForBytesWritten(int timeoutMs) {
    if (!m_device || !m_device->isOpen()) { m_error = QStringLiteral("Serial port is not open."); return false; }
    return m_device->waitForBytesWritten(timeoutMs);
}
bool SerialDataSource::hasError() const {
    return !m_device || !m_device->isOpen() || m_failed
        || (m_device->error() != QSerialPort::NoError && m_device->error() != QSerialPort::TimeoutError);
}
QString SerialDataSource::errorString() const { return m_error; }
QSerialPort* SerialDataSource::createSerialPort() { return new QSerialPort; }
bool SerialDataSource::configureBaudRate(QSerialPort& port, qint32 baud) { return port.setBaudRate(baud); }
