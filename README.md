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
