/* BOOTAA64.EFI - minimal UEFI (AArch64) bootloader that loads an XNU (Darwin)
 * kernel Mach-O image from the boot volume and hands control to its entry point.
 *
 * DEMO ONLY. This packages a Darwin kernel into a UEFI-loadable payload to show
 * how "wrapping" works. It does NOT make the kernel bootable on non-Apple SoCs:
 * XNU requires Apple hardware abstraction (AIC, Apple device tree from iBoot,
 * Apple PMU/UART/DART) and is built arm64e for Apple silicon. On a Snapdragon
 * (SM8635 / peridot) it will fault immediately regardless of this loader.
 *
 * On handoff it builds an arm64 `boot_args` (see bootargs.h, mirrored from
 * xnu/pexpert/pexpert/arm64/boot.h) and enters the kernel per the arm64 XNU
 * convention: x0 = physical address of boot_args (start.s reads virtBase /
 * physBase / memSize / topOfKernelData / bootFlags, which is why the offsets
 * must match the kernel build exactly).
 *
 * Default build only loads + reports. Define DO_JUMP=1 to actually
 * ExitBootServices and branch to the kernel entry.
 */
#include "efi.h"
#include "macho.h"
#include "bootargs.h"

#ifndef DO_JUMP
#define DO_JUMP 0
#endif

/* Where the kernel image lives on the boot volume (FAT ESP). */
static CHAR16 KERNEL_PATH[] = L"\\kernel.bin";

/* Passed to XNU via boot_args.CommandLine. -v keeps it verbose so a fault is
 * actually printed; the rest mirrors what a dev board would pass. */
static const char DEFAULT_BOOTARGS[] =
    "-v keepsyms=1 debug=0x144 serial=3 -unsafe_kernel_text";

/* EFI_MEMORY_DESCRIPTOR (40 bytes) - only used to size DRAM. */
typedef struct {
    UINT32 Type;
    UINT32 Pad;
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} efi_memory_descriptor;

/* ---- tiny console helpers -------------------------------------------------- */
static void efi_print(EFI_SYSTEM_TABLE *st, const char *s)
{
    CHAR16 buf[256];
    UINTN n = 0;
    if (!st || !st->ConOut || !st->ConOut->OutputString)
        return;
    while (*s && n < 255)
        buf[n++] = (CHAR16)(unsigned char)*s++;
    buf[n] = 0;
    st->ConOut->OutputString(st->ConOut, buf);
}

static void efi_print_hex(EFI_SYSTEM_TABLE *st, UINT64 v)
{
    char out[19];
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 16; i++)
        out[2 + i] = "0123456789abcdef"[(v >> (60 - i * 4)) & 0xf];
    out[18] = 0;
    efi_print(st, out);
}

static void efi_print_dec(EFI_SYSTEM_TABLE *st, UINT64 v)
{
    char out[21];
    int i = 20;
    out[i] = 0;
    if (v == 0) out[--i] = '0';
    while (v) { out[--i] = (char)('0' + (v % 10)); v /= 10; }
    efi_print(st, &out[i]);
}

/* Draw a full-width colored strip via GOP so we have a visible beacon even if
 * the text console is not rendered during BDS. line selects the vertical slot. */
static void efi_beacon(EFI_SYSTEM_TABLE *st, UINTN line, UINT32 r, UINT32 g, UINT32 b)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS (*LocateProtocol)(EFI_GUID *, VOID *, VOID **);
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL px;
    UINTN w, h, y;

    LocateProtocol = (EFI_STATUS (*)(EFI_GUID *, VOID *, VOID **))bs->LocateProtocol;
    if (EFI_ERR(LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gop)))
        return;
    if (!gop || !gop->Mode || !gop->Mode->Info)
        return;

    w = gop->Mode->Info->HorizontalResolution;
    h = gop->Mode->Info->VerticalResolution;
    if (w == 0)
        return;

    px.Red = r; px.Green = g; px.Blue = b; px.Reserved = 0;
    y = (line * 60) % (h > 60 ? h - 60 : 1);
    gop->Blt(gop, &px, EfiBltVideoFill, 0, 0, 0, y, w, 40, 0);
}

