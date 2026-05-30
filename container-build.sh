#!/usr/bin/env bash
# Build this package's Debian (.deb) files in a Linux container.
#
# Standalone entry point that delegates to scripts/container-build.sh.
# Output lands in ../deb-output/ (override with DEB_OUTPUT). Dependency
# .debs (if any) must already exist there — build each in its own repo
# first (./container-build.sh).
#
# Environment overrides: DEBIAN_VERSION, TARGET_PLATFORM, CONTAINER_ENGINE,
# DEB_OUTPUT. See scripts/container-build.sh for details.
set -euo pipefail
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scripts/container-build.sh" "$@"
