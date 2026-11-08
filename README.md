# QtSpice

Qt 6 Widgets SPICE client library using `spice-client-glib-2.0`, without GTK widgets.
Includes a direct/`.vv` viewer, a Proxmox browser viewer, and a minimal embedding example.

Features include multiple monitors, keyboard capture, clipboard text/images, audio
playback and microphone forwarding, USB and CD/DVD redirection, file sending,
shared folders, proxies, snapshots, and manual reconnect.

## Build

Requires C++20, CMake 3.24+, pkg-config, Qt 6.5+ Core/Gui/Widgets/Network/Multimedia,
spice-client-glib-2.0 0.35+, and FFmpeg libswresample 4+ / libavutil 57+ development
packages. Default builds also need Qt Test and Qt 6.8+ WebEngineWidgets/WebChannel.

```sh
cmake -S . -B build
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
./build/simple-viewer spice://localhost:5900
./build/proxmox-viewer https://proxmox.example:8006
```

Set `QTSPICE_BUILD_PROXMOX=OFF` to omit WebEngine, `QTSPICE_BUILD_EXAMPLES=OFF`
to omit simple/embedded viewers, and `BUILD_TESTING=OFF` to omit Qt Test.
Native Wayland capture additionally uses Qt GuiPrivate headers, wayland-client,
wayland-protocols, wayland-scanner, and xkbcommon. X11 lock synchronization uses Xlib.

## Documentation

- [Embedding and widget API](docs/embedding.md)
- [Viewer usage and troubleshooting](docs/viewers.md)
- [Development and local CI with act](CONTRIBUTING.md)
- [Dependency notices](THIRD_PARTY.md)
- [Security reporting](SECURITY.md)

## Status and license

QtSpice is pre-1.0: public API and ABI stability are not guaranteed. Linux is the
validation target: Rocky Linux 9 with CRB/EPEL, Debian 13, and current Arch Linux.
Container tests cover builds and simulated behavior; physical USB/audio devices,
Wayland compositor permissions, and real guest integrations require manual testing.
Other operating systems are not currently validated.

QtSpice-owned code, examples, scripts, tests, and documentation are licensed under
**LGPL-3.0-or-later**. See [COPYING.LESSER](COPYING.LESSER) and [COPYING](COPYING).
Dependencies retain their own licenses. The optional libsoup submodule and patch
retain upstream licensing; ordinary builds do not require initializing it.

Rocky 9 CI builds checksum-pinned spice-gtk 0.42 (GTK disabled) and phodav 2.5
under /opt/qtspice-native because the native SPICE client is absent from its
enabled repositories. Qt and FFmpeg remain distribution packages. The setup script
records source URLs, checksums, and build options; Debian and Arch use packaged SPICE.
