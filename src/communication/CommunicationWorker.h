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
    using SourceFactory = std::function<IDataSource*()>; // Creates a new parentless source in the worker thread.
    enum class Kind { Read, Write };
    enum class Outcome { ReadResponse, BytesSent, DeviceError, NonexistentParameter,
                         Timeout, TransportError, Cancelled, NotConnected };
    struct Request {
        Kind kind;
        quint16 parameter;
        quint16 value;
        int timeoutMs;
    };
    struct Result {
        Kind kind;
        quint16 parameter;
        quint16 value; // Raw value on ReadResponse; otherwise the submitted value.
        Outcome outcome;
        quint16 errorCode{0};
        QString error;
    };
signals:
    // Acknowledges an open/disconnect control after all old transaction results.
    void connectionChanged(bool connected, const QString& error);
    void requestFinished(const CommunicationWorker::Result& result);
private:
    friend class CommunicationConnection;
    explicit CommunicationWorker(SourceFactory factory, QObject* parent);
    ~CommunicationWorker() override;
    // Thread-safe commands. No slot or queued invocation executes inside run().
    void open(const SerialSettings& settings);
    void disconnectPort();
    void setInterCommandDelayMs(int delayMs);
    void enqueue(const Request& request);
    void shutdown();
    void run() override;
    Result transact(IDataSource& source, PICTextProtocol& protocol, const Request& request);
    void wakeLocked();
    QList<Request> takeQueuedLocked();
    void complete(const Request& request, Outcome outcome);
    struct Control {
        SerialSettings settings;
        bool opening;
    };
    SourceFactory m_factory;
    int m_interCommandDelayMs{0};
    QMutex m_mutex;
    QSemaphore m_wake;
    bool m_wakePending{false}, m_stopping{false}, m_connected{false};
    std::optional<Control> m_control;
    QQueue<Request> m_reads, m_writes;
};
Q_DECLARE_METATYPE(CommunicationWorker::Result)
