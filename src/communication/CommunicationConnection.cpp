#include "CommunicationConnection.h"
#include "serialdatasource.h"
#include <QCoreApplication>
#include <QEvent>
#include <QThread>
#include <limits>
#include <utility>

CommunicationConnection::CommunicationConnection(CommunicationWorker::SourceFactory factory, QObject* parent)
    : QObject(parent),
      m_worker(new CommunicationWorker(factory ? std::move(factory) : [] { return new SerialDataSource; }, this)) {
    qRegisterMetaType<CommunicationWorker::Result>();
    qRegisterMetaType<PICTextProtocol::Response>();
    connect(m_worker, &CommunicationWorker::connectionChanged, this,
        [this](bool connected, const QString& error, quint64 epoch) {
            if (m_shutdown || epoch != m_epoch) return;
            m_connected = connected; m_controlPending = false;
            emit connectionChanged(connected, error);
        }, Qt::QueuedConnection);
    connect(m_worker, &CommunicationWorker::requestFinished, this,
        [this](const CommunicationWorker::Result& result) {
            const auto it = m_requests.find(result.id);
            if (it == m_requests.end()) return;
            (*it == CommunicationWorker::Kind::Read ? m_reads : m_writes)--;
            m_requests.erase(it); m_cancelling.remove(result.id);
            emit requestFinished(result);
        }, Qt::QueuedConnection);
    connect(m_worker, &CommunicationWorker::bytesSent, this, &CommunicationConnection::bytesSent, Qt::QueuedConnection);
    connect(m_worker, &CommunicationWorker::responseReceived, this,
        [this](const PICTextProtocol::Response& response, quint64 epoch) {
            if (!m_shutdown && epoch == m_epoch) emit responseReceived(response);
        }, Qt::QueuedConnection);
    connect(m_worker, &CommunicationWorker::framesDiscarded, this,
        [this](qsizetype count, quint64 epoch) {
            if (!m_shutdown && epoch == m_epoch) emit framesDiscarded(count);
        }, Qt::QueuedConnection);
    m_worker->start();
}

CommunicationConnection::~CommunicationConnection() { shutdown(); }
void CommunicationConnection::checkThread() const { Q_ASSERT(thread() == QThread::currentThread()); }

bool CommunicationConnection::connectPort(const SerialSettings& settings) {
    checkThread(); m_error.clear();
    if (m_shutdown || m_controlPending) { m_error = QStringLiteral("Connection change unavailable or pending."); return false; }
    if (settings.comport.trimmed().isEmpty() || settings.comport.contains(QChar(0)) || settings.baudrate <= 0) {
        m_error = QStringLiteral("Expected explicit nonblank comport without NUL and positive baudrate."); return false;
    }
    if (m_epoch == std::numeric_limits<quint64>::max()) { m_error = QStringLiteral("Session IDs exhausted."); return false; }
    m_connected = false; m_controlPending = true;
    const auto epoch = ++m_epoch;
    m_worker->open(settings, epoch);
    return true;
}

bool CommunicationConnection::disconnectPort() {
    checkThread(); m_error.clear();
    if (m_shutdown || m_controlPending || m_epoch == std::numeric_limits<quint64>::max()) {
        m_error = QStringLiteral("Connection change unavailable or pending."); return false;
    }
    m_connected = false; m_controlPending = true;
    const auto epoch = ++m_epoch;
    m_worker->disconnectPort(epoch);
    return true;
}

quint64 CommunicationConnection::read(quint16 parameter, int timeoutMs) {
    return submit(CommunicationWorker::Kind::Read, parameter, 0, timeoutMs);
}
quint64 CommunicationConnection::write(quint16 parameter, quint16 value, int timeoutMs) {
    return submit(CommunicationWorker::Kind::Write, parameter, value, timeoutMs);
}
quint64 CommunicationConnection::submit(CommunicationWorker::Kind kind, quint16 parameter, quint16 value, int timeoutMs) {
    checkThread(); m_error.clear();
    auto& count = kind == CommunicationWorker::Kind::Read ? m_reads : m_writes;
    if (m_shutdown || !m_connected || timeoutMs <= 0 || count == CommunicationWorker::QueueCapacity
        || m_nextId == std::numeric_limits<quint64>::max()) {
        m_error = QStringLiteral("Not connected, nonpositive timeout, capacity reached or IDs exhausted."); return 0;
    }
    const CommunicationWorker::Request request{++m_nextId, kind, parameter, value, timeoutMs};
    ++count; m_requests.insert(request.id, kind);
    const auto epoch = m_epoch;
    m_worker->enqueue(request, epoch);
    return request.id;
}

bool CommunicationConnection::cancel(quint64 id) {
    checkThread(); m_error.clear();
    if (m_shutdown || !m_requests.contains(id) || m_cancelling.contains(id)) {
        m_error = QStringLiteral("Request is unavailable or cancellation already pending."); return false;
    }
    m_cancelling.insert(id);
    m_worker->cancel(id);
    return true;
}

void CommunicationConnection::shutdown() {
    checkThread();
    if (m_shutdown) return;
    m_shutdown = true; m_connected = false;
    m_worker->shutdown();
    m_worker->wait(); // Bounded transport waits return to run() to observe shutdown.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall); // Deliver every admitted terminal result before destruction.
    Q_ASSERT(m_requests.isEmpty());
}