/* ---- read the whole kernel file into a pool buffer ------------------------- */
static EFI_STATUS load_kernel_file(EFI_HANDLE image, EFI_SYSTEM_TABLE *st,
                                   VOID **out_buf, UINTN *out_size)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL, *file = NULL;
    EFI_STATUS s;
    unsigned char info[512];
    UINTN info_size = sizeof(info);
    UINT64 fsize = 0;
    VOID *buf = NULL;
    UINTN rd;

    s = bs->HandleProtocol(image, &gEfiLoadedImageProtocolGuid, (VOID **)&li);
    if (EFI_ERR(s) || !li) return s;
    s = bs->HandleProtocol(li->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&fs);
    if (EFI_ERR(s) || !fs) return s;
    s = fs->OpenVolume(fs, &root);
    if (EFI_ERR(s)) return s;
    s = root->Open(root, &file, KERNEL_PATH, EFI_FILE_MODE_READ, 0);
    if (EFI_ERR(s)) { root->Close(root); return s; }

    /* EFI_FILE_INFO: FileSize is at offset 8 */
    s = file->GetInfo(file, &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERR(s) && info_size <= sizeof(info))
        *(&fsize) = *(UINT64 *)(info + 8); /* best effort */
    else
        fsize = *(UINT64 *)(info + 8);

    if (fsize == 0) { file->Close(file); root->Close(root); return 1; }

    s = bs->AllocatePool(EfiLoaderData, (UINTN)fsize, &buf);
    if (EFI_ERR(s)) { file->Close(file); root->Close(root); return s; }

    rd = (UINTN)fsize;
    s = file->Read(file, &rd, buf);
    file->Close(file);
    root->Close(root);
    if (EFI_ERR(s)) { bs->FreePool(buf); return s; }

    *out_buf = buf;
    *out_size = rd;
    return EFI_SUCCESS;
}

static UINTN align_up(UINTN v, UINTN a) { return (v + a - 1) & ~(a - 1); }

