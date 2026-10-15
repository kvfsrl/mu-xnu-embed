/* Minimal, self-contained UEFI definitions (no gnu-efi needed).
 * Only the subset used by the loader is declared. Field order for
 * EFI_BOOT_SERVICES must match the UEFI spec exactly. */
#ifndef EFI_H
#define EFI_H

typedef unsigned char       UINT8;
typedef unsigned short      UINT16;
typedef unsigned int        UINT32;
typedef unsigned long long  UINT64;
typedef signed long long    INT64;
typedef UINT64              UINTN;
typedef UINT16              CHAR16;
typedef unsigned char       BOOLEAN;
typedef void                VOID;

typedef UINT64 EFI_STATUS;
typedef VOID  *EFI_HANDLE;
typedef UINT64 EFI_PHYSICAL_ADDRESS;

#define NULL ((VOID *)0)
#define EFI_SUCCESS 0ULL
#define EFI_ERR(x) (((INT64)(x)) < 0)

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/* EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL - we only use OutputString (2nd entry). */
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID *Reset;
    EFI_STATUS (*OutputString)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
    VOID *TestString;
    VOID *QueryMode;
    VOID *SetMode;
    VOID *SetAttribute;
    VOID *ClearScreen;
    VOID *SetCursorPosition;
    VOID *EnableCursor;
    VOID *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    VOID *RaiseTPL;
    VOID *RestoreTPL;
    EFI_STATUS (*AllocatePages)(UINT32 Type, UINT32 MemoryType, UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
    EFI_STATUS (*GetMemoryMap)(UINTN *MemoryMapSize, VOID *MemoryMap, UINTN *MapKey, UINTN *DescriptorSize, UINT32 *DescriptorVersion);
    EFI_STATUS (*AllocatePool)(UINT32 PoolType, UINTN Size, VOID **Buffer);
    EFI_STATUS (*FreePool)(VOID *Buffer);
    VOID *CreateEvent;
    VOID *SetTimer;
    VOID *WaitForEvent;
    VOID *SignalEvent;
    VOID *CloseEvent;
    VOID *CheckEvent;
    VOID *InstallProtocolInterface;
    VOID *ReinstallProtocolInterface;
    VOID *UninstallProtocolInterface;
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface);
    VOID *Reserved;
    VOID *RegisterProtocolNotify;
    VOID *LocateHandle;
    VOID *LocateDevicePath;
    VOID *InstallConfigurationTable;
    VOID *LoadImage;
    VOID *StartImage;
    VOID *Exit;
    VOID *UnloadImage;
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);
    VOID *GetNextMonotonicCount;
    VOID *Stall;
    VOID *SetWatchdogTimer;
    VOID *ConnectController;
    VOID *DisconnectController;
    VOID *OpenProtocol;
    VOID *CloseProtocol;
    VOID *OpenProtocolInformation;
    VOID *ProtocolsPerHandle;
    VOID *LocateHandleBuffer;
    VOID *LocateProtocol;
    VOID *InstallMultipleProtocolInterfaces;
    VOID *UninstallMultipleProtocolInterfaces;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    UINT32 _pad0;
    EFI_HANDLE ConsoleInHandle;
    VOID *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    VOID *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    VOID *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    UINT32 Revision;
    UINT32 _pad0;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    VOID *FilePath;
    VOID *Reserved;
    UINT32 LoadOptionsSize;
    UINT32 _pad1;
    VOID *LoadOptions;
    VOID *ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType;
    UINT32 ImageDataType;
    EFI_PHYSICAL_ADDRESS Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

struct _EFI_FILE_PROTOCOL;
typedef struct _EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*Open)(struct _EFI_FILE_PROTOCOL *, struct _EFI_FILE_PROTOCOL **, CHAR16 *, UINT64, UINT64);
    EFI_STATUS (*Close)(struct _EFI_FILE_PROTOCOL *);
    EFI_STATUS (*Delete)(struct _EFI_FILE_PROTOCOL *);
    EFI_STATUS (*Read)(struct _EFI_FILE_PROTOCOL *, UINTN *, VOID *);
    EFI_STATUS (*Write)(struct _EFI_FILE_PROTOCOL *, UINTN *, VOID *);
    EFI_STATUS (*GetPosition)(struct _EFI_FILE_PROTOCOL *, UINT64 *);
    EFI_STATUS (*SetPosition)(struct _EFI_FILE_PROTOCOL *, UINT64);
    EFI_STATUS (*GetInfo)(struct _EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN *, VOID *);
    EFI_STATUS (*SetInfo)(struct _EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN, VOID *);
    EFI_STATUS (*Flush)(struct _EFI_FILE_PROTOCOL *);
} EFI_FILE_PROTOCOL;

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*OpenVolume)(struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* UEFI constants */
#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_MAX_ADDRESS 1
#define EFI_ALLOCATE_ADDRESS 2
#define EfiLoaderData 2
#define EFI_FILE_MODE_READ 0x0000000000000001ULL

/* Standard GUIDs */
static EFI_GUID gEfiLoadedImageProtocolGuid = { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static EFI_GUID gEfiSimpleFileSystemProtocolGuid = { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static EFI_GUID gEfiFileInfoGuid = { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };

#endif /* EFI_H */
