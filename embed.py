#!/usr/bin/env python3
"""Embed the XNU (Darwin) UEFI loader + kernel into a Mu-Silicium checkout.

Usage:
    embed.py <mu_root> <BOOTAA64.EFI> <kernel.bin>

What it does (all edits are idempotent):
  1. Copies BOOTAA64.EFI -> <mu>/Platforms/Xiaomi/peridotPkg/XnuLoader/
  2. Copies kernel.bin   -> <mu>/Resources/Xnu/kernel.bin
  3. Builds BOOTAA64.EFI into the peridot FV as an APPLICATION file (a new GUID).
  4. Hooks PlatformBootManagerLib (BootManager.c) to LoadImage+StartImage it
     by that GUID right after the console is up.
  5. Teaches build_uefi.py to append [pad|XNUKERN1|size|kernel.bin] AFTER the
     UEFI FD in the Android boot image. BootShim copies only FD_SIZE bytes, so
     the appended kernel survives in RAM; the loader finds it by magic.
"""
import sys
import struct  # noqa: F401  (kept for parity with patched build_uefi.py)
from pathlib import Path

LOADER_GUID = "7A3B1C2D-4E5F-4061-8234-9A0B1C2D3E4F"
LOADER_DIR = Path("Platforms/Xiaomi/peridotPkg/XnuLoader")
FDF = Path("Platforms/Xiaomi/peridotPkg/peridot.fdf")
TOML = Path("Resources/Configs/peridot.toml")
BUILD_UEFI = Path("build_uefi.py")
BOOTMGR = Path("Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/BootManager.c")
BOOTMGR_INF = Path("Silicon/Silicium/SiliciumPkg/Library/BootManagerLib/BootManagerLib.inf")


def patch(path: Path, old: str, new: str, label: str):
    text = path.read_text()
    if new in text:
        print(f"  = {label}: already applied")
        return
    if old not in text:
        raise SystemExit(f"!! {label}: anchor not found in {path} (upstream changed?)")
    if text.count(old) != 1:
        raise SystemExit(f"!! {label}: anchor not unique in {path} ({text.count(old)}x)")
    path.write_text(text.replace(old, new))
    print(f"  + {label}")


