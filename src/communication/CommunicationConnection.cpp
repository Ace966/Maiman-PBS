#include "CommunicationConnection.h"
#include "serialdatasource.h"
#include <QCoreApplication>
#include <QEvent>
#include <utility>

CommunicationConnection::CommunicationConnection(CommunicationWorker::SourceFactory factory, QObject* parent)
    : QObject(parent),
      m_worker(new CommunicationWorker(factory ? std::move(factory) : [] { return new SerialDataSource; }, this)) {
    qRegisterMetaType<CommunicationWorker::Result>();
    connect(m_worker, &CommunicationWorker::connectionChanged,
        this, &CommunicationConnection::onConnectionChanged, Qt::QueuedConnection);
    connect(m_worker, &CommunicationWorker::requestFinished,
        this, &CommunicationConnection::onRequestFinished, Qt::QueuedConnection);
    m_worker->start();
}

CommunicationConnection::~CommunicationConnection() { shutdown(); }
void CommunicationConnection::onConnectionChanged(bool connected, const QString& error) {
    if (m_shutdown) return;
    m_connected = connected;
    m_controlPending = false;
    emit connectionChanged(connected, error);
}

void CommunicationConnection::onRequestFinished(const CommunicationWorker::Result& result) {
    (result.kind == CommunicationWorker::Kind::Read ? m_reads : m_writes)--;
    // Old transaction results precede a pending control's acknowledgement in the Worker stream.
    const bool disconnected = !m_shutdown && !m_controlPending && m_connected
        && (result.outcome == CommunicationWorker::Outcome::Timeout
            || result.outcome == CommunicationWorker::Outcome::TransportError);
    if (disconnected) m_connected = false;
    emit requestFinished(result);
    // A result callback may start a new control or shut down before this notification.
    if (disconnected && !m_shutdown && !m_controlPending && !m_connected)
        emit connectionChanged(false, result.error);
}

bool CommunicationConnection::connectPort(const SerialSettings& settings) {
    m_error.clear();
    if (m_shutdown || m_controlPending) {
        m_error = QStringLiteral("Connection change unavailable or pending.");
        return false;
    }
    if (settings.comport.trimmed().isEmpty() || settings.comport.contains(QChar(0)) || settings.baudrate <= 0) {
        m_error = QStringLiteral("Expected explicit nonblank comport without NUL and positive baudrate.");
        return false;
    }
    m_connected = false;
    m_controlPending = true;
    m_worker->open(settings);
    return true;
}

bool CommunicationConnection::disconnectPort() {
    m_error.clear();
    if (m_shutdown || m_controlPending) {
        m_error = QStringLiteral("Connection change unavailable or pending.");
        return false;
    }
    m_connected = false;
    m_controlPending = true;
    m_worker->disconnectPort();
    return true;
}

bool CommunicationConnection::setInterCommandDelayMs(int delayMs) {
    m_error.clear();
    if (delayMs < 0 || m_shutdown || m_connected || m_controlPending) {
        m_error = QStringLiteral("Delay must be nonnegative and configured while disconnected with no pending connection change.");
        return false;
    }
    m_worker->setInterCommandDelayMs(delayMs);
    return true;
}

bool CommunicationConnection::read(quint16 parameter, int timeoutMs) {
    return submit(CommunicationWorker::Kind::Read, parameter, 0, timeoutMs);
}
bool CommunicationConnection::write(quint16 parameter, quint16 value, int timeoutMs) {
    return submit(CommunicationWorker::Kind::Write, parameter, value, timeoutMs);
}
bool CommunicationConnection::submit(CommunicationWorker::Kind kind, quint16 parameter, quint16 value, int timeoutMs) {
    m_error.clear();
    auto& count = kind == CommunicationWorker::Kind::Read ? m_reads : m_writes;
    if (m_shutdown || !m_connected || timeoutMs <= 0 || count == CommunicationWorker::QueueCapacity) {
        m_error = QStringLiteral("Not connected, nonpositive timeout or capacity reached.");
        return false;
    }
    ++count;
    m_worker->enqueue(CommunicationWorker::Request{kind, parameter, value, timeoutMs});
    return true;
}

void CommunicationConnection::shutdown() {
    if (m_shutdown) return;
    m_shutdown = true;
    m_connected = false;
    m_worker->shutdown();
    m_worker->wait(); // Bounded transport waits return to run() to observe shutdown.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall); // Deliver every admitted terminal result before destruction.
    Q_ASSERT(m_reads == 0 && m_writes == 0);
}
