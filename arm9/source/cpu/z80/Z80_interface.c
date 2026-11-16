// =====================================================================================
// Copyright (c) 2026 Dave Bernazzani (wavemotion-dave)
//
// Copying and distribution of this emulator, its source code and associated
// readme files, with or without modification, are permitted in any medium without
// royalty provided this copyright notice is used and wavemotion-dave and
// Marat Fayzullin (fMSX core) are thanked profusely.
//
// The MoonLightSaga-DS emulator is offered as-is, without any warranty. Please see readme.md
//
// This file is our bridge between the Z80 CPU core and the rest of the system.
// MoonLightSaga currently supports the CZ80 CPU core.
// =====================================================================================
#include <nds.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "Z80_interface.h"
#include "../../MoonLightSaga-DS.h"
#include "../../MSX_generic.h"
#include "../../fdc.h"
#include "../../printf.h"

u8 FMPAC_SRAM_in_view   = 0; // Is the FMPAC SRAM in view?
u8 FMPAC_Key1           = 0; // FM-PAC SRAM requires a special key write sequence to open up SRAM access

// -----------------------------------------------------------------------------------------------
// All memory fetches run through this except OP codes which are read directly from memory
// for maximum speed (an Op Code fetch should never result in accessing special memory registers).
// -----------------------------------------------------------------------------------------------
ITCM_CODE u8 cpu_readmem16(u16 address)
{
    if ((special_memory_access & SPEC_MEM_DISK_CONTROLLER) && ((address & ~7u) == 0x7FF8))
    {
        if (address == 0x7FFF) return fdc_read(4);
        else return fdc_read(address & 7);
    }
    else if ((special_memory_access & (SPEC_MEM_SUBSLOT0_ACTIVE | SPEC_MEM_SUBSLOT_ACTIVE)) && (address == 0xFFFF)) // Subslot check... only when page 3 is mapped to an expanded slot
    {
        if (special_memory_access & SPEC_MEM_SUBSLOT0_ACTIVE) 
            return (u8)~msx_subslot0; // Compliment is returned.
        else
            return (u8)~msx_subslot;  // Compliment is returned.
    }

    // Otherwise normal read - just index into the 8K memory block and fetch the byte...
    return (MemoryMap[address>>13][address]);
}


// -------------------------------------------------------------------------
// The ASCII 8K Mapper:
// 4000h~5FFFh (mirror: C000h~DFFFh)    6000h (mirrors: 6001h~67FFh)    0
// 6000h~7FFFh (mirror: E000h~FFFFh)    6800h (mirrors: 6801h~68FFh)    0
// 8000h~9FFFh (mirror: 0000h~1FFFh)    7000h (mirrors: 7001h~77FFh)    0
// A000h~BFFFh (mirror: 2000h~3FFFh)    7800h (mirrors: 7801h~7FFFh)    0
// -------------------------------------------------------------------------
void __attribute__((noinline)) HandleAscii8(u8 media_id, u16 address, u8 value)
{
    u32 block = (value & MyMedia[media_id].mapperMask);
    u32 msx_offset = block * MyMedia[media_id].blockSize;
    u32 *src = (u32*)((u8*)ROM_Memory + msx_offset + (media_id ? ((MAX_CART_SIZE_KB/2) * 1024) : 0));

    if ((address & 0xF800) == 0x6000)
    {
        MSXCartPtr[media_id][2] = (u8*)src;  // Main ROM
        MSXCartPtr[media_id][6] = (u8*)src;  // Mirror
        MemoryMap[2] = MSXCartPtr[media_id][2] - 0x4000;
        if (bCartInPage[3] == (media_id+1))
        {
            MemoryMap[6] = MSXCartPtr[media_id][6] - 0xC000;
        }
    }
    else if ((address & 0xF800) == 0x6800)
    {
        MSXCartPtr[media_id][3] = (u8*)src;  // Main ROM
        MSXCartPtr[media_id][7] = (u8*)src;  // Mirror
        MemoryMap[3] = MSXCartPtr[media_id][3] - 0x6000;
        if (bCartInPage[3] == (media_id+1))
        {
            MemoryMap[7] = MSXCartPtr[media_id][7] - 0xE000;
        }
    }
    else if ((address & 0xF800) == 0x7000)
    {
        MSXCartPtr[media_id][4] = (u8*)src;  // Main ROM
        MSXCartPtr[media_id][0] = (u8*)src;  // Mirror
        if (bCartInPage[2] == (media_id+1))
        {
            MemoryMap[4] = MSXCartPtr[media_id][4] - 0x8000;
        }
        if (bCartInPage[0] == (media_id+1))
        {
            MemoryMap[0] = MSXCartPtr[media_id][0] - 0x0000;
        }
    }
    else if ((address & 0xF800) == 0x7800)
    {
        MSXCartPtr[media_id][5] = (u8*)src;  // Main ROM
        MSXCartPtr[media_id][1] = (u8*)src;  // Mirror
        if (bCartInPage[2] == (media_id+1))
        {
            MemoryMap[5] = MSXCartPtr[media_id][5] - 0xA000;
        }
        if (bCartInPage[0] == (media_id+1))
        {
            MemoryMap[1] = MSXCartPtr[media_id][1] - 0x2000;
        }
    }
}


