#!/bin/sh
# Rolidan portable: add to the application menu, or remove again.
#   ./install.sh              install for the current user (~/.local)
#   sudo ./install.sh         install for all users (/opt/rolidan, /usr/local)
#   ./install.sh --uninstall  remove (use sudo for an all-users install)
set -e
SRC=$(dirname "$(readlink -f "$0")")
if [ "$(id -u)" = 0 ]; then
    DEST=/opt/rolidan
    BIN=/usr/local/bin
    SHARE=/usr/local/share
else
    DEST=${XDG_DATA_HOME:-$HOME/.local/share}/rolidan/app
    BIN=$HOME/.local/bin
    SHARE=${XDG_DATA_HOME:-$HOME/.local/share}
fi

refresh() {
    update-desktop-database -q "$SHARE/applications" 2>/dev/null || true
    update-mime-database "$SHARE/mime" 2>/dev/null || true
    gtk-update-icon-cache -q -t "$SHARE/icons/hicolor" 2>/dev/null || true
}

if [ "$1" = "--uninstall" ]; then
    rm -rf "$DEST"
    rm -f "$BIN/rolidan" "$SHARE/applications/rolidan.desktop" \
          "$SHARE/icons/hicolor/scalable/apps/rolidan.svg" "$SHARE/mime/packages/rolidan.xml"
    refresh
    echo "Rolidan removed."
    exit 0
fi

if [ "$SRC" != "$DEST" ]; then
    rm -rf "$DEST"
    mkdir -p "$DEST"
    cp -a "$SRC/bin" "$SRC/lib" "$SRC/plugins" "$SRC/share" "$SRC/install.sh" "$DEST/"
fi
mkdir -p "$BIN" "$SHARE/applications" "$SHARE/icons/hicolor/scalable/apps" "$SHARE/mime/packages"
ln -sf "$DEST/bin/rolidan" "$BIN/rolidan"
sed "s|^Exec=rolidan|Exec=$DEST/bin/rolidan|" "$SRC/share/applications/rolidan.desktop" \
    > "$SHARE/applications/rolidan.desktop"
cp "$SRC/share/icons/hicolor/scalable/apps/rolidan.svg" "$SHARE/icons/hicolor/scalable/apps/"
cp "$SRC/share/mime/packages/rolidan.xml" "$SHARE/mime/packages/"
refresh
echo "Rolidan installed to $DEST"
echo "Start it from the application menu or with: rolidan"
echo "Remove it with: $DEST/install.sh --uninstall"
