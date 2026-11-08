#pragma once
#include <QtSpice/QSpiceSession.h>
#include <spice-client.h>
#include <functional>
#include <QMap>
#include <QPointer>

namespace QtSpice {
// Native callbacks use a shared lifetime token, never a raw controller pointer.
class UsbController {
public:
    explicit UsbController(Session *owner) : owner(owner) {}
    ~UsbController() { detach(); }
    void attach(SpiceSession *session);
    void detach();
    QList<UsbDeviceInfo> devices() const { return rows.values(); }
    void request(quint64 id, bool connect);
    void cancel();
    bool shareCd(const QString &path, QString *error = nullptr);
    void stopCd();
    void cdDeviceAdded(quint64 id);
    static QString validateCdSource(const QString &path);
    Session::SharedCdState cdState = Session::SharedCdState::Idle;
    QString cdSource, cdError;
    std::function<QString(const QString &)> createCd;

    void refreshChannels() { refresh(); }
    int freeChannels = 0;
    QString error;
    // Backend boundary also permits deterministic operation/lifecycle tests.
    using Completion = std::function<void(QString)>;
    std::function<void(quint64, bool, Completion)> operation;
    std::function<void()> cancellation;
    void update(QList<UsbDeviceInfo> devices, int free);
private:
    struct Lifetime { UsbController *controller; };
    std::shared_ptr<Lifetime> lifetime = std::make_shared<Lifetime>(Lifetime{this});
    Session *owner;
    SpiceUsbDeviceManager *manager = nullptr; // Owned by the native session.
    QMap<quint64, SpiceUsbDevice *> native;
    QMap<quint64, UsbDeviceInfo> rows;
    quint64 nextId = 0, serial = 0, busy = 0;
    bool connecting = false;
    quint64 cdId = 0;
    bool cdCreating = false, cdCancelled = false;
    void cdChanged() { Q_EMIT owner->sharedCdChanged(); }
    void syncCd();
    GCancellable *pending = nullptr;
    void refresh();
    void changed() { Q_EMIT owner->usbChanged(); }
    static void deviceSignal(SpiceUsbDeviceManager *, SpiceUsbDevice *, gpointer data);
    static void deviceError(SpiceUsbDeviceManager *, SpiceUsbDevice *, GError *, gpointer);
};
}
