// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <functional>

namespace QtSpice {
inline const QLoggingCategory &performanceLog() {
    static const QLoggingCategory category("qtspice.performance", QtWarningMsg);
    return category;
}

// Counts are per sample; gauges survive samples. Only collect when opted in.
// All callers and the timer run on the owning object's event-loop thread.
class IntervalStats final : public QObject {
public:
    IntervalStats(const char *kind, const QObject *identity) : kind_(kind), identity_(identity) {
        const bool optedIn = QCoreApplication::arguments().contains(QStringLiteral("--performance-stats"));
        if (!optedIn) return;
        elapsed_.start();
        timer_.setInterval(1000);
        connect(&timer_, &QTimer::timeout, this, [this] { sample(); });
        timer_.start();
    }
    bool enabled() const { return timer_.isActive() && performanceLog().isDebugEnabled(); }
    void add(const QString &name, quint64 value = 1) {
        if (enabled()) counts_[name] += value;
    }
    void gauge(const QString &name, quint64 value) {
        if (enabled()) gauges_[name] = value;
    }
    std::function<void()> updateGauges;
    void sample() {
        if (!enabled()) return;
        if (enabled() && updateGauges) updateGauges();
        const qint64 interval = elapsed_.restart();
        qCDebug(performanceLog) << kind_ << identity_ << "interval-ms" << interval
            << "counts" << counts_ << "gauges" << gauges_;
        for (auto &value : counts_) value = 0;
    }
private:
    const char *kind_;
    const QObject *identity_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QMap<QString, quint64> counts_, gauges_;
};
}
