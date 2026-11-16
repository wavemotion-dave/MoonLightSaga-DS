// =====================================================================================
// Copyright (c) 2026 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated
// readme files, with or without modification, are permitted in any medium without
// royalty provided this copyright notice is used and wavemotion-dave and
// Marat Fayzullin (fMSX core) are thanked profusely.
//
// The MoonLightSaga-DS emulator is offered as-is, without any warranty. Please see readme.md
// =====================================================================================
#ifndef _MSX_GENERIC_H_
#define _MSX_GENERIC_H_

#include "MoonLightSaga-DS.h"
#include "msx_music.h"
#include "cpu/z80/Z80_interface.h"
#include "cpu/vdp9938/vdp9938.h"
#include "cpu/ay38910/AY38910.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define MSXROM                      0x01
#define DIRECTORY                   0x02

#define ID_SHM_CANCEL               0x00
#define ID_SHM_YES                  0x01
#define ID_SHM_NO                   0x02

#define DPAD_NORMAL                 0
#define DPAD_DIAGONALS              1
#define DPAD_ARKANOID               2
#define DPAD_SLIDE_N_GLIDE          3

typedef struct {
  char szName[MAX_FILE_NAME_LEN+1];
  u8 uType;
  u32 uCrc;
} FI_MSX;

struct __attribute__((__packed__)) GlobalConfig_t
{
    u16 config_ver;
    u32 bios_checksums;
    char szLastRom[MAX_FILE_NAME_LEN+1];
    char szLastPath[MAX_FILE_NAME_LEN+1];
    char reserved1[MAX_FILE_NAME_LEN+1];
    char reserved2[MAX_FILE_NAME_LEN+1];
    u8  showFPS;
    u8  bDiskSounds;
    u32 config_checksum;
};

struct __attribute__((__packed__)) Config_t
{
    u32 game_crc;
    u8  keymap[12];
    u8  msxMapper;
    u8  maxSprites;
    u8  expansion;
    u8  yOffset;
    u8  frameSkip;
    u8  ymScale;
};

#define COMPRESS_BUFFER ((u8 *)(ROM_Memory + (MAX_CART_SIZE_KB * 1024) - (256 * 1024)))   // We use the back-end 256K of the ROM buffer for compression

#define NORAM                       0xFF    // When reading IO that is unmapped... we just return 0xFF

#define MACHINE_MSX2                0x00    // Standard MSX2 slot layout (slot 3 expanded with RAM in slot 3-3)
#define MACHINE_MSX1                0x01    // Standard MSX1 slot layout (nothing expanded, RAM in Slot 2)

#define EXP_NONE                    0       // No special expansion
#define EXP_FM_PAC                  1       // FM PAC / MSX-MUSIC

#define FS_NONE                     0       // No Frame Skip
#define FS_LIGHT                    1       // Light Frame Skip
#define FS_AGGRESSIVE               2       // Aggressive Frame Skip

#define YM_BAL_LOW                  0       // Low YM volume mix
#define YM_BAL_MED                  1       // Medium YM volume mix
#define YM_BAL_HIGH                 2       // High YM volume mix
#define YM_BAL_MAX                  3       // Max (normal) YM volume mix

#define VDP_9938                    0
#define VDP_9958                    4

extern struct Config_t       myConfig;
extern struct GlobalConfig_t myGlobalConfig;

extern u8 special_memory_access;                // 0x00 if no special access, otherwise one or more of the SPEC_MEM_xxx below

#define SPEC_MEM_SUBSLOT_ACTIVE     0x01        // Allows read/write to special SubSlot register at 0xFFFF
#define SPEC_MEM_SUBSLOT0_ACTIVE    0x02        // Allows read/write to special SubSlot register at 0xFFFF
#define SPEC_MEM_DISK_CONTROLLER    0x04        // Standard Memory-Mapped Disk Controller active in Page 1

extern u8 last_special_key;
extern u8 last_special_key_dampen;
extern u16 msx_init;
extern u16 msx_basic;
extern u8 skip_render;
extern u16 timingFrames;
extern s8 temp_offset;

extern u8 msx_caps_lock;
extern u8 msx_kana_lock;
extern u8 mirror_ram_bank[4];

extern u8 zanac_glitch_patch;

// Mapper Types
#define GUESS           0
#define MIRRORED        1
#define KON8            2
#define ASC8            3
#define SCC8            4
#define ASC16           5
#define GENERIC_8K      6
#define GENERIC_16K     7
#define XBLAM           8
#define SUPERLR         9
#define XEVIOUS         10
#define ASC8SRAM8       11
#define ASC8SRAM2       12
#define ASC16SRAM2      13
#define ASC16SRAM8      14
#define AT0K            15
#define AT4K            16
#define AT8K            17
#define LIN64           18
#define PAGE2           19
#define ROM_NORMAL      20
#define RESERVED_1      21
#define RESERVED_2      22
#define RESERVED_3      23
#define RTYPE           24  // Not one of the general mappers a user can pick
#define GAME_MASTER2    25  // Not one of the general mappers a user can pick
#define MAJUT           26  // Not one of the general mappers a user can pick
#define NAMCO           27  // Not one of the general mappers a user can pick
#define WIZARDRY        28  // Not one of the general mappers a user can pick
#define KOEISRAM32      29  // Not one of the general mappers a user can pick
#define MANBOW2         30  // Not one of the general mappers a user can pick
#define HAMARAJANIGHT   31  // Not one of the general mappers a user can pick
#define RBSC_FLASH      32  // Not one of the general mappers a user can pick
#define MATRACOMP       33  // Not one of the general mappers a user can pick
#define MSXDOS2         34  // Not one of the general mappers a user can pick
#define MEGA_SCC        35  // For our special MEGARAM SCC 'Cart'
#define FMPAC_SRAM      36  // For our special FM-PAC 'Cart'
#define SCCPLUS_RAM     37  // For our special SCC+ 'Cart'
#define END_MAPPERS     38