BOOTMGR_FUNC = '''STATIC CONST CHAR8 *ComboMessage = "[Volume Up] Boot Manager";

//
// XNU (Darwin) loader: embedded UEFI application in the peridot FV (peridot.fdf).
// We LoadImage/StartImage it by GUID once the console (and GOP) are up. If the
// loader reaches its hand-off path it runs ExitBootServices and branches to the
// kernel entry point, so this call never returns in that case.
//
STATIC EFI_GUID gXnuLoaderFileGuid = { 0x7A3B1C2D, 0x4E5F, 0x4061, { 0x82, 0x34, 0x9A, 0x0B, 0x1C, 0x2D, 0x3E, 0x4F } };

STATIC
VOID
LaunchXnuLoader (
  VOID
  )
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  FVFILE_DEVICE_PATH       *FvFile;
  EFI_HANDLE                ImageHandle;
  UINTN                     Size;

  Size       = sizeof (FVFILE_DEVICE_PATH) + sizeof (EFI_DEVICE_PATH_PROTOCOL);
  DevicePath = AllocateZeroPool (Size);
  if (DevicePath == NULL) {
    return;
  }

  FvFile                 = (FVFILE_DEVICE_PATH *)DevicePath;
  FvFile->Header.Type    = MESSAGING_DEVICE_PATH;
  FvFile->Header.SubType = MSG_FV_FILEPATH_DP;
  SetDevicePathNodeLength (&FvFile->Header, sizeof (FVFILE_DEVICE_PATH));
  CopyMem (&FvFile->NameGuid, &gXnuLoaderFileGuid, sizeof (EFI_GUID));
  SetDevicePathEndNode ((EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)DevicePath + sizeof (FVFILE_DEVICE_PATH)));

  DEBUG ((DEBUG_INFO, "XNU: launching embedded loader (FvFile %g)\\n", &gXnuLoaderFileGuid));

  Status = gBS->LoadImage (FALSE, gImageHandle, DevicePath, NULL, 0, &ImageHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "XNU: LoadImage failed: %r\\n", Status));
    FreePool (DevicePath);
    return;
  }

  Status = gBS->StartImage (ImageHandle, NULL, NULL);
  DEBUG ((DEBUG_INFO, "XNU: loader returned: %r\\n", Status));

  gBS->UnloadImage (ImageHandle);
  FreePool (DevicePath);
}

VOID
EFIAPI
PlatformBootManagerBeforeConsole ()'''


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    mu = Path(sys.argv[1]).resolve()
    efi = Path(sys.argv[2]).resolve()
    kernel = Path(sys.argv[3]).resolve()

    for p in (mu / FDF, mu / TOML, mu / BUILD_UEFI, mu / BOOTMGR, mu / BOOTMGR_INF):
        if not p.is_file():
            raise SystemExit(f"!! not a Mu-Silicium checkout? missing {p}")

    print("[*] copying payloads")
    dst = mu / LOADER_DIR
    dst.mkdir(parents=True, exist_ok=True)
    (dst / "BOOTAA64.EFI").write_bytes(efi.read_bytes())
    (mu / "Resources/Xnu").mkdir(parents=True, exist_ok=True)
    (mu / "Resources/Xnu/kernel.bin").write_bytes(kernel.read_bytes())
    print(f"  + {LOADER_DIR}/BOOTAA64.EFI ({efi.stat().st_size} bytes)")
    print(f"  + Resources/Xnu/kernel.bin ({kernel.stat().st_size} bytes)")

    print("[*] patching FDF")
    patch(mu / FDF,
          "  !include QcomPkg/Extra.fdf.inc",
          f"  # XNU (Darwin) loader - embedded UEFI application, launched by BootManagerLib.\n"
          f"  FILE APPLICATION = {LOADER_GUID} {{\n"
          f"    SECTION PE32 = peridotPkg/XnuLoader/BOOTAA64.EFI\n"
          f"  }}\n\n"
          f"  !include QcomPkg/Extra.fdf.inc",
          "peridot.fdf: embed BOOTAA64.EFI")

    print("[*] patching peridot.toml")
    patch(mu / TOML,
          '[boot_image_kernel]\nkernel_compression = "gzip"\nappend_dtb         = true',
          '[boot_image_kernel]\nkernel_compression = "gzip"\nappend_dtb         = true\nxnu_kernel         = "Resources/Xnu/kernel.bin"',
          "peridot.toml: xnu_kernel path")

    print("[*] patching build_uefi.py")
    patch(mu / BUILD_UEFI,
          "import os\nimport sys",
          "import os\nimport struct\nimport sys",
          "build_uefi.py: import struct")
    patch(mu / BUILD_UEFI,
          "            # Read File Content\n"
          "            combined_data = boot_shim_payload.read_bytes () + uefi_fd.read_bytes ()\n\n"
          "            # Write Combined File Content\n"
          "            boot_shim_fd.write_bytes (combined_data)",
          "            # Read File Content\n"
          "            combined_data = boot_shim_payload.read_bytes () + uefi_fd.read_bytes ()\n\n"
          "            # Append the XNU (Darwin) kernel after the FD. BootShim copies exactly\n"
          "            # FD_SIZE bytes from its payload to FD_BASE, so this tail is left in RAM\n"
          "            # for the embedded loader to locate (16-byte aligned magic 'XNUKERN1').\n"
          "            xnu_kernel_rel = image_kernel_config.get (\"xnu_kernel\")\n"
          "            if xnu_kernel_rel:\n"
          "                xnu_kernel_path = Path (xnu_kernel_rel)\n"
          "                if not xnu_kernel_path.is_file ():\n"
          "                    logger.error (f\"The XNU kernel \\\"{xnu_kernel_rel}\\\" is missing!\")\n"
          "                    return False\n"
          "                xnu_data = xnu_kernel_path.read_bytes ()\n"
          "                pad_align = (-len (combined_data)) % 16\n"
          "                combined_data += (b\"\\x00\" * pad_align) + b\"XNUKERN1\" + struct.pack (\"<Q\", len (xnu_data)) + xnu_data\n"
          "                logger.info (f\"Appended XNU kernel payload ({len (xnu_data)} bytes) after UEFI FD.\")\n\n"
          "            # Write Combined File Content\n"
          "            boot_shim_fd.write_bytes (combined_data)",
          "build_uefi.py: append XNU payload")

    print("[*] patching BootManagerLib")
    patch(mu / BOOTMGR_INF,
          "  DevicePathLib\n  PrintLib",
          "  DevicePathLib\n  PrintLib\n  BaseMemoryLib",
          "BootManagerLib.inf: BaseMemoryLib")
    patch(mu / BOOTMGR,
          '#include <Configuration/BootDevices.h>\n\n#include "BootManager.h"',
          '#include <Configuration/BootDevices.h>\n\n'
          '#include <Protocol/DevicePath.h>\n'
          '#include <Library/DevicePathLib.h>\n'
          '#include <Library/MemoryAllocationLib.h>\n'
          '#include <Library/BaseMemoryLib.h>\n\n'
          '#include "BootManager.h"',
          "BootManager.c: includes")
    patch(mu / BOOTMGR,
          'STATIC CONST CHAR8 *ComboMessage = "[Volume Up] Boot Manager";\n\nVOID\nEFIAPI\nPlatformBootManagerBeforeConsole ()',
          BOOTMGR_FUNC,
          "BootManager.c: LaunchXnuLoader()")
    patch(mu / BOOTMGR,
          "  // Execute Secondary After Console\n  DeviceBootManagerAfterConsole ();\n}",
          "  // Execute Secondary After Console\n  DeviceBootManagerAfterConsole ();\n\n"
          "  // Launch the embedded XNU (Darwin) loader. If it reaches its hand-off it\n"
          "  // ExitBootServices + branches to the kernel, so this never returns.\n"
          "  LaunchXnuLoader ();\n}",
          "BootManager.c: call LaunchXnuLoader()")

    print("[*] done. Now build with:  python3 build_uefi.py -d peridot")


if __name__ == "__main__":
    main()
