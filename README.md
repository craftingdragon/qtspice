# QtSpice

Qt 6 SPICE client library based on `spice-client-glib-2.0`, without GTK widgets.

## Build

Requires a C++20 compiler, CMake 3.24+, Qt 6 Core, Gui, Widgets, Test, Multimedia, WebEngineWidgets, WebChannel and Network, pkg-config, and `spice-client-glib-2.0` development files. Native Wayland relative pointer capture is built when Qt 6.5+, Qt GuiPrivate headers, `wayland-client`, `wayland-protocols`, and `wayland-scanner` are available.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The simple viewer connects to a direct SPICE endpoint:

```sh
./build/simple-viewer spice://localhost:5900
./build/simple-viewer /path/to/console.vv
./build/simple-viewer --snapshot /tmp/frame.png /path/to/console.vv
```

Display and input are implemented for guest monitors in separate windows with software rendering. A Proxmox `.vv` connection rendered a Rocky Linux login screen in live validation. Keyboard and client mode pointer input have been validated with Proxmox. Keyboard mapping currently covers standard US keys; broader layouts remain to be done.
When the guest agent supports clipboard sharing, both viewers synchronize UTF-8 text and images with the host clipboard in both directions. Mixed items retain both text and image content. SPICE PNG, BMP, TIFF, and JPEG representations are supported through installed Qt image codecs; other Qt-readable host image MIME formats (such as WebP) are converted for transport. Optional codecs are discovered at runtime. Transparent JPEG output is flattened onto white. Animated/multipage content uses the first frame/page.

Text is limited to 4 MiB, each encoded image to 64 MiB, and decoded image storage to 256 MiB (at most 64 megapixels at 32 bits per pixel). Image codecs run in bounded background pools; host images are encoded only when the guest requests a representation. Guest text and images are requested after a clipboard offer, with fallback between supported image formats. Unsupported or malformed content leaves the last usable clipboard intact; valid text from mixed items remains usable. File URLs are never opened, and file copying and primary selection remain unsupported. The host clipboard is preserved on disconnect. Event-based failures are logged under `qtspice.clipboard`; clipboard contents are never logged and no polling is added.
Both viewers offer **View → Auto Resize Guest**. It starts enabled, is remembered separately by each viewer, and adjusts each guest monitor after its window is resized when the guest agent and guest display stack apply SPICE monitor configuration. Turn it off to keep the guest's current resolution; fitted display scaling still works. Live validation with a Proxmox guest confirmed resizing under GNOME Mutter X11. With the same guest running GNOME Mutter Wayland, QtSpice submitted the monitor configuration request but the resolution did not change; this depends on the guest compositor, graphics driver, and agent support.
In server mouse mode, click the display to capture the pointer and press Ctrl+Alt+G to release it. Native Wayland capture uses the compositor's relative pointer and pointer constraints protocols. A compositor must advertise both protocols. The Wayland build uses Qt's GuiPrivate platform interface to obtain the window surface, so it must be rebuilt after a Qt version change.
The snapshot option saves the latest frame after two seconds and exits with a nonzero status if no frame arrived.

## Network proxies

Both viewers accept `--proxy` with an HTTP CONNECT or SOCKS5 URL:

```sh
./build/simple-viewer --proxy socks5://127.0.0.1:1080 /path/to/console.vv
./build/simple-viewer --proxy http://proxy.example:8080 spice://guest.internal:5900
./build/proxmox-viewer --proxy socks5://127.0.0.1:1080 https://pve.internal:8006
```

In the Proxmox viewer, the option applies to the embedded browser, downloads, and every console it opens. A `.vv` file's `proxy=` remains the SPICE proxy: QtSpice reaches it through the network proxy, then tunnels to the guest endpoint specified by the ticket. For example, SOCKS5 → Proxmox port 3128 → guest TLS port. Proxmox's `host=` contains a signed `pvespiceproxy:…` ticket; the bridge forwards it to the Proxmox HTTP proxy without interpreting it as a hostname or resolving it through SOCKS5. An unreachable or rejected proxy produces a connection error without falling back to a direct connection.

