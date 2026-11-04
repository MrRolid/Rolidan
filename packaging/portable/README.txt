Rolidan – portable build for Linux (x86_64)
===========================================

Runs on any distribution with glibc 2.38 or newer (Ubuntu 24.04+, Linux Mint 22+,
Debian 13, Fedora 39+, openSUSE Tumbleweed, Arch, Manjaro…). Qt is included.

Run without installing:
    ./rolidan

Add to the application menu (current user, no root needed):
    ./install.sh

Install for all users:
    sudo ./install.sh

Remove:
    ~/.local/share/rolidan/app/install.sh --uninstall
    (all users: sudo /opt/rolidan/install.sh --uninstall)

License: GPL-3.0, see share/doc/rolidan/.
