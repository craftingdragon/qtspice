#pragma once
#include "../reconnect.h"
#include <QWebEnginePage>
#include <QWebChannel>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QRegularExpression>
#include <utility>
#include <QUuid>

class TicketBridge final : public QObject {
    Q_OBJECT
public:
    explicit TicketBridge(QWebEnginePage *page) : QObject(page), page_(page) {
        auto *channel = new QWebChannel(page); channel->registerObject(QStringLiteral("tickets"), this); page->setWebChannel(channel);
        connect(page, &QWebEnginePage::loadFinished, this, [this](bool ok) {
            if (!ok || page_->url().scheme() != QStringLiteral("https")) return;
            QFile library(QStringLiteral(":/qtwebchannel/qwebchannel.js")); if (!library.open(QIODevice::ReadOnly)) return;
            page_->runJavaScript(QString::fromUtf8(library.readAll()) + QStringLiteral(R"JS(
new QWebChannel(qt.webChannelTransport, channel => {
 window.qtSpiceTickets = channel.objects.tickets;
 if (!window.PVE || !PVE.Utils || !PVE.Utils.openSpiceViewer) return;
 const original = PVE.Utils.openSpiceViewer;
 PVE.Utils.openSpiceViewer = function(url, params) {
  if (!/^\/nodes\/[^/]+\/qemu\/[0-9]+\/spiceproxy$/.test(url)) return original.apply(this, arguments);
  qtSpiceTickets.open(url, JSON.stringify(params || {}));
 };
});
)JS"));
        });
        connect(page, &QWebEnginePage::loadStarted, this, [this] {
            auto pending = std::exchange(pending_, {});
            for (auto done : pending) done({}, QStringLiteral("Return to Proxmox and log in, then reconnect"));
        });
    }
    std::function<void(QtSpice::ConnectionConfig, Reconnect::Source)> opened;
    void request(QString path, QString params, QUrl origin, Reconnect::Result done) {
        if (page_->url().adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment) != origin) {
            done({}, QStringLiteral("Return to the original Proxmox server and log in, then reconnect")); return;
        }
        QString id = QUuid::createUuid().toString(QUuid::WithoutBraces); pending_[id] = std::move(done);
        QTimer::singleShot(30000, this, [this, id] {
            if (pending_.contains(id)) { auto done = pending_.take(id); done({}, QStringLiteral("Console ticket request timed out")); }
        });
        QJsonObject args{{"id", id}, {"path", path}, {"params", QJsonDocument::fromJson(params.toUtf8()).object()}};
        page_->runJavaScript(QStringLiteral(R"JS((function(a) {
 if (!window.qtSpiceTickets || !window.Proxmox || !Proxmox.Utils.API2Request) return false;
 Proxmox.Utils.API2Request({url:a.path, params:a.params, method:'POST',
 success:r => {const raw='[virt-viewer]\n'+Object.entries(r.result.data).map(([k,v])=>k+'='+v+'\n').join(''); qtSpiceTickets.reply(a.id,raw,true);},
 failure:() => qtSpiceTickets.reply(a.id,'',false)}); return true;
})(%1))JS").arg(QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))), [guard = QPointer<TicketBridge>(this), id](const QVariant &started) {
            if (guard && !started.toBool() && guard->pending_.contains(id)) {
                auto done = guard->pending_.take(id); done({}, QStringLiteral("Return to Proxmox and log in, then reconnect"));
            }
        });
    }
public slots:
    void open(QString path, QString params) {
        static const QRegularExpression allowed(QStringLiteral("^/nodes/[^/]+/qemu/[0-9]+/spiceproxy$"));
        if (!allowed.match(path).hasMatch() || page_->url().scheme() != QStringLiteral("https")) return;
        auto origin = page_->url().adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
        QPointer<TicketBridge> guard(this);
        Reconnect::Source source = [guard, path, params, origin](Reconnect::Result done) {
            if (guard) guard->request(path, params, origin, done);
            else done({}, QStringLiteral("Proxmox browser is unavailable"));
        };
        source([guard, source](auto config, QString error) { if (!guard) return; if (config && guard->opened) guard->opened(*config, source); else emit guard->errorOccurred(error); });
    }
    void reply(QString id, QString raw, bool ok) {
        if (!pending_.contains(id)) return;
        auto done = pending_.take(id); QString error;
        auto config = ok && raw.size() <= 1024*1024 ? QtSpice::parseVv(raw.toUtf8(), &error) : std::nullopt;
        done(config, config ? QString() : QStringLiteral("Could not obtain console ticket; check Proxmox login, VM state and permissions"));
    }
signals:
    void errorOccurred(const QString &message);
private:
    QWebEnginePage *page_; QMap<QString, Reconnect::Result> pending_;
};
