#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
# SPDX-License-Identifier: MIT
set -euo pipefail
if [[ ${1:-} == --help || $# != 3 ]]; then
  printf 'Usage: DESKBRIDGE_DEPENDENCY_SOURCES=/sources %s prepared-flatpak-directory new-output-directory source-commit\n' "$0"
  printf 'Exports an already-built, tested sandbox. Does not install or launch the app.\n'
  exit 0
fi
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || { printf 'This recipe requires Linux x86_64.\n' >&2; exit 1; }
repo_dir=$(cd "$(dirname "$0")/../.." && pwd)
prepared=$(cd "$1" && pwd)
output_dir=$2
source_commit=$3
: "${DESKBRIDGE_DEPENDENCY_SOURCES:?Set the directory containing pinned libei and libportal sources}"
[[ $(git -C "$DESKBRIDGE_DEPENDENCY_SOURCES/libei" rev-parse HEAD) == 8a46bf3d4b6af7f25a61be53386cf618218114ef ]]
[[ $(git -C "$DESKBRIDGE_DEPENDENCY_SOURCES/libportal" rev-parse HEAD) == c23024018c8eb076549a1517fcb2d7f80d3e2ed5 ]]
[[ ! -e $output_dir ]] || { printf 'Refusing to overwrite output: %s\n' "$output_dir" >&2; exit 1; }
[[ -f $prepared/metadata && -x $prepared/files/libexec/deskbridge/DeskBridge && -x $prepared/files/libexec/deskbridge/deskflow-core ]]
mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd)
release_version=$(tr -d '\r\n' < "$repo_dir/VERSION")
export_dir="$output_dir/export"
mkdir -p "$export_dir/files/lib" "$export_dir/files/libexec" "$export_dir/files/share"
cp "$prepared/metadata" "$export_dir/metadata"
if [[ -d $prepared/export ]]; then cp -a "$prepared/export" "$export_dir/export"; fi
# Copy runtime payload explicitly, leaving build tools, Python packages,
# headers, pkg-config files, font caches and sandbox var data out of the bundle.
cp -a "$prepared/files/libexec/deskbridge" "$export_dir/files/libexec/"
if [[ -d $prepared/files/libexec/share ]]; then cp -a "$prepared/files/libexec/share" "$export_dir/files/libexec/"; fi
cp -a "$prepared/files/share/." "$export_dir/files/share/"
for library in "$prepared/files/lib"/libei.so* "$prepared/files/lib"/libeis.so* "$prepared/files/lib"/libportal.so* "$prepared/files/lib"/libportal-qt6.so*; do
  [[ -f $library ]] && cp -a "$library" "$export_dir/files/lib/"
done
license_dir="$export_dir/files/share/licenses/deskbridge"
mkdir -p "$license_dir/libei-1.6.0" "$license_dir/libportal-0.10.0" "$license_dir/project"
cp -a "$repo_dir/LICENSES/." "$license_dir/project/"
cp "$DESKBRIDGE_DEPENDENCY_SOURCES/libei/COPYING" "$license_dir/libei-1.6.0/COPYING"
cp "$DESKBRIDGE_DEPENDENCY_SOURCES/libportal/COPYING" "$license_dir/libportal-0.10.0/COPYING.LESSER"
gpl3_license=${DESKBRIDGE_GPL3_LICENSE:-/usr/share/common-licenses/GPL-3}
[[ -f $gpl3_license ]] || { printf 'Set DESKBRIDGE_GPL3_LICENSE to the full GNU GPL version 3 text.\n' >&2; exit 1; }
cp "$gpl3_license" "$license_dir/libportal-0.10.0/COPYING.GPL3"
cp "$repo_dir/README-LINUX.md" "$export_dir/files/share/README-DeskBridge-Linux.md"
cat > "$license_dir/THIRD-PARTY-NOTICES.md" <<EOF
# Redistributed components

DeskBridge $release_version, source commit $source_commit, derives from Deskflow.
Project source and license: https://github.com/shuaige619/DeskBridge
Upstream: https://github.com/deskflow/deskflow

Additional runtime libraries included in this app:
- libei/libeis 1.6.0, MIT. Exact COPYING under libei-1.6.0.
  Source commit: https://gitlab.freedesktop.org/libinput/libei/-/tree/8a46bf3d4b6af7f25a61be53386cf618218114ef
- libportal 0.10.0, LGPL 3.0. Exact COPYING and full GPL 3 under libportal-0.10.0.
  Source commit: https://github.com/flatpak/libportal/tree/c23024018c8eb076549a1517fcb2d7f80d3e2ed5
These libraries are dynamically linked and can be rebuilt/replaced from source.
No library source modifications were made for this package.

Qt 6.11.2, OpenSSL 3.5.7, GLib and system libraries are provided separately by
org.kde.Platform/x86_64/6.11; they are not copied into this app bundle.
KDE runtime source/license metadata: https://invent.kde.org/packaging/flatpak-kde-runtime
EOF
python3 - "$export_dir/files" "$HOME/" > "$output_dir/verification.json" <<'PY'
import json,re,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]);private=sys.argv[2].encode();problems=[];elf=[]
for p in root.rglob('*'):
 if p.is_symlink():
  if not p.resolve().is_relative_to(root):problems.append('External symlink: '+str(p.relative_to(root)))
  continue
 if not p.is_file():continue
 name=str(p.relative_to(root))
 if re.search(r'trusted-(clients|servers)|(?:DeskBridge|Deskflow)\.conf|\.(pem|key|p12|pfx|log)$',name,re.I):problems.append('Private filename: '+name)
 data=p.read_bytes()
 if data[:4]==b'\x7fELF':
  # Meson dependencies contain compiler working directories in debug sections.
  # Strip debug data from copies only, leaving build products untouched.
  subprocess.run(['strip','--strip-debug',str(p)],check=True)
  data=p.read_bytes();elf.append(name)
 if private in data:problems.append('Private build path: '+name)
print(json.dumps({'elfFiles':sorted(elf),'problems':problems},indent=2))
if problems:raise SystemExit(1)
PY
repository="$output_dir/repository"
bundle="$output_dir/DeskBridge-$release_version-linux-x86_64.flatpak"
flatpak build-export "$repository" "$export_dir" stable
flatpak build-bundle --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
  "$repository" "$bundle" local.shuai.deskbridge stable
cp "$repo_dir/README-LINUX.md" "$output_dir/INSTALL-Linux.md"
(cd "$output_dir" && sha256sum "$(basename "$bundle")" > SHA256SUMS)
printf 'Exported without installing: %s\n' "$bundle"
