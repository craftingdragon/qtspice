# QtSpice

Qt 6 SPICE client library based on `spice-client-glib-2.0`, without GTK widgets.

## Build

Requires a C++20 compiler, CMake 3.24+, Qt 6 Core, Gui, Widgets, Test, Multimedia, WebEngineWidgets and Network, pkg-config, and `spice-client-glib-2.0` development files. Native Wayland relative pointer capture is built when Qt 6.5+, Qt GuiPrivate headers, `wayland-client`, `wayland-protocols`, and `wayland-scanner` are available.

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

Display and input are implemented for the primary monitor with software rendering. A Proxmox `.vv` connection rendered a Rocky Linux login screen in live validation. Keyboard and client mode pointer input have been validated with Proxmox. Keyboard mapping currently covers standard US keys; broader layouts remain to be done.
When the guest agent supports clipboard sharing, the viewers synchronize plain UTF-8 text with the host clipboard in both directions. Transfers larger than 4 MiB, images, files, and the primary selection are not supported. The host clipboard is preserved when the console disconnects.
Both viewers offer **View → Auto Resize Guest**. It starts enabled, is remembered separately by each viewer, and adjusts the guest's primary display after window resizing when the guest agent and guest display stack apply SPICE monitor configuration. Turn it off to keep the guest's current resolution; fitted display scaling still works. Live validation with a Proxmox guest confirmed resizing under GNOME Mutter X11. With the same guest running GNOME Mutter Wayland, QtSpice submitted the monitor configuration request but the resolution did not change; this depends on the guest compositor, graphics driver, and agent support.
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
