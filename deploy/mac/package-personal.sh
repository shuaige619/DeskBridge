#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 DeskBridge personal fork contributors
# SPDX-License-Identifier: MIT
set -euo pipefail

if [[ ${1:-} == --help ]]; then
  printf 'Usage: %s [build-directory] [new-output.app]\n' "$0"
  printf 'Creates a self-contained local app, signs ad-hoc, and verifies it.\n'
  printf 'SIGNING_IDENTITY may select an existing signing identity instead.\n'
  printf 'Does not install, launch, notarize, alter quarantine, or change permissions.\n'
  exit 0
fi

[[ $(uname -s) == Darwin ]] || { printf 'This packaging script requires macOS.\n' >&2; exit 1; }
repo_dir=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=${1:-"$repo_dir/build/personal-mac"}
output_app=${2:-"$repo_dir/dist/DeskBridge-$(date +%Y%m%d-%H%M%S).app"}
[[ $output_app == *.app ]] || { printf 'Output must end in .app.\n' >&2; exit 1; }
[[ ! -e $output_app && ! -L $output_app ]] || { printf 'Refusing to overwrite existing output: %s\n' "$output_app" >&2; exit 1; }
[[ -f $build_dir/CMakeCache.txt ]] || { printf 'Missing CMake build directory: %s\n' "$build_dir" >&2; exit 1; }
build_dir=$(cd "$build_dir" && pwd)
grep -q '^DESKBRIDGE_PERSONAL_BUILD:BOOL=ON$' "$build_dir/CMakeCache.txt" || {
  printf 'Build directory is not a DeskBridge personal build.\n' >&2; exit 1;
}
deployqt=$(sed -n 's/^DEPLOYQT:FILEPATH=//p' "$build_dir/CMakeCache.txt")
[[ -x $deployqt ]] || { printf 'Cannot find the macdeployqt used to configure this build.\n' >&2; exit 1; }

# A fresh staging directory avoids changing build products or an installed app.
stage_dir=$(mktemp -d "$build_dir/personal-package.XXXXXX")
cmake --install "$build_dir" --prefix "$stage_dir"
stage_app="$stage_dir/DeskBridge.app"
[[ -x $stage_app/Contents/MacOS/DeskBridge && -x $stage_app/Contents/MacOS/deskflow-core ]] || {
  printf 'Expected GUI and core executables were not installed into %s\n' "$stage_app" >&2; exit 1;
}
cp "$repo_dir/README-PERSONAL.md" "$stage_app/Contents/Resources/README-PERSONAL.md"
ditto "$repo_dir/LICENSES" "$stage_app/Contents/Resources/LICENSES"
mkdir -p "$(dirname "$output_app")"
ditto "$stage_app" "$output_app"
output_app=$(cd "$(dirname "$output_app")" && pwd)/$(basename "$output_app")

