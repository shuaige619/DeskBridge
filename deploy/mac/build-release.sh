#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
set -euo pipefail
if [[ ${1:-} == --help ]]; then
  printf 'Usage: QT_PREFIX=/sdk OPENSSL_PREFIX=/openssl %s [build-directory]\n' "$0"
  printf 'Builds arm64 release and isolated regression tests; no installation or launch.\n'
  exit 0
fi
repo_dir=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=${1:-"$repo_dir/build/release-macos-arm64"}
: "${QT_PREFIX:?Set QT_PREFIX to a complete Qt SDK}"
: "${OPENSSL_PREFIX:?Set OPENSSL_PREFIX to the private OpenSSL prefix}"
mkdir -p "$build_dir"
build_dir=$(cd "$build_dir" && pwd)
map_flags="-ffile-prefix-map=\"$repo_dir=/usr/src/deskbridge\" -ffile-prefix-map=\"$build_dir=/usr/src/deskbridge/build\""
QTTOOLS_PREFIX="$QT_PREFIX" QTTRANSLATIONS_PREFIX="$QT_PREFIX" \
  "$repo_dir/deploy/mac/build-personal.sh" "$build_dir" \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=14.4 \
  "-DOPENSSL_ROOT_DIR=$OPENSSL_PREFIX" -DOPENSSL_USE_STATIC_LIBS=ON \
  "-DOPENSSL_INCLUDE_DIR=$OPENSSL_PREFIX/include" \
  "-DOPENSSL_SSL_LIBRARY=$OPENSSL_PREFIX/lib/libssl.a" \
  "-DOPENSSL_CRYPTO_LIBRARY=$OPENSSL_PREFIX/lib/libcrypto.a" \
  "-DCMAKE_C_FLAGS=$map_flags" "-DCMAKE_CXX_FLAGS=$map_flags" \
  -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON
# These suites use isolated test settings and do not start the installed core.
tests=(ScreenLayoutTests ScreenLayoutPersistenceTests ServerConfigTests ServerTests
  ServerProxyTests ClientProxyTests CoreProcessStopTests PacketStreamFilterTests
  InputTimingMetricsTests DeviceIllustrationTests PersonalWindowTests PersonalDialogTests
  PreferenceChangesTests PreferencesApplyControllerTests AdvancedPageTests
  ShortcutRulesTests ShortcutMapperTests ShiftTapGestureTests ShortcutPageTests ChordRecorderTests
  ShortcutApplyControllerTests ComputerShortcutTests OSXShortcutMapperTests LoginStartupTests
  SettingsTests IpcServerAccessTests)
test_regex=$(IFS='|'; printf '%s' "${tests[*]}")
state_dir=$(mktemp -d "${TMPDIR:-/tmp}/deskbridge-release-tests.XXXXXX")
trap 'rm -rf -- "$state_dir"' EXIT
HOME="$state_dir" XDG_CONFIG_HOME="$state_dir/config" XDG_STATE_HOME="$state_dir/state" \
  QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir/src/unittests" \
  -R "^($test_regex)$" --no-tests=error --output-on-failure
