#!/usr/bin/env bash
# Building and packaging for release

set -ex

build() {
    cargo build --target "$TARGET" --release --verbose
}

copy() {
    local out_dir=$(pwd)
    local out_server="$out_dir/$PROJECT_NAME-server-$TRAVIS_TAG-$TARGET"
    local out_client="$out_dir/$PROJECT_NAME-client-$TRAVIS_TAG-$TARGET"

    # copy server
    cp "target/$TARGET/release/$PROJECT_NAME-server" "$out_server"
    strip "$out_server"

    # copy client
    cp "target/$TARGET/release/$PROJECT_NAME-client" "$out_client"
    strip "$out_client"
}

main() {
    build
    copy
}

main