URLs may contain `user:password@` for HTTP Basic or SOCKS5 username/password authentication; percent-encode reserved characters in credentials (for example `%40` for `@`). Defaults are port 8080 for HTTP and 1080 for SOCKS5. Destination names are resolved by the proxy, including the Proxmox SPICE proxy hostname when chaining. Proxy settings are not saved by the viewers. `--proxy` overrides an optional QtSpice `network-proxy=` field in a `.vv` file; library users can set `ConnectionConfig::networkProxy`. `ConnectionConfig::proxy` and `.vv` `proxy=` also accept HTTP or SOCKS5 URLs.

The implementation uses an asynchronous loopback HTTP bridge and GIO proxy negotiation, including for authenticated SOCKS5 in WebEngine. SPICE retains its native TLS negotiation and certificate verification; browser HTTPS also passes through unchanged. Connections have a 30-second proxy negotiation timeout, and relay buffers are bounded. HTTP and SOCKS5 proxy authentication themselves are unencrypted; SPICE TLS and browser HTTPS protect the tunneled application traffic. UDP proxying, PAC, automatic system proxy configuration, and HTTPS-encrypted proxy endpoints are not implemented by `--proxy`.

The proxy Qt Tests use local fake proxies to cover authentication, remote hostname resolution, Proxmox chaining, socket cancellation, error handling, large transfers, and native SPICE TLS startup. The WebEngine test uses a temporary browser profile. In restricted runners these tests need permission to bind loopback sockets and start WebEngine. For a headless run:

```sh
QT_QPA_PLATFORM=offscreen QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu ctest --test-dir build --output-on-failure
```

## Proxmox browser example

```sh
./build/proxmox-viewer https://your-proxmox-host:8006
```

Log in through the embedded browser and choose **Console → SPICE**. The example intercepts the downloaded `.vv` file and opens the console in a QtSpice window. Login cookies are persisted by its named WebEngine profile. For an overridable certificate warning, **Continue** remembers that exact certificate, origin, and warning type across restarts; a changed certificate or different Proxmox origin prompts again. The SPICE connection uses the CA and certificate subject from the `.vv` file.

Inside each Proxmox SPICE console window, choose **View → Save Next Frame…** and select a PNG filename. The next nonempty frame received by that connection is saved at its original guest resolution, before widget scaling, without decorations or overlays. The action waits for a new frame; an idle guest may need a screen update. It captures even a black frame and can be used again after each save. Existing output files are replaced atomically. Success appears in the console status bar; save errors also open a warning dialog.

## Display scaling

Both viewers provide **View → Fullscreen** (**F11**) in the console window. Fullscreen uses the window's current screen, hides the menu/status bars, with no floating controls. Leaving restores the previous window geometry or maximized state and bar visibility. Each console controls fullscreen independently; it starts windowed.

When keyboard capture is active, F11 is sent to the guest. Press **Ctrl+Alt+Enter** (including keypad Enter) to exit fullscreen directly, even during keyboard capture. Alternatively, release input with **Ctrl+Alt+G**, then press **F11**. Entering or leaving fullscreen releases held inputs and suspends capture until the next console click, preserving the Capture Keyboard preference. Auto Resize Guest and the selected scaling mode continue to apply; disable Auto Resize Guest to keep a fixed guest resolution.

Both viewers offer **View → Show Menu at Top Edge**, disabled by default and remembered separately per viewer. In fullscreen, move the pointer to the top edge to slide the menu over the console without resizing the guest. Revealing the menu releases input capture; click the console to capture again. With relative pointer capture, release it with **Ctrl+Alt+G** before moving to the edge.

Both viewers offer **View → Scaling** (in each Proxmox SPICE console window):

- **Smooth Fit** is the default. It preserves aspect ratio and filters scaled frames for cleaner text and edges.
- **Unfiltered Fit** fits the desktop using nearest-neighbor scaling.
- **Native 1:1 Pixels** maps each guest pixel to one display pixel, accounting for the screen's device pixel ratio. Smaller desktops are centered; larger desktops have scrollbars. The mouse wheel over the desktop continues going to the guest; use the scrollbars to pan.

