#!/bin/sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
output=${1:?usage: build.sh OUTPUT_DIRECTORY}
mkdir -p "$output"
flags="-mcpu=arm926ej-s -marm -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic -Wall -Wextra -Werror"
arm-none-eabi-gcc $flags -c "$here/entry.S" -o "$output/entry.o"
arm-none-eabi-gcc $flags -c "$here/hook.c" -o "$output/hook.o"
arm-none-eabi-ld -T "$here/link.ld" "$output/entry.o" "$output/hook.o" -o "$output/a7300-display.elf"
arm-none-eabi-objcopy -O binary "$output/a7300-display.elf" "$output/a7300-display.bin"
cc -std=c99 -O2 -Wall -Wextra -Werror -o "$output/test-transpose" "$here/test-transpose.c"
"$output/test-transpose"
