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
