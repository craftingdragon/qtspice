#include "framesnapshot.h"
#include <QImageWriter>
#include <QSaveFile>
#include <utility>

FrameSnapshot::FrameSnapshot(QObject *parent) : QObject(parent) {}

void FrameSnapshot::arm(const QString &path) {
    if (path.isEmpty() || !path_.isEmpty()) return;
    path_ = path;
    Q_EMIT pendingChanged(true);
}

void FrameSnapshot::cancel() {
    if (path_.isEmpty()) return;
    path_.clear();
    Q_EMIT pendingChanged(false);
}

void FrameSnapshot::capture(const QImage &frame) {
    if (path_.isEmpty() || frame.isNull()) return;
    const QString path = std::exchange(path_, {});
    Q_EMIT pendingChanged(false);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        Q_EMIT failed(QStringLiteral("Could not save snapshot to %1: %2").arg(path, file.errorString()));
        return;
    }
    QImageWriter writer(&file, "png");
    if (!writer.write(frame)) {
        Q_EMIT failed(QStringLiteral("Could not encode snapshot for %1: %2").arg(path, writer.errorString()));
        return;
    }
    if (!file.commit()) {
        Q_EMIT failed(QStringLiteral("Could not save snapshot to %1: %2").arg(path, file.errorString()));
        return;
    }
    Q_EMIT saved(path);
}