/* ---- load the Mach-O kernel into memory, return its entry point ------------ */
static EFI_STATUS load_macho(EFI_SYSTEM_TABLE *st, VOID *img, UINTN img_size,
                             UINT64 *out_entry, UINT64 *out_base, UINT64 *out_size,
                             UINT64 *out_virt_base)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    mach_header_64 *mh = (mach_header_64 *)img;
    UINT8 *base = (UINT8 *)img;
    load_command *lc;
    UINT64 min_va = ~0ULL, max_va = 0, text_va = 0;
    UINT64 symoff_n = 0, nsyms_n = 0, stroff_n = 0;
    UINT64 entryoff = 0, thread_pc = 0;
    int have_main = 0, have_text = 0, have_thread = 0;
    EFI_PHYSICAL_ADDRESS alloc = 0, alloc_base;
    UINTN pages, span;
    UINT64 slide;
    UINTN i, off;

    if (mh->magic != MH_MAGIC_64) {
        efi_print(st, "kernel.bin: bad Mach-O magic\r\n");
        return 1;
    }

    /* pass 1: geometry + symbol table + entry */
    off = sizeof(mach_header_64);
    for (i = 0; i < mh->ncmds; i++) {
        lc = (load_command *)(base + off);
        if (lc->cmd == LC_SEGMENT_64) {
            segment_command_64 *sc = (segment_command_64 *)lc;
            if (sc->vmsize) {
                if (sc->vmaddr < min_va) min_va = sc->vmaddr;
                if (sc->vmaddr + sc->vmsize > max_va) max_va = sc->vmaddr + sc->vmsize;
            }
            if (sc->segname[0] == '_' && sc->segname[1] == '_' &&
                sc->segname[2] == 'T' && sc->segname[3] == 'E' &&
                sc->segname[4] == 'X' && sc->segname[5] == 'T' &&
                sc->segname[6] == 0) {
                text_va = sc->vmaddr;
                have_text = 1;
            }
        } else if (lc->cmd == LC_SYMTAB) {
            symtab_command *sy = (symtab_command *)lc;
            symoff_n = sy->symoff; nsyms_n = sy->nsyms; stroff_n = sy->stroff;
        } else if (lc->cmd == LC_UNIXTHREAD) {
            UINT32 *w = (UINT32 *)lc;
            if (w[2] == ARM_THREAD_STATE64 && lc->cmdsize >= ARM_THREAD_PC_OFF + 8) {
                thread_pc = *(UINT64 *)((UINT8 *)lc + ARM_THREAD_PC_OFF);
                have_thread = 1;
            }
        } else if (lc->cmd == LC_MAIN) {
            entry_point_command *ep = (entry_point_command *)lc;
            entryoff = ep->entryoff;
            have_main = 1;
        }
        off += lc->cmdsize;
    }

    if (min_va == ~0ULL) { efi_print(st, "kernel.bin: no segments\r\n"); return 1; }

    span = (UINTN)(max_va - min_va);
    pages = align_up(span, 4096) / 4096;
    alloc_base = align_up((UINTN)min_va, 4096);

    /* Prefer the preferred virtual base; fall back to anywhere (KASLR-ish slide). */
    if (EFI_ERR(bs->AllocatePages(EFI_ALLOCATE_ADDRESS, EfiLoaderData, pages, &alloc_base))) {
        alloc = 0;
        if (EFI_ERR(bs->AllocatePages(EFI_ALLOCATE_ANY_PAGES, EfiLoaderData, pages, &alloc))) {
            efi_print(st, "kernel.bin: out of memory\r\n");
            return 1;
        }
        slide = (UINT64)alloc - min_va;
    } else {
        alloc = alloc_base;
        slide = 0;
    }

    /* zero the region (covers BSS and inter-segment gaps) */
    {
        UINT8 *p = (UINT8 *)alloc;
        for (i = 0; i < pages * 4096; i++) p[i] = 0;
    }

    /* pass 2: copy segment file data */
    off = sizeof(mach_header_64);
    for (i = 0; i < mh->ncmds; i++) {
        lc = (load_command *)(base + off);
        if (lc->cmd == LC_SEGMENT_64) {
            segment_command_64 *sc = (segment_command_64 *)lc;
            if (sc->filesize && sc->fileoff + sc->filesize <= img_size) {
                UINT8 *src = base + sc->fileoff;
                UINT8 *dst = (UINT8 *)(sc->vmaddr + slide);
                UINT64 k;
                for (k = 0; k < sc->filesize; k++) dst[k] = src[k];
            }
        }
        off += lc->cmdsize;
    }

    /* resolve entry point: LC_UNIXTHREAD pc is authoritative (iBoot reads it) */
    *out_entry = 0;
    if (have_thread)
        *out_entry = thread_pc + slide;
    if (!*out_entry && nsyms_n && symoff_n + nsyms_n * sizeof(nlist_64) <= img_size) {
        nlist_64 *sym = (nlist_64 *)(base + symoff_n);
        for (i = 0; i < nsyms_n; i++) {
            const char *name = (const char *)(base + stroff_n + sym[i].n_strx);
            const char *p = name;
            if (*p == '_') p++;
            if (p[0]=='s'&&p[1]=='t'&&p[2]=='a'&&p[3]=='r'&&p[4]=='t'&&p[5]==0) {
                *out_entry = sym[i].n_value + slide;
                break;
            }
        }
    }
    if (!*out_entry && have_main && have_text)
        *out_entry = text_va + entryoff + slide;
    if (!*out_entry && have_text)
        *out_entry = text_va + slide;
    if (!*out_entry)
        *out_entry = min_va + slide;

    *out_base = (UINT64)alloc;
    *out_size = span;
    *out_virt_base = min_va;
    return EFI_SUCCESS;
}

/* ---- size DRAM from the UEFI memory map (highest physical end) -------------- */
static UINT64 dram_top_from_map(EFI_SYSTEM_TABLE *st)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    UINTN map_size = 0, map_key = 0, desc_size = 0;
    UINT32 desc_ver = 0;
    VOID *map = NULL;
    UINT64 top = 0;
    UINTN n, i;

    bs->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
    map_size += desc_size * 4;
    if (EFI_ERR(bs->AllocatePool(EfiLoaderData, map_size, &map)))
        return 0;
    if (EFI_ERR(bs->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver)))
        return 0;

    n = map_size / desc_size;
    for (i = 0; i < n; i++) {
        efi_memory_descriptor *d = (efi_memory_descriptor *)((UINT8 *)map + i * desc_size);
        UINT64 end = d->PhysicalStart + d->NumberOfPages * 4096;
        if (end > top) top = end;
    }
    bs->FreePool(map);
    return top;
}

