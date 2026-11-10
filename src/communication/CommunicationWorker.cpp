#include "CommunicationWorker.h"
#include <QDeadlineTimer>
#include <memory>
#include <utility>

CommunicationWorker::CommunicationWorker(SourceFactory factory, QObject* parent)
    : QThread(parent), m_factory(std::move(factory)) {}

CommunicationWorker::~CommunicationWorker() { shutdown(); wait(); }

void CommunicationWorker::wakeLocked() {
    if (!m_wakePending) { m_wakePending = true; m_wake.release(); }
}

void CommunicationWorker::open(const SerialSettings& settings, quint64 epoch) {
    QMutexLocker lock(&m_mutex);
    if (m_stopping) return;
    m_control = Control{settings, epoch, true};
    m_connected = false;
    wakeLocked();
}

void CommunicationWorker::disconnectPort(quint64 epoch) {
    QMutexLocker lock(&m_mutex);
    if (m_stopping) return;
    m_control = Control{{}, epoch, false};
    m_connected = false;
    wakeLocked();
}

void CommunicationWorker::setInterCommandDelayMs(int delayMs) {
    QMutexLocker lock(&m_mutex);
    m_interCommandDelayMs = delayMs;
}

void CommunicationWorker::enqueue(const Request& request) {
    {
        QMutexLocker lock(&m_mutex);
        if (m_connected) {
            // The sole facade bounds outstanding requests, including active and undelivered results.
            auto& queue = request.kind == Kind::Write ? m_writes : m_reads;
            queue.enqueue(request);
            wakeLocked();
            return;
        }
    }
    complete(request, Outcome::NotConnected, QStringLiteral("Connection is not available."));
}

void CommunicationWorker::cancel(quint64 id) {
    std::optional<Request> removed;
    {
        QMutexLocker lock(&m_mutex);
        if (m_activeId == id) { m_cancelActive = true; wakeLocked(); return; }
        for (auto* queue : {&m_writes, &m_reads}) {
            for (qsizetype i = 0; i < queue->size(); ++i) if (queue->at(i).id == id) {
                removed = queue->takeAt(i);
                break;
            }
            if (removed) break;
        }
        if (removed) wakeLocked();
    }
    if (removed) complete(*removed, Outcome::Cancelled, QStringLiteral("Queued request cancelled."));
}

void CommunicationWorker::shutdown() {
    QMutexLocker lock(&m_mutex);
    m_stopping = true;
    m_connected = false;
    wakeLocked();
}

bool CommunicationWorker::interrupted() {
    QMutexLocker lock(&m_mutex);
    return m_stopping || m_control.has_value() || m_cancelActive;
}

QList<CommunicationWorker::Request> CommunicationWorker::takeQueuedLocked() {
    QList<Request> requests;
    while (!m_writes.isEmpty()) requests.append(m_writes.dequeue());
    while (!m_reads.isEmpty()) requests.append(m_reads.dequeue());
    return requests;
}

void CommunicationWorker::complete(const Request& request, Outcome outcome, const QString& error) {
    emit requestFinished(Result{request.id, request.kind, request.parameter, request.value, outcome, 0, error});
}

void CommunicationWorker::run() {
    std::unique_ptr<IDataSource> source;
    PICTextProtocol protocol;
    quint64 epoch = 0;
    QDeadlineTimer nextCommand(0, Qt::PreciseTimer);
    bool delayPending = false;
    const auto close = [&] {
        if (source) { source->close(); source.reset(); }
        protocol.reset();
        nextCommand = QDeadlineTimer(0, Qt::PreciseTimer);
    };
    for (;;) {
        // Neither wait holds the state mutex; control/submission wakes interrupt the delay.
        bool woke = true;
        if (delayPending) woke = m_wake.tryAcquire(1, nextCommand);
        else m_wake.acquire();
        delayPending = false;
        std::optional<Control> control;
        std::optional<Request> request;
        QList<Request> cancelled;
        bool stopping;
        {
            QMutexLocker lock(&m_mutex);
            if (woke) m_wakePending = false; // Expiry must not clear a concurrent producer's token.
            stopping = m_stopping;
            if (stopping || m_control) {
                control = std::exchange(m_control, std::nullopt);
                m_connected = false;
                cancelled = takeQueuedLocked();
            } else if (m_connected && (!m_writes.isEmpty() || !m_reads.isEmpty())) {
                if (nextCommand.hasExpired()) {
                    request = !m_writes.isEmpty() ? m_writes.dequeue() : m_reads.dequeue();
                    m_activeId = request->id;
                    m_cancelActive = false;
                } else delayPending = true; // Still use a timed wait if expiry races the next iteration.
            }
            if (!stopping && !delayPending && (!m_writes.isEmpty() || !m_reads.isEmpty())) wakeLocked();
        }
        if (stopping || control) {
            close();
            const auto reason = stopping ? QStringLiteral("Connection shut down.")
                : control->opening ? QStringLiteral("Connection replaced.") : QStringLiteral("Disconnected.");
            for (const auto& queued : cancelled) complete(queued, Outcome::Cancelled, reason);
            if (stopping) { emit connectionChanged(false, reason, epoch); return; }
            epoch = control->epoch;
            bool opened = false;
            QString error;
            if (control->opening) {
                source.reset(m_factory());
                if (source) {
                    Q_ASSERT(source->thread() == QThread::currentThread() && !source->parent());
                    opened = source->open(control->settings);
                    if (!opened) error = source->errorString();
                } else error = QStringLiteral("Source factory returned no source.");
                if (!opened) close();
            } else error = reason;
            {
                QMutexLocker lock(&m_mutex);
                m_connected = opened && !m_stopping;
            }
            emit connectionChanged(opened, error, epoch);
            continue;
        }
        if (!request) continue;
        auto result = transact(*source, protocol, *request);
        bool closing;
        {
            QMutexLocker lock(&m_mutex);
            // Completion and cancellation have one mutex-protected decision point.
            if (m_stopping || m_control || m_cancelActive) {
                result.outcome = Outcome::Cancelled;
                result.value = request->value;
                result.errorCode = 0;
                result.error = QStringLiteral("Active request cancelled; explicit reconnect required.");
            }
            m_activeId = 0;
            m_cancelActive = false;
            closing = result.outcome == Outcome::Timeout || result.outcome == Outcome::TransportError
                || result.outcome == Outcome::Cancelled;
            if (closing) { m_connected = false; cancelled = takeQueuedLocked(); }
            else nextCommand = QDeadlineTimer(m_interCommandDelayMs, Qt::PreciseTimer);
        }
        if (closing) {
            close();
            emit connectionChanged(false, result.error, epoch);
        }
        emit requestFinished(result);
        for (const auto& queued : cancelled) complete(queued, Outcome::Cancelled, result.error);
    }
}

