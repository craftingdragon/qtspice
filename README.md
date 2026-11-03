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

## Proxmox browser example

```sh
./build/proxmox-viewer https://your-proxmox-host:8006
```

Log in through the embedded browser and choose **Console → SPICE**. The example intercepts the downloaded `.vv` file and opens the console in a QtSpice window. Login cookies are persisted by its named WebEngine profile. For an overridable certificate warning, **Continue** remembers that exact certificate, origin, and warning type across restarts; a changed certificate or different Proxmox origin prompts again. The SPICE connection uses the CA and certificate subject from the `.vv` file.