/* ---- find a kernel appended after the firmware FD --------------------------- */
/* Mu-Silicium's BootShim (BootShim.S) copies exactly FD_SIZE bytes from its
 * payload (the FD that follows its code) to FD_BASE, then branches there. So
 * anything appended AFTER the FD in the Android boot image survives untouched
 * in RAM. We append [magic | size | kernel.bin] (see build_uefi.py patch) and
 * scan physical RAM for the magic. MMU maps RAM 1:1 in DXE, so PA == VA. */
#define XNUP_MAGIC 0x314E52454B554E58ULL /* "XNUKERN1" little-endian */

typedef struct {
    UINT64 magic;
    UINT64 size;
} xnup_header;

static EFI_STATUS find_appended_kernel(EFI_SYSTEM_TABLE *st,
                                       VOID **out_buf, UINTN *out_size)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    UINTN map_size = 0, map_key = 0, desc_size = 0;
    UINT32 desc_ver = 0;
    VOID *map = NULL;
    UINTN n, i;
    UINT64 off;

    bs->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
    map_size += desc_size * 8;
    if (EFI_ERR(bs->AllocatePool(EfiLoaderData, map_size, &map)))
        return 1;
    if (EFI_ERR(bs->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver))) {
        bs->FreePool(map);
        return 1;
    }

    n = map_size / desc_size;
    for (i = 0; i < n; i++) {
        efi_memory_descriptor *d = (efi_memory_descriptor *)((UINT8 *)map + i * desc_size);
        UINT64 bytes;

        /* scan RAM-backed descriptors; MMIO/port-space (>=11) must be skipped */
        if (d->Type > 10)
            continue;
        bytes = d->NumberOfPages * 4096;
        for (off = 0; off + 16 <= bytes; off += 16) {
            xnup_header *h = (xnup_header *)(UINTN)(d->PhysicalStart + off);
            if (h->magic == XNUP_MAGIC && h->size > 0 && h->size < (256ULL << 20)) {
                *out_buf = (VOID *)(h + 1);
                *out_size = (UINTN)h->size;
                bs->FreePool(map);
                return EFI_SUCCESS;
            }
        }
    }
    bs->FreePool(map);
    return 1;
}

/* ---- build and place the XNU boot_args, return its physical address --------- */
/* start.s (_start) is entered with x0 = physical pointer to boot_args and reads
 * virtBase/physBase/memSize/topOfKernelData/bootFlags via the BA_* offsets. */
static boot_args *build_boot_args(EFI_SYSTEM_TABLE *st,
                                  UINT64 kphys, UINT64 kvirt, UINTN span,
                                  UINT64 dram_top, UINT64 *out_phys)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_PHYSICAL_ADDRESS addr = 0;
    boot_args *ba;
    UINTN i, pages = (sizeof(boot_args) + 4095) / 4096;

    if (EFI_ERR(bs->AllocatePages(EFI_ALLOCATE_ANY_PAGES, EfiLoaderData, pages, &addr)))
        return NULL;
    ba = (boot_args *)(UINTN)addr;
    for (i = 0; i < pages * 4096; i++) ((UINT8 *)ba)[i] = 0;

    ba->Revision = kBootArgsRevision2;
    ba->Version  = kBootArgsVersion2;
    ba->virtBase = kvirt;
    ba->physBase = kphys;
    ba->memSize  = (dram_top > kphys) ? (dram_top - kphys) : (UINT64)span;
    ba->topOfKernelData = kphys + span;
    /* No Apple device tree available; XNU will fault parsing it on non-Apple HW. */
    ba->deviceTreeP = NULL;
    ba->deviceTreeLength = 0;
    ba->machineType = 0;
    for (i = 0; DEFAULT_BOOTARGS[i] && i < BOOT_LINE_LENGTH - 1; i++)
        ba->CommandLine[i] = DEFAULT_BOOTARGS[i];
    ba->CommandLine[i] = 0;
    ba->bootFlags = 0;
    ba->memSizeActual = dram_top;

    *out_phys = (UINT64)addr;
    return ba;
}

