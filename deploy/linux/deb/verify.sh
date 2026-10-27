#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
unset LD_LIBRARY_PATH LD_PRELOAD LD_AUDIT QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH PYTHONOPTIMIZE
package=/job/output/deskbridge_1.27.0.3-1~ubuntu22.04_amd64.deb
results=/job/verify-results
mkdir -p "$results"
chmod 777 "$results"
[[ $(dpkg --print-architecture) == amd64 ]]
. /etc/os-release
[[ $ID == ubuntu && $VERSION_ID == 22.04 ]]
apt-get update
# Do not install verification tools until the package has resolved on its own.
apt-get install -y --no-install-recommends "$package"
for tool in cmake qmake6 flatpak; do
    if command -v "$tool"; then
        printf 'FAIL: unexpected build/runtime manager: %s\n' "$tool" >&2
        exit 1
    fi
done
installed_packages=$(dpkg-query -W -f='${binary:Package}\t${db:Status-Status}\n')
if printf '%s\n' "$installed_packages" | grep -E '^(qt6-base-dev|libqt6core6|libportal-dev|libei-dev)(:[^[:space:]]+)?[[:space:]]+installed$' > /dev/null; then
    printf 'FAIL: system Qt or development dependency installed\n' >&2
    exit 1
fi
[[ -x /usr/bin/deskbridge && -x /opt/deskbridge/libexec/deskbridge/deskflow-core ]]
elf_count=0
: > "$results/ldd-before-tools.log"
while IFS= read -r -d '' path; do
    if [[ $(od -An -N4 -tx1 "$path" | tr -d ' \n') != 7f454c46 ]]; then continue; fi
    elf_count=$((elf_count + 1))
    if ! resolved=$(LC_ALL=C ldd "$path" 2>&1); then
        printf 'FAIL: ldd %s\n%s\n' "$path" "$resolved" >&2
        exit 1
    fi
    printf '%s\n%s\n\n' "$path" "$resolved" >> "$results/ldd-before-tools.log"
    if [[ $resolved == *'not found'* ]]; then
        printf 'FAIL: missing dependency before tools: %s\n%s\n' "$path" "$resolved" >&2
        exit 1
    fi
done < <(find /opt/deskbridge -type f -print0)
[[ $elf_count -ge 2 ]]
printf 'PASS: all %s packaged ELF files resolve using only the deb and its dependencies.\n' "$elf_count"

apt-get install -y --no-install-recommends python3 binutils xvfb xauth dbus-x11 x11-utils x11-apps xdotool desktop-file-utils fonts-noto-cjk
desktop-file-validate /usr/share/applications/local.shuai.deskbridge.desktop
python3 - <<'PY'
from pathlib import Path
import re
import subprocess

paths = []
private_names = {path.name for path in Path('/opt/deskbridge/lib').iterdir()}
for path in Path('/opt/deskbridge').rglob('*'):
    if path.is_symlink():
        assert path.exists(), f'Dangling packaged symlink: {path}'
        continue
    if not path.is_file():
        continue
    with path.open('rb') as stream:
        if stream.read(4) != b'\x7fELF':
            continue
    paths.append(path)
    header = subprocess.check_output(['readelf', '-h', str(path)], text=True)
    assert 'Advanced Micro Devices X86-64' in header, str(path)
    dynamic = subprocess.check_output(['readelf', '-d', str(path)], text=True)
    assert '/work/' not in dynamic and '/job/' not in dynamic, str(path)
    resolved = subprocess.check_output(['ldd', str(path)], text=True)
    assert 'not found' not in resolved, (str(path), resolved)
    for name, origin in re.findall(r'^\s*(\S+)\s+=>\s+(\S+)', resolved, re.MULTILINE):
        if name in private_names or name.startswith(('libQt6', 'libei.so', 'libportal.so')):
            assert Path(origin).resolve().is_relative_to('/opt/deskbridge'), (name, origin)
