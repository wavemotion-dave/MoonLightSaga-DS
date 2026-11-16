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
#include <nds.h>

#include <ctype.h>
#include <dirent.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "CRC32.h"
#include "MoonLightSaga-DS.h"
#include "MSX_generic.h"
#include "lzav.h"
#include "printf.h"
#include "topscreen.h"

char szName[256];
char szFile[256];
char strBuf[40];

struct Config_t         myConfig __attribute((aligned(4))) __attribute__((section(".dtcm")));
struct GlobalConfig_t   myGlobalConfig;

void MapPlayer1(void)
{
    myConfig.keymap[0] = 0;   // NDS D-Pad mapped to MSX Joystick UP
    myConfig.keymap[1] = 1;   // NDS D-Pad mapped to MSX Joystick DOWN
    myConfig.keymap[2] = 2;   // NDS D-Pad mapped to MSX Joystick LEFT
    myConfig.keymap[3] = 3;   // NDS D-Pad mapped to MSX Joystick RIGHT
    myConfig.keymap[4] = 4;   // NDS A Button mapped to MSX Button 1
    myConfig.keymap[5] = 5;   // NDS B Button mapped to MSX Button 2
    myConfig.keymap[6] = 82;  // NDS X Button mapped to PAN UP
    myConfig.keymap[7] = 83;  // NDS Y Button mapped to PAN DN
    myConfig.keymap[8] = 49;  // NDS R      mapped to CTRL
    myConfig.keymap[9] = 48;  // NDS L      mapped to SHIFT
    myConfig.keymap[10] = 86; // NDS Start  mapped to A+B (Menu)
    myConfig.keymap[11] = 52; // NDS Select mapped to SPACE
}


void SetDefaultGlobalConfig(void)
{
    // A few global defaults...
    memset(&myGlobalConfig, 0x00, sizeof(myGlobalConfig));
    myGlobalConfig.showFPS = 0;           // Don't show FPS counter by default
    myGlobalConfig.bDiskSounds = 0;       // Default is to have disk sounds
}

void SetDefaultGameConfig(void)
{
    myConfig.game_crc = 0; // No game in this slot yet

    MapPlayer1(); // Default to Player 1 mapping

    myConfig.msxMapper = GUESS;                 // MSX mapper takes its best guess
    myConfig.maxSprites = 1;                    // 0 means limit to the original 4/8 sprites of the VDP, 1 means 32 sprites for emulation
    myConfig.yOffset = 0;                       // Default is no Y offset
    myConfig.expansion = EXP_FM_PAC;            // Default is MSX-MUSIC
    myConfig.frameSkip = 2;                     // Frame Skip is set to 'HIGH' to get TurboR running as best we can
    myConfig.ymScale = YM_BAL_HIGH;             // Normal YM is scaled to be 2nd loudest
}

// -------------------------------------------------------------------------
// Try to match our loaded game to a configuration my matching CRCs
// -------------------------------------------------------------------------
void FindConfig(void)
{
    SetDefaultGlobalConfig();
    SetDefaultGameConfig();
}


// ----------------------------------------------------------------------
// Read file twice and ensure we get the same CRC... if not, do it again
// until we get a clean read. Return the filesize to the caller...
// ----------------------------------------------------------------------
u32 ReadFileCarefully(char *filename, u8 *buf, u32 buf_size, u32 buf_offset, u32 *crc)
{
    u32 crc1 = 0;
    u32 crc2 = 1;
    u32 fileSize = 0;

    // --------------------------------------------------------------------------------------------
    // I've seen some rare issues with reading files from the SD card on a DSi so we're doing
    // this slow and careful - we will read twice and ensure that we get the same CRC both times.
    // --------------------------------------------------------------------------------------------
    do
    {
        // Read #1
        crc1 = 0xFFFFFFFF;
        FILE *file = fopen(filename, "rb");
        if (file)
        {
            if (buf_offset)
            {
                fseek(file, buf_offset, SEEK_SET);
            }
            fileSize = fread(buf, 1, buf_size, file);
            crc1 = getCRC32(buf, fileSize);
            fclose(file);
        }

        // Read #2
        crc2 = 0xFFFFFFFF;
        FILE *file2 = fopen(filename, "rb");
        if (file2)
        {
            if (buf_offset)
            {
                fseek(file2, buf_offset, SEEK_SET);
            }
            fileSize = fread(buf, 1, buf_size, file2);
            crc2 = getCRC32(buf, fileSize);
            fclose(file2);
        }

        // TODO add a trap for infinite loop - exit with error.
    } while (crc1 != crc2); // If the file couldn't be read, file_size will be 0 and the CRCs will both be 0xFFFFFFFF

    // -----------------------------------------------------------------------
    // If we are a .DSK file, we need to use the filename itself as the CRC32
    // or else disks that get re-written will end up with a different CRC32.
    // -----------------------------------------------------------------------
    if (isDisk(filename))
    {
        crc1 = crcBasedOnFilename(filename);
    }

    if (crc)
    {
        *crc = crc1;
    }

    return fileSize;
}

void UpperScreenMainMenu(void)
{
    swiWaitForVBlank();

    videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG);
    bg0 = bgInit(0, BgType_Text8bpp, BgSize_T_256x512, 31, 0);
    bg1 = bgInit(1, BgType_Text8bpp, BgSize_T_256x512, 29, 0);
    bgSetPriority(bg0, 1);
    bgSetPriority(bg1, 0);

    decompress(topscreenTiles, bgGetGfxPtr(bg0), LZ77Vram);
    decompress(topscreenMap, (void*) bgGetMapPtr(bg0), LZ77Vram);
    dmaCopy((void *) topscreenPal,(u16*) BG_PALETTE,256*2);
}


