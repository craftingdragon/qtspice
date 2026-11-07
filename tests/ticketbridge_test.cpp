#include "../examples/proxmox-viewer/ticketbridge.h"
#include <QtTest>
#include <QWebEngineProfile>
class TicketBridgeTest : public QObject {
    Q_OBJECT
private slots:
    void browserIssuesFreshTicketsAndRetainsIdentity() {
        QWebEngineProfile profile; QWebEnginePage page(&profile); TicketBridge bridge(&page);
        QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
        page.setHtml(QStringLiteral(R"HTML(<script>
window.PVE={Utils:{openSpiceViewer:function(){}}};
window.calls=0;
window.Proxmox={Utils:{API2Request:function(r){
 window.lastPath=r.url;
 r.success({result:{data:{type:'spice',host:'127.0.0.1',port:5900,password:'fresh-'+(++window.calls)}}});
}}};
</script>)HTML"), QUrl("https://pve.invalid/"));
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        bool ready = false;
        QTRY_VERIFY_WITH_TIMEOUT(([&] { page.runJavaScript("!!window.qtSpiceTickets", [&](auto v) { ready = v.toBool(); }); return ready; })(), 10000);
        Reconnect::Source source; QString password;
        bridge.opened = [&](auto config, auto next) { password = config.password; source = next; };
        page.runJavaScript("PVE.Utils.openSpiceViewer('/nodes/node1/qemu/101/spiceproxy',{proxy:'pve.invalid'})");
        QTRY_COMPARE(password, QStringLiteral("fresh-1")); QVERIFY(bool(source));
        source([&](auto config, QString) { QVERIFY(config); password = config->password; });
        QTRY_COMPARE(password, QStringLiteral("fresh-2"));
        bool correct = false; page.runJavaScript("lastPath === '/nodes/node1/qemu/101/spiceproxy'", [&](auto v) { correct = v.toBool(); });
        QTRY_VERIFY(correct);
        bool denied = false;
        page.runJavaScript("Proxmox.Utils.API2Request = r => r.failure()", [&](auto) {
            source([&](auto config, QString error) { denied = !config && error.contains("login"); });
        });
        QTRY_VERIFY(denied);
        bool failed = false;
        bridge.request("/nodes/node1/qemu/101/spiceproxy", "{}", QUrl("https://other.invalid"), [&](auto config, QString error) { failed = !config && !error.isEmpty(); });
        QVERIFY(failed);
    }
};
QTEST_MAIN(TicketBridgeTest)
#include "ticketbridge_test.moc"