/* ---- entry point ----------------------------------------------------------- */
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    VOID *img = NULL;
    UINTN img_size = 0;
    UINT64 entry = 0, base = 0, size = 0, kvirt = 0;
    UINT64 dram_top = 0, ba_phys = 0;
    boot_args *ba = NULL;
    EFI_STATUS s;

    efi_print(st, "\r\n[xnuload] Darwin/XNU loader (Mu-Silicium UEFI AArch64)\r\n");
    efi_beacon(st, 0, 0xFF, 0x00, 0xFF); /* magenta: loader app is running */

    efi_print(st, "[xnuload] scanning RAM for appended XNU kernel...\r\n");
    s = find_appended_kernel(st, &img, &img_size);
    if (EFI_ERR(s)) {
        efi_beacon(st, 1, 0xFF, 0x00, 0x00); /* red: not found by magic */
        efi_print(st, "[xnuload] not found; falling back to \\kernel.bin on ESP\r\n");
        s = load_kernel_file(image, st, &img, &img_size);
        if (EFI_ERR(s)) {
            efi_print(st, "[xnuload] cannot find a kernel image\r\n");
            return s;
        }
    } else {
        efi_beacon(st, 1, 0x00, 0xFF, 0x00); /* green: appended kernel found */
        efi_print(st, "[xnuload] appended kernel @ ");
        efi_print_hex(st, (UINT64)img);
        efi_print(st, "\r\n");
    }
    efi_print(st, "[xnuload] kernel image: ");
    efi_print_dec(st, img_size);
    efi_print(st, " bytes at ");
    efi_print_hex(st, (UINT64)img);
    efi_print(st, "\r\n");

    s = load_macho(st, img, img_size, &entry, &base, &size, &kvirt);
    if (EFI_ERR(s)) return s;

    efi_print(st, "[xnuload] loaded segments at ");
    efi_print_hex(st, base);
    efi_print(st, " (span ");
    efi_print_dec(st, size);
    efi_print(st, ", link base ");
    efi_print_hex(st, kvirt);
    efi_print(st, ")\r\n[xnuload] entry point: ");
    efi_print_hex(st, entry);
    efi_print(st, "\r\n");

    dram_top = dram_top_from_map(st);
    if (dram_top == 0) dram_top = base + size;

    ba = build_boot_args(st, base, kvirt, (UINTN)size, dram_top, &ba_phys);
    if (!ba) {
        efi_print(st, "[xnuload] cannot allocate boot_args\r\n");
        return 1;
    }
    efi_print(st, "[xnuload] boot_args @ ");
    efi_print_hex(st, ba_phys);
    efi_print(st, " physBase=");
    efi_print_hex(st, ba->physBase);
    efi_print(st, " virtBase=");
    efi_print_hex(st, ba->virtBase);
    efi_print(st, " memSize=");
    efi_print_hex(st, ba->memSize);
    efi_print(st, "\r\n[xnuload] cmdline: ");
    efi_print(st, ba->CommandLine);
    efi_print(st, "\r\n");

#if DO_JUMP
    {
        EFI_BOOT_SERVICES *bs = st->BootServices;
        UINTN map_size = 0, map_key = 0, desc_size = 0;
        UINT32 desc_ver = 0;
        VOID *map = NULL;
        void (*kentry)(VOID *);

        bs->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
        map_size += desc_size * 4;
        if (EFI_ERR(bs->AllocatePool(EfiLoaderData, map_size, &map))) {
            efi_print(st, "[xnuload] no mem for map\r\n");
            return 1;
        }
        for (;;) {
            map_size = 0;
            bs->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver);
            if (!EFI_ERR(bs->ExitBootServices(image, map_key))) break;
        }
        efi_print(st, "[xnuload] ExitBootServices done, jumping to XNU _start\r\n");
        efi_beacon(st, 2, 0xFF, 0xFF, 0x00); /* yellow: about to jump */
        /* x0 = physical address of boot_args (arm64 XNU entry convention). */
        kentry = (void (*)(VOID *))(UINTN)entry;
        kentry((VOID *)ba_phys);
    }
#else
    efi_print(st, "[xnuload] dry-run (built without DO_JUMP): not transferring control\r\n");
#endif
    return EFI_SUCCESS;
}
