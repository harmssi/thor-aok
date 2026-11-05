#!/bin/sh
# Thor AoK: builds the APK into dist/ThorAoK.apk (and installs it with --install).
# Signed with the release key when ~/.config/thoraok/env names one
# (OPENAOK_KEYSTORE, OPENAOK_KEYSTORE_PASSWORD, OPENAOK_KEY_ALIAS,
# OPENAOK_KEY_PASSWORD), otherwise with the debug key. Keys and passwords
# never go into the repository.
set -eu
cd "$(dirname "$0")/.."
[ -f "$HOME/.config/thoraok/env" ] && . "$HOME/.config/thoraok/env"
export OPENAOK_VERSION="${OPENAOK_VERSION:-$(cat thor/VERSION)}"
(cd android && ./fetch_deps.sh && ./gradlew assembleRelease)
mkdir -p dist
cp android/app/build/outputs/apk/release/app-release.apk dist/ThorAoK.apk
echo "dist/ThorAoK.apk ($OPENAOK_VERSION)"
if [ "${1:-}" = "--install" ]; then adb install -r dist/ThorAoK.apk; fi
