#!/bin/sh
set -eu

UBOOT_REPOSITORY=https://github.com/grimace-sdk/u-boot-brain.git
UBOOT_COMMIT=d06065f73526eb5e118d6c503c7daa4e482dc48b
NKBIN_MAKER_REPOSITORY=https://github.com/brain-hackers/nkbin_maker.git
NKBIN_MAKER_COMMIT=4a97a4e8074cec12ac8989bcfcab9b9f7d47156d
GRIMACE_REPOSITORY=https://github.com/grimace-sdk/grimace.git
GRIMACE_COMMIT=c8cd459f4e82cc979aae03d21619cd431110f703
CE_DEBUG_REPOSITORY=https://github.com/grimace-sdk/ce-debug.git
CE_DEBUG_COMMIT=12f5760b4cfc6d15640de83477fd03c102b75295
LLVM_URL=https://github.com/grimace-sdk/llvm-builds/releases/download/v3/velo-llvm-v3-linux-x86_64.tar.xz
LLVM_SHA256=c3f0b67e22105ef68bbb71d0a316b268ef3e44e5b83aab93b0f9a071d2706b77
BUILDER_IMAGE=debian@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad
DEBIAN_SNAPSHOT=20261023T000000Z
BRAIN_MODEL=${BRAIN_MODEL:-pwsh2}
case "$BRAIN_MODEL" in
    pwsh2) defconfig=pwsh2_ce_defconfig; boot_file=edsa2exe.bin ;;
    pwa7300) defconfig=pwa7300_ce_defconfig; boot_file=edna3exe.bin ;;
    pwsh4) defconfig=pwsh4_ce_defconfig; boot_file=edsh4exe.bin ;;
    *) echo "Unsupported BRAIN_MODEL: $BRAIN_MODEL" >&2; exit 2 ;;
esac

repository=$(cd "$(dirname "$0")" && pwd)
output_directory=${1:-$repository/out}
mkdir -p "$output_directory"
output_directory=$(cd "$output_directory" && pwd)

if [ "${IN_BUILDER:-}" != 1 ]; then
    BRAINSURGEON_VERSION=${BRAINSURGEON_VERSION:-$(git -C "$repository" describe --tags --always --dirty 2>/dev/null || echo unknown)}
    exec docker run --rm --platform linux/amd64 \
        -e IN_BUILDER=1 -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
        -e BRAIN_MODEL="$BRAIN_MODEL" -e BRAINSURGEON_VERSION="$BRAINSURGEON_VERSION" \
        -v "$repository:/source:ro" -v "$output_directory:/out" \
        "$BUILDER_IMAGE" sh /source/build.sh /out
fi

