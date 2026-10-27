#!/usr/bin/env bash
set -euo pipefail
export PATH=/work/venv/bin:$PATH
export QT_PREFIX=/work/Qt/6.8.3/gcc_64
export CMAKE_PREFIX_PATH=$QT_PREFIX
export DEPS_PREFIX=/work/deps
export PKG_CONFIG_PATH=$DEPS_PREFIX/lib/pkgconfig
export LD_LIBRARY_PATH=$QT_PREFIX/lib:$DEPS_PREFIX/lib
export DESKBRIDGE_INSTALL_PREFIX=/opt/deskbridge
export DESKBRIDGE_BUILD_JOBS=${DESKBRIDGE_BUILD_JOBS:-10}
mkdir -p /work/sources
if [[ ! -d /work/sources/libei ]]; then
  git clone --depth 1 --branch 1.6.0 https://gitlab.freedesktop.org/libinput/libei.git /work/sources/libei
fi
[[ $(git -C /work/sources/libei rev-parse HEAD) == 8a46bf3d4b6af7f25a61be53386cf618218114ef ]]
if [[ ! -d /work/sources/libportal ]]; then
  git clone --depth 1 --branch 0.9.1 https://github.com/flatpak/libportal.git /work/sources/libportal
fi
[[ $(git -C /work/sources/libportal rev-parse HEAD) == 8f5dc8d192f6e31dafe69e35219e3b707bde71ce ]]
if [[ ! -f /work/build-libei/build.ninja ]]; then
  meson setup /work/build-libei /work/sources/libei --prefix="$DEPS_PREFIX" --libdir=lib --buildtype=release -Ddocumentation=[] -Dtests=disabled -Dliboeffis=disabled -Dlibeis=disabled -Dlibei=enabled
fi
meson compile -C /work/build-libei -j6
meson install -C /work/build-libei
if [[ ! -f /work/build-libportal/build.ninja ]]; then
  meson setup /work/build-libportal /work/sources/libportal --prefix="$DEPS_PREFIX" --libdir=lib --buildtype=release -Ddocs=false -Dtests=false -Dportal-tests=false -Dvapi=false -Dintrospection=false -Dbackend-gtk3=disabled -Dbackend-gtk4=disabled -Dbackend-qt5=disabled -Dbackend-qt6=disabled
fi
meson compile -C /work/build-libportal -j6
meson install -C /work/build-libportal
if [[ ! -d /work/source ]]; then cp -a /job/source /work/source; fi
id deskbridge-build >/dev/null 2>&1 || useradd -m -s /bin/bash deskbridge-build
mkdir -p /work/build-deskbridge
chown -R deskbridge-build:deskbridge-build /work/source /work/build-deskbridge
cd /work/source
# Permission-denied regression cases require an ordinary user, not root.
runuser -u deskbridge-build -- env PATH="$PATH" QT_PREFIX="$QT_PREFIX" CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" PKG_CONFIG_PATH="$PKG_CONFIG_PATH" LD_LIBRARY_PATH="$LD_LIBRARY_PATH" DESKBRIDGE_INSTALL_PREFIX="$DESKBRIDGE_INSTALL_PREFIX" DESKBRIDGE_BUILD_JOBS="$DESKBRIDGE_BUILD_JOBS" ./deploy/linux/build-personal.sh --test /work/build-deskbridge -DBUILD_INSTALLER=ON -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12 -DGIT_SHA_SHORT=92fba665 -DCMAKE_INSTALL_LIBDIR=lib '-DCMAKE_C_FLAGS=-ffile-prefix-map=/work=/usr/src/deskbridge' '-DCMAKE_CXX_FLAGS=-ffile-prefix-map=/work=/usr/src/deskbridge'
DESTDIR=/work/stage cmake --install /work/build-deskbridge
