#pragma once

#include <QtSpice/QSpiceSession.h>
#include <functional>
#include <glib.h>
#include <spice-client.h>
#include <QSet>

struct QtSpice::Session::Worker {
    explicit Worker(Session *owner);
    ~Worker();
    void post(std::function<void()> task);
    void connectToConfig(const ConnectionConfig &config);
    void disconnect();
    static void onChannelNew(SpiceSession *, SpiceChannel *, gpointer);
    static void onChannelDestroy(SpiceSession *, SpiceChannel *, gpointer);
    static void onChannelEvent(SpiceChannel *, SpiceChannelEvent, gpointer);

    Session *owner;
    SpiceSession *session = nullptr;
    SpiceDisplayChannel *display = nullptr;
    SpiceInputsChannel *inputs = nullptr;
    bool inputsReady = false;
    SpiceCursorChannel *cursor = nullptr;
    SpiceMainChannel *main = nullptr;
    int displayId = 0;
    int lastX = 0;
    int lastY = 0;
    bool hasPointer = false;
    QSet<quint32> pressedKeys;
    QSet<int> pressedButtons;
    int buttonState = 0;
    void releaseInputs();
    static void onMouseUpdate(SpiceMainChannel *, gpointer);
    static void onDisplayChanged(SpiceChannel *, gpointer);
    static void onInvalidate(SpiceChannel *, gint, gint, gint, gint, gpointer);
    static void onCursorSet(SpiceCursorChannel *, gint, gint, gint, gint, gpointer, gpointer);
    static void onCursorHide(SpiceCursorChannel *, gpointer);
};
