#pragma once
#include "CommunicationWorker.h"

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
    bool read(quint16 parameter, int timeoutMs = 1000); // False: rejected, no terminal result.
    bool write(quint16 parameter, quint16 value, int timeoutMs = 1000);
    void shutdown(); // Final, joins outside Worker; create another facade to restart.
    bool isConnected() const { return m_connected; }
    int outstanding() const { return m_reads + m_writes; }
signals:
    void connectionChanged(bool connected, const QString& error);
    void requestFinished(const CommunicationWorker::Result& result);
private slots:
    void onConnectionChanged(bool connected, const QString& error);
    void onRequestFinished(const CommunicationWorker::Result& result);
private:
    bool submit(CommunicationWorker::Kind kind, quint16 parameter, quint16 value, int timeoutMs);
    CommunicationWorker* m_worker; // QThread object owned here in the creator thread.
    int m_reads{0}, m_writes{0}; // Outstanding credits, including active and undelivered results.
    bool m_connected{false}, m_controlPending{false}, m_shutdown{false};
};
