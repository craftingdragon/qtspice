#pragma once

#include <QtSpice/QSpiceSession.h>
#include <functional>
#include <thread>
#include <glib.h>
#include <spice-client.h>

struct QtSpice::Session::Worker {
    explicit Worker(Session *owner);
    ~Worker();
    void post(std::function<void()> task);
    void connectToHost(const QString &host, quint16 port);
    void disconnect();
    static void onChannelNew(SpiceSession *, SpiceChannel *, gpointer);
    static void onChannelDestroy(SpiceSession *, SpiceChannel *, gpointer);
    static void onChannelEvent(SpiceChannel *, SpiceChannelEvent, gpointer);

    Session *owner;
    GMainContext *context;
    GMainLoop *loop;
    SpiceSession *session = nullptr;
    std::thread thread;
};