// MSX DOS 2 by ASCII only maps 0x4000-0x7FFF and the hotpsot is at 0x7FFE
void __attribute__((noinline)) HandleMSXDOS2(u8 media_id, u16 address, u8 value)
{
    u32 block = (value & MyMedia[media_id].mapperMask);
    u32 msx_offset = block * MyMedia[media_id].blockSize;
    u32 *src = (u32*)((u8*)ROM_Memory + msx_offset + (media_id ? ((MAX_CART_SIZE_KB/2) * 1024) : 0));

    if (address == 0x7FFE)
    {
        MSXCartPtr[media_id][2] = (u8*)src;
        MSXCartPtr[media_id][3] = (u8*)src+0x2000;
        MemoryMap[2] = MSXCartPtr[media_id][2] - 0x4000;
        MemoryMap[3] = MSXCartPtr[media_id][3] - 0x6000;
    }
}

// -------------------------------------------------------------------
// The FM-PAC presents 8K of SRAM for a game to use to save progress.
// This handler is called when any byte in the 0x4000-0x5FFF range is
// written and will determine if/when the SRAM is available.
// -------------------------------------------------------------------
#define FMPAC_KEY1 0x4D
#define FMPAC_KEY2 0x69
void __attribute__((noinline)) HandleFMPAC(u8 media_id, u16 address, u8 value)
{
    if (address == 0x5FFE)
    {
        // Key register
        SRAM_Memory[0x1FFE] = value;

        if ((SRAM_Memory[0x1FFF] == 0x69) && (value == 0x4D))
        {
            FMPAC_SRAM_in_view = 1;
            MSXCartPtr[MEDIA_CART2][2] = (u8*)SRAM_Memory;
            MemoryMap[2] = MSXCartPtr[MEDIA_CART2][2] - 0x4000;
        }
        else
        {
            FMPAC_SRAM_in_view = 0;
            MSXCartPtr[MEDIA_CART2][2] = (u8*)TURBO_FMPAC;
            MemoryMap[2] = MSXCartPtr[MEDIA_CART2][2] - 0x4000;
        }
        return;
    }
    else if (address == 0x5FFF)
    {
        // Key register
        SRAM_Memory[0x1FFF] = value;

        if ((SRAM_Memory[0x1FFE] == 0x4D) && (value == 0x69))
        {
            FMPAC_SRAM_in_view = 1;
            MSXCartPtr[MEDIA_CART2][2] = (u8*)SRAM_Memory;
            MemoryMap[2] = MSXCartPtr[MEDIA_CART2][2] - 0x4000;
        }
        else
        {
            FMPAC_SRAM_in_view = 0;
            MSXCartPtr[MEDIA_CART2][2] = (u8*)TURBO_FMPAC;
            MemoryMap[2] = MSXCartPtr[MEDIA_CART2][2] - 0x4000;
        }
        return;
    }

    // Otherwise see if we are able to write into the SRAM
    if (FMPAC_SRAM_in_view && (address >= 0x4000) && (address < 0x5FFE))
    {
        SRAM_Memory[address & 0x1FFF] = value;
        sram_show_status = 5;
    }
}

