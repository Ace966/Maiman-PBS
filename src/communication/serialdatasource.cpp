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
    return m_errorString;
}

std::unique_ptr<QSerialPort> SerialDataSource::createSerialPort() {
    return std::make_unique<QSerialPort>();
}

bool SerialDataSource::configureBaudRate(QSerialPort& port, qint32 baud) {
    return port.setBaudRate(baud);
}
