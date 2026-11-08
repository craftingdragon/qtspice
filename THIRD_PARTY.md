# Dependencies and provenance

QtSpice-owned code is LGPL-3.0-or-later. Dependency terms remain separate; this file
is an inventory, not a replacement for upstream notices or distribution obligations.
No dependency binaries are distributed by the source-only CI workflow.

| Component | Use | Upstream license information |
| --- | --- | --- |
| Qt 6 | Widgets, networking, multimedia, optional WebEngine | https://doc.qt.io/qt-6/licensing.html |
| spice-client-glib / spice-gtk | Native protocol client (no GTK widgets linked) | https://gitlab.freedesktop.org/spice/spice-gtk |
| FFmpeg libavutil/libswresample | PCM conversion | https://ffmpeg.org/legal.html |
| GLib/GIO | Native event and transport integration | https://gitlab.gnome.org/GNOME/glib |
| Wayland/protocols, xkbcommon, Xlib | Optional native input integration | Preserve the licenses shipped with the installed packages and generated protocol sources |
| libsoup | Optional pinned WebDAV workaround | third_party/libsoup/COPYING and upstream source headers |

Qt and FFmpeg licensing depends on the modules and build configuration. Check the
licenses of the actual packages when distributing binaries; the QtSpice license
does not relicense them. Generated Wayland code retains protocol notices.

The optional libsoup submodule is upstream code under its own notices. The patch
in patches/ retains upstream licensing and attribution; see patches/README.md.
Do not apply QtSpice SPDX headers to upstream sources or the upstream-derived patch.

Clipboard test images are synthetic gradients generated with ImageMagick; see
tests/data/clipboard/README.md. They contain no external artwork. Protocol test
credentials and endpoints are synthetic fixtures, not deployment configuration.

Rocky CI additionally builds upstream spice-gtk 0.42 and phodav 2.5 from
checksum-verified archives. Their LGPL notices remain installed with those
container dependencies; URLs and build configuration are in ci/install-deps.sh.
