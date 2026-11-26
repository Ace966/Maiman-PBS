#include "DeviceService.h"

DeviceService::DeviceService(CommunicationConnection& connection, DeviceRepository& repository, QObject* parent)
    : QObject(parent), m_connection(connection), m_repository(repository),
      m_state(connection.isConnected() ? State::Connected : State::Disconnected) {
    Q_ASSERT(thread() == connection.thread());
    Q_ASSERT(thread() == repository.thread());
    bindCallbacks();
}

void DeviceService::bindCallbacks() {
    // Retire this service's old in-flight deliveries before a new session can become Connected.
    QObject::disconnect(&m_connection, nullptr, this, nullptr);
    connect(&m_connection, &CommunicationConnection::connectionChanged,
        this, &DeviceService::onConnectionChanged, Qt::DirectConnection);
    m_results = connect(&m_connection, &CommunicationConnection::requestFinished,
        this, &DeviceService::onRequestFinished, Qt::DirectConnection);
}

void DeviceService::setState(State state, const QString& error) {
    m_error = error;
    if (state == m_state) return;
    m_state = state;
    emit stateChanged(state);
}

bool DeviceService::connectDevice(const SerialSettings& settings) {
    if (!m_connection.connectPort(settings)) return false;
    bindCallbacks();
    setState(State::Connecting);
    return true;
}

bool DeviceService::disconnectDevice() {
    if (!m_connection.disconnectPort()) return false;
    QObject::disconnect(m_results);
    setState(State::Disconnected);
    return true;
}

void DeviceService::onConnectionChanged(bool connected, const QString& error) {
    if (m_state == State::Disconnected) return;
    if (connected) {
        if (m_state == State::Connecting) setState(State::Connected);
        return;
    }
    if (m_state == State::Connecting || !error.isEmpty()) {
        QObject::disconnect(m_results);
        setState(State::Error, error);
    } else if (m_state != State::Error) {
        QObject::disconnect(m_results);
        setState(State::Disconnected);
    }
}

void DeviceService::onRequestFinished(const CommunicationWorker::Result& result) {
    if (m_state != State::Connected) return;
    using Outcome = CommunicationWorker::Outcome;
    if (result.outcome == Outcome::Timeout || result.outcome == Outcome::TransportError) {
        QObject::disconnect(m_results);
        setState(State::Error, result.error);
        return;
    }
    if (result.kind == CommunicationWorker::Kind::Read && result.outcome == Outcome::ReadResponse
        && m_connection.isConnected()) {
        m_repository.update(result.parameter, result.value);
    }
}
