#!/bin/sh
set -eu

usage="Usage: NK_IMAGES=DIRECTORY sh tools/check-nkgen.sh BUILD_DIRECTORY

Runs the JS reference (tools/make-boot-nkp.mjs --folder crc) and the host build of the generator (nkgen-host) on every
*.bin image in NK_IMAGES, with and without --no-profile and --no-skip, and compares the card trees byte for byte.
When both refuse an image, their messages must match. BUILD_DIRECTORY has bstart.exe and the CE programs (sh build.sh out).
NKGEN_ROOT replaces the repository as the source of profiles/, tools/skip-modules.json, inputs/ and binaries/ (tools/test/make-fixture-image.mjs makes one)."

repository=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-}
if [ -z "${NK_IMAGES:-}" ] || [ -z "$build" ] || [ "$build" = --help ]; then
    echo "$usage"
    exit 2
fi
build=$(cd "$build" && pwd)
root=${NKGEN_ROOT:-$repository}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

sh "$repository/nkgen/build.sh" "$work/bin" host
node "$repository/tools/make-nkgen-pak.mjs" --root "$root" --build "$build" --output "$work/nkgen.pak" --release check > /dev/null

failures=0
compared=0
for image in "$NK_IMAGES"/*.bin; do
    name=$(basename "$image" .bin)
    for mode in "" "--no-profile" "--no-skip" "--no-profile --no-skip"; do
        label="$name${mode:+ $mode}"
        tag=$(echo "$label" | tr ' ' '_')
        js_card=$work/js/$tag
        c_card=$work/c/$tag
        js_status=0
        c_status=0
        node "$repository/tools/make-boot-nkp.mjs" --root "$root" --build "$build" --image "$image" --folder crc $mode \
            --card "$js_card" > "$work/js-$tag.log" 2>&1 || js_status=$?
        "$work/bin/nkgen-host" --image "$image" --pak "$work/nkgen.pak" --card "$c_card" $mode \
            > "$work/c-$tag.log" 2> "$work/c-$tag.err" || c_status=$?
        compared=$((compared + 1))
        if [ $js_status -ne 0 ] || [ $c_status -ne 0 ]; then
            js_error=$(grep -m 1 '^Error: ' "$work/js-$tag.log" || true)
            c_error=$(grep -m 1 '^Error: ' "$work/c-$tag.err" || true)
            if [ $js_status -ne 0 ] && [ $c_status -ne 0 ] && [ "$js_error" = "$c_error" ]; then
                echo "same   $label: both refuse: ${js_error#Error: }"
            else
                echo "DIFFER $label: JS ${js_error:-succeeded}; C ${c_error:-succeeded}"
                failures=$((failures + 1))
            fi
            continue
        fi
        identity=$(sed -n 's/.*, identity \([0-9a-f]*\)$/\1/p' "$work/js-$tag.log" | head -1)
        find "$c_card" -name nkgen.log -delete
        if ! diff -r "$js_card" "$c_card" > "$work/diff-$tag.txt"; then
            echo "DIFFER $label"
            head -20 "$work/diff-$tag.txt"
            failures=$((failures + 1))
        elif [ ! -d "$c_card/pwsh2-boot/nk-$identity" ]; then
            echo "DIFFER $label: no nk-$identity folder"
            failures=$((failures + 1))
        else
            arena=$(sed -n 's/^arena peak \([0-9]*\) bytes$/\1/p' "$work/c-$tag.log")
            took=$(sed -n 's/.*, \([0-9]*\) ms$/\1/p' "$work/c-$tag.log")
            files=$(find "$c_card" -type f | wc -l | tr -d ' ')
            echo "same   $label: nk-$identity, $files files, arena peak $arena bytes, $took ms"
        fi
    done
done
echo "$compared runs, $failures differ"
[ $failures -eq 0 ]
