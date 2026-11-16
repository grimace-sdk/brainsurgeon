#!/bin/sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
output=${1:?usage: build.sh OUTPUT_DIRECTORY [host|sanitize|arm [VERSION]]}
target=${2:-host}
mkdir -p "$output"
output=$(cd "$output" && pwd)
cd "$here"
sources="generate.c manifest.c bootnkp.c registry.c records.c romimage.c romdll.c peimports.c sha256.c util.c a7300.c"
warnings="-Wall -Wextra -Werror -Wcast-align -Wshadow -Wstrict-prototypes"
sanitize="-std=c99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Wall -Wextra -DNKGEN_HOSTED"

case "$target" in
    host)
        ${CC:-cc} -std=c99 -O2 $warnings -DNKGEN_HOSTED -o "$output/nkgen-host" host.c $sources
        ;;
    sanitize)
        ${CC:-cc} $sanitize -o "$output/nkgen-host-sanitize" host.c $sources
        ${CC:-cc} $sanitize -o "$output/nkgen-fuzz" fuzz.c $sources
        ;;
    arm)
        version=$(printf '%.31s' "${3:-unknown}")
        flags="-std=c99 -mcpu=arm926ej-s -marm -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic -mno-unaligned-access -ffixed-r9
            -fno-tree-loop-distribute-patterns -fno-unwind-tables -fno-asynchronous-unwind-tables $warnings -Wcast-align=strict"
        mkdir -p "$output/arm"
        objects=""
        for source in $sources runtime.c; do
            object="$output/arm/${source%.c}.o"
            arm-none-eabi-gcc $flags -fcallgraph-info=su -c "$source" -o "$object"
            objects="$objects $object"
        done
        arm-none-eabi-gcc -mcpu=arm926ej-s -marm -DNKGEN_VERSION_TEXT="\"$version\"" -c entry.S -o "$output/arm/entry.o"
        arm-none-eabi-gcc -nostdlib -Wl,--no-warn-rwx-segments -T nkgen.ld "$output/arm/entry.o" $objects -lgcc -o "$output/nkgen.elf"
        arm-none-eabi-objcopy -O binary "$output/nkgen.elf" "$output/nkgen.bin"
        if arm-none-eabi-objdump -d "$output/arm/entry.o" $objects | grep -qwE 'r9|sb'; then
            echo "the generator uses r9, which holds U-Boot's global data pointer" >&2
            exit 1
        fi
        ${CC:-cc} -std=c99 -O2 $warnings -o "$output/nkgen-seal" seal.c
        "$output/nkgen-seal" "$output/nkgen.bin"
        python3 stack-usage.py 8192 nkgen_generate "$output"/arm/*.ci
        ;;
    *)
        echo "Unknown target $target" >&2
        exit 2
        ;;
esac
