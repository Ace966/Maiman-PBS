#pragma once

#include "CommunicationConnection.h"
#include "DeviceRepository.h"

#include <QMetaObject>
#include <QObject>


class DeviceService : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected, Error };
    Q_ENUM(State)

    explicit DeviceService(CommunicationConnection& connection, DeviceRepository& repository,
                           QObject* parent = nullptr);
    State state() const { return m_state; }
    QString errorString() const { return m_error; }
    bool connectDevice(const SerialSettings& settings);
    bool disconnectDevice();

signals:
    void stateChanged(DeviceService::State state);

private:
    void bindCallbacks();
    void setState(State state, const QString& error = {});
    void onConnectionChanged(bool connected, const QString& error);
    void onRequestFinished(const CommunicationWorker::Result& result);

    CommunicationConnection& m_connection;
    DeviceRepository& m_repository;
    QMetaObject::Connection m_results;
    State m_state;
    QString m_error;
};
