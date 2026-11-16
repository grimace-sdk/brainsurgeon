#!/bin/sh
set -eu

repository=$(cd "$(dirname "$0")/../.." && pwd)
iterations=${NKGEN_FUZZ_ITERATIONS:-5000}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

node "$repository/tools/test/make-fixture-image.mjs" "$work/fixture"
NKGEN_ROOT="$work/fixture/root" NK_IMAGES="$work/fixture/images" sh "$repository/tools/check-nkgen.sh" "$work/fixture/build"

sh "$repository/nkgen/build.sh" "$work/bin" sanitize
node "$repository/tools/make-nkgen-pak.mjs" --root "$work/fixture/root" --build "$work/fixture/build" --output "$work/fixture.pak" --release fixture
"$work/bin/nkgen-host-sanitize" --image "$work/fixture/images/fixture.bin" --pak "$work/fixture.pak" --card "$work/card" --quiet
for image in "$work/fixture/images"/*.bin; do
    echo "fuzz $(basename "$image")"
    "$work/bin/nkgen-fuzz" --image "$image" --pak "$work/fixture.pak" --iterations "$iterations"
done
