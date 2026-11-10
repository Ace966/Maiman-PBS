#include "CommunicationConnection.h"
#include "serialdatasource.h"
#include <QCoreApplication>
#include <QEvent>
#include <limits>
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
void CommunicationConnection::onConnectionChanged(bool connected, const QString& error, quint64 epoch) {
    if (m_shutdown || epoch != m_epoch) return;
    m_connected = connected;
    m_controlPending = false;
    emit connectionChanged(connected, error);
}

void CommunicationConnection::onRequestFinished(const CommunicationWorker::Result& result) {
    m_requests.remove(result.id);
    emit requestFinished(result);
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
    if (m_epoch == std::numeric_limits<quint64>::max()) {
        m_error = QStringLiteral("Session IDs exhausted.");
        return false;
    }
    m_connected = false;
    m_controlPending = true;
    m_worker->open(settings, ++m_epoch);
    return true;
}

bool CommunicationConnection::disconnectPort() {
    m_error.clear();
    if (m_shutdown || m_controlPending || m_epoch == std::numeric_limits<quint64>::max()) {
        m_error = QStringLiteral("Connection change unavailable or pending.");
        return false;
    }
    m_connected = false;
    m_controlPending = true;
    m_worker->disconnectPort(++m_epoch);
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

quint64 CommunicationConnection::read(quint16 parameter, int timeoutMs) {
    return submit(CommunicationWorker::Kind::Read, parameter, 0, timeoutMs);
}
quint64 CommunicationConnection::write(quint16 parameter, quint16 value, int timeoutMs) {
    return submit(CommunicationWorker::Kind::Write, parameter, value, timeoutMs);
}
quint64 CommunicationConnection::submit(CommunicationWorker::Kind kind, quint16 parameter, quint16 value, int timeoutMs) {
    m_error.clear();
    int count = 0;
    for (const auto& pending : m_requests)
        if (pending.kind == kind) ++count;
    if (m_shutdown || !m_connected || timeoutMs <= 0 || count == CommunicationWorker::QueueCapacity
        || m_nextId == std::numeric_limits<quint64>::max()) {
        m_error = QStringLiteral("Not connected, nonpositive timeout, capacity reached or IDs exhausted.");
        return 0;
    }
    const CommunicationWorker::Request request{++m_nextId, kind, parameter, value, timeoutMs};
    m_requests.insert(request.id, Pending{kind});
    m_worker->enqueue(request);
    return request.id;
}

bool CommunicationConnection::cancel(quint64 id) {
    m_error.clear();
    auto pending = m_requests.find(id);
    if (m_shutdown || pending == m_requests.end() || pending->cancelling) {
        m_error = QStringLiteral("Request is unavailable or cancellation already pending.");
        return false;
    }
    pending->cancelling = true;
    m_worker->cancel(id);
    return true;
}

void CommunicationConnection::shutdown() {
    if (m_shutdown) return;
    m_shutdown = true;
    m_connected = false;
    m_worker->shutdown();
    m_worker->wait(); // Bounded transport waits return to run() to observe shutdown.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall); // Deliver every admitted terminal result before destruction.
    Q_ASSERT(m_requests.isEmpty());
}
