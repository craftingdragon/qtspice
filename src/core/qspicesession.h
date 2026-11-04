#pragma once

#include <QtSpice/QSpiceSession.h>
#include <functional>
#include <glib.h>
#include <spice-client.h>
#include <QSet>
#include "tunnelproxy.h"
#include "qspiceaudio.h"

struct QtSpice::Session::Worker {
    explicit Worker(Session *owner);
    ~Worker();
    void post(std::function<void()> task);
    void connectToConfig(const ConnectionConfig &config);
    void disconnect();
    static void onChannelNew(SpiceSession *, SpiceChannel *, gpointer);
    static void onChannelDestroy(SpiceSession *, SpiceChannel *, gpointer);
    static void onChannelEvent(SpiceChannel *, SpiceChannelEvent, gpointer);
    AudioPlayback audio;
    std::unique_ptr<QtSpice::TunnelProxy> proxy;

    QSet<SpiceChannel *> webdav;
    QSet<SpiceChannel *> webdavOpened;
    int sharingGeneration = 0;
    void applySharing();
    Session *owner;
    SpiceSession *session = nullptr;
    SpiceDisplayChannel *display = nullptr;
    SpiceInputsChannel *inputs = nullptr;
    bool inputsReady = false;
    SpiceCursorChannel *cursor = nullptr;
    SpiceMainChannel *main = nullptr;
    QSize desiredDisplaySize;
    QSize sentDisplaySize;
    bool mainOpened = false;
    bool clipboardReady = false;
    bool hostOwnsClipboard = false;
    bool guestClipboardRequestPending = false;
    QByteArray hostClipboard;
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
