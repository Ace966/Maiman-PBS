#include "serialdatasource.h"
#include <QThread>

SerialDataSource::~SerialDataSource() { close(); }

bool SerialDataSource::open(const SerialSettings& settings) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_device) { m_error = QStringLiteral("Serial port is already open."); return false; }
    m_error.clear(); m_failed = false;
    auto port = createSerialPort();
    if (!port) { m_error = QStringLiteral("Serial port factory returned no device."); return false; }
    port->setPortName(settings.comport);
    if (!configureBaudRate(*port, settings.baudrate)
        || !port->setDataBits(QSerialPort::Data8) || !port->setParity(QSerialPort::NoParity)
        || !port->setStopBits(QSerialPort::OneStop) || !port->setFlowControl(QSerialPort::NoFlowControl)) {
        m_error = QStringLiteral("Cannot configure '%1': %2 (Qt serial error %3).")
            .arg(settings.comport, port->errorString()).arg(int(port->error()));
        return false;
    }
    if (!port->open(QIODevice::ReadWrite)) {
        m_error = QStringLiteral("Cannot open '%1': %2 (Qt serial error %3).")
            .arg(settings.comport, port->errorString()).arg(int(port->error()));
        return false;
    }
    m_device = port.release();
    m_device->setParent(this); // Local RAII ownership transfers to Qt ownership.
    connect(m_device, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError || error == QSerialPort::TimeoutError) return;
        m_error = QStringLiteral("%1 (Qt serial error %2).")
            .arg(m_device->errorString()).arg(int(error));
        m_failed = true;
    });
    return true;
}

void SerialDataSource::close() {
    Q_ASSERT(thread() == QThread::currentThread());
    auto* port = m_device;
    m_device = nullptr;
    if (!port) return;
    disconnect(port, nullptr, this, nullptr);
    if (port->isOpen()) port->close();
    delete port;
}
qint64 SerialDataSource::write(const QByteArray& data) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_device || !m_device->isOpen()) { m_error = QStringLiteral("Serial port is not open."); return -1; }
    const auto count = m_device->write(data);
    if (count < 0) { m_error = m_device->errorString(); m_failed = true; }
    return count;
}
qint64 SerialDataSource::read(char* data, qint64 maximum) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (!m_device || !m_device->isOpen()) { m_error = QStringLiteral("Serial port is not open."); return -1; }
    const auto count = m_device->read(data, maximum);
    if (count < 0) { m_error = m_device->errorString(); m_failed = true; }
    return count;
}
qint64 SerialDataSource::bytesAvailable() const { return m_device ? m_device->bytesAvailable() : 0; }
qint64 SerialDataSource::bytesToWrite() const { return m_device ? m_device->bytesToWrite() : 0; }
bool SerialDataSource::waitForReadyRead(int timeoutMs) {
    Q_ASSERT(thread() == QThread::currentThread());
    Q_ASSERT(timeoutMs > 0);
    if (!m_device || !m_device->isOpen()) { m_error = QStringLiteral("Serial port is not open."); return false; }
    return m_device->waitForReadyRead(timeoutMs);
}
bool SerialDataSource::waitForBytesWritten(int timeoutMs) {
    Q_ASSERT(thread() == QThread::currentThread());
    Q_ASSERT(timeoutMs > 0);
    if (!m_device || !m_device->isOpen()) { m_error = QStringLiteral("Serial port is not open."); return false; }
    return m_device->waitForBytesWritten(timeoutMs);
}
bool SerialDataSource::hasError() const {
    return !m_device || !m_device->isOpen() || m_failed
        || (m_device->error() != QSerialPort::NoError && m_device->error() != QSerialPort::TimeoutError);
}
QString SerialDataSource::errorString() const { return m_error; }
std::unique_ptr<QSerialPort> SerialDataSource::createSerialPort() { return std::make_unique<QSerialPort>(); }
bool SerialDataSource::configureBaudRate(QSerialPort& port, qint32 baud) { return port.setBaudRate(baud); }
