#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends ca-certificates curl git build-essential gcc-12 g++-12 pkg-config python3-pip python3-venv python3-dev patchelf dpkg-dev binutils xz-utils zstd desktop-file-utils libssl-dev libx11-dev libxtst-dev libxkbfile-dev libxrandr-dev libxinerama-dev libxi-dev libxext-dev libxkbcommon-dev libxkbcommon-x11-dev libxkbregistry-dev libxcb-cursor-dev libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-render-util0-dev libxcb-xinerama0-dev libxcb-shape0-dev libxcb-util-dev libxcb-randr0-dev libxcb-xfixes0-dev libxcb-sync-dev libxcb-render0-dev libxcb-shm0-dev libgl1-mesa-dev libegl1-mesa-dev libfontconfig1-dev libfreetype6-dev libdbus-1-dev libglib2.0-dev libgirepository1.0-dev libgtk-3-dev libwayland-dev wayland-protocols libevdev-dev libsystemd-dev libgudev-1.0-dev libxml2-dev libyaml-dev libtool autoconf automake doxygen graphviz python3-jinja2 libxtst6 xvfb xauth dbus-x11
python3 -m venv /work/venv
/work/venv/bin/pip install --disable-pip-version-check --index-url "${DESKBRIDGE_PYPI_INDEX:-https://pypi.org/simple}" aqtinstall==3.3.0 cmake==3.30.5 meson==1.7.2 ninja==1.11.1.1 Jinja2==3.1.6
/work/venv/bin/aqt list-qt linux desktop --arch 6.8.3
/work/venv/bin/python3 /job/recipe/install-qt.py install-qt linux desktop 6.8.3 linux_gcc_64 --outputdir /work/Qt --archives qtbase qttools qttranslations qtsvg qtwayland icu qtdeclarative --base "${DESKBRIDGE_QT_MIRROR:-https://download.qt.io}"
