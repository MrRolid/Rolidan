#!/bin/bash
# Kept for compatibility: builds only the bundled .deb (see build-release.sh).
exec "$(dirname "$0")/build-release.sh" "$@" deb
