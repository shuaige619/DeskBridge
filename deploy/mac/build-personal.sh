#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 DeskBridge personal fork contributors
# SPDX-License-Identifier: MIT
set -euo pipefail

if [[ ${1:-} == --help ]]; then
  printf 'Usage: %s [build-directory] [extra CMake configure arguments...]\n' "$0"
  printf 'Builds a local DeskBridge.app; does not package, install, or launch it.\n'
  exit 0
fi

[[ $(uname -s) == Darwin ]] || { printf 'This build script requires macOS.\n' >&2; exit 1; }
repo_dir=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=${1:-"$repo_dir/build/personal-mac"}
if [[ $# -gt 0 ]]; then shift; fi

# Homebrew's split Qt packages live in separate prefixes. Custom Qt installs can
# use QT_PREFIX / QTTOOLS_PREFIX, or supply their own CMAKE_PREFIX_PATH.
qt_prefix=${QT_PREFIX:-}
qttools_prefix=${QTTOOLS_PREFIX:-}
qttranslations_prefix=${QTTRANSLATIONS_PREFIX:-}
openssl_prefix=${OPENSSL_PREFIX:-}
for brew_prefix in /opt/homebrew /usr/local; do
  if [[ -z $qt_prefix && -d $brew_prefix/opt/qtbase ]]; then qt_prefix=$brew_prefix/opt/qtbase; fi
  if [[ -z $qt_prefix && -d $brew_prefix/opt/qt ]]; then qt_prefix=$brew_prefix/opt/qt; fi
  if [[ -z $qttools_prefix && -d $brew_prefix/opt/qttools ]]; then qttools_prefix=$brew_prefix/opt/qttools; fi
  if [[ -z $qttranslations_prefix && -d $brew_prefix/opt/qttranslations ]]; then qttranslations_prefix=$brew_prefix/opt/qttranslations; fi
  if [[ -z $openssl_prefix && -d $brew_prefix/opt/openssl@3 ]]; then openssl_prefix=$brew_prefix/opt/openssl@3; fi
done
export PATH="${qt_prefix:+$qt_prefix/bin:}${qttools_prefix:+$qttools_prefix/bin:}$PATH"
prefix_path=${CMAKE_PREFIX_PATH:-}
for prefix in "$qt_prefix" "$qttools_prefix" "$qttranslations_prefix" "$openssl_prefix"; do
  if [[ -n $prefix ]]; then prefix_path="${prefix_path:+$prefix_path;}$prefix"; fi
done

for command_name in cmake ninja macdeployqt; do
  command -v "$command_name" >/dev/null || { printf 'Missing dependency: %s\n' "$command_name" >&2; exit 1; }
done
configure_options=(
  -DCMAKE_BUILD_TYPE=Release
  "-DCMAKE_PREFIX_PATH=$prefix_path"
  "-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=$qttools_prefix"
  -DBUILD_OSX_BUNDLE=ON
  -DDESKBRIDGE_PERSONAL_BUILD=ON
  -DBUILD_INSTALLER=OFF
  -DBUILD_TESTS=OFF
  -DSKIP_BUILD_TESTS=ON
  -DAPPLE_CODESIGN_DEV:STRING=
)
for translation_dir in "$qttranslations_prefix/share/qt/translations" "$qt_prefix/translations" "$qt_prefix/share/qt/translations"; do
  if [[ -f $translation_dir/qtbase_en.qm ]]; then
    configure_options+=("-D_QT_QM_FILE:FILEPATH=$translation_dir/qtbase_en.qm")
    break
  fi
done
cmake -S "$repo_dir" -B "$build_dir" -G Ninja "${configure_options[@]}" "$@"
cmake --build "$build_dir" --parallel "${DESKBRIDGE_BUILD_JOBS:-$(sysctl -n hw.logicalcpu)}"
printf '\nBuilt: %s/bin/DeskBridge.app\n' "$build_dir"
printf 'Package with: %s/deploy/mac/package-personal.sh "%s"\n' "$repo_dir" "$build_dir"
