#include "CommunicationWorker.h"
#include <QThread>
#include <QTimer>
#include <utility>

CommunicationWorker::CommunicationWorker(SourceFactory factory) : m_factory(std::move(factory)) {}

void CommunicationWorker::open(const SerialSettings& settings, quint64 epoch) {
    Q_ASSERT(thread() == QThread::currentThread());
    m_epoch = epoch;
    closeSession(Outcome::Cancelled, QStringLiteral("Connection replaced."), false);
    if (m_shutdown) return;
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, [this] {
            if (!m_active || expired()) return;
            m_timer->start(int(qMax<qint64>(1, m_deadline.remainingTime())));
        });
    }
    m_source = m_factory();
    if (!m_source) { emit connectionChanged(false, QStringLiteral("Source factory returned no source."), epoch); return; }
    Q_ASSERT(m_source->thread() == thread() && !m_source->parent());
    m_source->setParent(this);
    if (!m_source->open(settings)) {
        const auto error = m_source->errorString();
        closeSession(Outcome::Cancelled, error, false);
        emit connectionChanged(false, error, epoch);
        return;
    }
    auto* source = m_source;
    connect(source, &IDataSource::readyRead, this, [this, source, epoch] {
        if (source == m_source && epoch == m_epoch) readAvailable();
    }, Qt::QueuedConnection);
    connect(source, &IDataSource::bytesWritten, this, [this, source, epoch](qint64) {
        if (source == m_source && epoch == m_epoch) pumpOutput();
    }, Qt::QueuedConnection);
    connect(source, &IDataSource::errorOccurred, this, [this, source, epoch](const QString& error) {
        if (source == m_source && epoch == m_epoch) failConnection(Outcome::TransportError, error);
    }, Qt::QueuedConnection);
    emit connectionChanged(true, {}, epoch);
    if (source->bytesAvailable() > 0) postRead();
}

void CommunicationWorker::disconnectPort(quint64 epoch) {
    m_epoch = epoch;
    closeSession(Outcome::Cancelled, QStringLiteral("Disconnected."), true);
}

void CommunicationWorker::enqueue(const Request& request, quint64 epoch) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_shutdown || !m_source || epoch != m_epoch) {
        cancelled(request, Outcome::NotConnected, QStringLiteral("Connection is not available."));
        return;
    }
    auto& queue = request.kind == Kind::Write ? m_writes : m_reads;
    if (queue.size() == QueueCapacity) {
        cancelled(request, Outcome::Cancelled, QStringLiteral("Worker queue is full."));
        return;
    }
    queue.enqueue(request);
    scheduleNext();
}

void CommunicationWorker::scheduleNext() {
    if (m_nextPosted || m_shutdown) return;
    m_nextPosted = true;
    QMetaObject::invokeMethod(this, [this] { m_nextPosted = false; startNext(); }, Qt::QueuedConnection);
}

void CommunicationWorker::startNext() {
    if (m_shutdown || !m_source || m_active || (m_writes.isEmpty() && m_reads.isEmpty())) return;
    const auto request = !m_writes.isEmpty() ? m_writes.dequeue() : m_reads.dequeue();
    m_active = Active{request, request.kind == Kind::Write
        ? PICTextProtocol::encodeWrite(request.parameter, request.value)
        : PICTextProtocol::encodeRead(request.parameter)};
    m_deadline = QDeadlineTimer(request.timeoutMs, Qt::PreciseTimer);
    m_timer->start(request.timeoutMs);
    pumpOutput();
}

bool CommunicationWorker::expired() {
    if (!m_active || !m_deadline.hasExpired()) return false;
    failConnection(Outcome::Timeout, QStringLiteral("Transaction deadline expired; explicit reconnect required."));
    return true;
}

void CommunicationWorker::postPump() {
    if (m_pumpPosted) return;
    m_pumpPosted = true;
    QMetaObject::invokeMethod(this, [this] { m_pumpPosted = false; pumpOutput(); }, Qt::QueuedConnection);
}

void CommunicationWorker::pumpOutput() {
    if (!m_active || !m_source || expired()) return;
    if (m_source->hasError()) { failConnection(Outcome::TransportError, m_source->errorString()); return; }
    if (m_active->offset < m_active->bytes.size()) {
        const auto remaining = m_active->bytes.sliced(m_active->offset);
        const auto count = m_source->write(remaining);
        if (count < 0 || count > remaining.size() || m_source->hasError()) {
            failConnection(Outcome::TransportError, QStringLiteral("Transport write failed: %1").arg(m_source->errorString()));
            return;
        }
        m_active->offset += count;
        if (count == 0) return;
    }
    const auto pending = m_source->bytesToWrite();
    if (pending < 0 || m_source->hasError()) { failConnection(Outcome::TransportError, m_source->errorString()); return; }
    if (m_active->offset < m_active->bytes.size()) {
        if (pending == 0) postPump();
        return;
    }
    if (pending != 0) return;
    if (!m_active->sent) { m_active->sent = true; emit bytesSent(m_active->request.id); }
    if (m_active->response) {
        const auto kind = m_active->response->kind;
        finish(kind == PICTextProtocol::Kind::DeviceError ? Outcome::DeviceError
            : kind == PICTextProtocol::Kind::NonexistentParameter ? Outcome::NonexistentParameter : Outcome::ReadResponse);
    } else if (m_active->request.kind == Kind::Write) finish(Outcome::BytesSent);
}

