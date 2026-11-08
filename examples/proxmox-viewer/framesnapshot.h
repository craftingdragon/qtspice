// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class FrameSnapshot final : public QObject {
    Q_OBJECT
public:
    explicit FrameSnapshot(QObject *parent = nullptr);
    void arm(const QString &path);
    void cancel();
    void capture(const QImage &frame);

signals:
    void pendingChanged(bool pending);
    void saved(const QString &path);
    void failed(const QString &message);

private:
    QString path_;
};
