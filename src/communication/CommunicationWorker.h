#pragma once
#include "IDataSource.h"
#include "PICTextProtocol.h"
#include <QMutex>
#include <QSemaphore>
#include <QThread>
#include <QQueue>
#include <functional>
#include <optional>

class CommunicationConnection;

// The QThread object stays in its creator thread; run() owns all transport I/O.
class CommunicationWorker : public QThread {
    Q_OBJECT
public:
    static constexpr int QueueCapacity = 32;
    using SourceFactory = std::function<IDataSource*()>; // Called in the worker thread.
    enum class Kind { Read, Write };
    enum class Outcome { ReadResponse, BytesSent, DeviceError, NonexistentParameter,
                         Timeout, TransportError, Cancelled, NotConnected };
    struct Request {
        quint64 id;
        Kind kind;
        quint16 parameter;
        quint16 value;
        int timeoutMs;
    };
    struct Result {
        quint64 id;
        Kind kind;
        quint16 parameter;
        quint16 value; // Raw value on ReadResponse; otherwise the submitted value.
        Outcome outcome;
        quint16 errorCode{0};
        QString error;
    };
signals:
    void connectionChanged(bool connected, const QString& error, quint64 epoch);
    void requestFinished(const CommunicationWorker::Result& result);
    void bytesSent(quint64 id);
    void responseReceived(const PICTextProtocol::Response& response, quint64 epoch);
    void framesDiscarded(qsizetype count, quint64 epoch);
private:
    friend class CommunicationConnection;
    explicit CommunicationWorker(SourceFactory factory, QObject* parent);
    ~CommunicationWorker() override;
    // Thread-safe commands. No slot or queued invocation executes inside run().
    void open(const SerialSettings& settings, quint64 epoch);
    void disconnectPort(quint64 epoch);
    void enqueue(const Request& request, quint64 epoch);
    void cancel(quint64 id);
    void shutdown();
    void run() override;
    Result transact(IDataSource& source, PICTextProtocol& protocol, const Request& request, quint64 epoch);
    bool interrupted();
    void wakeLocked();
    QList<Request> takeQueuedLocked();
    void complete(const Request& request, Outcome outcome, const QString& error);
    struct Control {
        SerialSettings settings;
        quint64 epoch;
        bool opening;
    };
    SourceFactory m_factory;
    QMutex m_mutex;
    QSemaphore m_wake;
    bool m_wakePending{false}, m_stopping{false}, m_connected{false}, m_cancelActive{false};
    quint64 m_sessionEpoch{0}, m_activeId{0};
    std::optional<Control> m_control;
    QQueue<Request> m_reads, m_writes;
};
Q_DECLARE_METATYPE(CommunicationWorker::Result)
