#pragma once

#include <QtSpice/QSpiceSession.h>
#include <functional>
#include <glib.h>
#include <spice-client.h>
#include <QSet>
#include "tunnelproxy.h"
#include "qspiceaudio.h"
#include "qspiceusb.h"
#include "filetransfer.h"
#include "intervalstats.h"
#include "keyboardcommands.h"
#include "clipboardtransfer.h"

struct QtSpice::Session::Worker {
    explicit Worker(Session *owner);
    ~Worker();
    quint64 generation = 0;
    void post(std::function<void()> task);
    void connectToConfig(const ConnectionConfig &config);
    void disconnect();
    static void onChannelNew(SpiceSession *, SpiceChannel *, gpointer);
    static void onChannelDestroy(SpiceSession *, SpiceChannel *, gpointer);
    static void onChannelEvent(SpiceChannel *, SpiceChannelEvent, gpointer);
    IntervalStats diagnostics;
    quint64 pendingFrames = 0;
    AudioPlayback audio;
    UsbController usb;
    FileTransferController transfers;
    std::unique_ptr<QtSpice::TunnelProxy> proxy;

    QSet<SpiceChannel *> webdav;
    QSet<SpiceChannel *> webdavOpened;
    int sharingGeneration = 0;
    void applySharing();
    Session *owner;
    SpiceSession *session = nullptr;
    SpiceDisplayChannel *display = nullptr;
    QMap<int, SpiceDisplayChannel *> displays;
    QMap<MonitorId, MonitorInfo> desiredMonitors;
    bool layoutPending = false;
    bool awaitingTopology = true;
    quint64 layoutSerial = 0;
    void refreshMonitors();
    void observeMonitorLayout(const QList<MonitorInfo> &observed);
    void renderMonitors(SpiceChannel *channel, QRect invalidated = {});
    SpiceInputsChannel *inputs = nullptr;
    bool inputsReady = false;
    QtSpice::KeyboardReadiness keyboardReadiness;
    SpiceCursorChannel *cursor = nullptr;
    SpiceMainChannel *main = nullptr;
    QSize desiredDisplaySize;
    QSize sentDisplaySize;
    bool mainOpened = false;
    bool clipboardReady = false;
    ClipboardTransfer clipboard;
    int displayId = 0;
    int lastX = 0;
    int lastY = 0;
    bool hasPointer = false;
    QSet<quint32> pressedKeys;
    QSet<int> pressedButtons;
    int buttonState = 0;
    void releaseInputs();
    void sendDisplaySize();
    static void onMouseUpdate(SpiceMainChannel *, gpointer);
    static void onAgentUpdate(SpiceMainChannel *, gpointer);
    static gboolean onClipboardGrab(SpiceMainChannel *, guint, gpointer, guint, gpointer);
    static void onClipboardData(SpiceMainChannel *, guint, guint, gpointer, guint, gpointer);
    static void onClipboardRelease(SpiceMainChannel *, guint, gpointer);
    static gboolean onClipboardRequest(SpiceMainChannel *, guint, guint, gpointer);
    static void onDisplayChanged(SpiceChannel *, gpointer);
    static void onInvalidate(SpiceChannel *, gint, gint, gint, gint, gpointer);
    static void onCursorSet(SpiceCursorChannel *, gint, gint, gint, gint, gpointer, gpointer);
    static void onCursorHide(SpiceCursorChannel *, gpointer);
};
