/* Minimal 64-bit Mach-O definitions (arm64) for reading an XNU kernel image. */
#ifndef MACHO_H
#define MACHO_H

typedef unsigned int   u32;
typedef unsigned long long u64;
typedef long long      i64;

#define MH_MAGIC_64   0xfeedfacfU
#define CPU_TYPE_ARM64 0x0100000cU

#define LC_SEGMENT_64  0x19U
#define LC_SYMTAB      0x02U
#define LC_UNIXTHREAD  0x05U
#define LC_MAIN        0x80000028U

#define ARM_THREAD_STATE64 6U
/* offset of arm_thread_state64.pc inside an LC_UNIXTHREAD load command */
#define ARM_THREAD_PC_OFF  272U

typedef struct {
    u32 magic;
    u32 cputype;
    u32 cpusubtype;
    u32 filetype;
    u32 ncmds;
    u32 sizeofcmds;
    u32 flags;
    u32 reserved;
} mach_header_64;

typedef struct {
    u32 cmd;
    u32 cmdsize;
} load_command;

typedef struct {
    u32 cmd;
    u32 cmdsize;
    char segname[16];
    u64 vmaddr;
    u64 vmsize;
    u64 fileoff;
    u64 filesize;
    u32 maxprot;
    u32 initprot;
    u32 nsects;
    u32 flags;
} segment_command_64;

typedef struct {
    u32 cmd;
    u32 cmdsize;
    u32 symoff;
    u32 nsyms;
    u32 stroff;
    u32 strsize;
} symtab_command;

typedef struct {
    u32 n_strx;
    unsigned char n_type;
    unsigned char n_sect;
    unsigned short n_desc;
    u64 n_value;
} nlist_64;

typedef struct {
    u32 cmd;
    u32 cmdsize;
    u64 entryoff;
    u64 stacksize;
} entry_point_command;

#endif /* MACHO_H */