// --------------------------------------------------------------------
// Let the user select new options for the currently loaded game...
// --------------------------------------------------------------------
void MoonLightSagaMainMenu(void)
{
    u8 bPlayGame = 0;
    u8 screenTouched = 1;

    bMainMenuActive = 1;

    // Upper Screen Background
    UpperScreenMainMenu();

    // Lower Screen Background
    BottomScreenMainMenu();

    while (!bPlayGame)
    {
        if (keysCurrent() & KEY_START)
        {
            while (keysCurrent())
            {
                swiWaitForVBlank();
            }
            bPlayGame = 1;
        }

        // If the touch-screen is pressed... react to it
        if ((keysCurrent() & KEY_TOUCH) && !screenTouched)
        {
            screenTouched = 5;
            touchPosition touch;
            touchRead(&touch);

            if ((touch.py >= 100) && (touch.py < 155) && (touch.px < 150)) // PLAY
            {
                bPlayGame = 1;
            }
            else if ((touch.py >= 155) && (touch.py < 192) && (touch.px < 150)) // INSTRUCTIONS

            {
                ShowInstructions();
                BottomScreenMainMenu();
            }
        }
        else
        {
            if (screenTouched)
            {
                screenTouched--;
            }
        }

        swiWaitForVBlank();
    }

    bMainMenuActive = 0;
}

//*****************************************************************************
// Displays a message on the screen
//*****************************************************************************
void DSPrint(int iX, int iY, int iScr, char *szMessage)
{
    u16 *pusScreen, *pusMap;
    u16 usCharac;
    char *pTrTxt = szMessage;

    pusScreen = (u16 *)(iScr != 1 ? bgGetMapPtr(bg1b) : bgGetMapPtr(bg1)) + iX + (iY << 5);
    pusMap = (u16 *)(iScr != 1 ? (iScr == 6 ? bgGetMapPtr(bg0b) + 24 * 32 : (iScr == 0 ? bgGetMapPtr(bg0b) + 24 * 32 : (iScr == 4) ? bgGetMapPtr(bg0b) + 28 * 32 : bgGetMapPtr(bg0b) + 26 * 32)) : bgGetMapPtr(bg0) + 51 * 32);

    while ((*pTrTxt) != '\0')
    {
        char ch = *pTrTxt++;
        if (ch >= 'a' && ch <= 'z')
        {
            ch -= 32; // Faster than strcpy/strtoupper
        }

        if (((ch) < ' ') || ((ch) > '_'))
        {
            usCharac = *(pusMap); // Will render as a vertical bar
        }
        else if ((ch) < '@')
        {
            usCharac = *(pusMap + (ch) - ' '); // Number from 0-9 or punctuation
        }
        else
        {
            usCharac = *(pusMap + 32 + (ch) - '@'); // Character from A-Z
        }
        *pusScreen++ = usCharac;
    }
}

// Just a bit faster than the generic DSPrint() when dealing with numbers only
void DSPrint_fps(u16 fps)
{
    u16 *pusScreen, *pusMap;
    char tmpStr[4];

    if (fps / 100)
    {
        tmpStr[0] = '0' + fps / 100;
    }
    else
    {
        tmpStr[0] = ' ';
    }
    tmpStr[1] = '0' + (fps % 100) / 10;
    tmpStr[2] = '0' + (fps % 100) % 10;
    tmpStr[3] = 0;

    pusScreen = (u16 *)bgGetMapPtr(bg1b);
    pusMap = (u16 *)bgGetMapPtr(bg0b) + 24 * 32;
    char *cPtr = tmpStr;

    while ((*cPtr) != '\0')
    {
        char ch = *cPtr++;
        *pusScreen++ = *(pusMap + (ch) - ' '); // Number from 0-9 or punctuation
    }
}

/******************************************************************************
 * Routine FadeToColor :  Fade from background to black or white
 ******************************************************************************/
void FadeToColor(unsigned char ucSens, unsigned short ucBG, unsigned char ucScr, unsigned char valEnd, unsigned char uWait)
{
    unsigned short ucFade;
    unsigned char ucBcl;

    // Fade-out to black
    if (ucScr & 0x01)
    {
        REG_BLDCNT = ucBG;
    }
    if (ucScr & 0x02)
    {
        REG_BLDCNT_SUB = ucBG;
    }
    if (ucSens == 1)
    {
        for (ucFade = 0; ucFade < valEnd; ucFade++)
        {
            if (ucScr & 0x01)
            {
                REG_BLDY = ucFade;
            }
            if (ucScr & 0x02)
            {
                REG_BLDY_SUB = ucFade;
            }
            for (ucBcl = 0; ucBcl < uWait; ucBcl++)
            {
                swiWaitForVBlank();
            }
        }
    }
    else
    {
        for (ucFade = 16; ucFade > valEnd; ucFade--)
        {
            if (ucScr & 0x01)
            {
                REG_BLDY = ucFade;
            }
            if (ucScr & 0x02)
            {
                REG_BLDY_SUB = ucFade;
            }
            for (ucBcl = 0; ucBcl < uWait; ucBcl++)
            {
                swiWaitForVBlank();
            }
        }
    }
}

void _putchar(char character) {}

// End of file
