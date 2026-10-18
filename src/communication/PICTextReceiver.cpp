#include "PICTextReceiver.h"
#include "DataThread.h"

PICTextReceiver::PICTextReceiver(DataThread& worker, QObject* parent) : QObject(parent) {
    qRegisterMetaType<PICTextProtocol::Response>();
    connect(&worker, &QThread::started, this, &PICTextReceiver::reset, Qt::QueuedConnection);
    connect(&worker, &DataThread::readyRead, this, &PICTextReceiver::receive, Qt::QueuedConnection);
    connect(&worker, &QThread::finished, this, &PICTextReceiver::reset, Qt::QueuedConnection);
}

void PICTextReceiver::receive(const QByteArray& bytes) {
    const auto result = m_assembler.append(bytes);
    for (const auto& frame : result.frames) emit responseReceived(PICTextProtocol::decode(frame));
    if (result.discardedFrames) emit framesDiscarded(result.discardedFrames);
}

void PICTextReceiver::reset() {
    m_assembler.reset();
}
