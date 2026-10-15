#!/usr/bin/env bash
# Cross-build a UEFI AArch64 application on non-Windows hosts.
#   ./build.sh            -> dry-run loader (loads + reports, no jump)
#   DO_JUMP=1 ./build.sh  -> loader actually ExitBootServices + jumps to kernel
set -euo pipefail
cd "$(dirname "$0")"

DO_JUMP="${DO_JUMP:-0}"
OUT="${OUT:-BOOTAA64.EFI}"

CC="clang --target=aarch64-pc-windows-msvc -ffreestanding -nostdinc -fno-builtin \
    -fno-stack-protector -fshort-wchar -mno-red-zone -O2 -Wall -Wextra \
    -DDO_JUMP=${DO_JUMP}"

echo ">> compiling loader.c (DO_JUMP=${DO_JUMP})"
$CC -c loader.c -o loader.o

echo ">> linking ${OUT}"
lld-link /nologo /subsystem:efi_application /entry:efi_main /machine:arm64 \
    /nodefaultlib /out:"${OUT}" loader.o

echo ">> done: ${OUT}"
file "${OUT}"
ls -l "${OUT}"
