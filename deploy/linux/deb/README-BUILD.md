# Rebuilding the Ubuntu 22.04 amd64 DEB

Application source: DeskBridge v1.27.0.3, commit `92fba6658f9530844c1fa23f43365e5c820c7b11`. Application sources are unmodified. Recipes are release-only tooling; the package version adds the Debian revision `-1~ubuntu22.04`.

Use an isolated Ubuntu 22.04 amd64 Docker container with `/job` mapped to a writable job directory and a fresh volume at `/work`. Copy these files under `/job/recipe/`. Extract the application source archive into `/job/source/` before setup:

```bash
git clone --branch v1.27.0.3 --depth 1 https://github.com/shuaige619/DeskBridge.git deskbridge-source
test "$(git -C deskbridge-source rev-parse HEAD)" = 92fba6658f9530844c1fa23f43365e5c820c7b11
mkdir -p job/source
git -C deskbridge-source archive HEAD | tar -x -C job/source
# In the Ubuntu 22.04 amd64 build container, as root:
/job/recipe/setup.sh
/job/recipe/build.sh
DESKBRIDGE_SOURCE_COMMIT=92fba6658f9530844c1fa23f43365e5c820c7b11 \
  SOURCE_DATE_EPOCH=1792639254 \
  python3 /job/recipe/package.py
```

The install-qt.py wrapper identifies aqt with its own User-Agent for mirrors that reject generic Python requests; all official SHA256 checks remain enabled.

Optional trusted download mirrors can be selected through `DESKBRIDGE_PYPI_INDEX` and `DESKBRIDGE_QT_MIRROR`; official Qt source tarballs are always verified against pinned SHA256 values. The original build used TUNA mirrors to avoid slow international downloads. Ubuntu package signatures and normal HTTPS certificate checks remained enabled.

The exact Ubuntu package versions, pip tools, package ELF/dependency records and provenance are included with the recipe. The application is compiled with Ubuntu 22.04 repository GCC/G++ 12.3 because GCC 11 lacks the C++20 make_shared array overload. System libstdc++6 remains the ordinary Jammy runtime.

Qt 6.8.3 comes from its unmodified official `linux_gcc_64` SDK; libei 1.6.0 and libportal 0.9.1 are compiled from the exact asserted commits. Qt module source digests and corresponding-source URLs are in the DEB NOTICE and provenance records.

Use a second clean Ubuntu 22.04 amd64 container, with the job directory mapped to `/job`, to run `/job/recipe/verify.sh`. It first installs only the DEB and its declared dependencies and verifies every ELF dependency before adding test tools. Tests then check ordinary-user GUI/core versions, a visible X11 main window, purge/reinstall and user-data preservation. This is a packaging/startup test, not real keyboard/mouse, cross-machine or Wayland hardware acceptance.
