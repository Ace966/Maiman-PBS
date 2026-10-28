#pragma once
#include "IDataSource.h"
#include "PICTextProtocol.h"
#include <QDeadlineTimer>
#include <QQueue>
#include <functional>
#include <optional>

class QTimer;
class CommunicationConnection;

// Hosted by CommunicationConnection; all mutable state and I/O stay in its thread.
class CommunicationWorker : public QObject {
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
    explicit CommunicationWorker(SourceFactory factory);
private slots:
    void open(const SerialSettings& settings, quint64 epoch);
    void disconnectPort(quint64 epoch);
    void enqueue(const Request& request, quint64 epoch);
    void cancel(quint64 id);
    void shutdown();
private:
    void scheduleNext();
    void startNext();
    void pumpOutput();
    void readAvailable();
    void postPump();
    void postRead();
    bool expired();
    void finish(Outcome outcome, const QString& error = {});
    void failConnection(Outcome outcome, const QString& error);
    void closeSession(Outcome outcome, const QString& error, bool notify);
    void cancelled(const Request& request, Outcome outcome, const QString& error);
    struct Active {
        Request request;
        QByteArray bytes;
        qsizetype offset{0};
        bool sent{false};
        std::optional<PICTextProtocol::Response> response;
    };
    SourceFactory m_factory;
    IDataSource* m_source{nullptr}; // Owned QObject child.
    QTimer* m_timer{nullptr}; // Created and used in worker thread.
    quint64 m_epoch{0};
    bool m_shutdown{false}, m_nextPosted{false}, m_pumpPosted{false}, m_readPosted{false};
    QQueue<Request> m_reads, m_writes;
    std::optional<Active> m_active;
    QDeadlineTimer m_deadline;
    PICTextProtocol m_protocol;
};
Q_DECLARE_METATYPE(CommunicationWorker::Result)
