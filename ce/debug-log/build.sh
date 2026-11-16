#!/bin/sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
output=${1:?usage: build.sh OUTPUT_DIRECTORY}
mkdir -p "$output"
flags="-mcpu=arm926ej-s -marm -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic -Wall -Wextra -Werror"
arm-none-eabi-gcc $flags -c "$here/entry.S" -o "$output/debug-log-entry.o"
arm-none-eabi-gcc $flags -c "$here/debug_log.c" -o "$output/debug-log.o"
arm-none-eabi-ld -T "$here/link.ld" "$output/debug-log-entry.o" "$output/debug-log.o" -o "$output/debug-log.elf"
arm-none-eabi-objcopy -O binary "$output/debug-log.elf" "$output/debug-log.bin"