The selected mode is remembered separately for each viewer. Auto Resize Guest works in every scaling mode. Native mode requests the full widget size in physical display pixels, accounting for display scaling; disable Auto Resize Guest to retain a fixed guest resolution. Snapshots always retain the original guest pixels, independently of the display mode.

### WebDAV shared folders

Both viewers provide **Files → Shared Folder…** inside the SPICE console. Select a local folder, explicitly choose **Read-only** or **Read/write**, enable sharing, and press **Apply**. Read-only lets the guest browse and copy host files. Read/write also permits creating, modifying, and deleting files in the shared folder. **Close** dismisses unapplied edits; Apply takes effect immediately. Use the same dialog to change the folder/access or disable sharing. Changes close existing WebDAV connections, so finish file operations first. The dialog reports channel and guest-service readiness separately from the console connection status.

Sharing starts disabled and is not saved. Both viewers also accept an explicit folder/access pair:

```sh
./build/simple-viewer --shared-dir /path/to/folder --shared-dir-mode ro connection.vv
./build/proxmox-viewer --shared-dir /path/to/folder --shared-dir-mode rw https://proxmox.example:8006
```

Both arguments are required together. Proxmox CLI sharing applies to every SPICE console opened during that process; each console can then change or disable it independently. Downloaded `.vv` files cannot enable folder sharing. Library users can supply `ConnectionConfig::sharedDirectory`, call `Session::setSharedDirectory`, and observe `sharingChanged`, `sharingState()`, and `sharingError()`.