export DEBIAN_FRONTEND=noninteractive
rm -f /etc/apt/sources.list.d/*
cat > /etc/apt/sources.list <<SOURCES
deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/$DEBIAN_SNAPSHOT trixie main
deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/$DEBIAN_SNAPSHOT trixie-updates main
deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/$DEBIAN_SNAPSHOT trixie-security main
SOURCES
apt-get -o Acquire::Retries=3 update -qq
apt-get install -y -qq --no-install-recommends \
    ca-certificates curl git make gcc libc6-dev bc bison flex libssl-dev python3 xz-utils \
    gcc-arm-none-eabi libnewlib-arm-none-eabi cmake ninja-build > /dev/null

work=/work
mkdir -p "$work"

fetch() {
    git -C "$work" init -q "$1"
    git -C "$work/$1" fetch -q --depth 1 "$2" "$3"
    git -C "$work/$1" checkout -q FETCH_HEAD
}

fetch u-boot-brain "$UBOOT_REPOSITORY" "$UBOOT_COMMIT"
fetch nkbin_maker "$NKBIN_MAKER_REPOSITORY" "$NKBIN_MAKER_COMMIT"
fetch grimace "$GRIMACE_REPOSITORY" "$GRIMACE_COMMIT"
fetch ce-debug "$CE_DEBUG_REPOSITORY" "$CE_DEBUG_COMMIT"

curl -sSfL -o "$work/llvm.tar.xz" "$LLVM_URL"
echo "$LLVM_SHA256  $work/llvm.tar.xz" | sha256sum -c --quiet
mkdir -p "$work/llvm"
tar -xJf "$work/llvm.tar.xz" -C "$work/llvm" --strip-components=1

cd "$work/u-boot-brain"
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)
export SOURCE_DATE_EPOCH
export LOCALVERSION=
make -s "$defconfig"
make -s -j"$(nproc)" CROSS_COMPILE=arm-none-eabi- u-boot.bin
make -s -C "$work/nkbin_maker" bsd-ce
"$work/nkbin_maker/bsd-ce" u-boot.bin
cp u-boot.bin /out/u-boot.bin
cp nk.bin "/out/$boot_file"

cmake -S /source/ce -B "$work/ce" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$work/grimace/cmake/toolchain.cmake" \
    -DGRIMACE_ARCH=arm -DGRIMACE_CE_VERSION=6 -DGRIMACE_PLATFORM=brain -DGRIMACE_LLVM_ROOT="$work/llvm" \
    -DBRAINSURGEON_VERSION="${BRAINSURGEON_VERSION:-unknown}" > /dev/null
cmake --build "$work/ce"
cp "$work/ce/bstart.exe" "$work/ce/autorotate.exe" "$work/ce/crashlog.exe" \
    "$work/ce/brain-control.cpl" "$work/ce/braincrypt.dll" "$work/ce/brainras.dll" "$work/ce/brainsip.dll" "$work/ce/keymap.exe" /out/
cmake -S "$work/ce-debug" -B "$work/ce-debug-build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$work/grimace/cmake/toolchain.cmake" \
    -DGRIMACE_ARCH=arm -DGRIMACE_CE_VERSION=6 -DGRIMACE_PLATFORM=brain -DGRIMACE_LLVM_ROOT="$work/llvm" > /dev/null
cmake --build "$work/ce-debug-build"
cp "$work/ce-debug-build/agent/debugmgr.exe" "$work/ce-debug-build/usbfn/usbserial.dll" /out/
sh /source/ce/debug-log/build.sh "$work/debug-log"
cp "$work/debug-log/debug-log.bin" /out/
sh /source/ce/a7300-display/build.sh "$work/a7300-display" > /dev/null
cp "$work/a7300-display/a7300-display.bin" /out/
sh /source/nkgen/build.sh "$work/nkgen" arm "${BRAINSURGEON_VERSION:-unknown}"
cp "$work/nkgen/nkgen.bin" /out/

{
    echo "brainsurgeon ${BRAINSURGEON_VERSION:-unknown}"
    echo "board $BRAIN_MODEL ($defconfig)"
    echo "u-boot-brain $UBOOT_REPOSITORY $UBOOT_COMMIT"
    echo "nkbin_maker $NKBIN_MAKER_COMMIT"
    echo "grimace $GRIMACE_REPOSITORY $GRIMACE_COMMIT"
    echo "ce-debug $CE_DEBUG_REPOSITORY $CE_DEBUG_COMMIT"
    echo "llvm $LLVM_URL $LLVM_SHA256"
    echo "image $BUILDER_IMAGE"
    echo "debian snapshot $DEBIAN_SNAPSHOT"
    arm-none-eabi-gcc --version | head -1
    echo "SOURCE_DATE_EPOCH $SOURCE_DATE_EPOCH"
    cd /out && sha256sum u-boot.bin "$boot_file" bstart.exe autorotate.exe crashlog.exe \
        brain-control.cpl braincrypt.dll brainras.dll brainsip.dll keymap.exe debugmgr.exe usbserial.dll debug-log.bin a7300-display.bin nkgen.bin
} > /out/build-info.txt
chown -R "$HOST_UID:$HOST_GID" /out
cat /out/build-info.txt
