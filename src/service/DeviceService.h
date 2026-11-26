#pragma once

#include "CommunicationConnection.h"
#include "DeviceRepository.h"

#include <QMetaObject>
#include <QObject>
#include <QList>
#include <QTimer>
#include <optional>


class DeviceService : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected, Error };
    Q_ENUM(State)
    enum class PollPriority { High, Low };
    struct PollEntry {
    public:
        quint16 code;
        PollPriority priority;
    };

    explicit DeviceService(CommunicationConnection& connection, DeviceRepository& repository,
                           QObject* parent = nullptr);
    State state() const { return m_state; }
    QString errorString() const { return m_error; }
    bool connectDevice(const SerialSettings& settings);
    bool disconnectDevice();
    bool setPollingList(const QList<PollEntry>& entries);

signals:
    void stateChanged(DeviceService::State state);

private:
    void bindCallbacks();
    void armPolling();
    void pollNext();
    void setState(State state, const QString& error = {});
    void onConnectionChanged(bool connected, const QString& error);
    void onRequestFinished(const CommunicationWorker::Result& result);

    CommunicationConnection& m_connection;
    DeviceRepository& m_repository;
    QMetaObject::Connection m_results;
    State m_state;
    QString m_error;
    QTimer m_pollTimer;
    QList<quint16> m_pollOrder;
    qsizetype m_pollIndex{0};
    std::optional<quint16> m_pollPending;
};
