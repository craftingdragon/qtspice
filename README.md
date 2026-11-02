# QtSpice

Qt 6 SPICE client library based on `spice-client-glib-2.0`, without GTK widgets.

## Build

Requires a C++20 compiler, CMake 3.24+, Qt 6 Core and Test, pkg-config, and `spice-client-glib-2.0` development files.

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

Display and input are implemented for the primary monitor with software rendering. A Proxmox `.vv` connection rendered a Rocky Linux login screen in live validation. Keyboard mapping currently covers standard US keys; broader layouts remain to be done. The Proxmox browser example follows in the next milestone.
The snapshot option saves the latest frame after two seconds and exits with a nonzero status if no frame arrived.

## Proxmox browser example

```sh
./build/proxmox-viewer https://your-proxmox-host:8006
```

Log in through the embedded browser and choose **Console → SPICE**. The example intercepts the downloaded `.vv` file and opens the console in a QtSpice window. Login cookies are persisted by its named WebEngine profile. For an overridable certificate warning, **Continue** remembers that exact certificate, origin, and warning type across restarts; a changed certificate or different Proxmox origin prompts again. The SPICE connection uses the CA and certificate subject from the `.vv` file.
