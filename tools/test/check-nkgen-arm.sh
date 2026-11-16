#!/bin/sh
set -eu

usage="Usage: NK_IMAGES=DIRECTORY sh tools/test/check-nkgen-arm.sh BUILD_DIRECTORY

Runs the ARM build of the generator (nkgen.bin, at 0x42000000 as U-Boot loads it) under qemu-system-arm on every
*.bin image in NK_IMAGES, with and without --no-profile, and compares its outputs with the host build's.
Alignment checking is on, so an unaligned access stops the run. Needs arm-none-eabi-gcc and qemu-system-arm.
NKGEN_ROOT replaces the repository as the source of profiles/, tools/skip-modules.json, inputs/ and binaries/."

repository=$(cd "$(dirname "$0")/../.." && pwd)
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
sh "$repository/nkgen/build.sh" "$work/bin" arm check
harness=$repository/nkgen/qemu
flags="-mcpu=arm926ej-s -marm -O2 -ffreestanding -fno-builtin -nostdlib -mno-unaligned-access -Wall -Wextra -Werror"
arm-none-eabi-gcc $flags -c "$harness/start.S" -o "$work/start.o"
arm-none-eabi-gcc $flags -c "$harness/harness.c" -o "$work/harness.o"
arm-none-eabi-gcc -nostdlib -Wl,--no-warn-rwx-segments -T "$harness/harness.ld" "$work/start.o" "$work/harness.o" -lgcc -o "$work/harness.elf"
node "$repository/tools/make-nkgen-pak.mjs" --root "$root" --build "$build" --output "$work/nkgen.pak" --release check > /dev/null
pak_length=$(wc -c < "$work/nkgen.pak" | tr -d ' ')

failures=0
for image in "$NK_IMAGES"/*.bin; do
    name=$(basename "$image" .bin)
    image_length=$(wc -c < "$image" | tr -d ' ')
    for mode in "" "--no-profile"; do
        label="$name${mode:+ $mode}"
        tag=$(echo "$label" | tr ' ' '_')
        flag=2
        [ -n "$mode" ] && flag=3
        run=$work/arm/$tag
        mkdir -p "$run/raw" "$run/card"
        host_status=0
        "$work/bin/nkgen-host" --image "$image" --pak "$work/nkgen.pak" --card "$work/host/$tag" $mode --quiet 2> "$work/host-$tag.err" || host_status=$?
        arm_status=0
        (cd "$run/raw" && qemu-system-arm -M virt -cpu cortex-a15 -m 1G -nographic -no-reboot -semihosting \
            -kernel "$work/harness.elf" \
            -device loader,file="$work/bin/nkgen.bin",addr=0x42000000,force-raw=on \
            -device loader,file="$work/nkgen.pak",addr=0x42100000,force-raw=on \
            -device loader,file="$image",addr=0x48000000,force-raw=on \
            -device loader,addr=0x47fffff0,data="$image_length",data-len=4 \
            -device loader,addr=0x47fffff4,data="$pak_length",data-len=4 \
            -device loader,addr=0x47fffff8,data="$flag",data-len=4) > "$work/arm-$tag.log" 2>&1 || arm_status=$?
        if [ $host_status -ne 0 ] || [ $arm_status -ne 0 ]; then
            host_error=$(grep -m 1 '^Error: ' "$work/host-$tag.err" || true)
            arm_error=$(grep -m 1 -E '^(Error: |fault: )' "$work/arm-$tag.log" || true)
            if [ $host_status -ne 0 ] && [ $arm_status -ne 0 ] && [ "$host_error" = "$arm_error" ]; then
                echo "same   $label: both refuse: ${host_error#Error: }"
            else
                echo "DIFFER $label: host ${host_error:-succeeded}; ARM ${arm_error:-succeeded}"
                failures=$((failures + 1))
            fi
            continue
        fi
        while read -r output path; do
            mkdir -p "$(dirname "$run/card/$path")"
            mv "$run/raw/$output" "$run/card/$path"
        done < "$run/raw/manifest.txt"
        find "$work/host/$tag" -name nkgen.log -delete
        if diff -r "$work/host/$tag" "$run/card" > "$work/diff-$tag.txt"; then
            arena=$(sed -n 's/^arena peak \([0-9]*\) bytes$/\1/p' "$work/arm-$tag.log")
            echo "same   $label: $(find "$run/card" -type f | wc -l | tr -d ' ') files, arena peak $arena bytes"
        else
            echo "DIFFER $label"
            head -20 "$work/diff-$tag.txt"
            failures=$((failures + 1))
        fi
    done
done
echo "$failures differ"
[ $failures -eq 0 ]
