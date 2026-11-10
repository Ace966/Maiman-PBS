#pragma once
#include "CommunicationWorker.h"
#include <QHash>

// Caller-thread facade and sole communication-thread owner. All public calls are sequential
// in this object's thread. Admission bounds include queued delivery and results.
class CommunicationConnection : public QObject {
    Q_OBJECT
public:
    explicit CommunicationConnection(CommunicationWorker::SourceFactory factory = {}, QObject* parent = nullptr);
    ~CommunicationConnection() override;
    bool connectPort(const SerialSettings& settings); // True: attempt posted, not yet connected.
    bool disconnectPort();
    bool setInterCommandDelayMs(int delayMs); // Nonnegative; disconnected, no pending connection change.
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
private slots:
    void onConnectionChanged(bool connected, const QString& error, quint64 epoch);
    void onRequestFinished(const CommunicationWorker::Result& result);
private:
    quint64 submit(CommunicationWorker::Kind kind, quint16 parameter, quint16 value, int timeoutMs);
    CommunicationWorker* m_worker; // QThread object owned here in the creator thread.
    struct Pending {
        CommunicationWorker::Kind kind;
        bool cancelling{false};
    };
    QHash<quint64, Pending> m_requests;
    quint64 m_nextId{0}, m_epoch{0};
    bool m_connected{false}, m_controlPending{false}, m_shutdown{false};
    QString m_error;
};
