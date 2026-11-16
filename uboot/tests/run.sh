#!/bin/sh
set -eu

tests=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ -z "${UBOOT_SOURCE:-}" ]; then
    eval "$(grep -E '^UBOOT_(REPOSITORY|COMMIT)=' "$tests/../../build.sh")"
    git -C "$work" init -q u-boot-brain
    git -C "$work/u-boot-brain" fetch -q --depth 1 "$UBOOT_REPOSITORY" "$UBOOT_COMMIT"
    git -C "$work/u-boot-brain" checkout -q FETCH_HEAD
    UBOOT_SOURCE=$work/u-boot-brain
fi

loader=$UBOOT_SOURCE/board/sharp/common/wince
cc -std=gnu11 -Wall -Werror -Wno-unused-function -Wno-format-truncation -I"$tests/stubs" -I"$loader" -o "$work/nkpatch-test" "$tests/nkpatch-test.c" -lz
"$work/nkpatch-test"
cc -std=gnu11 -Wall -Werror -I"$tests/stubs" -I"$loader" -o "$work/progress-test" "$tests/progress-test.c"
"$work/progress-test"
cc -std=gnu11 -Wall -Werror -I"$tests/stubs" -I"$loader" -o "$work/menu-test" "$tests/menu-test.c"
"$work/menu-test"

card_flags="-std=gnu11 -Wall -Werror -Wno-unused-function -Wno-format-truncation -I$tests/card-stubs -I$tests/stubs -I$loader"
cc $card_flags -o "$work/nkgen-test" "$tests/nkgen-test.c" -lz
"$work/nkgen-test"

repository=$(cd "$tests/../.." && pwd)
mkdir -p "$work/generator"
for source in $(sed -n 's/^sources="\(.*\)"$/\1/p' "$repository/nkgen/build.sh"); do
    cc -std=c99 -O2 -DNKGEN_HOSTED -Dcrc32=nkgen_crc32 -c "$repository/nkgen/$source" -o "$work/generator/${source%.c}.o"
done
cc $card_flags -o "$work/nkgen-flow-test" "$tests/nkgen-flow-test.c" "$work/generator"/*.o -lz
sh "$repository/nkgen/build.sh" "$work/bin" host

flow() {
    name=$(basename "$1" .bin)
    mkdir -p "$work/flow/$name"
    "$work/nkgen-flow-test" "$1" "$2" "$work/flow/$name" > "$work/flow-$name.log" || { cat "$work/flow-$name.log"; exit 1; }
    "$work/bin/nkgen-host" --image "$1" --pak "$2" --card "$work/host/$name" --quiet
    diff -r -x nkgen.log -x cache.txt -x boot-status.txt -x nkgen.bin -x nkgen.pak "$work/host/$name" "$work/flow/$name"
    echo "PASS flow $name: $(grep -c '^PASS' "$work/flow-$name.log") checks, card matches nkgen-host"
}

node "$repository/tools/test/make-fixture-image.mjs" "$work/fixture" > /dev/null
node "$repository/tools/make-nkgen-pak.mjs" --root "$work/fixture/root" --build "$work/fixture/build" \
    --output "$work/fixture.pak" --release fixture > /dev/null
for image in "$work/fixture/images"/*.bin; do
    flow "$image" "$work/fixture.pak"
done
if [ -n "${NK_IMAGES:-}" ] && [ -n "${NKGEN_BUILD:-}" ]; then
    node "$repository/tools/make-nkgen-pak.mjs" --build "$NKGEN_BUILD" --output "$work/nkgen.pak" --release check > /dev/null
    for image in "$NK_IMAGES"/*.bin; do
        flow "$image" "$work/nkgen.pak"
    done
fi