The VM must expose the `org.spice-space.webdav.0` SPICE virtio port, and the guest must run **spice-webdavd**. See the [upstream folder-sharing setup](https://spice.pages.freedesktop.org/spice-space/spice-user-manual.html#_folder_sharing). QtSpice does not modify VM settings. In a Linux guest, open the share through the file manager's network locations or `dav://localhost:9843`; Windows needs the SPICE WebDAV service and its drive mapping tools. “Active” means the channel and guest port are open, not that the guest filesystem is mounted. Missing channel/service support is reported in the dialog.

Transfers happen through the guest file manager, using libspice's WebDAV server and filesystem semantics (including its handling of symlinks and host permissions). No GTK widget library or separate public HTTP listener is added. This milestone does not implement drag-and-drop uploads or per-file progress reporting.

### Chromium diagnostics

The Proxmox viewer suppresses routine Chromium log output by default, including repeated TLS handshake diagnostics for self-signed certificates. Fatal Chromium diagnostics remain enabled. Certificate validation, certificate prompts, and QtSpice connection/error reporting continue normally.

To enable Chromium diagnostics for troubleshooting, supply an explicit logging setting before starting the viewer:

```sh
QTWEBENGINE_CHROMIUM_FLAGS='--enable-logging=stderr --log-level=0' ./build/proxmox-viewer https://proxmox.example:8006
```

Explicit logging switches in `QTWEBENGINE_CHROMIUM_FLAGS` override the quiet default; unrelated Chromium flags and proxy configuration are preserved. See [Qt WebEngine debugging documentation](https://doc.qt.io/qt-6/qtwebengine-debugging.html).

### Private libsoup fix for incomplete WebDAV downloads

On the tested libspice 0.42/libsoup 3.8.1 combination, a chunked download could deliver all file bytes but omit the final HTTP blank line. curl reported error 18 and Dolphin/KIO could pause near completion. The optional private build patches libsoup's asynchronous response close to finish the trailer on the main context. It does not change the guest or replace system packages.

Initialize the pinned dependency and build it (requires Git, Python 3, patch, Meson, Ninja, a C compiler, GLib development tools including `glib-mkenums`, and libsoup's development dependencies):

```sh
git submodule update --init third_party/libsoup
python3 scripts/build-private-libsoup.py
scripts/with-private-libsoup ./build/proxmox-viewer https://proxmox.example:8006
# Also works for simple-viewer:
scripts/with-private-libsoup ./build/simple-viewer connection.vv
```

The script stages patched sources in `build/libsoup-src`, runs upstream chunk-IO tests, and installs only into `build/libsoup-private`. It does not download Meson fallback dependencies. Ordinary CMake builds and viewer commands continue using system libraries. To roll back, launch the viewer without `scripts/with-private-libsoup`.

To run native protocol regression tests, additionally install `spice-server` development files and OpenSSL's command-line tool, then:

```sh
cmake -S . -B build -DQTSPICE_BUILD_WEBDAV_INTEGRATION_TESTS=ON
cmake --build build -j6
QT_QPA_PLATFORM=offscreen scripts/with-private-libsoup ctest --test-dir build --output-on-failure
```

These optional tests exercise local SPICE servers, TLS, exact chunk termination, and synthetic 64 MiB downloads. Running them against affected system libsoup is expected to fail. A custom build directory can be passed to the build script; set `QTSPICE_PRIVATE_LIBSOUP_PREFIX` to its `libsoup-private` directory when using the launcher. See [patch details](patches/README.md).

The example viewers route libspice's exact localized “resource temporarily unavailable” warning to `qtspice.transport` debug logging. This is the libspice 0.42 TLS backpressure diagnostic; normal operation retries the write. To inspect these messages, launch with `QT_LOGGING_RULES='qtspice.transport.debug=true'`. Other SPICE warnings, critical assertions (including duplicate cursor initialization), and connection errors remain visible. This policy applies to the example applications; embedding QtSpice does not install a global log handler.

## Audio playback

Guest audio plays through Qt Multimedia using the host default output device. Playback starts enabled, unmuted, at full local volume. Both viewers provide an **Audio** menu inside the console with Enable Audio, Mute, a volume slider, and playback status. Settings are remembered separately by each viewer; existing consoles remain independent. Local volume is combined with guest volume/mute without changing the guest mixer.

The VM needs a sound device configured to use SPICE audio and a working guest audio driver. Microphone forwarding is not implemented. Missing host devices or unsupported guest PCM formats produce an audio status error while the desktop remains connected. Output follows changes to the default device. Buffering is bounded; latency reporting is an estimate, not a guarantee of precise audio/video synchronization.

### Connection diagnostics

Connection lifecycle logging is quiet by default. Enable `qtspice.connection` to record session state changes, channel creation/destruction, named channel events (including migration switching), and native error domains, codes, and messages:

```sh
QT_LOGGING_RULES='qtspice.connection.debug=true' QT_MESSAGE_PATTERN='%{time yyyy-MM-dd hh:mm:ss.zzz} %{category}: %{message}' ./build/proxmox-viewer
```

Use the same environment variables with simple-viewer or the private-libsoup wrapper. Session object addresses distinguish concurrent consoles. Connection configuration, passwords, and proxy URLs are omitted; native error messages may still identify network endpoints. These logs diagnose disconnects; they do not measure bandwidth or change compression/reconnection behavior. To also inspect expected TLS backpressure messages in the example viewers, add `;qtspice.transport.debug=true` to `QT_LOGGING_RULES`.

Pass `--performance-stats` to either viewer for one sample per second per display session, widget, and proxy tunnel (including idle samples). Each sample reports its actual `interval-ms`; a longer interval can indicate an event-loop stall. `counts` reset each interval; `gauges` are current values. Collection and sampling timers are disabled by default, even if the performance logging category is enabled. The option enables that category at startup.

- Display: invalidations, copies, output frame bytes, total copy microseconds, delivered frames, and pending queued frames. Copies include monitor/primary notifications; delivered counts `frameReady` emission, including empty frames.
- Widget: received frame signals and paint events. Paints can merge several frames or result from window exposure; they are not a video FPS measurement. An attachment line maps widget addresses to session addresses.
- Proxy: upload/download forwarded and socket-written bytes, plus pending reads/writes in each direction. Down means remote to local client; up means the reverse. Written means accepted by the local socket transport, not acknowledged by the final server. Counters include tunnel protocol bytes, and tunnels may also belong to browser traffic. No proxy URL or ticket is logged.

To collect both connection and performance diagnostics:

```sh
QT_LOGGING_RULES='qtspice.connection.debug=true;qtspice.performance.debug=true' \
QT_MESSAGE_PATTERN='%{time yyyy-MM-dd hh:mm:ss.zzz} %{category}: %{message}' \
scripts/with-private-libsoup ./build/proxmox-viewer --performance-stats https://your-proxmox-host:8006
```

Compare pending proxy buffers with display invalidations/delivery during a freeze. Pending frames growing indicates queued delivery falling behind; invalidations stopping while audio continues indicates a display-path stall, but does not alone identify the remote server as the cause. Very long sample intervals indicate delayed event-loop execution. These diagnostics do not change buffering, rendering, or compression.

## Keyboard actions and lock keys

Both viewers provide **Input → Send Ctrl+Alt+Delete**, available once the guest inputs channel is ready. It releases held guest keys before sending the complete sequence; it has no local shortcut and also works from the fullscreen menu.

**Input → Synchronize Lock Keys** is enabled by default and remembered separately for each viewer. While the console has focus, it matches guest Caps Lock, Num Lock, and Scroll Lock to the known local states. It synchronizes on focus entry, connection readiness, capture resumption, and local lock changes. Capture suspension pauses synchronization until you click the console again. Disabling the option leaves ordinary lock-key forwarding enabled. Guest lock changes do not change the local keyboard LEDs.

Native Wayland reads the compositor's keymap/modifier events using libxkbcommon and Qt's existing display connection. X11 queries named XKB indicators on focus entry and lock-key events; XWayland reflects the state exposed by its X server. Only indicators known to the native backend are synchronized; unknown guest lock bits are preserved. Builds without native support keep normal key forwarding and explain synchronization unavailability in the action's tooltip. There is no polling timer.

Optional Linux build dependencies are `xkbcommon` plus the existing Qt GuiPrivate/wayland-client support for Wayland, and `libX11` with an X11-enabled Qt for X11. Synchronization diagnostics use `qtspice.input.debug=true`.

## Keyboard capture

Both viewers offer **Input → Capture Keyboard**, remembered separately per viewer and initially off. With capture enabled, focusing the console requests desktop shortcuts such as Meta/Super and Alt+Tab for the guest. A permanent status label distinguishes waiting, pending compositor approval, active, suspended, and unavailable/failed capture.

**Ctrl+Alt+G releases both keyboard and pointer capture**, releases held guest inputs, and clears console focus. The saved preference stays enabled, but capture remains suspended across focus changes until you click the console again. Turning the toggle off and on also clears suspension. Ordinary focus loss, menus/dialogs, hiding, or disconnecting releases capture; focus return normally resumes it.

Native Wayland uses the compositor keyboard-shortcuts-inhibit protocol with Qt's connection; no GTK widgets are used. The compositor may request permission, reject capture, reserve an escape shortcut, or revoke capture. Revoked inhibitors are not forcibly recreated. Native X11 uses Qt's native window keyboard grab. XWayland behavior remains subject to compositor policy. Unsupported backends retain ordinary input. This does not expand the existing keyboard scancode mapping to every multimedia/vendor key.

### Multiple monitors

Both viewers open separate windows for active guest monitors. **View → Monitors** enables and disables advertised heads. Closing an extra monitor window disables that guest monitor; closing the main console ends the session. Each window supports independent fullscreen and the existing capture-safe escape shortcuts. Move windows to local screens yourself; automatic screen assignment is not implemented.

Viewer configuration changes arrange enabled heads horizontally, top-aligned. Resizing one window preserves the other enabled heads. Requests wait for the guest agent, and an unconfirmed status appears if the guest does not apply the configuration within five seconds. Guest-originated topology changes are displayed without requiring a reconnect. Monitor topology and local window placement are not saved across sessions.

QXL must advertise enough heads and have enough video memory for all guest surfaces. Linux multi-head channel 0 and additional single-head display channels are supported; nonzero monitor surfaces and ambiguous overlapping SPICE display IDs are rejected. Actual resizing depends on the guest compositor/driver/agent, particularly with guest Wayland. Existing surface allocation and dimension limits still apply to the combined backing surface. One session shares clipboard, audio, and folder sharing across all its windows.

### Optional Linux X11 guest autoresize helper

Some QXL guests advertise the viewer-requested mode as preferred without the desktop applying it. The optional [spice-guest-autoresize.py](scripts/spice-guest-autoresize.py) runs **inside the guest**, as its graphical desktop user. It applies preferred modes to connected `Virtual-*` outputs, arranged horizontally, at startup and following DRM hotplug or XRandR events. It debounces events and waits without periodic polling. This is intended for Linux X11 guests such as Alpine with LXQt/Openbox, not Wayland guests or the host desktop.

Copy the script into the guest. On Alpine, install dependencies as root:

```sh
apk add python3 py3-xlib xrandr
```

Then install as the normal guest desktop user:

```sh
python3 spice-guest-autoresize.py --install
```

This copies the script to `~/.local/bin/` and writes an XDG autostart entry to `~/.config/autostart/` (or `$XDG_CONFIG_HOME/autostart/`). Log out and back in, or run `~/.local/bin/spice-guest-autoresize.py` in a terminal for immediate testing. `--once` applies the current layout and exits. Stop an already-running copy before updating; the lock prevents duplicate instances from managing the same session.

Avoid competing saved fixed-resolution LXQt monitor profiles. The helper deliberately manages all `Virtual-*` output sizes and positions, and leaves other output names alone. Keep the SPICE guest agent installed and running; this helper only applies display modes and does not replace the agent.

Logs are written to `~/.local/state/spice-guest-autoresize/helper.log` (or beneath `$XDG_STATE_HOME`). They include the process startup, DRM listener availability, applied modes and errors. If netlink subscription is unavailable, the helper logs the failure and continues with XRandR events only. Disable **SPICE guest automatic resize** in LXQt Session Settings → Autostart and stop its process to undo the automatic behavior.

The parser, installer and a simulated DRM-only event path are tested without an X server or Python-Xlib installed. Live guest startup and resizing still need validation for the guest's desktop/driver combination. Run the tests with `python3 tests/spice_guest_autoresize_test.py`; CTest includes them when a Python 3 interpreter is available.

### Reconnecting consoles

After a disconnect, use **Connection → Reconnect** or the button in the console. Each click makes one attempt; **Cancel Reconnect** stops it. Attempts time out after 30 seconds. There are no automatic retries. The disconnected controls also work in fullscreen.

Direct connections reuse their endpoint. For a local `.vv`, Reconnect rereads the original file; **Connection → Load Connection File…** selects a fresh ticket without restarting simple-viewer. CLI network proxy overrides and current folder-sharing choices are retained. The `--snapshot` command keeps its existing noninteractive behavior.

For QEMU consoles opened through the Proxmox browser, reconnect requests fresh credentials for the same VM through the browser's existing login. Keep the browser open on the original Proxmox server. If login expired, log in there and click Reconnect again. Unsupported pages and legacy downloaded consoles cannot refresh credentials this way. VM migration to another node requires reopening its console from Proxmox. Qt WebChannel is required for the Proxmox viewer.

Console windows retain geometry, fullscreen and input/scaling preferences during recovery. Previously requested monitor settings are restored for heads advertised by the reconnected guest; this still requires working guest-agent support. Closing an extra monitor while disconnected disables its restoration. Pending snapshots and held input are cleared; click the console to resume capture. Connection data and topology are not persisted by reconnect support.

### USB redirection

Both viewers provide **Devices → USB Devices…**, including in monitor windows
and the fullscreen edge menu. Select a device and choose Connect to transfer it
to the guest, or Disconnect to return it to the host. Redirected devices are
unavailable to host applications. Closing the dialog leaves redirection active.
Input devices are hidden by default; enable **Show input devices** to select
them. Already redirected input devices remain visible so they can be released.

The VM needs SPICE USB redirection channels, for example:

```xml
<redirdev bus="usb" type="spicevmc"/>
<redirdev bus="usb" type="spicevmc"/>
```

Each redirected device consumes a channel. The guest needs a USB controller and
its normal drivers for that device. The client requires a build of
`spice-client-glib-2.0` with USB redirection support, plus the distribution's USB
access/ACL helper and permission policy where required. Permission errors appear
in the device dialog; configure the distribution's device permissions rather
than running the viewer as root. No GTK widget library is used.

USB selection is manual: hotplugging and reconnecting never automatically attach
devices. Disconnecting the console releases devices. No selections are saved,
and device updates use native events rather than periodic polling. Linux is the
initial validation platform.
