# QtSpice

Qt 6 SPICE client library based on `spice-client-glib-2.0`, without GTK widgets.

## Build

Requires a C++20 compiler, CMake 3.24+, Qt 6 Core and Test, pkg-config, and `spice-client-glib-2.0` development files.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The first milestone provides a session core. Display, input, connection files, and viewer examples follow in later milestones.
