#!/bin/bash
# Build a self-contained .deb with Qt bundled in /opt/rolidan, for desktops
# whose distribution ships an older Qt (e.g. Ubuntu 24.04 with Qt 6.4).
# Requirement on the target: glibc >= 2.39 (Ubuntu 24.04+, Mint 22+, Debian 13).
#
# Usage: packaging/build-bundled-deb.sh [build-dir]   (default: build-release)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$ROOT/build-release}
VERSION=$(sed -n 's/^project(Rolidan VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
PKGVER="${VERSION}+bundled1"
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
APP="$STAGE/opt/rolidan"
QTPLUGINS=$(qtpaths6 --query QT_INSTALL_PLUGINS)

cmake --build "$BUILD" --target rolidan

mkdir -p "$APP/bin" "$APP/lib" "$APP/plugins" "$STAGE/usr/bin" "$STAGE/DEBIAN" \
         "$STAGE/usr/share/applications" "$STAGE/usr/share/icons/hicolor/scalable/apps" \
         "$STAGE/usr/share/mime/packages"
install -m755 "$BUILD/rolidan" "$APP/bin/rolidan.bin"

# Plugins Rolidan needs at runtime.
PLUGIN_DIRS=(platforms imageformats iconengines platformthemes platforminputcontexts
             xcbglintegrations wayland-shell-integration wayland-decoration-client
             wayland-graphics-integration-client)
for d in "${PLUGIN_DIRS[@]}"; do
    [ -d "$QTPLUGINS/$d" ] && cp -a "$QTPLUGINS/$d" "$APP/plugins/"
done
# fcitx5 input method (Japanese, Chinese...): the plugin is built against this
# exact Qt, so take Debian's package without installing it on the build host.
IMDL="$STAGE/fcitx5"
mkdir -p "$IMDL"
if (cd "$IMDL" && apt-get download -q fcitx5-frontend-qt6 libfcitx5-qt6-1 >/dev/null 2>&1); then
    for deb in "$IMDL"/*.deb; do dpkg-deb -x "$deb" "$IMDL/root"; done
    find "$IMDL/root" -name 'libfcitx5platforminputcontextplugin.so' \
        -exec cp -L {} "$APP/plugins/platforminputcontexts/" \;
    find "$IMDL/root" -name 'libFcitx5Qt6DBusAddons.so.*' -type f -exec cp -L {} "$APP/lib/" \;
    for f in "$APP/lib"/libFcitx5Qt6DBusAddons.so.*.*; do
        [ -e "$f" ] && ln -sf "$(basename "$f")" "$APP/lib/libFcitx5Qt6DBusAddons.so.1"
    done
    for c in "$IMDL"/root/usr/share/doc/*/copyright; do
        [ -f "$c" ] && install -D -m644 "$c" \
            "$STAGE/usr/share/doc/rolidan/third-party/$(basename "$(dirname "$c")")/copyright"
    done
else
    echo "warning: fcitx5 Qt6 plugin not downloaded (no network?); fcitx5 IME will not work" >&2
fi
rm -rf "$IMDL"

# Not needed for a desktop app.
rm -f "$APP/plugins/platforms/"{libqvnc,libqlinuxfb,libqminimalegl,libqvkkhrdisplay,libqeglfs}.so

# Libraries every desktop provides itself (bundling them breaks drivers/themes).
EXCLUDE='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libgcc_s|libGL|libGLX|libGLdispatch|libOpenGL|libEGL|libgbm|libdrm|libX11|libX11-xcb|libxcb|libXau|libXdmcp|libXext|libXrender|libxshmfence|libwayland-client|libwayland-cursor|libwayland-egl|libwayland-server|libfontconfig|libfreetype|libexpat|libz|libpng16|libbrotli.*|libbz2|liblzma|libzstd|liblz4|libglib-2\.0|libgobject-2\.0|libgio-2\.0|libgmodule-2\.0|libgthread-2\.0|libdbus-1|libsystemd|libudev|libcap|libgcrypt|libgpg-error|libselinux|libmount|libblkid|libuuid|libpcre2-8|libffi|libgtk-3|libgdk-3|libgdk_pixbuf-2\.0|libpango.*|libcairo.*|libharfbuzz.*|libatk.*|libepoxy|libatspi|libcloudproviders|libfribidi|libthai|libdatrie|libgraphite2|libpixman-1|libXcursor|libXfixes|libXi|libXinerama|libXrandr|libXcomposite|libXdamage|libvulkan|libmd|libbsd|libasound|libpulse.*|libtinfo)\.so'

copy_deps() {
    LD_LIBRARY_PATH="$APP/lib" ldd "$1" 2>/dev/null | awk '/=> \//{print $3}' | while read -r lib; do
        name=$(basename "$lib")
        [[ "$name" =~ $EXCLUDE ]] && [[ ! "$name" =~ ^libxcb- ]] && continue
        [ -e "$APP/lib/$name" ] && continue
        cp -L "$lib" "$APP/lib/$name"
        copy_deps "$lib"
    done
}
copy_deps "$APP/bin/rolidan.bin"
while IFS= read -r -d '' plugin; do copy_deps "$plugin"; done < <(find "$APP/plugins" -name '*.so' -print0)
# Newer libstdc++ than older distributions have; safe because it is backwards compatible.
cp -L "$(g++ -print-file-name=libstdc++.so.6)" "$APP/lib/"

# Licenses: Rolidan (GPL-3.0) and every bundled library (Qt is LGPL-3.0; the
# libraries stay separate .so files in /opt/rolidan/lib, so they can be replaced).
DOC="$STAGE/usr/share/doc/rolidan"
mkdir -p "$DOC/third-party"
install -m644 "$ROOT/LICENSE" "$DOC/copyright"
{
    while IFS= read -r -d '' f; do
        # the Debian package that ships a library with this name
        { dpkg -S "*/$(basename "$f")" 2>/dev/null || true; } | head -n1 | cut -d: -f1 | cut -d, -f1
    done < <(find "$APP/lib" "$APP/plugins" -name '*.so*' -print0)
    printf '%s\n' fcitx5-frontend-qt6 libfcitx5-qt6-1 libstdc++6 libjpeg62-turbo libwebp7 libsharpyuv0
} | sort -u | while read -r pkg; do
    if [ -n "$pkg" ] && [ -f "/usr/share/doc/$pkg/copyright" ]; then
        install -D -m644 "/usr/share/doc/$pkg/copyright" "$DOC/third-party/$pkg/copyright"
    fi
done
cat > "$DOC/README.third-party" <<EOF
Rolidan $VERSION is free software under the GNU General Public License version 3
(see copyright). Its source code is published together with this package.

This package bundles shared libraries in /opt/rolidan/lib and /opt/rolidan/plugins,
taken unchanged from Debian $(cut -d. -f1 /etc/debian_version). Their licenses are in
third-party/<package>/copyright; their source code is available from Debian
(apt-get source <package>, or https://sources.debian.org).
Qt is used under the GNU LGPL version 3: you may replace the Qt libraries in
/opt/rolidan/lib with your own compatible build.
EOF

# Never pick up the system's Qt (possibly older, incompatible plugins).
cat > "$APP/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Libraries = lib
EOF

cat > "$APP/bin/rolidan" <<'EOF'
#!/bin/sh
# Launcher: use the bundled Qt from /opt/rolidan.
HERE=$(dirname "$(readlink -f "$0")")/..
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
unset QT_QPA_PLATFORMTHEME_PATH
exec "$HERE/bin/rolidan.bin" "$@"
EOF
chmod 755 "$APP/bin/rolidan"
ln -s /opt/rolidan/bin/rolidan "$STAGE/usr/bin/rolidan"
install -m644 "$ROOT/data/rolidan.desktop" "$STAGE/usr/share/applications/"
install -m644 "$ROOT/data/icons/rolidan.svg" "$STAGE/usr/share/icons/hicolor/scalable/apps/"
install -m644 "$ROOT/data/rolidan-mime.xml" "$STAGE/usr/share/mime/packages/rolidan.xml"

SIZE=$(du -sk "$STAGE" | cut -f1)
cat > "$STAGE/DEBIAN/control" <<EOF
Package: rolidan
Version: $PKGVER
Architecture: amd64
Maintainer: Rolidan developers <noreply@localhost>
Installed-Size: $SIZE
Depends: libc6 (>= 2.39), libgl1, libegl1, libfontconfig1, libfreetype6, libxcb1, libx11-6
Section: graphics
Priority: optional
Description: Fast image editor: paste, annotate, save
 Self-contained build with Qt $(qtpaths6 --qt-version) bundled in /opt/rolidan.
EOF

OUT="$BUILD/rolidan_${PKGVER}_amd64.deb"
dpkg-deb --root-owner-group --build "$STAGE" "$OUT" >/dev/null
echo "$OUT"