assert len(paths) >= 2, 'Missing packaged ELF binaries'
print(f'PASS: all {len(paths)} ELF files are amd64; private dependencies stay inside /opt/deskbridge.')
PY
useradd -m -s /bin/bash debtest
verify_versions() {
    runuser -u debtest -- python3 - <<'PY'
import os
import re
import subprocess

environment = dict(os.environ, QT_QPA_PLATFORM='offscreen')
for executable in ('/usr/bin/deskbridge', '/opt/deskbridge/libexec/deskbridge/deskflow-core'):
    output = subprocess.check_output([executable, '--version'], env=environment, text=True, stderr=subprocess.STDOUT)
    versions = re.findall(r'(?<![\w.])v?(\d+\.\d+\.\d+(?:\.\d+)?)(?![\w.])', output)
    assert versions and versions[0] == '1.27.0.3', (executable, output)
    print(output, end='' if output.endswith('\n') else '\n')
PY
}
verify_gui() {
    runuser -u debtest -- dbus-run-session -- xvfb-run -a -s '-screen 0 1280x900x24' python3 /job/recipe/verify_gui.py "$1"
}
verify_user_data() {
    runuser -u debtest -- python3 - <<'PY'
from pathlib import Path
import hashlib
import json
import os

manifest = json.loads(Path('/job/verify-results/user-data.json').read_text())
for name, expected in manifest.items():
    path = Path(name)
    assert path.is_file(), f'User data removed: {path}'
    assert path.stat().st_uid == os.getuid(), f'User data ownership changed: {path}'
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, f'User data changed: {path}'
print(f'PASS: {len(manifest)} user configuration, TLS and state files retain contents and ownership.')
PY
}
verify_versions
verify_gui initial
# Add harmless markers to the real profile after startup, preserving all defaults,
# including the application's auto-start settings and generated TLS identity.
runuser -u debtest -- python3 - <<'PY'
from pathlib import Path
import hashlib
import json
import os

config = Path('/home/debtest/.config/DeskBridge/DeskBridge.conf')
assert config.is_file() and config.stat().st_size, 'GUI did not create its actual user configuration'
with config.open('a') as stream:
    stream.write('\n[debVerification]\nsentinel=preserve-user-settings\n')
tls = config.parent / 'tls'
tls.mkdir(exist_ok=True)
(tls / 'DEB-VERIFICATION-SENTINEL').write_text('preserve-user-tls\n')
state = Path('/home/debtest/.local/state/DeskBridge.state')
state.parent.mkdir(parents=True, exist_ok=True)
with state.open('a') as stream:
    stream.write('\n[debVerification]\nsentinel=preserve-user-state\n')
paths = sorted(path for path in config.parent.rglob('*') if path.is_file()) + [state]
assert all(path.stat().st_uid == os.getuid() for path in paths), 'Profile contains files not owned by debtest'
manifest = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
Path('/job/verify-results/user-data.json').write_text(json.dumps(manifest, indent=2) + '\n')
PY
verify_user_data
apt-get purge -y deskbridge
[[ ! -e /opt/deskbridge && ! -e /usr/bin/deskbridge && ! -e /usr/share/applications/local.shuai.deskbridge.desktop ]]
verify_user_data
apt-get install -y --no-install-recommends "$package"
verify_user_data
verify_versions
verify_gui reinstall
[[ $(dpkg-query -W -f='${Version} ${Architecture} ${db:Status-Status}' deskbridge) == '1.27.0.3-1~ubuntu22.04 amd64 installed' ]]
printf '\nPASS: clean Ubuntu 22.04 amd64 install, pre-tool ELF dependency resolution, exact GUI/core version, normal-user X11 main window, purge/data preservation, reinstall and GUI restart.\n'
printf 'Scope: installation and startup smoke test only; real keyboard/mouse sharing, cross-screen operation and Wayland were not tested.\n'
