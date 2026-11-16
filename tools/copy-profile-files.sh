#!/bin/sh
set -eu

repository=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "${1:?Usage: tools/copy-profile-files.sh BUILD_DIRECTORY DESTINATION}" && pwd)
destination=${2:?Usage: tools/copy-profile-files.sh BUILD_DIRECTORY DESTINATION}

node "$repository/tools/profile-files.mjs" | while read -r path; do
    case "$path" in
        build/*) source="$build/${path#build/}" ;;
        *) source="$repository/$path" ;;
    esac
    mkdir -p "$(dirname "$destination/$path")"
    cp "$source" "$destination/$path"
done
cp "$repository/profiles/README.md" "$destination/profiles/"
cp "$repository/binaries/README.md" "$destination/binaries/"
