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
```

Display and input are implemented for the primary monitor with software rendering. Keyboard mapping currently covers standard US keys; live server validation and broader layouts remain to be done. Connection files and the Proxmox viewer follow in later milestones.
