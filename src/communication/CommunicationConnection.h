#pragma once
#include "CommunicationWorker.h"
#include <QHash>
#include <QSet>

class QThread;

// Caller-thread facade and sole QThread owner. All public calls are sequential
// in this object's thread. Admission bounds include queued delivery and results.
class CommunicationConnection : public QObject {
    Q_OBJECT
public:
    explicit CommunicationConnection(CommunicationWorker::SourceFactory factory = {}, QObject* parent = nullptr);
    ~CommunicationConnection() override;
    bool connectPort(const SerialSettings& settings); // True: attempt posted, not yet connected.
    bool disconnectPort();
    quint64 read(quint16 parameter, int timeoutMs = 1000); // Zero: rejected, no terminal result.
    quint64 write(quint16 parameter, quint16 value, int timeoutMs = 1000);
    bool cancel(quint64 id); // A completion already in flight may win the race.
    void shutdown(); // Final, joins outside Worker; create another facade to restart.
    bool isConnected() const { return m_connected; }
    int outstanding() const { return m_requests.size(); }
    QString lastSubmissionError() const { return m_error; }
signals:
    void connectionChanged(bool connected, const QString& error);
    void requestFinished(const CommunicationWorker::Result& result);
    void bytesSent(quint64 id);
    void responseReceived(const PICTextProtocol::Response& response);
    void framesDiscarded(qsizetype count);
private:
    quint64 submit(CommunicationWorker::Kind kind, quint16 parameter, quint16 value, int timeoutMs);
    void checkThread() const;
    QThread* m_thread;
    CommunicationWorker* m_worker; // Deleted in worker thread at QThread::finished.
    QHash<quint64, CommunicationWorker::Kind> m_requests;
    QSet<quint64> m_cancelling;
    int m_reads{0}, m_writes{0};
    quint64 m_nextId{0}, m_epoch{0};
    bool m_connected{false}, m_controlPending{false}, m_shutdown{false};
    QString m_error;
};
