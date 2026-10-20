#!/bin/bash
# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
set -euo pipefail
if [[ ${1:-} == --help ]]; then
  printf 'Usage: THIRD_PARTY_LICENSE_DIR=/notices QT_PREFIX=/sdk %s [build-directory] [new-output-directory]\n' "$0"
  printf 'Creates arm64 DMG/ZIP, an Applications shortcut, installation guide and SHA256SUMS.\n'
  exit 0
fi
[[ $(uname -s) == Darwin ]] || { printf 'This script requires macOS.\n' >&2; exit 1; }
repo_dir=$(cd "$(dirname "$0")/../.." && pwd)
build_dir=${1:-"$repo_dir/build/release-macos-arm64"}
output_dir=${2:-"$repo_dir/dist/macos-release-$(date +%Y%m%d-%H%M%S)"}
: "${THIRD_PARTY_LICENSE_DIR:?Set THIRD_PARTY_LICENSE_DIR to the redistributed dependency notices}"
[[ ! -e $output_dir ]] || { printf 'Refusing to overwrite output: %s\n' "$output_dir" >&2; exit 1; }
mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd)
"$repo_dir/deploy/mac/package-personal.sh" "$build_dir" "$output_dir/DeskBridge.app"
python3 "$repo_dir/deploy/mac/verify-distribution.py" "$output_dir/DeskBridge.app" \
  --private-prefix "$HOME/" > "$output_dir/verification.json"
release_version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$output_dir/DeskBridge.app/Contents/Info.plist")
minimum_os=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$output_dir/DeskBridge.app/Contents/Info.plist")
stem="DeskBridge-$release_version-macos-arm64"
cat > "$output_dir/INSTALL-macOS.txt" <<EOF
DeskBridge $release_version — Apple Silicon, macOS $minimum_os or later

1. Verify SHA256SUMS, then open $stem.dmg.
2. Drag DeskBridge.app onto Applications. Quit the existing DeskBridge first
   when replacing it; its private settings are stored outside the app.
3. Open DeskBridge from Applications. This build has an ad-hoc signature and
   is not notarized. Follow macOS's normal Open / Privacy & Security approval
   flow if prompted. No security settings or quarantine flags are changed.
4. Grant the requested Accessibility and Input Monitoring permissions through
   System Settings. Configure the source/client role and verify TLS identity.
5. The homepage topology editor requires the matching DeskBridge source.

The ZIP contains the same app. Neither archive contains paired device names,
certificates, private keys, trust records, user settings or runtime logs.
Licenses and dependency source links are inside the app's Resources folder.
Source: https://github.com/shuaige619/DeskBridge/tree/v$release_version
EOF
image_root=$(mktemp -d "$build_dir/dmg-root.XXXXXX")
mount_dir=
cleanup() {
  if [[ -n $mount_dir ]]; then /usr/bin/hdiutil detach "$mount_dir" >/dev/null || true; fi
  rm -rf -- "$image_root"
}
trap cleanup EXIT
ditto "$output_dir/DeskBridge.app" "$image_root/DeskBridge.app"
ln -s /Applications "$image_root/Applications"
cp "$output_dir/INSTALL-macOS.txt" "$image_root/INSTALL.txt"
/usr/bin/hdiutil create -quiet -srcfolder "$image_root" -volname "DeskBridge $release_version" \
  -fs HFS+ -format UDZO "$output_dir/$stem.dmg"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$output_dir/DeskBridge.app" "$output_dir/$stem.zip"
mount_dir=$(mktemp -d "$build_dir/dmg-mount.XXXXXX")
/usr/bin/hdiutil attach -quiet -readonly -nobrowse -mountpoint "$mount_dir" "$output_dir/$stem.dmg"
[[ $(readlink "$mount_dir/Applications") == /Applications ]]
/usr/bin/codesign --verify --deep --strict "$mount_dir/DeskBridge.app"
/usr/bin/hdiutil detach -quiet "$mount_dir"
rmdir "$mount_dir"
mount_dir=
(cd "$output_dir" && /usr/bin/shasum -a 256 "$stem.dmg" "$stem.zip" > SHA256SUMS)
printf 'Verified distribution: %s\n' "$output_dir"