// --------------------------------------------------------------------
// Handle writes to the Disk Controller (FDC) memory mapped registers.
// --------------------------------------------------------------------
void __attribute__((noinline)) HandleDiskControl(u16 address, u8 value)
{
    if (address <= 0x7FFB) fdc_write(address & 3, value);
    if (address == 0x7FFC) fdc_setSide((value & 1) ? 1:0);  // Side: [xxxxxxxS]
    if (address == 0x7FFD) fdc_setDrive((value & 3)); // Drive: [xxxxxxxD]
}

typedef void (*MapperWriteHandler)(u8 media_id, u16 address, u8 value);

static MapperWriteHandler const mapperWriteHandlers[END_MAPPERS] __attribute__((section(".dtcm"))) =
{
    [ASC8]              = HandleAscii8,
    [MSXDOS2]           = HandleMSXDOS2,
    [FMPAC_SRAM]        = HandleFMPAC,
};

// ---------------------------------------------------------------------
// Write memory handles both normal writes and bankswitched since
// write is much less common than reads... All of the MSX cart mappers
// are vectored here so we can handle all the bank switching, etc.
// ---------------------------------------------------------------------
ITCM_CODE void cpu_writemem16(u16 address, u8 value)
{
    if ((special_memory_access & (SPEC_MEM_SUBSLOT0_ACTIVE | SPEC_MEM_SUBSLOT_ACTIVE)) && (address == 0xFFFF)) // Subslot check... only when page 3 is mapped to an expanded slot
    {
        if (special_memory_access & SPEC_MEM_SUBSLOT0_ACTIVE) msx_subslot0 = value;
        else msx_subslot = value;
        cpu_writeport_msx(0xA8, Port_PPI_A); // Enable the new map...
    }
    else if (bRAMInPage[address >> 14]) // RAM Exists in this slot... write it.
    {
        MemoryMap[address>>13][address]=value;
    }
    else if ((special_memory_access & SPEC_MEM_DISK_CONTROLLER) && (address >= 0x7FF8) && (address <= 0x7FFF)) // Disk controller memory map...
    {
        HandleDiskControl(address, value);
    }
    else if (bCartInPage[address>>14]) // Is there a Cartridge mapped into the write address area?
    {
        u8 media_id = bCartInPage[address>>14] - 1;     // Is this MEDIA_CART1 or MEDIA_CART2?
        u8 mapper_type = MyMedia[media_id].mapperType;  // And what type of CART is it?

        // -----------------------------------------------------------------------
        // Each cartridge mapper is different - call into the appropriate handler.
        // A table-lookup here keeps this execution path relatively deterministic
        // no matter how many mappers we have.
        // -----------------------------------------------------------------------
        if (mapperWriteHandlers[mapper_type] != NULL)
        {
            mapperWriteHandlers[mapper_type](media_id, address, value);
        }
    }
}

// -----------------------------------------------------------------
// Reset a few key variables needed for proper Z80 interface use...
// -----------------------------------------------------------------
void Z80_Interface_Reset(void)
{
    CPU.CycleDeficit      = 0;
    FMPAC_SRAM_in_view    = 0;
    FMPAC_Key1            = 0;
}

// -----------------------------------------------------------------
// Trap and report illegal opcodes to the MoonLightSaga debugger...
// -----------------------------------------------------------------
void Trap_Bad_Ops(char *prefix, byte I, word W)
{
}

// End of file