void CommunicationWorker::postRead() {
    if (m_readPosted) return;
    m_readPosted = true;
    QMetaObject::invokeMethod(this, [this] { m_readPosted = false; readAvailable(); }, Qt::QueuedConnection);
}

void CommunicationWorker::readAvailable() {
    if (!m_source || expired()) return;
    if (m_source->hasError()) { failConnection(Outcome::TransportError, m_source->errorString()); return; }
    const auto available = m_source->bytesAvailable();
    if (available < 0) { failConnection(Outcome::TransportError, QStringLiteral("Invalid available-byte count.")); return; }
    if (!available) return;
    QByteArray bytes(qMin<qint64>(available, 4096), Qt::Uninitialized);
    const auto count = m_source->read(bytes.data(), bytes.size());
    if (count < 0 || count > bytes.size() || m_source->hasError()) {
        failConnection(Outcome::TransportError, QStringLiteral("Transport read failed: %1").arg(m_source->errorString()));
        return;
    }
    bytes.resize(count);
    const auto parsed = m_protocol.append(bytes);
    for (const auto& response : parsed.responses) {
        if (m_active && expired()) break;
        emit responseReceived(response, m_epoch);
        if (!m_active || m_active->response) continue;
        if ((response.kind == PICTextProtocol::Kind::ParameterValue
             && m_active->request.kind == Kind::Read && response.parameter == m_active->request.parameter)
            || response.kind == PICTextProtocol::Kind::DeviceError
            || response.kind == PICTextProtocol::Kind::NonexistentParameter) {
            m_active->response = response;
            if (m_active->sent) pumpOutput();
        }
    }
    if (parsed.discardedFrames && m_source) emit framesDiscarded(parsed.discardedFrames, m_epoch);
    if (m_source && count > 0 && m_source->bytesAvailable() > 0) postRead();
}

void CommunicationWorker::finish(Outcome outcome, const QString& error) {
    if (!m_active) return;
    const auto active = *m_active;
    m_active.reset();
    m_timer->stop();
    Result result{active.request.id, active.request.kind, active.request.parameter, active.request.value, outcome, 0, error};
    if (active.response) {
        if (outcome == Outcome::ReadResponse) result.value = active.response->value;
        result.errorCode = active.response->errorCode;
    }
    emit requestFinished(result);
    scheduleNext(); // Never select the next request inside this input chunk.
}

void CommunicationWorker::cancelled(const Request& request, Outcome outcome, const QString& error) {
    emit requestFinished(Result{request.id, request.kind, request.parameter, request.value, outcome, 0, error});
}

void CommunicationWorker::closeSession(Outcome outcome, const QString& error, bool notify) {
    if (m_timer) m_timer->stop();
    auto* source = m_source;
    m_source = nullptr;
    if (source) { disconnect(source, nullptr, this, nullptr); source->close(); delete source; }
    m_protocol.reset();
    if (notify) emit connectionChanged(false, error, m_epoch);
    if (m_active) finish(outcome, error);
    while (!m_writes.isEmpty()) cancelled(m_writes.dequeue(), Outcome::Cancelled, error);
    while (!m_reads.isEmpty()) cancelled(m_reads.dequeue(), Outcome::Cancelled, error);
}

void CommunicationWorker::failConnection(Outcome outcome, const QString& error) { closeSession(outcome, error, true); }

void CommunicationWorker::cancel(quint64 id) {
    if (m_active && m_active->request.id == id) {
        failConnection(Outcome::Cancelled, QStringLiteral("Active request cancelled; explicit reconnect required."));
        return;
    }
    for (auto* queue : {&m_writes, &m_reads}) {
        for (qsizetype i = 0; i < queue->size(); ++i) if (queue->at(i).id == id) {
            const auto request = queue->takeAt(i);
            cancelled(request, Outcome::Cancelled, QStringLiteral("Queued request cancelled."));
            return;
        }
    }
}

void CommunicationWorker::shutdown() {
    Q_ASSERT(thread() == QThread::currentThread());
    m_shutdown = true;
    closeSession(Outcome::Cancelled, QStringLiteral("Connection shut down."), true);
}
