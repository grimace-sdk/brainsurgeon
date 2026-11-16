#!/bin/sh
set -eu

repository=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "${1:?Usage: tools/package.sh BUILD_DIRECTORY [VERSION]}" && pwd)
version=${2:-$(git -C "$repository" describe --tags --always --dirty)}
name=brainsurgeon-$version
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
package=$stage/$name
card=$package/card

mkdir -p "$card/pwsh2-boot/Windows" "$package/tools/lib"
cp "$build/edsa2exe.bin" "$card/EDSA2EXE.BIN"
cp "$build/edsa2exe.bin" "$card/EDSA1EXE.BIN"
node "$repository/tools/make-edsa2cfg.mjs" "$card/EDSA2CFG.BIN" 0xffffffff 0x22 > /dev/null
cp "$card/EDSA2CFG.BIN" "$card/EDSA1CFG.BIN"
if [ -f "$build/edna3exe.bin" ]; then
    cp "$build/edna3exe.bin" "$card/EDNA3EXE.BIN"
    node "$repository/tools/make-edsa2cfg.mjs" "$card/EDNA3CFG.BIN" 0 0x02 > /dev/null
fi
if [ -f "$build/edsh4exe.bin" ]; then
    cp "$build/edsh4exe.bin" "$card/EDSH4EXE.BIN"
fi
cp "$build/autorotate.exe" "$build/crashlog.exe" "$card/pwsh2-boot/"
: > "$card/pwsh2-boot/dump-nk"
cp -R "$repository/card/pwsh2-boot/." "$card/pwsh2-boot/"
tab=$(printf '\t')
node "$repository/tools/profile-files.mjs" --card | while IFS=$tab read -r path file; do
    case "$file" in
        build/*) source="$build/${file#build/}" ;;
        *) source="$repository/$file" ;;
    esac
    mkdir -p "$(dirname "$card/$path")"
    cp "$source" "$card/$path"
done
cp "$repository/card/launch-example.txt" "$package/launch-example.txt"
cp "$build"/build-info*.txt "$package/"
sh "$repository/tools/copy-profile-files.sh" "$build" "$package"
cp "$repository/tools/make-boot-nkp.mjs" "$repository/tools/skip-modules.json" "$package/tools/"
cp "$repository"/tools/lib/*.mjs "$package/tools/lib/"
cp "$repository/README.md" "$repository/LICENSE" "$package/"
cp -R "$repository/docs" "$package/docs"
(cd "$package" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
rm -f "$build/$name.zip"
(cd "$stage" && find "$name" | sort | zip -q -X -@ "$build/$name.zip")
echo "$build/$name.zip"
