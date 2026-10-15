#include "serialdatasource.h"

SerialDataSource::SerialDataSource() = default;

void SerialDataSource::init(const QVariantMap& portSettings) {
    m_port.clear();
    m_baud = 0;
    m_validConfiguration = false;
    m_errorString.clear();

    const auto port = portSettings.value(QStringLiteral("comport"));
    if (port.metaType().id() != QMetaType::QString) {
        m_errorString = QStringLiteral("Invalid comport: expected a nonempty port name string.");
        return;
    }
    const auto portName = port.toString();
    if (portName.trimmed().isEmpty() || portName.contains(QChar(0))) {
        m_errorString = QStringLiteral("Invalid comport: expected a nonblank port name without NUL.");
        return;
    }

    const auto baud = portSettings.value(QStringLiteral("baudrate"));
    const int baudValue = baud.metaType().id() == QMetaType::Int ? baud.toInt() : 0;
    if (baudValue <= 0) {
        m_errorString = QStringLiteral("Invalid baudrate: expected a positive Int value.");
        return;
    }

    m_port = portName;
    m_baud = baudValue;
    m_validConfiguration = true;
}

QIODevice* SerialDataSource::createAndConnect() {
    if (!m_validConfiguration) {
        return nullptr;
    }
    if (m_device) {
        m_errorString = QStringLiteral("The caller still owns the previous serial device; destroy it before creating another.");
        return nullptr;
    }

    m_errorString.clear();
    auto device = createSerialPort();
    if (!device) {
        m_errorString = QStringLiteral("Serial port creation returned no device.");
        return nullptr;
    }
    if (!configureBaudRate(*device, m_baud)) {
        m_errorString = QStringLiteral("Cannot configure baudrate %1 for '%2': %3 (Qt serial error %4).")
        .arg(m_baud).arg(m_port, device->errorString()).arg(int(device->error()));
        return nullptr;
    }
    device->setPortName(m_port);
    if (!device->open(QIODevice::ReadWrite)) {
        m_errorString = QStringLiteral("Cannot open serial port '%1': %2 (Qt serial error %3).")
        .arg(m_port, device->errorString()).arg(int(device->error()));
        return nullptr;
    }

    m_device = device.get();
    return device.release();
}

QString SerialDataSource::errorString() const {
    if (m_device && m_device->error() != QSerialPort::NoError
        && m_device->error() != QSerialPort::TimeoutError) {
        return QStringLiteral("%1 (Qt serial error %2).")
            .arg(m_device->errorString()).arg(int(m_device->error()));
    }
    if (!m_errorString.isEmpty()) return m_errorString;
    if (m_device && !m_device->isOpen()) return QStringLiteral("Serial port is closed.");
    return m_errorString;
}

bool SerialDataSource::requireOpen() {
    if (m_device && m_device->isOpen()) return true;
    m_errorString = QStringLiteral("Serial port is not open.");
    return false;
}

void SerialDataSource::close() {
    if (m_device && m_device->isOpen()) m_device->close();
}

qint64 SerialDataSource::write(const QByteArray& data) {
    if (!requireOpen()) return -1;
    const qint64 count = m_device->write(data);
    if (count < 0) m_errorString = m_device->errorString();
    return count;
}

qint64 SerialDataSource::read(char* data, qint64 maximum) {
    if (!requireOpen()) return -1;
    const qint64 count = m_device->read(data, maximum);
    if (count < 0) m_errorString = m_device->errorString();
    return count;
}

qint64 SerialDataSource::bytesAvailable() const {
    return m_device ? m_device->bytesAvailable() : 0;
}

qint64 SerialDataSource::bytesToWrite() const {
    return m_device ? m_device->bytesToWrite() : 0;
}

bool SerialDataSource::waitForReadyRead(int milliseconds) {
    return requireOpen() && m_device->waitForReadyRead(milliseconds);
}

bool SerialDataSource::waitForBytesWritten(int milliseconds) {
    return requireOpen() && m_device->waitForBytesWritten(milliseconds);
}

bool SerialDataSource::hasError() const {
    return !m_device || !m_device->isOpen()
        || (m_device->error() != QSerialPort::NoError && m_device->error() != QSerialPort::TimeoutError);
}

std::unique_ptr<QSerialPort> SerialDataSource::createSerialPort() {
    return std::make_unique<QSerialPort>();
}

bool SerialDataSource::configureBaudRate(QSerialPort& port, qint32 baud) {
    return port.setBaudRate(baud);
}
