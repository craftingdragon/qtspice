#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
set -euo pipefail
case "${1:?Expected rocky9, debian13, or arch}" in
  rocky9)
    export PKG_CONFIG_PATH=/opt/qtspice-native/lib/pkgconfig
    export LD_LIBRARY_PATH=/opt/qtspice-native/lib
    dnf -y install dnf-plugins-core epel-release
    dnf config-manager --set-enabled crb
    dnf -y upgrade
    dnf -y install gcc-c++ cmake ninja-build git pkgconf-pkg-config qt6-qtbase-devel \
      qt6-qtbase-private-devel qt6-qtmultimedia-devel qt6-qtwebengine-devel qt6-qtwebchannel-devel \
      qt6-qtimageformats python3-pyparsing python3-six spice-protocol glib2-devel pixman-devel \
      openssl-devel libjpeg-turbo-devel zlib-devel json-glib-devel meson gstreamer1-devel \
      gstreamer1-plugins-base-devel libusb1-devel usbredir-devel libsoup-devel libxml2-devel gcc make \
      curl-minimal libswresample-free-devel libavutil-free-devel wayland-devel wayland-protocols-devel \
      libxkbcommon-devel libX11-devel xorg-x11-server-Xvfb dbus-x11 python3 xorg-x11-xauth findutils tar \
      gzip xz
    curl -fL --retry 3 https://download.gnome.org/sources/phodav/2.5/phodav-2.5.tar.xz -o /tmp/phodav.tar.xz
    echo '71f0a9cd70afd4dd1412a0298331dbb8ac71c0377f52117afc15eb88dc6fb910  /tmp/phodav.tar.xz' | sha256sum -c -
    tar -xf /tmp/phodav.tar.xz -C /tmp
    meson setup /tmp/phodav-build /tmp/phodav-2.5 --prefix=/opt/qtspice-native --libdir=lib \
      --wrap-mode=nodownload -Dgtk_doc=disabled -Davahi=disabled
    meson compile -C /tmp/phodav-build
    meson install -C /tmp/phodav-build
    curl -fL --retry 3 https://www.spice-space.org/download/gtk/spice-gtk-0.42.tar.xz -o /tmp/spice.tar.xz
    echo '9380117f1811ad1faa1812cb6602479b6290d4a0d8cc442d44427f7f6c0e7a58  /tmp/spice.tar.xz' | sha256sum -c -
    tar -xf /tmp/spice.tar.xz -C /tmp
    meson setup /tmp/spice-build /tmp/spice-gtk-0.42 --prefix=/opt/qtspice-native --libdir=lib \
      --wrap-mode=nodownload -Dgtk=disabled -Dintrospection=disabled -Dvapi=disabled -Dgtk_doc=disabled \
      -Dpolkit=disabled -Dlibcap-ng=disabled -Dsmartcard=disabled -Dwebdav=enabled -Dusbredir=enabled
    meson compile -C /tmp/spice-build
    meson install -C /tmp/spice-build
    ;;
  debian13)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends build-essential cmake ninja-build git pkg-config \
      qt6-base-dev qt6-base-private-dev qt6-multimedia-dev qt6-webengine-dev qt6-webchannel-dev \
      qt6-image-formats-plugins libspice-client-glib-2.0-dev libswresample-dev libavutil-dev \
      libwayland-dev wayland-protocols libxkbcommon-dev libx11-dev xvfb xauth dbus-x11 python3 \
      ca-certificates
    ;;
  arch)
    pacman -Syu --noconfirm --needed base-devel cmake ninja git pkgconf qt6-base qt6-multimedia \
      qt6-webengine qt6-webchannel qt6-imageformats spice-gtk ffmpeg wayland wayland-protocols \
      libxkbcommon libx11 xorg-server-xvfb xorg-xauth dbus python ca-certificates
    ;;
  *) exit 2 ;;
esac
useradd -m builder
