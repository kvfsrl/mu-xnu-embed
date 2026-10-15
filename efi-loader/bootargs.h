/* XNU arm64 boot_args, mirrored from
 *   xnu/pexpert/pexpert/arm64/boot.h
 * The kernel entry (_start in osfmk/arm64/start.s) expects x0 = physical
 * pointer to this structure. start_first_cpu reads the fields via the
 * BA_* offsets emitted by osfmk/arm64/genassym.c:
 *   BA_VIRT_BASE         = offsetof(boot_args, virtBase)
 *   BA_PHYS_BASE         = offsetof(boot_args, physBase)
 *   BA_MEM_SIZE          = offsetof(boot_args, memSize)
 *   BA_TOP_OF_KERNEL_DATA= offsetof(boot_args, topOfKernelData)
 *   BA_BOOT_FLAGS        = offsetof(boot_args, bootFlags)
 *
 * NOTE: BOOT_LINE_LENGTH is IBOOT_MAX_ENV_VAR_DATA_SIZE in a real kernel
 * build (that header lives in the iBoot SDK). The public xnu tree falls
 * back to 1024 for simulators, which is what we assume here. If the target
 * kernel was built with a different value, only the bootFlags/memSizeActual
 * offsets shift; virtBase/physBase/memSize/topOfKernelData do not.
 */
#ifndef BOOTARGS_H
#define BOOTARGS_H

#include "efi.h"

#define kBootArgsRevision2   2
#define kBootArgsVersion2    2

#ifndef BOOT_LINE_LENGTH
#define BOOT_LINE_LENGTH 1024
#endif

typedef struct Boot_Video {
    UINT64 v_baseAddr;
    UINT64 v_display;
    UINT64 v_rowBytes;
    UINT64 v_width;
    UINT64 v_height;
    UINT64 v_depth;
} Boot_Video;

typedef struct boot_args {
    UINT16    Revision;
    UINT16    Version;
    UINT64    virtBase;
    UINT64    physBase;
    UINT64    memSize;
    UINT64    topOfKernelData;
    Boot_Video Video;
    UINT32    machineType;
    UINT32    _pad_machineType;
    VOID     *deviceTreeP;
    UINT32    deviceTreeLength;
    char      CommandLine[BOOT_LINE_LENGTH];
    UINT64    bootFlags;
    UINT64    memSizeActual;
} boot_args;

#define kBootFlagsDarkBoot (1ULL << 0)

#endif /* BOOTARGS_H */
