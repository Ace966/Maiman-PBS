#include "DeviceService.h"
#include <QSet>

DeviceService::DeviceService(CommunicationConnection& connection, DeviceRepository& repository, QObject* parent)
    : QObject(parent), m_connection(connection), m_repository(repository),
      m_state(connection.isConnected() ? State::Connected : State::Disconnected), m_pollTimer(this) {
    Q_ASSERT(thread() == connection.thread());
    Q_ASSERT(thread() == repository.thread());
    bindCallbacks();
    m_pollTimer.setSingleShot(true);
    m_pollTimer.setTimerType(Qt::PreciseTimer);
    m_pollTimer.setInterval(50); // Pace polling and waits for manual traffic.
    connect(&m_pollTimer, &QTimer::timeout, this, &DeviceService::pollNext);
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
    if (state != State::Connected) {
        m_pollTimer.stop();
        m_pollPending.reset();
        m_pollIndex = 0;
    }
    if (state == m_state) return;
    m_state = state;
    armPolling();
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

bool DeviceService::setPollingList(const QList<PollEntry>& entries) {
    QSet<quint16> seen;
    QList<quint16> high;
    QList<quint16> low;
    for (const auto& entry : entries) {
        if (!m_repository.configuration().parameters().contains(entry.code) || seen.contains(entry.code)
            || (entry.priority != PollPriority::High && entry.priority != PollPriority::Low)) return false;
        seen.insert(entry.code);
        if (entry.priority == PollPriority::High) high.append(entry.code);
        else low.append(entry.code);
    }
    // Two complete High passes, then one Low pass: 2:1 per register regardless of group size.
    m_pollOrder = high + high + low;
    m_pollIndex = 0;
    m_pollTimer.stop();
    armPolling();
    return true;
}

void DeviceService::armPolling() {
    if (m_state == State::Connected && !m_pollOrder.isEmpty() && !m_pollPending) m_pollTimer.start();
}

void DeviceService::pollNext() {
    if (m_state != State::Connected || m_pollOrder.isEmpty() || m_pollPending) return;
    // Results have no request IDs. Start in a drained queue so this is the next Read terminal;
    // later manual Reads remain behind it in the existing FIFO. Writes keep their own priority.
    if (m_connection.outstanding() != 0) {
        armPolling();
        return;
    }
    const auto code = m_pollOrder.at(m_pollIndex);
    if (!m_connection.read(code)) return; // With no outstanding work, rejection means a closed connection.
    m_pollPending = code;
    m_pollIndex = (m_pollIndex + 1) % m_pollOrder.size();
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
    if (result.kind == CommunicationWorker::Kind::Read && m_pollPending == result.parameter) {
        m_pollPending.reset();
        armPolling(); // Before update(): repository notifications may reenter or destroy this service.
    }
    if (result.kind == CommunicationWorker::Kind::Read && result.outcome == Outcome::ReadResponse
        && m_connection.isConnected()) {
        m_repository.update(result.parameter, result.value);
    }
}
