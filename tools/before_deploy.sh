#!/usr/bin/env bash
# Building and packaging for release

set -euo pipefail

: "${TARGET:?TARGET must be set}"
: "${RELEASE_TAG:?RELEASE_TAG must be set}"
PROJECT_NAME=${PROJECT_NAME:-fakio}
CHANNEL=${CHANNEL:-stable}

suffix=""
case "$TARGET" in
    *-windows-*) suffix=".exe" ;;
esac

cargo +"$CHANNEL" build --locked --target "$TARGET" --release --bins --verbose
mkdir -p dist

for binary in server client; do
    output="dist/$PROJECT_NAME-$binary-$RELEASE_TAG-$TARGET$suffix"
    cp "target/$TARGET/release/$PROJECT_NAME-$binary$suffix" "$output"
    if [[ -z "$suffix" ]]; then
        strip "$output"
    fi
done
