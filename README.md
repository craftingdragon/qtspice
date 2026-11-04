# QtSpice

Qt 6 SPICE client library based on `spice-client-glib-2.0`, without GTK widgets.

## Build

Requires a C++20 compiler, CMake 3.24+, Qt 6 Core, Gui, Widgets, Test, WebEngineWidgets and Network, pkg-config, and `spice-client-glib-2.0` development files. Native Wayland relative pointer capture is built when Qt 6.5+, Qt GuiPrivate headers, `wayland-client`, `wayland-protocols`, and `wayland-scanner` are available.

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
