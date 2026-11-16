#!/bin/sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
output=${1:?usage: build.sh OUTPUT_DIRECTORY [host]}
target=${2:-host}
mkdir -p "$output"
output=$(cd "$output" && pwd)
cd "$here"
sources="generate.c manifest.c bootnkp.c registry.c records.c romimage.c romdll.c peimports.c sha256.c util.c a7300.c"
warnings="-Wall -Wextra -Werror -Wcast-align -Wshadow -Wstrict-prototypes"

case "$target" in
    host)
        ${CC:-cc} -std=c99 -O2 $warnings -DNKGEN_HOSTED -o "$output/nkgen-host" host.c $sources
        ;;
    *)
        echo "Unknown target $target" >&2
        exit 2
        ;;
esac