CommunicationWorker::Result CommunicationWorker::transact(
    IDataSource& source, PICTextProtocol& protocol, const Request& request) {
    QDeadlineTimer deadline(request.timeoutMs, Qt::PreciseTimer);
    Result result{request.id, request.kind, request.parameter, request.value, Outcome::BytesSent, 0, {}};
    std::optional<PICTextProtocol::Response> response;
    const auto fail = [&](Outcome outcome, const QString& error) {
        result.outcome = outcome; result.error = error;
        return false;
    };
    const auto check = [&] {
        if (interrupted()) return fail(Outcome::Cancelled, QStringLiteral("Active request cancelled; explicit reconnect required."));
        if (deadline.hasExpired()) return fail(Outcome::Timeout, QStringLiteral("Transaction deadline expired; explicit reconnect required."));
        if (source.hasError()) return fail(Outcome::TransportError, source.errorString());
        return true;
    };
    const auto readAvailable = [&] {
        if (!check()) return false;
        const auto available = source.bytesAvailable();
        if (available < 0) return fail(Outcome::TransportError, QStringLiteral("Invalid available-byte count."));
        if (!available) return true;
        QByteArray bytes(qMin<qint64>(available, 4096), Qt::Uninitialized);
        const auto count = source.read(bytes.data(), bytes.size());
        if (count < 0 || count > bytes.size() || source.hasError())
            return fail(Outcome::TransportError, QStringLiteral("Transport read failed: %1").arg(source.errorString()));
        bytes.resize(count);
        const auto parsed = protocol.append(bytes);
        for (const auto& received : parsed.responses) {
            if (!response && ((received.kind == PICTextProtocol::Kind::ParameterValue
                    && request.kind == Kind::Read && received.parameter == request.parameter)
                    || received.kind == PICTextProtocol::Kind::DeviceError
                    || received.kind == PICTextProtocol::Kind::NonexistentParameter)) response = received;
        }
        return true;
    };
    const auto wait = [&](bool writing) {
        // Slices only bound control latency; they never refresh the transaction deadline.
        const int remaining = int(qMin<qint64>(20, deadline.remainingTime()));
        if (remaining > 0) {
            if (writing) source.waitForBytesWritten(remaining);
            else source.waitForReadyRead(remaining);
        }
    };
    const auto bytes = request.kind == Kind::Write ? PICTextProtocol::encodeWrite(request.parameter, request.value)
                                                  : PICTextProtocol::encodeRead(request.parameter);
    qsizetype offset = 0;
    for (;;) {
        if (!readAvailable()) return result;
        auto pending = source.bytesToWrite();
        if (pending < 0) { fail(Outcome::TransportError, QStringLiteral("Invalid pending-byte count.")); return result; }
        qint64 accepted = 0;
        if (!pending && offset < bytes.size()) {
            const auto remaining = bytes.sliced(offset);
            accepted = source.write(remaining);
            if (accepted < 0 || accepted > remaining.size() || source.hasError()) {
                fail(Outcome::TransportError, QStringLiteral("Transport write failed: %1").arg(source.errorString()));
                return result;
            }
            offset += accepted;
            if (!readAvailable()) return result; // Retain an early response until output drains.
            pending = source.bytesToWrite();
            if (pending < 0) { fail(Outcome::TransportError, QStringLiteral("Invalid pending-byte count.")); return result; }
        }
        if (!check()) return result;
        if (offset == bytes.size() && pending == 0) break;
        if (accepted > 0 && pending == 0) continue; // Finite progress, at most the command length.
        wait(pending > 0); // A zero write without pending bytes waits for input/expiry, never spins.
    }
    while (request.kind == Kind::Read && !response) {
        if (!readAvailable()) return result;
        if (!response) wait(false);
    }
    if (!check()) return result;
    if (response) {
        if (response->kind == PICTextProtocol::Kind::DeviceError) {
            result.outcome = Outcome::DeviceError; result.errorCode = response->errorCode;
        } else if (response->kind == PICTextProtocol::Kind::NonexistentParameter) result.outcome = Outcome::NonexistentParameter;
        else { result.outcome = Outcome::ReadResponse; result.value = response->value; }
    }
    return result; // Standard Write success is local BytesSent, never device-state confirmation.
}
