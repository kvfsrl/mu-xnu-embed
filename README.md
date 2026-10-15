# mu-xnu-embed — run the XNU (Darwin) UEFI loader inside Mu-Silicium (peridot)

Embeds our `BOOTAA64.EFI` XNU loader into the peridot UEFI firmware and launches
it from BDS, with the kernel appended to the Android boot image (RAM-only, so
`fastboot boot` writes nothing).

## Layout
```
embed.py                     patcher (idempotent) — run against a Mu-Silicium tree
apply.sh                     thin wrapper: ./apply.sh <mu_root> <BOOTAA64.EFI> <kernel.bin>
.github/workflows/build-peridot-xnu.yml   GitHub Actions build (trigger with gh CLI)
```

The loader source lives at `../efi-loader/` (loader.c, efi.h, macho.h, bootargs.h,
build.sh). It is a plain UEFI AArch64 app; `DO_JUMP=1` makes it actually
`ExitBootServices` and branch to XNU `_start` with `x0 = &boot_args`.

## How it wires up
1. **FDF** (`peridotPkg/peridot.fdf`): `BOOTAA64.EFI` is added to `FvMain` as an
   `APPLICATION` file with GUID `7A3B1C2D-4E5F-4061-8234-9A0B1C2D3E4F`.
2. **BDS** (`BootManagerLib/BootManager.c`): after console is up,
   `LaunchXnuLoader()` builds an `FvFile(<GUID>)` device path and calls
   `LoadImage` + `StartImage`. If the loader hands off, BDS never continues.
3. **Boot image** (`build_uefi.py`): appends `[pad16 | "XNUKERN1" | u64 size |
   kernel.bin]` **after** the UEFI FD (inside the gzip stream). Mu-Silicium's
   `BootShim.S` copies only `FD_SIZE` bytes, so the tail stays in RAM; the loader
   scans for the magic and loads the Mach-O from there.
4. **Loader** (`efi-loader/loader.c`): `find_appended_kernel()` scans RAM for the
   magic, builds `boot_args`, prints `[xnuload] ...` to the console, and (with
   `DO_JUMP=1`) transfers control.

## Build locally
```bash
git clone --recursive https://github.com/Project-Silicium/Mu-Silicium.git mu
cd efi-loader && DO_JUMP=1 OUT=BOOTAA64.EFI ./build.sh && cd ..
./mu-xnu-embed/apply.sh mu efi-loader/BOOTAA64.EFI /path/to/kernel.bin
cd mu && python3 build_uefi.py -d peridot
# => Mu-peridot-0.img  (fastboot boot Mu-peridot-0.img)
```

## Build in GitHub Actions (your fork, via gh)
```bash
gh workflow run build-peridot-xnu.yml \
  -f kernel_url="https://.../kernel.bin" \
  -f mu_repo="<you>/Mu-Silicium"
gh run watch
```

## Reality check
This makes the **loader** run and print `[xnuload]` lines, builds a valid
`boot_args`, and reaches XNU `_start`. The kernel will still fault on SM8635
because XNU needs Apple hardware abstraction (AIC, iBoot device tree, PMU/UART/
DART) and is built arm64e for Apple silicon. Nothing here changes that — the
loader proves the UEFI→XNU hand-off path only.
