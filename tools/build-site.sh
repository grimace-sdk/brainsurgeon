#!/bin/sh
set -eu

repository=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "${1:?Usage: tools/build-site.sh BUILD_DIRECTORY SITE_DIRECTORY}" && pwd)
site=${2:?Usage: tools/build-site.sh BUILD_DIRECTORY SITE_DIRECTORY}

rm -rf "$site"
mkdir -p "$site/lib"
cp "$repository/web/index.html" "$repository/web/app.mjs" "$site/"
cp "$repository"/tools/lib/*.mjs "$site/lib/"
cp "$repository/tools/skip-modules.json" "$site/"
sh "$repository/tools/copy-profile-files.sh" "$build" "$site"
git -C "$repository" describe --tags --always --dirty > "$site/build.txt"
echo "$site"
