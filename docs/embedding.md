# Embedding QtSpice in a Qt Widgets application

QtSpice provides a connection (`Session`) and a monitor view (`Widget`). One
session can supply several widgets. Both belong on the QApplication GUI thread;
run the Qt event loop. On Linux, SPICE dispatch uses Qt's GLib event dispatcher:
do not disable GLib integration or move the session to a worker thread.

## Build and run

Use C++20 and CMake 3.24 or newer. The library requires Qt 6.5+ Core, Gui,
Widgets, Network and Multimedia, spice-client-glib-2.0 0.35+, pkg-config, and
FFmpeg libswresample 4+ / libavutil 57+ development packages. Native Wayland
capture additionally needs the optional dependencies described in the README.
GTK widget libraries are not used.

For a project with a QtSpice checkout in `qtspice/`:

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyConsole LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(QTSPICE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QTSPICE_BUILD_PROXMOX OFF CACHE BOOL "" FORCE)
add_subdirectory(qtspice)
add_executable(my-console main.cpp)
target_link_libraries(my-console PRIVATE QtSpice::QtSpice)
```

Use [the complete minimal example](../examples/embedded-viewer/main.cpp) as
`main.cpp`. Run `my-console localhost 5900`. It embeds the widget without private
headers, native SPICE calls, or viewer helper classes. Its connection is plain
SPICE; use TLS configuration below for a remote secure endpoint.

Standalone builds enable `QTSPICE_BUILD_EXAMPLES` (simple and embedded viewers)
and `QTSPICE_BUILD_PROXMOX` (Proxmox viewer and, with testing enabled, WebEngine
tests). Both default off in a subdirectory. `BUILD_TESTING=OFF` removes the Qt
Test requirement; disabling Proxmox removes WebEngine and WebChannel requirements.
There is currently no installed `find_package(QtSpice)` package.

## Ownership, connection and errors

`Widget::setSession()` borrows its pointer; it never reparents or deletes the
session. `session()` returns that pointer. Attaching the same session does
nothing. `setSession(nullptr)` detaches and clears the view; destroying the
session also clears the view and emits `sessionChanged(nullptr)`. Detaching a
widget does not disconnect a session used by other views. A parent-owned session
as in the example is convenient; a separately owned session may also outlive all
its widgets. Attach before or after connection. The default monitor is `{0,0}`.

Connections are asynchronous. Observe `stateChanged` and `errorOccurred`; a
Connected main channel does not imply display, inputs, or agent readiness.
Disconnect explicitly using `disconnectFromHost()`. Call `connectToConfig()`
again when your application wants to reconnect. Do not log passwords or tickets.

```cpp
QtSpice::ConnectionConfig config;
config.host = QStringLiteral("guest.example");
config.tlsPort = 5901;
config.caPem = trustedCaBytes;
config.certificateSubject = expectedCertificateSubject;
// Optional outer tunnel; TLS still authenticates the SPICE endpoint.
config.networkProxy = QStringLiteral("socks5://127.0.0.1:1080");
session->connectToConfig(config);
```

Read a `.vv` file with QFile and pass its bytes to
`QtSpice::parseVv(bytes, &error)` from `<QtSpice/QSpiceConnection.h>`. Check the
returned optional before calling `connectToConfig(*config)`. The parsed
configuration carries TLS and SPICE proxy settings; `networkProxy` is an optional
outer tunnel. `parseProxyUrl()` validates explicit HTTP CONNECT or SOCKS5 URLs.

## Widget API reference

All properties use their named getter, setter and `...Changed` signal, except
read-only `keyboardCaptureState` and `lockSynchronizationAvailable`.
Notifications are emitted when values change, not on identical assignments.

| Property | Default | Meaning |
| --- | --- | --- |
| `session` | nullptr | Non-owned connection; `setSession(nullptr)` detaches |
| `monitor` | `{0,0}` | `MonitorId` is a display-channel ID and monitor ID, not a list index |
| `scalingMode` | `SmoothFit` | SmoothFit preserves aspect ratio with filtering; UnfilteredFit fits without smoothing; NativePixels uses unscaled pixels and scrollbars |
| `autoResizeGuest` | true | Debounced requests to resize this guest monitor to the visible view; requires agent/display-stack support |
| `keyboardCaptureEnabled` | false | Preference to inhibit local shortcuts while focused and active |
| `keyboardCaptureState` | Disabled | Actual capture outcome, independent of the preference |
| `synchronizeLockKeys` | true | Synchronize known host Caps/Num/Scroll Lock state on focus/readiness |
| `lockSynchronizationAvailable` | platform dependent | Whether host lock state can currently be read |

`KeyboardCaptureState` values are Disabled, WaitingForFocus, Pending, Active,
Suspended, Unavailable and Failed. An enabled preference does not guarantee that
the compositor supports or grants capture. `releaseInputCapture()` releases held
inputs, pointer and keyboard capture, and suspends capture until the next console
click. Ctrl+Alt+G does the same. Focus loss, hiding and disconnect also release
held inputs. Server mouse mode captures the pointer on click.

`guestResizeRequested(QSize)` reports the widget's resize request, not guest
confirmation. Scaling changes only the local view; automatic resize changes the
guest resolution if supported. Disable automatic resize for fixed-resolution
views, especially when showing the same monitor in multiple widgets. `sizeHint()`
is the cached frame size or 800×600 before a frame arrives.

## Multiple monitors and session features

Read `session->monitors()` initially and again on `monitorsChanged`. For each
wanted enabled entry, attach a widget to the same session and call
`setMonitor(info.id)`. The application owns window creation/removal. Do not assume
monitor IDs are consecutive. Actual topology comes from `monitors()`; monitor
configuration requests may remain unconfirmed or be ignored by the guest.

Attaching a widget enables the session's shared host clipboard bridge when the
agent is ready. Clipboard defaults to Bidirectional for text and images. Set
`session->setClipboardMode(Session::ClipboardMode::Disabled)` before attachment
if host clipboard sharing is unwanted. Other modes are HostToGuest and
GuestToHost. Removing the last view deactivates the bridge. Clipboard content is
preserved when disconnecting.

Playback, microphone, USB, file sending, CD sharing and shared directories are
Session APIs; widgets do not provide menus for them. Microphone recording needs
explicit consent while connected and resets on disconnect. Consult readiness and
error accessors before exposing actions. `diagnostics()` provides channel and
agent information; its monitor status is request status, not proof of application.

Menus, fullscreen, saved preferences, snapshots, automatic window management and
reconnect workflows belong to the embedding application. The full viewers show
examples of those policies; the library does not load viewer QSettings.
