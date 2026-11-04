#!/bin/bash
# Build self-contained release files with Qt bundled, for distributions whose
# Qt is older than 6.8. Requirement on the target: x86_64, glibc >= 2.38
# (Ubuntu 24.04+, Mint 22+, Debian 13, Fedora 39+, openSUSE Tumbleweed, Arch…).
#
# Usage: packaging/build-release.sh [build-dir] [formats...]
#   build-dir  default: build-release
#   formats    deb tar appimage rpm (default: all available)
# Output: <build-dir>/dist/
#
# Tools: dpkg-deb (deb), tar (tar), mksquashfs from squashfs-tools (appimage),
# rpmbuild from rpm (rpm). The AppImage runtime is downloaded once from
# https://github.com/AppImage/type2-runtime unless APPIMAGE_RUNTIME points to it.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$ROOT/build-release
if [ $# -gt 0 ] && [ -d "$1" ]; then
    BUILD=$(cd "$1" && pwd)
    shift
fi
FORMATS=("$@")
if [ ${#FORMATS[@]} -eq 0 ]; then
    FORMATS=(deb tar)
    command -v mksquashfs >/dev/null && FORMATS+=(appimage)
    command -v rpmbuild >/dev/null && FORMATS+=(rpm)
fi
VERSION=$(sed -n 's/^project(Rolidan VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
DIST="$BUILD/dist"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
QTPLUGINS=$(qtpaths6 --query QT_INSTALL_PLUGINS)
QTVER=$(qtpaths6 --qt-version)
DEBIAN_RELEASE=$(cut -d. -f1 /etc/debian_version)
mkdir -p "$DIST"

cmake --build "$BUILD" --target rolidan

# ------------------------------------------------------------------ bundle
# $APP gets bin/ lib/ plugins/ share/: the same tree for every format.
APP="$WORK/app"
mkdir -p "$APP/bin" "$APP/lib" "$APP/plugins" "$APP/share/applications" \
         "$APP/share/icons/hicolor/scalable/apps" "$APP/share/mime/packages"
install -m755 "$BUILD/rolidan" "$APP/bin/rolidan.bin"

# Plugins Rolidan needs at runtime.
PLUGIN_DIRS=(platforms imageformats iconengines platformthemes platforminputcontexts printsupport
             xcbglintegrations wayland-shell-integration wayland-decoration-client
             wayland-graphics-integration-client)
for d in "${PLUGIN_DIRS[@]}"; do
    [ -d "$QTPLUGINS/$d" ] && cp -a "$QTPLUGINS/$d" "$APP/plugins/"
done
# HEIC decoders/encoders of libheif are plugins of their own (libde265, x265…).
HEIFPLUGINS=$(ls -d /usr/lib/*-linux-gnu/libheif/plugins 2>/dev/null | head -n1)
if [ -n "$HEIFPLUGINS" ] && ls "$APP/plugins/imageformats/"kimg_heif* >/dev/null 2>&1; then
    mkdir -p "$APP/lib/heif-plugins"
    cp -L "$HEIFPLUGINS"/*.so "$APP/lib/heif-plugins/"
fi
if ! ls "$APP/plugins/imageformats/"kimg_* >/dev/null 2>&1; then
    echo "warning: KImageFormats not installed (kimageformat6-plugins): no AVIF, HEIC, JPEG XL, RAW, PSD…" >&2
fi
# Not needed for a desktop app.
rm -f "$APP/plugins/platforms/"{libqvnc,libqlinuxfb,libqminimalegl,libqvkkhrdisplay,libqeglfs}.so

DOC="$APP/share/doc/rolidan"
mkdir -p "$DOC/third-party"

# fcitx5 input method (Japanese, Chinese...): the plugin is built against this
# exact Qt, so take Debian's package without installing it on the build host.
IMDL="$WORK/fcitx5"
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
        [ -f "$c" ] && install -D -m644 "$c" "$DOC/third-party/$(basename "$(dirname "$c")")/copyright"
    done
else
    echo "warning: fcitx5 Qt6 plugin not downloaded (no network?); fcitx5 IME will not work" >&2
fi

# Libraries every desktop provides itself (bundling them breaks drivers/themes).
# libcups talks to the local print server, so it must be the system one; without
# it the print plugin is skipped and Rolidan can still print to PDF.
EXCLUDE='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libgcc_s|libGL|libGLX|libGLdispatch|libOpenGL|libEGL|libgbm|libdrm|libX11|libX11-xcb|libxcb|libXau|libXdmcp|libXext|libXrender|libxshmfence|libwayland-client|libwayland-cursor|libwayland-egl|libwayland-server|libfontconfig|libfreetype|libexpat|libz|libpng16|libbrotli.*|libbz2|liblzma|libzstd|liblz4|libglib-2\.0|libgobject-2\.0|libgio-2\.0|libgmodule-2\.0|libgthread-2\.0|libdbus-1|libsystemd|libudev|libcap|libgcrypt|libgpg-error|libselinux|libmount|libblkid|libuuid|libpcre2-8|libffi|libgtk-3|libgdk-3|libgdk_pixbuf-2\.0|libpango.*|libcairo.*|libharfbuzz.*|libatk.*|libepoxy|libatspi|libcloudproviders|libfribidi|libthai|libdatrie|libgraphite2|libpixman-1|libXcursor|libXfixes|libXi|libXinerama|libXrandr|libXcomposite|libXdamage|libvulkan|libmd|libbsd|libasound|libpulse.*|libtinfo|libcups)\.so'

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
for plugin in "$APP/lib/heif-plugins"/*.so; do [ -e "$plugin" ] && copy_deps "$plugin"; done
# Newer libstdc++ than older distributions have; safe because it is backwards compatible.
cp -L "$(g++ -print-file-name=libstdc++.so.6)" "$APP/lib/"

# Licenses: Rolidan (GPL-3.0) and every bundled library (Qt is LGPL-3.0; the
# libraries stay separate .so files in lib/, so they can be replaced).
install -m644 "$ROOT/LICENSE" "$DOC/copyright"
{
    while IFS= read -r -d '' f; do
        # the Debian package that ships a library with this name
        { dpkg -S "*/$(basename "$f")" 2>/dev/null || true; } | head -n1 | cut -d: -f1 | cut -d, -f1
    done < <(find "$APP/lib" "$APP/plugins" -name '*.so*' -print0)
    printf '%s\n' libstdc++6 libjpeg62-turbo libwebp7 libsharpyuv0
} | sort -u | while read -r pkg; do
    if [ -n "$pkg" ] && [ -f "/usr/share/doc/$pkg/copyright" ]; then
        install -D -m644 "/usr/share/doc/$pkg/copyright" "$DOC/third-party/$pkg/copyright"
    fi
done
cat > "$DOC/README.third-party" <<EOF
Rolidan $VERSION is free software under the GNU General Public License version 3
(see copyright). Source code: https://github.com/ (the Rolidan project page).

This build bundles shared libraries in lib/ and plugins/, taken unchanged from
Debian $DEBIAN_RELEASE. Their licenses are in third-party/<package>/copyright; their
source code is available from Debian (apt-get source <package>, or
https://sources.debian.org). Qt $QTVER is used under the GNU LGPL version 3: you
may replace the Qt libraries in lib/ with your own compatible build.
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
# Launcher: use the bundled Qt next to this script.
HERE=$(dirname "$(readlink -f "$0")")/..
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
[ -d "$HERE/lib/heif-plugins" ] && export LIBHEIF_PLUGIN_PATH="$HERE/lib/heif-plugins"
unset QT_QPA_PLATFORMTHEME_PATH
exec "$HERE/bin/rolidan.bin" "$@"
EOF
chmod 755 "$APP/bin/rolidan"
install -m644 "$ROOT/data/rolidan.desktop" "$APP/share/applications/"
install -m644 "$ROOT/data/icons/rolidan.svg" "$APP/share/icons/hicolor/scalable/apps/"
install -m644 "$ROOT/data/rolidan-mime.xml" "$APP/share/mime/packages/rolidan.xml"

# System layout used by deb and rpm: /opt/rolidan + /usr/bin, /usr/share.
system_tree() {
    local dest=$1
    mkdir -p "$dest/opt" "$dest/usr/bin" "$dest/usr/share"
    cp -a "$APP" "$dest/opt/rolidan"
    mv "$dest/opt/rolidan/share/applications" "$dest/opt/rolidan/share/icons" \
       "$dest/opt/rolidan/share/mime" "$dest/opt/rolidan/share/doc" "$dest/usr/share/"
    rmdir "$dest/opt/rolidan/share"
    ln -s /opt/rolidan/bin/rolidan "$dest/usr/bin/rolidan"
}

# ------------------------------------------------------------------ formats
build_deb() {
    local stage="$WORK/deb" pkgver="${VERSION}+bundled1"
    system_tree "$stage"
    mkdir -p "$stage/DEBIAN"
    cat > "$stage/DEBIAN/control" <<EOF
Package: rolidan
Version: $pkgver
Architecture: amd64
Maintainer: Rolidan developers <noreply@localhost>
Installed-Size: $(du -sk "$stage" | cut -f1)
Depends: libc6 (>= 2.38), libgl1, libegl1, libfontconfig1, libfreetype6, libxcb1, libx11-6
Recommends: libcups2
Section: graphics
Priority: optional
Description: Fast IrfanView-style image viewer and editor
 Paste, annotate, adjust, save. Self-contained build with Qt $QTVER
 bundled in /opt/rolidan.
EOF
    local out="$DIST/rolidan_${pkgver}_amd64.deb"
    dpkg-deb --root-owner-group --build "$stage" "$out" >/dev/null
    echo "$out"
}

build_tar() {
    local name="rolidan-$VERSION-x86_64"
    local dir="$WORK/tar/$name"
    mkdir -p "$WORK/tar"
    cp -a "$APP" "$dir"
    install -m755 "$ROOT/packaging/portable/install.sh" "$dir/install.sh"
    install -m644 "$ROOT/packaging/portable/README.txt" "$dir/README.txt"
    ln -s bin/rolidan "$dir/rolidan"
    local out="$DIST/$name.tar.xz"
    XZ_OPT=-9T0 tar -C "$WORK/tar" --owner=0 --group=0 -cJf "$out" "$name"
    echo "$out"
}

build_appimage() {
    command -v mksquashfs >/dev/null || { echo "appimage: mksquashfs missing (apt install squashfs-tools)" >&2; return 1; }
    local runtime=${APPIMAGE_RUNTIME:-$BUILD/appimage-runtime-x86_64}
    if [ ! -s "$runtime" ]; then
        curl -fsSL -o "$runtime.part" \
            https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
        mv "$runtime.part" "$runtime"
    fi
    local dir="$WORK/AppDir"
    cp -a "$APP" "$dir"
    cat > "$dir/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
exec "$HERE/bin/rolidan" "$@"
EOF
    chmod 755 "$dir/AppRun"
    cp "$dir/share/applications/rolidan.desktop" "$dir/rolidan.desktop"
    sed -i 's/^Categories=.*/&\nX-AppImage-Version='"$VERSION"'/' "$dir/rolidan.desktop"
    cp "$dir/share/icons/hicolor/scalable/apps/rolidan.svg" "$dir/rolidan.svg"
    ln -s rolidan.svg "$dir/.DirIcon"
    local out="$DIST/Rolidan-$VERSION-x86_64.AppImage"
    mksquashfs "$dir" "$WORK/app.squashfs" -root-owned -noappend -comp zstd -quiet >/dev/null
    cat "$runtime" "$WORK/app.squashfs" > "$out"
    chmod 755 "$out"
    echo "$out"
}

build_rpm() {
    command -v rpmbuild >/dev/null || { echo "rpm: rpmbuild missing (apt install rpm)" >&2; return 1; }
    local top="$WORK/rpm"
    mkdir -p "$top"/{BUILD,RPMS,SOURCES,SPECS,SRPMS,tree}
    system_tree "$top/tree"
    cat > "$top/SPECS/rolidan.spec" <<EOF
Name:           rolidan
Version:        $VERSION
Release:        1.bundled
Summary:        Fast IrfanView-style image viewer and editor
License:        GPL-3.0-only AND LGPL-3.0-only AND BSD-3-Clause
URL:            https://github.com/
BuildArch:      x86_64
AutoReqProv:    no
Requires:       glibc >= 2.38
Requires:       libGL.so.1()(64bit), libEGL.so.1()(64bit), libfontconfig.so.1()(64bit)
Requires:       libfreetype.so.6()(64bit), libxcb.so.1()(64bit), libX11.so.6()(64bit)
Requires:       libX11-xcb.so.1()(64bit)
Recommends:     libcups.so.2()(64bit)

%description
Paste, annotate, adjust, save. Self-contained build with Qt $QTVER
bundled in /opt/rolidan.

%define __strip /bin/true
%define debug_package %{nil}
%define _build_id_links none
%define _binary_payload w9T0.xzdio

%install
cp -a $top/tree/. %{buildroot}/

%post
update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || :
update-mime-database /usr/share/mime >/dev/null 2>&1 || :
gtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || :

%postun
update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || :
update-mime-database /usr/share/mime >/dev/null 2>&1 || :

%files
/opt/rolidan
/usr/bin/rolidan
/usr/share/applications/rolidan.desktop
/usr/share/icons/hicolor/scalable/apps/rolidan.svg
/usr/share/mime/packages/rolidan.xml
%doc /usr/share/doc/rolidan
EOF
    rpmbuild --quiet --define "_topdir $top" --define "_dbpath $top/db" -bb "$top/SPECS/rolidan.spec" >/dev/null
    local rpm
    rpm=$(find "$top/RPMS" -name '*.rpm' | head -n1)
    cp "$rpm" "$DIST/"
    echo "$DIST/$(basename "$rpm")"
}

for f in "${FORMATS[@]}"; do
    "build_$f"
done
