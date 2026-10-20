#pragma once

#include <QMetaType>
#include <QObject>
#include "PICTextProtocol.h"

class DataThread;

// Construct and connect consumers in the caller thread before starting worker.
// The parent/caller owns this object; worker is borrowed, never controlled here.
class PICTextReceiver : public QObject {
    Q_OBJECT
public:
    explicit PICTextReceiver(DataThread& worker, QObject* parent = nullptr);

public slots:
    void reset(); // Receiver thread only; does not purge already queued input.

signals:
    void responseReceived(const PICTextProtocol::Response& response);
    void framesDiscarded(qsizetype count); // Aggregate count after this chunk's responses.

private:
    void receive(const QByteArray& bytes);
    PICTextProtocol::FrameAssembler m_assembler;
};

Q_DECLARE_METATYPE(PICTextProtocol::Response)