#define MAX_GUESS_MAPPER 8   // The highest guess we can guess when examining ROM data

#define    MAX_CART_SIZE_KB     512
#define    MAX_DISK_SIZE_KB     720
#define    RAM_SIZE_KB         2048

extern u8 *DISK_CACHE[3];

extern u8 *ROM_Memory;
extern u8 *DISK_Memory;
extern u8 *DISK_Memory1;
extern u8 *RAM_Memory;
extern u8 SRAM_Memory[0x2000];

extern u8 TURBO_BIOS[32*1024];
extern u8 TURBO_EXT[16*1024];
extern u8 TURBO_FMPAC[16*1024];
extern u8 TURBO_DISK[16*1024];
extern u8 TURBO_KANJI[32*1024];
extern u8 TURBO_OPENING[16*1024];
extern u8 KANJI_FONT[256*1024];

extern u8 s1990_is_r800;
extern u8 bCartInPage[4];
extern u8 bRAMInPage[4];
extern u8 *MSXCartPtr[2][8];
extern u8 *MSXRamPtr[8];
extern u8 *MemoryMap[8];
extern u8  sccplus_page[4];
extern u8  sccplus_mode;
extern AY38910 myAY;
extern YM      myYM;
extern u8 msx_scc_enable;
extern u8 JoyMode;
extern u8 bClickRemoveNeeded;
extern u32 JoyState;
extern u8 Port_PPI_A;
extern u8 Port_PPI_B;
extern u8 Port_PPI_C;
extern void ProcessBufferedKeys(void);
extern u8 BufferedKeys[32];
extern u8 BufferedKeysWriteIdx;
extern u8 BufferedKeysReadIdx;
extern u8 key_shift;
extern u8 key_ctrl;
extern u8 key_kana;
extern u8 key_graph;
extern u8 msx_music_capable_game;
extern u8 msx_subslot;
extern u8 msx_subslot0;
extern u8 XPalReal0;
extern u8 ALatch;
extern u32 frame_number;
extern u8 CurrentEpochSaved;
extern u8 msx_irq_pending;
extern u8 palette_latch;
extern u8 OccBuf[320];
extern u8 zoom_screen;
extern u8 bMainMenuActive;

// --------------------------------------------------
// Some CPU and VDP and SGM stuff that we need
// --------------------------------------------------
extern void Loop9938(void);
extern void msxUpdateScreen(void);
extern void LoadFileAndComputeCRC(u8 media_id);
extern void msxLoadState();
extern void msxSaveState();
extern void msxWipeRAM(void);
extern void msx_reset(void);
extern void msx_restore_bios(void);
extern void BufferKey(u8 key);
extern void BufferKeys(char *str);
extern void MSX_InitialMemoryLayout(void);
extern void msxSaveSRAM(void);
extern void msxLoadSRAM(void);
extern void Z80_Interface_Reset(void);
extern void LoadFavorites(void);
extern void preserveCompressedMem(void);
extern void restoreCompressedMem(void);
extern void BuildScreen8ColorMap(void);
extern void allocateCompressedMem(void);
extern void restoreCompressedMem(void);
extern void LoadConfigDatabase(void);
extern void MoonLightSagaFindFiles(u8 media_id);
extern void MoonLightSagaMainMenu(void);
extern void DSPrint(int iX,int iY,int iScr,char *szMessage);
extern void DSPrint_fps(u16 fps);
extern void MoonLightSagaChangeKeymap(void);
extern void MoonLightSagaGameOptions(bool);
extern void FadeToColor(unsigned char ucSens, unsigned short ucBG, unsigned char ucScr, unsigned char valEnd, unsigned char uWait);
extern u32  ReadFileCarefully(char *filename, u8 *buf, u32 buf_size, u32 buf_offset, u32 *crc);
extern u8   MoonLightSagaChooseFile(u8 media_id, u8 allow_dir_change);
extern u8   showMessage(char *szCh1, char *szCh2);
extern u32  LoopZ80(void);
extern u8   RomDB_Lookup(u8 media_id);
extern void msxInit(void);
extern void IndirectRegWrite9938(u8 Value);
extern void EnsureSaveDirectory(void);
extern void intro_logo(void);
extern void UpperScreenMainMenu(void);
extern void ShowInstructions(void);


#endif
