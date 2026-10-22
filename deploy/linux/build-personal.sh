#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

if [[ ${1:-} == --help ]]; then
  printf 'Usage: %s [--test] [build-directory] [extra CMake configure arguments...]\n' "$0"
  printf 'Builds DeskBridge and its core on Linux; does not install or launch them.\n'
  printf -- '--test also runs isolated connection, UI, layout, protocol and hotkey regression tests.\n'
  exit 0
fi
[[ $(uname -s) == Linux ]] || { printf 'This build script requires Linux.\n' >&2; exit 1; }
run_tests=OFF
if [[ ${1:-} == --test ]]; then run_tests=ON; shift; fi
repo_dir=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=${1:-"$repo_dir/build/personal-linux"}
if [[ $# -gt 0 ]]; then shift; fi
for command_name in cmake ninja pkg-config c++; do
  command -v "$command_name" >/dev/null || { printf 'Missing dependency: %s\n' "$command_name" >&2; exit 1; }
done
for requirement in 'openssl >= 3.0' 'libei-1.0 >= 1.3' 'libportal >= 0.9.1' xkbcommon xkbregistry x11 xtst xkbfile; do
  pkg-config --exists "$requirement" || { printf 'Missing development dependency: %s\n' "$requirement" >&2; exit 1; }
done
prefix_path=${CMAKE_PREFIX_PATH:-}
if [[ -n ${QT_PREFIX:-} ]]; then prefix_path="${prefix_path:+$prefix_path;}$QT_PREFIX"; fi
cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_PREFIX_PATH=$prefix_path" \
  "-DCMAKE_INSTALL_PREFIX=${DESKBRIDGE_INSTALL_PREFIX:-$HOME/.local/opt/deskbridge}" \
  -DHELP2MAN:FILEPATH= \
  -DDESKBRIDGE_PERSONAL_BUILD=ON -DBUILD_INSTALLER=OFF \
  "-DBUILD_TESTS=$run_tests" -DSKIP_BUILD_TESTS=ON "$@"
build_dir=$(cd "$build_dir" && pwd)
build_targets=(DeskBridge deskflow-core app_translations)
regression_targets=(
  ScreenLayoutTests ScreenLayoutPersistenceTests CoreProcessStopTests
  ServerProxyTests ServerConfigTests ClientProxyTests PacketStreamFilterTests
  InputTimingMetricsTests ServerTests DeviceIllustrationTests IpcServerAccessTests
  SettingsTests TrayIconTests ShortcutRulesTests ShortcutMapperTests
  ShiftTapGestureTests ComputerShortcutTests AdvancedPageTests PreferenceChangesTests
  PreferencesApplyControllerTests PersonalDialogTests PersonalWindowTests
  ShortcutPageTests ChordRecorderTests ShortcutApplyControllerTests
  ConnectionOptionsDialogTests EditorDialogTests ServerSettingsDialogTests
  SupportDialogTests ConnectionIdentityTests ReceiverDeviceControlDialogTests
  ComputerTests HotkeySerializationTests
)
if [[ $run_tests == ON ]]; then
  build_targets+=("${regression_targets[@]}")
fi
cmake --build "$build_dir" --target "${build_targets[@]}" --parallel "${DESKBRIDGE_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
if [[ $run_tests == ON ]]; then
  command -v python3 >/dev/null || { printf 'Missing test dependency: python3\n' >&2; exit 1; }
  test_state=$(mktemp -d "${TMPDIR:-/tmp}/deskbridge-linux-tests.XXXXXX")
  trap 'rm -rf -- "$test_state"' EXIT
  mkdir -m 700 "$test_state/runtime"
  test_pattern=$(IFS='|'; printf '%s' "${regression_targets[*]}")
  XDG_CONFIG_HOME="$test_state/config" XDG_STATE_HOME="$test_state/state" \
    XDG_RUNTIME_DIR="$test_state/runtime" QT_QPA_PLATFORM=offscreen \
    ctest --test-dir "$build_dir/src/unittests" --no-tests=error \
    -R "^($test_pattern)$" --output-on-failure --output-junit "$build_dir/linux-regressions.xml"
  python3 -m unittest discover -s "$repo_dir/tools" -p 'test_prepare_personal_*.py' -v
fi
printf '\nBuilt: %s/bin/DeskBridge\nCore: %s/bin/deskflow-core\n' "$build_dir" "$build_dir"
printf 'Installation is a separate step; see README-LINUX.md.\n'