# Include the separate core executable when scanning dependencies (notably
# OpenSSL), so neither executable relies on the build machine's Homebrew paths.
deploy_options=(
  "-executable=$output_app/Contents/MacOS/deskflow-core"
  -always-overwrite
  "-codesign=${SIGNING_IDENTITY:--}"
)
# Split Homebrew Qt modules (e.g. QtSvg used by the SVG image plugin) are
# siblings of qtbase, outside macdeployqt's default framework search path.
qt_opt_dirs=(/opt/homebrew/opt /usr/local/opt)
if [[ -n ${QT_PREFIX:-} ]]; then qt_opt_dirs+=("$(dirname "$QT_PREFIX")"); fi
svg_lib_dir=
for qt_opt_dir in "${qt_opt_dirs[@]}"; do
  for qt_lib_dir in "$qt_opt_dir"/qt*/lib; do
    [[ -d $qt_lib_dir ]] || continue
    if [[ -z $svg_lib_dir && -d $qt_lib_dir/QtSvg.framework ]]; then svg_lib_dir=$qt_lib_dir; fi
    for qt_framework in "$qt_lib_dir"/*.framework; do
      if [[ -d $qt_framework ]]; then
        deploy_options+=("-libpath=$qt_lib_dir")
        break
      fi
    done
  done
done
# Qt 6.12's @rpath resolver does not consult -libpath. Seed the SVG plugins
# before deployment and resolve their QtSvg load to its actual source location.
# macdeployqt then sees QtSvg as an ordinary framework dependency, copies it,
# rewrites these temporary source loads, and signs the finished bundle. Only
# copies inside this new output bundle are modified; installed Qt is untouched.
qtpaths="$(dirname "$deployqt")/qtpaths"
if [[ -n $svg_lib_dir && -x $qtpaths ]]; then
  qt_plugin_dir=$("$qtpaths" --query QT_INSTALL_PLUGINS)
  for svg_plugin in iconengines/libqsvgicon.dylib imageformats/libqsvg.dylib; do
    if [[ -f $qt_plugin_dir/$svg_plugin ]]; then
      plugin_copy="$output_app/Contents/PlugIns/$svg_plugin"
      mkdir -p "$(dirname "$plugin_copy")"
      ditto "$qt_plugin_dir/$svg_plugin" "$plugin_copy"
      while IFS= read -r svg_dependency; do
        case "$svg_dependency" in
          @rpath/QtSvg.framework/*)
            source_dependency="$svg_lib_dir/${svg_dependency#@rpath/}"
            [[ -f $source_dependency ]] || { printf 'Missing SVG dependency: %s\n' "$source_dependency" >&2; exit 1; }
            /usr/bin/install_name_tool -change "$svg_dependency" "$source_dependency" "$plugin_copy"
            ;;
        esac
      done < <(/usr/bin/otool -L "$plugin_copy" | sed '1d; s/^[[:space:]]*//; s/ (compatibility version.*//')
    fi
  done
fi
deploy_log="$stage_dir/macdeployqt.log"
if ! "$deployqt" "$output_app" "${deploy_options[@]}" 2>&1 | tee "$deploy_log"; then
  printf 'macdeployqt failed; see %s\n' "$deploy_log" >&2
  exit 1
fi
# Some Qt releases report deployment errors while returning success.
if grep -Eq '(^|[[:space:]])ERROR:' "$deploy_log"; then
  printf 'macdeployqt reported an error; see %s\n' "$deploy_log" >&2
  exit 1
fi

external_dependency=0
while IFS= read -r -d '' candidate; do
  if /usr/bin/file -b "$candidate" | grep -q 'Mach-O'; then
    while IFS= read -r dependency; do
      case "$dependency" in
        @*|/System/Library/*|/usr/lib/*|/Library/Apple/System/Library/*) ;;
        /*)
          printf 'Unbundled dependency in %s: %s\n' "$candidate" "$dependency" >&2
          external_dependency=1
          ;;
      esac
    # Inspect dependency load commands directly. `otool -L` also includes a
    # dylib's own LC_ID_DYLIB, which may retain an upstream absolute install ID
    # even when every actual load has been rewritten to the bundled library.
    done < <(/usr/bin/otool -l "$candidate" | awk '
      $1 == "cmd" {
        dependency = ($2 ~ /^LC_(LOAD_DYLIB|LOAD_WEAK_DYLIB|REEXPORT_DYLIB|LOAD_UPWARD_DYLIB|LAZY_LOAD_DYLIB)$/)
      }
      dependency && $1 == "name" {
        sub(/^[[:space:]]*name[[:space:]]+/, "")
        sub(/ \(offset [0-9]+\)$/, "")
        print
        dependency = 0
      }
    ')
  fi
done < <(find "$output_app/Contents" -type f -print0)
[[ $external_dependency == 0 ]] || { printf 'Dependency verification failed.\n' >&2; exit 1; }
/usr/bin/plutil -lint "$output_app/Contents/Info.plist"
/usr/bin/codesign --verify --deep --strict --verbose=2 "$output_app"
printf '\nPackaged and verified: %s\n' "$output_app"
printf 'Staged files retained for inspection: %s\n' "$stage_dir"
