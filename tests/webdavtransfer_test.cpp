#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <QtSpice/QSpiceSession.h>
#include <QtTest>
#include <spice-server/spice.h>
#include <spice/enums.h>
#include <spice/protocol.h>
struct SpiceTimer {
    QTimer timer;
};
struct SpiceWatch {
    QSocketNotifier *r, *w;
};
static QByteArray request, response, httpResponse, block;
static SpiceCharDeviceInstance device;
static int runTransfer(bool tls) {
    QEventLoop app;
    request.clear();
    response.clear();
    httpResponse.clear();
    QTemporaryDir dir;
    QFile file(dir.filePath("bulk.bin"));
    if (!file.open(QIODevice::WriteOnly))
        return 4;
    block.resize(1024 * 1024);
    quint32 seed = 123456;
    for (auto &ch : block) {
        seed = 1664525 * seed + 1013904223;
        ch = char(seed >> 24);
    }
    for (int i = 0; i < 64; i++)
        file.write(block);
    file.close();
    SpiceCoreInterface core{};
    core.base = {SPICE_INTERFACE_CORE, "probe", SPICE_INTERFACE_CORE_MAJOR,
                 SPICE_INTERFACE_CORE_MINOR};
    core.timer_add = [](SpiceTimerFunc fn, void *data) {
        auto *t = new SpiceTimer;
        t->timer.setSingleShot(true);
        QObject::connect(&t->timer, &QTimer::timeout, [fn, data] { fn(data); });
        return t;
    };
    core.timer_start = [](SpiceTimer *t, uint32_t ms) { t->timer.start(ms); };
    core.timer_cancel = [](SpiceTimer *t) { t->timer.stop(); };
    core.timer_remove = [](SpiceTimer *t) { delete t; };
    core.watch_update_mask = [](SpiceWatch *w, int mask) {
        w->r->setEnabled(mask & SPICE_WATCH_EVENT_READ);
        w->w->setEnabled(mask & SPICE_WATCH_EVENT_WRITE);
    };
    core.watch_add = [](int fd, int mask, SpiceWatchFunc fn, void *data) {
        auto *w =
            new SpiceWatch{new QSocketNotifier(fd, QSocketNotifier::Read),
                           new QSocketNotifier(fd, QSocketNotifier::Write)};
        w->r->setEnabled(mask & 1);
        w->w->setEnabled(mask & 2);
        QObject::connect(w->r, &QSocketNotifier::activated,
                         [fn, fd, data] { fn(fd, 1, data); });
        QObject::connect(w->w, &QSocketNotifier::activated,
                         [fn, fd, data] { fn(fd, 2, data); });
        return w;
    };
    core.watch_remove = [](SpiceWatch *w) {
        w->r->setEnabled(false);
        w->w->setEnabled(false);
        w->r->deleteLater();
        w->w->deleteLater();
        delete w;
    };
    core.channel_event = [](int, SpiceChannelEventInfo *) {};
    auto *server = spice_server_new();
    QTcpServer port;
    port.listen(QHostAddress::LocalHost);
    int number = port.serverPort();
    port.close();
    const QByteArray cert = QFile::encodeName(dir.filePath("cert.pem"));
    const QByteArray key = QFile::encodeName(dir.filePath("key.pem"));
    if (tls) {
        QProcess openssl;
        openssl.start(QStandardPaths::findExecutable("openssl"),
                      {"req", "-x509", "-newkey", "rsa:2048", "-nodes",
                       "-keyout", QString::fromLocal8Bit(key), "-out",
                       QString::fromLocal8Bit(cert), "-subj", "/CN=localhost",
                       "-days", "1"});
        if (!openssl.waitForFinished(30000) || openssl.exitCode() != 0)
            return 5;
        spice_server_set_tls(server, number, cert.constData(), cert.constData(),
                             key.constData(), nullptr, nullptr, nullptr);
    } else
        spice_server_set_port(server, number);
    spice_server_set_addr(server, "127.0.0.1", 0);
    spice_server_set_noauth(server);
    if (spice_server_init(server, &core))
        return 2;
    fprintf(stderr, "server initialized\n");
    SpiceCharDeviceInterface iface{};
    iface.base = {SPICE_INTERFACE_CHAR_DEVICE, "probe",
                  SPICE_INTERFACE_CHAR_DEVICE_MAJOR,
                  SPICE_INTERFACE_CHAR_DEVICE_MINOR};
    iface.state = [](SpiceCharDeviceInstance *s, int connected) {
        qInfo() << "device connected" << connected;
        if (connected)
            spice_server_port_event(s, SPICE_PORT_EVENT_OPENED);
    };
    iface.read = [](SpiceCharDeviceInstance *, uint8_t *buf, int len) {
        int n = qMin(len, int(request.size()));
        memcpy(buf, request.constData(), n);
        request.remove(0, n);
        return n;
    };
    iface.write = [](SpiceCharDeviceInstance *, const uint8_t *buf, int len) {
        response.append(reinterpret_cast<const char *>(buf), len);
        while (response.size() >= 10) {
            quint16 n = qFromLittleEndian<quint16>(response.constData() + 8);
            if (response.size() < 10 + n)
                break;
            httpResponse.append(response.constData() + 10, n);
            response.remove(0, 10 + n);
        }
        return len;
    };
    device.base.sif = &iface.base;
    device.subtype = "port";
    device.portname = "org.spice-space.webdav.0";
    spice_server_add_interface(server, &device.base);
    QtSpice::Session session;
    QObject::connect(&session, &QtSpice::Session::sharingChanged, [&] {
        qInfo() << "sharing" << int(session.sharingState())
                << session.sharingError();
    });
    QtSpice::ConnectionConfig cfg;
    cfg.host = "127.0.0.1";
    if (tls) {
        cfg.tlsPort = number;
        QFile ca(QString::fromLocal8Bit(cert));
        if (!ca.open(QIODevice::ReadOnly))
            return 6;
        cfg.caPem = ca.readAll();
        cfg.certificateSubject = "CN=localhost";
    } else
        cfg.port = number;
    cfg.sharedDirectory = QtSpice::SharedDirectory{
        dir.path(), QtSpice::SharedDirectoryAccess::ReadOnly};
    session.connectToConfig(cfg);
    QTimer::singleShot(1500, &app, [&] {
        QByteArray http = "GET /bulk.bin HTTP/1.1\r\nHost: "
                          "localhost\r\nConnection: close\r\n\r\n";
        qint64 id = qToLittleEndian<qint64>(1);
        quint16 n = qToLittleEndian<quint16>(http.size());
        request.append(reinterpret_cast<char *>(&id), 8);
        request.append(reinterpret_cast<char *>(&n), 2);
        request.append(http);
        spice_server_char_device_wakeup(&device);
    });
    QTimer monitor;
    monitor.start(1000);
    QObject::connect(&monitor, &QTimer::timeout, [&] {
        fprintf(stderr, "received HTTP bytes %lld\n",
                (long long)httpResponse.size());
        int end = httpResponse.indexOf("\r\n\r\n");
        if (end >= 0) {
            QByteArray body;
            int offset = end + 4;
            bool complete = false;
            while (true) {
                int e = httpResponse.indexOf("\r\n", offset);
                if (e < 0)
                    break;
                bool ok = false;
                quint64 n =
                    httpResponse.mid(offset, e - offset).toULongLong(&ok, 16);
                if (!ok) {
                    fprintf(stderr, "not chunked header: %s\n",
                            httpResponse.left(end).constData());
                    break;
                }
                offset = e + 2;
                if (n == 0) {
                    complete = httpResponse.mid(offset, 2) == "\r\n";
                    if (!complete)
                        fprintf(stderr,
                                "missing terminal CRLF; remaining %lld bytes\n",
                                (long long)(httpResponse.size() - offset));
                    break;
                }
                if (httpResponse.size() - offset < 2 ||
                    n > quint64(httpResponse.size() - offset - 2))
                    break;
                if (httpResponse.mid(offset + n, 2) != "\r\n") {
                    app.exit(7);
                    return;
                }
                body.append(httpResponse.constData() + offset, n);
                offset += n + 2;
            }
            if (complete) {
                bool valid = body == block.repeated(64);
                fprintf(stderr,
                        "complete body bytes %lld content verified %d\n",
                        (long long)body.size(), valid);
                app.exit(valid ? 0 : 3);
            }
        }
    });
    QTimer::singleShot(15000, &app, [&] {
        qInfo() << "timeout";
        app.exit(1);
    });
    int result = app.exec();
    fprintf(stderr, "result %d wire %lld tail %s\n", result,
            (long long)response.size(),
            httpResponse.right(12).toHex().constData());
    session.disconnectFromHost();
    spice_server_destroy(server);
    return result;
}

class WebdavTransferTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void transfer_data() {
        QTest::addColumn<bool>("tls");
        QTest::newRow("plain") << false;
        QTest::newRow("TLS") << true;
    }
    void transfer() {
        QFETCH(bool, tls);
        if (tls && QStandardPaths::findExecutable("openssl").isEmpty())
            QSKIP("openssl is required for ephemeral TLS credentials");
        QCOMPARE(runTransfer(tls), 0);
    }
};
QTEST_GUILESS_MAIN(WebdavTransferTest)
#include "webdavtransfer_test.moc"
