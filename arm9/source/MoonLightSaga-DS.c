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
#include <nds/fifomessages.h>

#include <ctype.h>
#include <fat.h>
#include <maxmod9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "CRC32.h"
#include "MoonLightSaga-DS.h"
#include "MSX_generic.h"
#include "V9938.h"
#include "cpu/vdp9938/vdp9938.h"
#include "fdc.h"
#include "choose.h"
#include "mainmenu.h"
#include "msx_kbd.h"
#include "warning1.h"
#include "warning2.h"
#include "instructions.h"
#include "msx_music.h"
#include "cpu/z80/Z80_interface.h"
#include "printf.h"
#include "screenshot.h"
#include "soundbank.h"
#include "soundbank_bin.h"

// ------------------------------------------------------------------
// These 16 debug registers plus indexes DX/DY are the lifeline for
// development of this emulator. We can use these to track all sorts
// of things going on and help to isolate and fix problems. The
// debugger can be turned on in options to see these values.
// ------------------------------------------------------------------
u32 debug[0x10] = {0};
u32 DX = 0;
u32 DY = 0;

u32 last_vsync_count    __attribute__((section(".dtcm"))) = 0xFEEDBEEF;
s8 temp_offset          __attribute__((section(".dtcm"))) = 0;
u8 slide_dampen         __attribute__((section(".dtcm"))) = 0;
u8 skip_render          __attribute__((section(".dtcm"))) = 0;
u8 zoom_screen          __attribute__((section(".dtcm"))) = 0;
u8 bClickRemoveNeeded   __attribute__((section(".dtcm"))) = 1;
u8 turbo_r_boost = 0;
u32 boost_counter=0;
u8 *DISK_CACHE[3] = {0,0,0};

u8 ShowChooseDisk(void);

// -------------------------------------------------------------------------------------------
// The MSX systems have ROM, RAM, BIOS, Music Carts or SRAM. So we create generic buffers
// for all this here... these are sized big enough to handle the largest memory necessary
// to render games playable. We give enough room here to run 98% of the MSX catalog.
//
// These memory buffers will be pointed to by the MemoryMap[] array. This array contains 8
// pointers that can break down the Z80 memory into 8k chunks.
// -------------------------------------------------------------------------------------------

u8 *ROM_Memory;                         // ROM Carts up to 1MB/4MB (that's pretty huge in the Z80 world!)
u8 *DISK_Memory;                        // Disk Memory will be 720K to support DSDD disks
u8 *DISK_Memory1;                       // Disk Memory will be 720K to support DSDD disks
u8 *RAM_Memory;                         // RAM is 2048K for this emulation.
u8 SRAM_Memory[0x2000] ALIGN(32) = {0}; // To hold the 8K of SRAM used by FM-PAC

u8 io_show_status = 0;     // Used to indicate a disk activity icons
u8 sram_show_status = 0;   // Used to show SRAM icon for SRAM-based carts
u8 bMainMenuActive = 0;    // Set to 1 when main menu is active

Media_t MyMedia[3];        // Covering CART1, CART2 and DISK, DISK1, DISK2

// --------------------------------------------------------------------------
// For machines that have a full keybaord, we use the Left and Right
// shoulder buttons on the NDS to emulate the SHIFT and CTRL keys...
// --------------------------------------------------------------------------
u8 key_shift __attribute__((section(".dtcm"))) = false;
u8 key_ctrl  __attribute__((section(".dtcm"))) = false;
u8 key_kana  __attribute__((section(".dtcm"))) = false;
u8 key_graph __attribute__((section(".dtcm"))) = false;

// ---------------------------------------------------------------------------
// Some timing and frame rate computations to keep the emulation on pace...
// ---------------------------------------------------------------------------
u16 emuActFrames    __attribute__((section(".dtcm"))) = 0;  // Actual frames rendered - used for FPS calculation
u16 timingFrames    __attribute__((section(".dtcm"))) = 0;  // This one will cycle 0-59 for the 60Hz refresh
u8 soundEmuPause    __attribute__((section(".dtcm"))) = 1;  // Set to 1 to pause (mute) sound, 0 is sound unmuted (sound channels active)

u8 kbd_key          __attribute__((section(".dtcm"))) = 0;  // 0 if no key pressed, othewise the ASCII key (e.g. 'A', 'B', '3', etc)
u16 nds_key         __attribute__((section(".dtcm"))) = 0;  // 0 if no key pressed, othewise the NDS keys from keysCurrent() or similar
u8 last_mapped_key  __attribute__((section(".dtcm"))) = 0;  // The last mapped key which has been pressed - used for key click feedback
u8 kbd_keys_pressed __attribute__((section(".dtcm"))) = 0;  // Each frame we check for keys pressed - since we can map keyboard keys to the NDS, there may be several pressed at once
u8 kbd_keys[12]     __attribute__((section(".dtcm"))) = {0};// Up to 12 possible keys pressed at the same time (we have 12 NDS physical buttons though it's unlikely that more than 2 or maybe 3 would be pressed)

// ---------------------------------------------------------
// These less-used vars don't need to be in fast memory...
// ---------------------------------------------------------
u8 bStartSoundEngine = false;  // Set to true to unmute sound after 1 frame of rendering...
int bg0, bg1, bg0b, bg1b;      // Some vars for NDS background screen handling
volatile u16 vusCptVBL = 0;    // We use this as a basic timer for the random screenshots shown
volatile u32 dsVSyncCount = 0; // So compiler doesn't cache this as it changes dynamically with interrupt calls
u8 touch_debounce = 0;         // A bit of touch-screen debounce
u8 key_debounce = 0;           // A bit of key debounce
u8 msx_caps_lock = 0;          // Set to 1 when MSX caps lock is active
u8 msx_kana_lock = 0;          // Set to 1 when MSX kana lock is active

// The DS/DSi has 12 keys that can be mapped
u16 NDS_keyMap[12] __attribute__((section(".dtcm"))) = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B, KEY_X, KEY_Y, KEY_R, KEY_L, KEY_START, KEY_SELECT};

// --------------------------------------------------------------------
// The key map for the MSX... mapped into the NDS controller
// --------------------------------------------------------------------
u32 keyCoresp[MAX_KEY_OPTIONS] __attribute__((section(".dtcm"))) = {
    JST_UP,
    JST_DOWN,
    JST_LEFT,
    JST_RIGHT,
    JST_FIRE1,
    JST_FIRE2,

    JST_UP << 16, // P2 versions of the above...
    JST_DOWN << 16,
    JST_LEFT << 16,
    JST_RIGHT << 16,
    JST_FIRE1 << 16,
    JST_FIRE2 << 16,

    META_KBD_A,
    META_KBD_B,
    META_KBD_C,
    META_KBD_D,
    META_KBD_E,
    META_KBD_F,
    META_KBD_G,
    META_KBD_H,
    META_KBD_I,
    META_KBD_J,
    META_KBD_K,
    META_KBD_L,
    META_KBD_M,
    META_KBD_N,
    META_KBD_O,
    META_KBD_P,
    META_KBD_Q,
    META_KBD_R,
    META_KBD_S,
    META_KBD_T,
    META_KBD_U,
    META_KBD_V,
    META_KBD_W,
    META_KBD_X,
    META_KBD_Y,
    META_KBD_Z,
    META_KBD_0,
    META_KBD_1,
    META_KBD_2,
    META_KBD_3,
    META_KBD_4,
    META_KBD_5,
    META_KBD_6,
    META_KBD_7,
    META_KBD_8,
    META_KBD_9,
    META_KBD_SHIFT,
    META_KBD_CTRL,
    META_KBD_KANA,
    META_KBD_GRAPH,
    META_KBD_SPACE,
    META_KBD_RETURN,
    META_KBD_ESC,
    META_KBD_HOME,
    META_KBD_UP,
    META_KBD_DOWN,
    META_KBD_LEFT,
    META_KBD_RIGHT,
    META_KBD_PERIOD,
    META_KBD_COMMA,
    META_KBD_COLON,
    META_KBD_SEMI,
    META_KBD_QUOTE,
    META_KBD_SLASH,
    META_KBD_CARET,
    META_KBD_MINUS,
    META_KBD_LBRACKET,
    META_KBD_RBRACKET,
    META_KBD_ATSIGN,
    META_KBD_YEN,
    META_KBD_BS,
    META_KBD_TAB,
    META_KBD_INS,
    META_KBD_DEL,
    META_KBD_STOP_BRK,
    META_KBD_F1,
    META_KBD_F2,
    META_KBD_F3,
    META_KBD_F4,
    META_KBD_F5,
    META_KBD_PANUP,
    META_KBD_PANDN,
    META_KBD_SHOWTOP,
    META_KBD_SHOWBOT,
    META_KBD_A_PLUS_B,
};

// ------------------------------------------------------------
// Utility function to pause the sound...
// ------------------------------------------------------------
void SoundPause(void)
{
    soundEmuPause = 1;
}

// ------------------------------------------------------------
// Utility function to un pause the sound...
// ------------------------------------------------------------
void SoundUnPause(void)
{
    soundEmuPause = 0;
}

// --------------------------------------------------------------------------------------------
// MAXMOD streaming setup and handling...
// We were using the normal ARM7 sound core but it sounded "scratchy" and so with the help
// of FluBBa, we've swiched over to the maxmod sound core which performs much better.
// --------------------------------------------------------------------------------------------
#define sample_rate (22050)    // To match the sound driver - this is good enough quality for the DS
#define buffer_size (512 + 16) // Enough buffer that we don't have to fill it too often. Must be multiple of 16.

mm_ds_system sys   __attribute__((section(".dtcm")));
mm_stream myStream __attribute__((section(".dtcm")));

// -------------------------------------------------------------------------------------------
// maxmod will call this routine when the buffer is half-empty and requests that
// we fill the sound buffer with more samples. They will request 'len' samples and
// we will fill exactly that many. If the sound is paused, we fill with 'mute' samples.
// -------------------------------------------------------------------------------------------
s16 last_sample __attribute__((section(".dtcm"))) = 0;

// ------------------------------------------------------------------------------------------------------------
// On exit from Sound Pause and whenever the sound driver changes (AY -> AY+MSX-MUSIC), there is a short
// pop/click heard as the new mixer comes online. This routine is designed to perform a simple quick mixing
// and drive the sample output towards the new target to soften the pop/click. It's not perfect but it helps...
// ------------------------------------------------------------------------------------------------------------
mm_word ClickRemoval(mm_word len, mm_addr dest)
{
    s16 mix1[16];

    ay38910Mixer(16, mix1, &myAY); // Mix AY and give us something to move towards

    if (msx_music_capable_game)
    {
        YMMixer(len - 16, dest, &myYM); // Keep the YM stepping normally
        YMMixer(16, mix1, &myYM);       // Mix directly on top of AY
    }

    int target = mix1[15];          // This is what we are smoothing towards

    s16 *p = (s16 *)dest;
    int step = abs(target - last_sample) / len;
    for (int i = 0; i < len; i++)
    {
        if (last_sample < (target - step))
        {
            last_sample += step;
        }
        if (last_sample > (target + step))
        {
            last_sample -= step;
        }
        *p++ = last_sample;
    }

    bClickRemoveNeeded--;

    return len;
}

// -----------------------------------------------------------------------------------------------
// This is our main sound mixer. It's called periodically by the maxmod handler when it needs
// more samples in the buffer. We mix AY + MSX-MUSIC as that's the sound setup for Illusion City.
// -----------------------------------------------------------------------------------------------
ITCM_CODE mm_word OurSoundMixer(mm_word len, mm_addr dest, mm_stream_formats format)
{
    if (soundEmuPause) // If paused, just "mix" in mute sound chip... all channels are OFF
    {
        s16 *p = (s16 *)dest;
        for (int i = 0; i < len; i++)
        {
            *p++ = last_sample; // To prevent pops and clicks... just keep outputting the last sample
        }
    }
    else
    {
        if (bClickRemoveNeeded)
        {
            return ClickRemoval(len, dest);
        }

        ay38910Mixer(len, dest, &myAY);
        if (msx_music_capable_game)
        {
            YMMixer(len, dest, &myYM);
        }

        last_sample = ((s16 *)dest)[len - 1];
    }

    return len;
}

// -------------------------------------------------------------------------------------------
// Setup the maxmod audio stream - this will be a 16-bit Stereo PCM output at 55KHz which
// sounds about right for the MSX audio data stream.
// -------------------------------------------------------------------------------------------
void setupStream(void)
{
    //----------------------------------------------------------------
    //  initialize maxmod with our small 4-effect soundbank
    //----------------------------------------------------------------
    mmInitDefaultMem((mm_addr)soundbank_bin);

    mmLoadEffect(SFX_CLICKNOQUIT);
    mmLoadEffect(SFX_KEYCLICK);
    mmLoadEffect(SFX_MUS_INTRO);
    mmLoadEffect(SFX_FLOPPY);

    //----------------------------------------------------------------
    //  open stream
    //----------------------------------------------------------------
    myStream.sampling_rate = sample_rate;                                   // sample_rate for the emulated MSX to match the AY/SCC drivers
    myStream.buffer_length = buffer_size;                                   // buffer length = (512+16)
    myStream.callback = OurSoundMixer;                                      // set callback function
    myStream.format = MM_STREAM_16BIT_MONO;                                 // format = mono 16-bit
    myStream.timer = MM_TIMER0;                                             // use hardware timer 0
    myStream.manual = false;                                                // use automatic filling
    mmStreamOpen(&myStream);

    //----------------------------------------------------------------
    //  when using 'automatic' filling, your callback will be triggered
    //  every time half of the wave buffer is processed.
    //
    //  so:
    //  25000 (rate)
    //  ----- = ~21 Hz for a full pass, and ~42hz for half pass
    //  1200  (length)
    //----------------------------------------------------------------
    //  with 'manual' filling, you must call mmStreamUpdate
    //  periodically (and often enough to avoid buffer underruns)
    //----------------------------------------------------------------
}

// ----------------------------------------------------------------------------------
// Ensure the sound chips are reset and associated helper variables are initialized.
// ----------------------------------------------------------------------------------
void sound_chip_reset()
{
    s16 mixbuf[16];

    SoundPause();

    bClickRemoveNeeded = 150; // When we re-enable... smooth it to avoid pops/clicks

    // -----------------------------------------------------------
    // The AY sound chip is for our baseline MSX sound handling.
    // -----------------------------------------------------------
    ay38910Reset(&myAY);            // Reset the "AY" sound chip
    ay38910IndexW(0x07, &myAY);     // Register 7 is ENABLE
    ay38910DataW(0x3F, &myAY);      // All OFF (negative logic)
    ay38910Mixer(8, mixbuf, &myAY); // Do an initial mix conversion to prime the output
    myAY.ayPortBOut = 0x80;         // Kana Lock off to start

    // -----------------------------------------------------------------
    // The YM2413 chip is the MSX MUSIC standard for late-era games...
    // -----------------------------------------------------------------
    YMReset(&myYM);
    YMMixer(8, mixbuf, &myYM); // Do an initial mix conversion to prime the output
}

// -----------------------------------------------------------------------
// We setup the sound chips - disabling all volumes to start.
// -----------------------------------------------------------------------
void dsInstallSoundEmuFIFO(void)
{
    sound_chip_reset();       // Reset the SN, AY and SCC chips
    setupStream();            // Setup maxmod stream...
    bStartSoundEngine = true; // Volume will 'unpause' after 1 frame in the main loop.
}

//*****************************************************************************
// Reset the MSX - mostly CPU and memory plus some timer vars for housekeeping.
//*****************************************************************************
void ResetMSX(void)
{
    JoyMode = JOYMODE_JOYSTICK; // Joystick mode key
    JoyState = 0x00000000;      // Nothing pressed to start

    Reset9938(); // Reset the video chip

    sound_chip_reset(); // Reset the SN, AY and SCC chips

    Z80_Interface_Reset(); // Reset the Z80 Interface
    ResetZ80(&CPU);        // Reset the Z80 CPU core

    msx_reset(); // Reset the MSX specific vars

    msx_caps_lock = 0; // MSX CAPS lock off
    msx_kana_lock = 0; // MSX KANA lock off

    zoom_screen = 0; // Assume no zoom to start

    msxWipeRAM();       // Wipe main RAM area (config chooses zero or random)
    msx_restore_bios(); // Put the BIOS back in place and point to it

    turbo_r_boost = 0;
    boost_counter = 0;

    // -----------------------------------------------------------
    // Timer 1 is used to time frame-to-frame of actual emulation
    // -----------------------------------------------------------
    TIMER1_CR = 0;
    TIMER1_DATA = 0;
    TIMER1_CR = TIMER_ENABLE | TIMER_DIV_1024;

    // -----------------------------------------------------------
    // Timer 2 is used to time once per second events
    // -----------------------------------------------------------
    TIMER2_CR = 0;
    TIMER2_DATA = 0;
    TIMER2_CR = TIMER_ENABLE | TIMER_DIV_1024;
    timingFrames = 0;
    skip_render = 0;
    io_show_status = 0;
    sram_show_status = 0;
}


// ------------------------------------------------------------------------
// The status line shows the status of the SCC, MSX-MUSIC, disk icons, etc.
// ------------------------------------------------------------------------
void DisplayStatusLine(void)
{
    if (sram_show_status)
    {
        DSPrint(28, 0, 2, "'()"); // Show SRAM icon
        DSPrint(28, 1, 2, "GHI"); // Show SRAM icon
        if (--sram_show_status == 0)
        {
            msxSaveSRAM();
        }
    }
    else if (io_show_status)
    {
        if (io_show_status == 8) // Disk Write
        {
            DSPrint(28, 0, 2, "678"); // Show Disk icon
            DSPrint(28, 1, 2, "VWX"); // Show Disk icon
            io_show_status = 6;       // Show icon briefly
            if (myGlobalConfig.bDiskSounds)
            {
                mmEffect(SFX_FLOPPY); // Short disk sound effect
            }
        }
        else if (io_show_status == 7) // Disk Read
        {
            DSPrint(28, 0, 2, "345"); // Show Disk icon
            DSPrint(28, 1, 2, "STU"); // Show Disk icon
            io_show_status = 6;       // Show icon briefly
            if (myGlobalConfig.bDiskSounds)
            {
                mmEffect(SFX_FLOPPY); // Short disk sound effect
            }
        }

        if (--io_show_status == 0) // Done with read/write activity
        {
            fdc_persist_disk(); // See if the disk needs to be persisted.
        }
    }
    // ------------------------------------------------------------
    // If we aren't showing the disk icon or SRAM above, we can
    // show one of the various music symbols for SCC or MSX-MUSIC.
    // ------------------------------------------------------------
    else
    {
        DSPrint(28, 0, 0, "   ");
        DSPrint(28, 1, 0, "   ");
    }

    // -----------------------------------------------------------------
    // If the full MSX keyboard is showing, we can show some symbols
    // to indicate if the CAPS lock is enabled, shift/ctrl/graph, etc.
    // -----------------------------------------------------------------

    // Caps Lock
    DSPrint(1, 23, 0, (msx_caps_lock ? "@" : " "));
    DSPrint(2, 23, (msx_caps_lock ? 2 : 0), (msx_caps_lock ? "@" : " "));

    if (msx_kana_lock != ((myAY.ayPortBOut & 0x80) ? 0 : 1))
    {
        msx_kana_lock = (myAY.ayPortBOut & 0x80) ? 0 : 1;
        BottomScreenKeypad();
    }

    DSPrint(1, 19, 0, (key_shift ? "A" : " "));
    DSPrint(2, 19, 0, (key_shift ? "!" : " "));

    DSPrint(1, 15, 0, (key_ctrl ? "@" : " "));
    DSPrint(2, 15, (key_ctrl ? 2 : 0), (key_ctrl ? "@" : " "));

    if (zoom_screen) // Pan and Scan on Mode 7 and Mode 0 with 80 Columns
    {
        key_graph = 0;
        DSPrint(4, 21, 0, ":;<");
        DSPrint(4, 22, 0, "Z[\\");
    }
    else
    {
        DSPrint(4, 21, 0, "   ");
        DSPrint(4, 22, 0, "   ");
    }
    DSPrint(5, 23, (key_graph ? 2 : 0), (key_graph ? "]" : " "));
}

// ------------------------------------------------------
// Handle the MSX virtual keyboard on the bottom screen.
// ------------------------------------------------------
u8 last_special_key = 0;
u8 last_special_key_dampen = 0;
u8 last_kbd_key = 0;
u8 dampen_zoom = 0;

#define CUR_W 43
#define CUR_H 58

int cursor_hit(int x, int y)   /* x,y relative to image top-left */
{
    if (x < 0 || y < 0 || x >= CUR_W || y >= CUR_H)
        return 0x00;

    int dx = 2 * x - CUR_W;    /* -43..+43 */
    int dy = 2 * y - CUR_H;    /* -58..+58 */
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    if (adx * CUR_H > ady * CUR_W)
        return dx < 0 ? KBD_KEY_LEFT : KBD_KEY_RIGHT;
    else
        return dy < 0 ? KBD_KEY_UP : KBD_KEY_DOWN;
}

u8 handle_msx_keyboard_press(u16 iTx, u16 iTy) // MSX Keyboard
{
    if ((iTx >= 214) && (iTy >= 103) && (iTy < 162)) // Triangular Arrow Keys... do our best
    {
        kbd_key = cursor_hit(iTx-214, iTy-103);
    }
    else if ((iTy >= 12) && (iTy < 42)) // Row 1 (top row with Function Keys)
    {
        if ((iTx >= 0) && (iTx < 22))
        {
            kbd_key = KBD_KEY_ESC;
        }
        else if ((iTx >= 22) && (iTx < 44))
        {
            kbd_key = KBD_KEY_HOME;
        }
        else if ((iTx >= 44) && (iTx < 73))
        {
            kbd_key = KBD_KEY_F1;
        }
        else if ((iTx >= 73) && (iTx < 102))
        {
            kbd_key = KBD_KEY_F2;
        }
        else if ((iTx >= 102) && (iTx < 131))
        {
            kbd_key = KBD_KEY_F3;
        }
        else if ((iTx >= 131) && (iTx < 160))
        {
            kbd_key = KBD_KEY_F4;
        }
        else if ((iTx >= 160) && (iTx < 190))
        {
            kbd_key = KBD_KEY_F5;
        }
        else if ((iTx >= 190) && (iTx < 212))
        {
            kbd_key = KBD_KEY_BS;
        }
        else if ((iTx >= 212) && (iTx < 235))
        {
            kbd_key = KBD_KEY_INS;
        }
        else if ((iTx >= 235) && (iTx < 255))
        {
            kbd_key = KBD_KEY_DEL;
        }
    }
    else if ((iTy >= 42) && (iTy < 72)) // Row 2 (number row)
    {
        if ((iTx >= 0) && (iTx < 15))
        {
            kbd_key = '@';
        }
        else if ((iTx >= 15) && (iTx < 31))
        {
            kbd_key = '1';
        }
        else if ((iTx >= 31) && (iTx < 45))
        {
            kbd_key = '2';
        }
        else if ((iTx >= 45) && (iTx < 61))
        {
            kbd_key = '3';
        }
        else if ((iTx >= 61) && (iTx < 75))
        {
            kbd_key = '4';
        }
        else if ((iTx >= 75) && (iTx < 91))
        {
            kbd_key = '5';
        }
        else if ((iTx >= 91) && (iTx < 106))
        {
            kbd_key = '6';
        }
        else if ((iTx >= 106) && (iTx < 121))
        {
            kbd_key = '7';
        }
        else if ((iTx >= 121) && (iTx < 135))
        {
            kbd_key = '8';
        }
        else if ((iTx >= 135) && (iTx < 151))
        {
            kbd_key = '9';
        }
        else if ((iTx >= 151) && (iTx < 165))
        {
            kbd_key = '0';
        }
        else if ((iTx >= 165) && (iTx < 181))
        {
            kbd_key = '-';
        }
        else if ((iTx >= 181) && (iTx < 195))
        {
            kbd_key = '=';
        }
        else if ((iTx >= 195) && (iTx < 210))
        {
            kbd_key = '|'; // YEN
        }
        else if ((iTx >= 210) && (iTx < 255))
        {
            kbd_key = KBD_KEY_SEL;
        }
    }
    else if ((iTy >= 72) && (iTy < 102)) // Row 3 (QWERTY row)
    {
        if ((iTx >= 0) && (iTx < 23))
        {
            kbd_key = KBD_KEY_TAB;
        }
        else if ((iTx >= 23) && (iTx < 39))
        {
            kbd_key = 'Q';
        }
        else if ((iTx >= 39) && (iTx < 54))
        {
            kbd_key = 'W';
        }
        else if ((iTx >= 54) && (iTx < 69))
        {
            kbd_key = 'E';
        }
        else if ((iTx >= 69) && (iTx < 83))
        {
            kbd_key = 'R';
        }
        else if ((iTx >= 83) && (iTx < 99))
        {
            kbd_key = 'T';
        }
        else if ((iTx >= 99) && (iTx < 113))
        {
            kbd_key = 'Y';
        }
        else if ((iTx >= 113) && (iTx < 129))
        {
            kbd_key = 'U';
        }
        else if ((iTx >= 129) && (iTx < 143))
        {
            kbd_key = 'I';
        }
        else if ((iTx >= 143) && (iTx < 158))
        {
            kbd_key = 'O';
        }
        else if ((iTx >= 158) && (iTx < 174))
        {
            kbd_key = 'P';
        }
        else if ((iTx >= 174) && (iTx < 189))
        {
            kbd_key = '[';
        }
        else if ((iTx >= 189) && (iTx < 203))
        {
            kbd_key = ']';
        }
        else if ((iTx >= 203) && (iTx < 214))
        {
            kbd_key = KBD_KEY_DEAD;
        }
        else if ((iTx >= 214) && (iTx < 255))
        {
            kbd_key = KBD_KEY_STOP;
        }
    }
    else if ((iTy >= 102) && (iTy < 132)) // Row 4 (ASDF row)
    {
        if ((iTx >= 0) && (iTx < 27))
        {
            kbd_key = KBD_KEY_CTRL;
            last_special_key = KBD_KEY_CTRL;
            last_special_key_dampen = 20;
        }
        else if ((iTx >= 27) && (iTx < 43))
        {
            kbd_key = 'A';
        }
        else if ((iTx >= 43) && (iTx < 58))
        {
            kbd_key = 'S';
        }
        else if ((iTx >= 58) && (iTx < 72))
        {
            kbd_key = 'D';
        }
        else if ((iTx >= 72) && (iTx < 87))
        {
            kbd_key = 'F';
        }
        else if ((iTx >= 87) && (iTx < 102))
        {
            kbd_key = 'G';
        }
        else if ((iTx >= 102) && (iTx < 117))
        {
            kbd_key = 'H';
        }
        else if ((iTx >= 117) && (iTx < 132))
        {
            kbd_key = 'J';
        }
        else if ((iTx >= 132) && (iTx < 147))
        {
            kbd_key = 'K';
        }
        else if ((iTx >= 147) && (iTx < 161))
        {
            kbd_key = 'L';
        }
        else if ((iTx >= 161) && (iTx < 178))
        {
            kbd_key = ';';
        }
        else if ((iTx >= 178) && (iTx < 192))
        {
            kbd_key = KBD_KEY_QUOTE;
        }
        else if ((iTx >= 192) && (iTx < 214))
        {
            kbd_key = KBD_KEY_RET;
        }
    }
    else if ((iTy >= 132) && (iTy < 162)) // Row 5 (ZXCV row)
    {
        if ((iTx >= 0) && (iTx < 33))
        {
            kbd_key = KBD_KEY_SHIFT;
            last_special_key = KBD_KEY_SHIFT;
            last_special_key_dampen = 20;
        }
        else if ((iTx >= 33) && (iTx < 49))
        {
            kbd_key = 'Z';
        }
        else if ((iTx >= 49) && (iTx < 64))
        {
            kbd_key = 'X';
        }
        else if ((iTx >= 64) && (iTx < 78))
        {
            kbd_key = 'C';
        }
        else if ((iTx >= 78) && (iTx < 94))
        {
            kbd_key = 'V';
        }
        else if ((iTx >= 94) && (iTx < 109))
        {
            kbd_key = 'B';
        }
        else if ((iTx >= 109) && (iTx < 123))
        {
            kbd_key = 'N';
        }
        else if ((iTx >= 123) && (iTx < 139))
        {
            kbd_key = 'M';
        }
        else if ((iTx >= 139) && (iTx < 154))
        {
            kbd_key = ',';
        }
        else if ((iTx >= 154) && (iTx < 169))
        {
            kbd_key = '.';
        }
        else if ((iTx >= 169) && (iTx < 184))
        {
            kbd_key = '/';
        }
        else if ((iTx >= 184) && (iTx < 214))
        {
            kbd_key = KBD_KEY_RET;
        }
    }
    else if ((iTy >= 162) && (iTy < 192)) // Row 6 (SPACE BAR and icons row)
    {
        if ((iTx >= 1) && (iTx < 30))
        {
            kbd_key = KBD_KEY_CAPS;
        }
        else if ((iTx >= 30) && (iTx < 53)) // Graph Key... also Zoom for Screen 7
        {
            if (zoom_screen == 1)
            {
                if (!dampen_zoom)
                {
                    zoom_screen = 2;
                }
                dampen_zoom = 10;
            }
            else if (zoom_screen == 2)
            {
                if (!dampen_zoom)
                {
                    zoom_screen = 1;
                }
                dampen_zoom = 10;
            }
            else
            {
                kbd_key = KBD_KEY_GRAPH;
                last_special_key = KBD_KEY_GRAPH;
                last_special_key_dampen = 20;
            }
        }
        else if ((iTx >= 53) && (iTx < 163))
        {
            kbd_key = ' ';
        }
        else if ((iTx >= 163) && (iTx < 255))
        {
            return MENU_CHOICE_MENU;
        }
    }

    return MENU_CHOICE_NONE;
}

// ------------------------------------------------------------------------
// The main emulation loop is here... call into the Z80, VDP and PSG
// ------------------------------------------------------------------------
void MoonLightSagaRunEmu(void)
{
    u16 iTx, iTy;
    u32 JoyStickMap;
    static u32 lastUN = 0;
    static u8 dampenClick = 0;
    u8 meta_key = 0;

    // Setup the debug buffer for DSi use
    debug_init();

    // Clear top screen
    BG_PALETTE[0] = RGB15(0, 0, 0);
    memset((u8 *)0x06000000, 0x00, 0x40000); // Ensure screen is clear... (LCD_A and LCD_B)

    // Get the MSX Machine Emulator ready
    msxInit();

    Z80_Interface_Reset(); // Reset the Z80 Interface module
    ResetZ80(&CPU);        // Reset the CZ80 core CPU
    BottomScreenKeypad();  // Show the game-related screen with keypad / keyboard

    // Frame-to-frame timing...
    TIMER1_CR = 0;
    TIMER1_DATA = 0;
    TIMER1_CR = TIMER_ENABLE | TIMER_DIV_1024;

    // Once/second timing...
    TIMER2_CR = 0;
    TIMER2_DATA = 0;
    TIMER2_CR = TIMER_ENABLE | TIMER_DIV_1024;
    timingFrames = 0;

    // Force the sound engine to turn on when we start emulation
    bStartSoundEngine = true;

    // -------------------------------------------------------------------
    // Stay in this loop running the MSX game until the user exits...
    // -------------------------------------------------------------------
    while (1)
    {
        // Take a tour of the Z80 counter and display the screen if necessary
        if (!LoopZ80())
        {
            // If we've been asked to start the sound engine, rock-and-roll!
            if (bStartSoundEngine)
            {
                bStartSoundEngine = false;
                SoundUnPause();
            }

            // -------------------------------------------------------------
            // Stuff to do once/second such as FPS display and Debug Data
            // -------------------------------------------------------------
            if (TIMER1_DATA >= 32728) //  1000MS (1 sec)
            {
                if (boost_counter++ == 8) turbo_r_boost = 1;
                TIMER1_CR = 0;
                TIMER1_DATA = 0;
                TIMER1_CR = TIMER_ENABLE | TIMER_DIV_1024;
                u16 emuFps = emuActFrames;
                if (myGlobalConfig.showFPS)
                {
                    if (myGlobalConfig.showFPS != 2) // If not Full Speed... round towards 60
                    {
                        if (emuFps == 61)
                        {
                            emuFps = 60;
                        }
                        else if (emuFps == 59)
                        {
                            emuFps = 60;
                        }
                    }
                    DSPrint_fps(emuFps);
                }
                emuActFrames = 0;
            }

            emuActFrames++; // This one can go above or below 60... used for FPS calculation

            // ---------------------------------------------
            // We only support NTSC 60 frames per second...
            // ---------------------------------------------
            if (++timingFrames == 60)
            {
                TIMER2_CR = 0;
                TIMER2_DATA = 0;
                TIMER2_CR = TIMER_ENABLE | TIMER_DIV_1024;
                timingFrames = 0;
                DisplayStatusLine(); // Every half second
            }
            else if (timingFrames == 30) // Every half second
            {
                DisplayStatusLine();
            }

            // ----------------------------
            // Time 1 frame... 546 (NTSC)
            // ----------------------------
            while (TIMER2_DATA < (546*(timingFrames+1)))
            {
                if (skip_render) break;
            }

            // And copy out XBuf[] to the DS VRAM!
            msxUpdateScreen();

            // Screen 7 is 512px wide and we have the ability to render it in Zoom mode
            if (ScrMode == 7)
            {
                if (!zoom_screen)
                {
                    zoom_screen = 1;
                }
            }
            else if (ScrMode == 9) // 80 Column Mode (480px)
            {
                if (!zoom_screen)
                {
                    zoom_screen = 1;
                }
            }
            else
            {
                zoom_screen = 0;
            }

            // ---------------------------------------------------------------------------------
            // Hold the key press for a brief instant... some machines take longer than others
            // (eg MSX needs to see the keypress for many tens of milliseconds)... This allows
            // us to 'hold' the keypress in memory for several frames which is good enough.
            // ---------------------------------------------------------------------------------
            if (key_debounce > 0)
            {
                key_debounce--;
            }
            else
            {
                // -----------------------------------------------------------
                // This is where we accumulate the keys pressed... up to 12!
                // -----------------------------------------------------------
                kbd_keys_pressed = 0;
                memset(kbd_keys, 0x00, sizeof(kbd_keys));
                kbd_key = 0;

                // ------------------------------------------
                // Handle any screen touch events
                // ------------------------------------------
                if (keysCurrent() & KEY_TOUCH)
                {
                    // ------------------------------------------------------------------------------------------------
                    // Just a tiny bit of touch debounce so ensure touch screen is pressed for a fraction of a second.
                    // ------------------------------------------------------------------------------------------------
                    if (++touch_debounce > 1)
                    {
                        touchPosition touch;
                        touchRead(&touch);
                        iTx = touch.px;
                        iTy = touch.py;

                        meta_key = handle_msx_keyboard_press(iTx, iTy);

                        if (kbd_key != 0)
                        {
                            kbd_keys[kbd_keys_pressed++] = kbd_key;
                            key_debounce = 2;
                        }

                        // If the special menu key indicates we should show the choice menu, do so here...
                        if (meta_key == MENU_CHOICE_MENU)
                        {
                            meta_key = MENU_CHOICE_SWAP_DISK;
                        }

                        // -------------------------------------------------------------------
                        // If one of the special meta keys was picked, we handle that here...
                        // -------------------------------------------------------------------
                        switch (meta_key)
                        {
                        case MENU_CHOICE_SWAP_DISK:
                            SoundPause();
                            switch (ShowChooseDisk())
                            {
                                default: strcpy(MyMedia[MEDIA_DISK].currdisk, "MoonLightSaga-0.dsk"); memcpy(DISK_Memory, DISK_CACHE[0], MAX_DISK_SIZE_KB*1024); break;
                                case 1:  strcpy(MyMedia[MEDIA_DISK].currdisk, "MoonLightSaga-1.dsk"); memcpy(DISK_Memory, DISK_CACHE[1], MAX_DISK_SIZE_KB*1024); break;
                                case 2:  strcpy(MyMedia[MEDIA_DISK].currdisk, "MoonLightSaga-2.dsk"); memcpy(DISK_Memory, DISK_CACHE[2], MAX_DISK_SIZE_KB*1024); break;
                            }

                            // -------------------------------------------------------------------------
                            // Do NOT save this new disk/filename into MyMedia[].filename as it would
                            // overwrite the master. Same for filecrc. Instead we save it into filename2
                            // which always represents the current file for the disk swapped in.
                            // -------------------------------------------------------------------------
                            fdc_init(2, (MyMedia[MEDIA_DISK].filesize / 1024 == 360) ? 1 : 2, 80, 9, 512, 1, DISK_Memory, DISK_Memory1);
                            fdc_reset(false);

                            BottomScreenKeypad();
                            SoundUnPause();
                            BufferKey(' '); // This will start the game loading the disk...
                            break;

                        default:
                            break;
                        }

                        if (++dampenClick > 0) // Make sure the key is pressed for an appreciable amount of time...
                        {
                            if ((kbd_key != 0) && (lastUN == 0))
                            {
                                mmEffect(SFX_KEYCLICK); // Play short key click for feedback...
                            }
                            lastUN = kbd_key;
                        }
                    }
                } //  SCR_TOUCH
                else
                {
                    touch_debounce = 0;
                    lastUN = 0;
                    dampenClick = 0;
                    last_kbd_key = 0;
                    if (dampen_zoom)
                    {
                        dampen_zoom--;
                    }
                }
            }

            // ------------------------------------------------------------------------
            //  Test DS keypresses (ABXY, L/R) and map to corresponding MSX keys
            // ------------------------------------------------------------------------
            key_shift = false;
            key_ctrl = false;
            key_kana = false;
            key_graph = false;

            JoyStickMap = 0;
            nds_key = keysCurrent(); // Get any current keys pressed on the NDS

            // ----------------------------------------------------------------------
            // If we are in Screen 7 'Zoom' mode, the L/R keys take on new meaning...
            // ----------------------------------------------------------------------
            if ((zoom_screen == 2) && (nds_key & (KEY_L | KEY_R)) && ((nds_key & KEY_Y) == 0))
            {
                if (nds_key & KEY_R)
                {
                    if (screenPanX < 256)
                    {
                        screenPanX += 4;
                    }
                }
                if (nds_key & KEY_L)
                {
                    if (screenPanX > 0)
                    {
                        screenPanX -= 4;
                    }
                }
                nds_key &= ~(KEY_L | KEY_R); // We lose the ability for normal handling of these keys
            }

            if ((nds_key & KEY_L) && (nds_key & KEY_R) && (nds_key & KEY_X))
            {
                // Screen swap top/bottom
                lcdSwap();
                WAITVBL; WAITVBL; WAITVBL; WAITVBL; WAITVBL; WAITVBL;
            }
            else if ((nds_key & KEY_L) && (nds_key & KEY_R) && (nds_key & KEY_Y))
            {
                // Snapshot - show the little camera icon for a second or so...
                DSPrint(28, 0, 2, "9:;");
                DSPrint(28, 1, 2, "YZ[");
                screenshot();
                debug_save();
                WAITVBL; WAITVBL; WAITVBL; WAITVBL; WAITVBL; WAITVBL;
                DSPrint(28, 0, 6, "   ");
                DSPrint(28, 1, 6, "   ");
            }
            else if (nds_key & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_A | KEY_B | KEY_START | KEY_SELECT | KEY_R | KEY_L | KEY_X | KEY_Y))
            {
                // --------------------------------------------------------------------------------------------------
                // There are 12 NDS buttons (D-Pad, XYAB, L/R and Start+Select) - we allow mapping of any of these.
                // --------------------------------------------------------------------------------------------------
                for (u8 i = 0; i < 12; i++)
                {
                    if (nds_key & NDS_keyMap[i])
                    {
                        if (keyCoresp[myConfig.keymap[i]] < 0xFFFE0000) // Normal Joystick-type key map
                        {
                            JoyStickMap |= keyCoresp[myConfig.keymap[i]];
                        }
                        else // This is a keyboard maping... handle that here... just set the appropriate kbd_key
                        {
                            if ((keyCoresp[myConfig.keymap[i]] >= META_KBD_A) && (keyCoresp[myConfig.keymap[i]] <= META_KBD_Z))
                            {
                                kbd_key = ('A' + (keyCoresp[myConfig.keymap[i]] - META_KBD_A));
                            }
                            else if ((keyCoresp[myConfig.keymap[i]] >= META_KBD_0) && (keyCoresp[myConfig.keymap[i]] <= META_KBD_9))
                            {
                                kbd_key = ('0' + (keyCoresp[myConfig.keymap[i]] - META_KBD_0));
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_SPACE)
                            {
                                kbd_key = ' ';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_RETURN)
                            {
                                kbd_key = KBD_KEY_RET;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_ESC)
                            {
                                kbd_key = KBD_KEY_ESC;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_SHIFT)
                            {
                                key_shift = 1;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_CTRL)
                            {
                                key_ctrl = 1;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_KANA)
                            {
                                key_kana = 1;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_GRAPH)
                            {
                                key_graph = 1;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_HOME)
                            {
                                kbd_key = KBD_KEY_HOME;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_UP)
                            {
                                kbd_key = KBD_KEY_UP;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_DOWN)
                            {
                                kbd_key = KBD_KEY_DOWN;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_LEFT)
                            {
                                kbd_key = KBD_KEY_LEFT;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_RIGHT)
                            {
                                kbd_key = KBD_KEY_RIGHT;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_PERIOD)
                            {
                                kbd_key = '.';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_COMMA)
                            {
                                kbd_key = ',';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_COLON)
                            {
                                kbd_key = ':';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_SEMI)
                            {
                                kbd_key = ';';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_QUOTE)
                            {
                                kbd_key = KBD_KEY_QUOTE;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_SLASH)
                            {
                                kbd_key = '/';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_CARET)
                            {
                                kbd_key = '^';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_MINUS)
                            {
                                kbd_key = '-';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_LBRACKET)
                            {
                                kbd_key = '[';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_RBRACKET)
                            {
                                kbd_key = ']';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_BS)
                            {
                                kbd_key = KBD_KEY_BS;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_ATSIGN)
                            {
                                kbd_key = '@';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_YEN)
                            {
                                kbd_key = '|';
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_TAB)
                            {
                                kbd_key = KBD_KEY_TAB;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_INS)
                            {
                                kbd_key = KBD_KEY_INS;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_DEL)
                            {
                                kbd_key = KBD_KEY_DEL;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_STOP_BRK)
                            {
                                kbd_key = KBD_KEY_STOP;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_F1)
                            {
                                kbd_key = KBD_KEY_F1;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_F2)
                            {
                                kbd_key = KBD_KEY_F2;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_F3)
                            {
                                kbd_key = KBD_KEY_F3;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_F4)
                            {
                                kbd_key = KBD_KEY_F4;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_F5)
                            {
                                kbd_key = KBD_KEY_F5;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_PANUP)
                            {
                                if ((VDP[9] & 0x80))
                                {
                                    temp_offset = -myConfig.yOffset;
                                    slide_dampen = 15;
                                }
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_PANDN)
                            {
                                if ((VDP[9] & 0x80))
                                {
                                    temp_offset = (20 - myConfig.yOffset);
                                    slide_dampen = 15;
                                }
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_SHOWTOP)
                            {
                                myConfig.yOffset = 0;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_SHOWBOT)
                            {
                                myConfig.yOffset = 20;
                            }
                            else if (keyCoresp[myConfig.keymap[i]] == META_KBD_A_PLUS_B)
                            {
                                JoyStickMap |= (JST_FIRE1 | JST_FIRE2);
                            }

                            if (kbd_key != 0)
                            {
                                kbd_keys[kbd_keys_pressed++] = kbd_key;
                            }
                        }
                    }
                }
            }
            else // Nothing pressed...
            {
                last_mapped_key = 0;
            }

            // ------------------------------------------------------------------------------------------
            // Finally, check if there are any buffered keys that need to go into the keyboard handling.
            // ------------------------------------------------------------------------------------------
            ProcessBufferedKeys();

            // ---------------------------------------------------------
            // Accumulate all bits above into the Joystick State var...
            // ---------------------------------------------------------
            JoyState = JoyStickMap;
        }
    }
}

// ----------------------------------------------------------------------------------------
// We can use this VRAM as semi-fast RAM if needed... right now, no actual need.
// ----------------------------------------------------------------------------------------
void useVRAM(void)
{
    vramSetBankD(VRAM_D_LCD); // Not using this for video but 128K of faster RAM always useful!  Mapped at 0x06860000 -   Used as temp swap area when saving/loading state (256K total)
    vramSetBankE(VRAM_E_LCD); // Not using this for video but 64K of faster RAM always useful!   Mapped at 0x06880000 -   ..
    vramSetBankF(VRAM_F_LCD); // Not using this for video but 16K of faster RAM always useful!   Mapped at 0x06890000 -   ..
    vramSetBankG(VRAM_G_LCD); // Not using this for video but 16K of faster RAM always useful!   Mapped at 0x06894000 -   ..
    vramSetBankH(VRAM_H_LCD); // Not using this for video but 32K of faster RAM always useful!   Mapped at 0x06898000 -   ..
    vramSetBankI(VRAM_I_LCD); // Not using this for video but 16K of faster RAM always useful!   Mapped at 0x068A0000 -   16K available for general use...
}

u8 ShowChooseDisk(void)
{
    swiWaitForVBlank();

    // Wait for touch screen release...
    while ((keysCurrent() & KEY_TOUCH))
    {
        swiWaitForVBlank();
    }

    bg0b = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 31, 0);
    bg1b = bgInitSub(1, BgType_Text8bpp, BgSize_T_256x256, 29, 0);
    bgSetPriority(bg0b, 1);
    bgSetPriority(bg1b, 0);
    decompress(chooseTiles, bgGetGfxPtr(bg0b), LZ77Vram);
    decompress(chooseMap, (void *)bgGetMapPtr(bg0b), LZ77Vram);
    dmaCopy((void *)choosePal, (void *)BG_PALETTE_SUB, 256 * 2);
    unsigned short dmaVal = *(bgGetMapPtr(bg1b) + 24 * 32);
    dmaFillWords(dmaVal | (dmaVal << 16), (void *)bgGetMapPtr(bg1b), 32 * 24 * 2);

    for (;;)
    {
        // Wait for touch screen press...
        while (!(keysCurrent() & KEY_TOUCH))
        {
            swiWaitForVBlank();
        }

        touchPosition touch;
        touchRead(&touch);
        if ((touch.py >= 60)  && (touch.py < 97))  return 0;
        if ((touch.py >= 97)  && (touch.py < 136)) return 1;
        if ((touch.py >= 136) && (touch.py < 177)) return 2;
    }
}

void ShowWarning(u8 type)
{
    swiWaitForVBlank();

    bg0b = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 31, 0);
    bg1b = bgInitSub(1, BgType_Text8bpp, BgSize_T_256x256, 29, 0);
    bgSetPriority(bg0b, 1);
    bgSetPriority(bg1b, 0);
    if (type == 1)
    {
        decompress(warning1Tiles, bgGetGfxPtr(bg0b), LZ77Vram);
        decompress(warning1Map, (void *)bgGetMapPtr(bg0b), LZ77Vram);
        dmaCopy((void *)warning1Pal, (void *)BG_PALETTE_SUB, 256 * 2);
    }
    else
    {
        decompress(warning2Tiles, bgGetGfxPtr(bg0b), LZ77Vram);
        decompress(warning2Map, (void *)bgGetMapPtr(bg0b), LZ77Vram);
        dmaCopy((void *)warning2Pal, (void *)BG_PALETTE_SUB, 256 * 2);
    }

    unsigned short dmaVal = *(bgGetMapPtr(bg1b) + 24 * 32);
    dmaFillWords(dmaVal | (dmaVal << 16), (void *)bgGetMapPtr(bg1b), 32 * 24 * 2);

}


void ShowInstructions(void)
{
    swiWaitForVBlank();

    bg0b = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 31, 0);
    bg1b = bgInitSub(1, BgType_Text8bpp, BgSize_T_256x256, 29, 0);
    bgSetPriority(bg0b, 1);
    bgSetPriority(bg1b, 0);
    decompress(instructionsTiles, bgGetGfxPtr(bg0b), LZ77Vram);
    decompress(instructionsMap, (void *)bgGetMapPtr(bg0b), LZ77Vram);
    dmaCopy((void *)instructionsPal, (void *)BG_PALETTE_SUB, 256 * 2);
    unsigned short dmaVal = *(bgGetMapPtr(bg1b) + 24 * 32);
    dmaFillWords(dmaVal | (dmaVal << 16), (void *)bgGetMapPtr(bg1b), 32 * 24 * 2);
    
    while (keysCurrent()) {WAITVBL;}  // Wait for release
    while (!keysCurrent()) {WAITVBL;} // Wait for press
}

/*********************************************************************************
 * Init DS Emulator - setup VRAM banks and background screen rendering banks
 ********************************************************************************/
void MoonLightSagaInit(void)
{
    //  Stop blending effect of intro
    REG_BLDCNT = 0;
    REG_BLDCNT_SUB = 0;
    REG_BLDY = 0;
    REG_BLDY_SUB = 0;

    // Clear out media bays
    memset(MyMedia, 0x00, sizeof(MyMedia));
}


// ---------------------------------------------------------------------------
// Setup the bottom screen for the main menu icons (carts, disk, play button)
// ---------------------------------------------------------------------------
void BottomScreenMainMenu(void)
{
    swiWaitForVBlank();

    bg0b = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 31, 0);
    bg1b = bgInitSub(1, BgType_Text8bpp, BgSize_T_256x256, 29, 0);
    bgSetPriority(bg0b, 1);
    bgSetPriority(bg1b, 0);
    decompress(mainmenuTiles, bgGetGfxPtr(bg0b), LZ77Vram);
    decompress(mainmenuMap, (void *)bgGetMapPtr(bg0b), LZ77Vram);
    dmaCopy((void *)mainmenuPal, (void *)BG_PALETTE_SUB, 256 * 2);

    unsigned short dmaVal = *(bgGetMapPtr(bg1b) + 24 * 32);
    dmaFillWords(dmaVal | (dmaVal << 16), (void *)bgGetMapPtr(bg1b), 32 * 24 * 2);
}

// ---------------------------------------------------------------------------
// Keyboard decompression buffers in main RAM.
// Keep these static; do not put them on the stack.
// ---------------------------------------------------------------------------
static u16 kbdMapBuf[32 * 64] __attribute__((aligned(4))); // The two maps are 2K in size

// ---------------------------------------------------------------------------
// Setup the bottom screen with the correct virtual keyboard.
// ---------------------------------------------------------------------------
void BottomScreenKeypad(void)
{
    const void *tiles;
    const void *map;
    const u16 *pal;

    tiles = msx_kbdTiles;
    map = msx_kbdMap;
    pal = msx_kbdPal;
    u8 *Scratch_Memory = ROM_Memory + ((MAX_CART_SIZE_KB-64) * 1024);                         // TODO: remove

    // --------------------------------------------------------------
    // Decompress into RAM and then do the copy... this prevents the
    // graphics from flashing/garbage while the decompress happens.
    // --------------------------------------------------------------
    memcpy((u8 *)0x06880000, Scratch_Memory, 0x10000); // Save RAM so we can reuse the buffer for decompression
    decompress(tiles, Scratch_Memory, LZ77);
    decompress(map, kbdMapBuf, LZ77);

    // Get uncompressed byte sizes from the LZ77 headers.
    const u8 *t = (const u8 *)tiles;
    const u8 *m = (const u8 *)map;

    u32 tileSize = t[1] | ((u32)t[2] << 8) | ((u32)t[3] << 16);
    u32 mapSize = m[1] | ((u32)m[2] << 8) | ((u32)m[3] << 16);

    DC_FlushRange(Scratch_Memory, tileSize);
    DC_FlushRange(kbdMapBuf, mapSize);

    swiWaitForVBlank(); // Draw during VBLANK to avoid artifacts

    bgHide(bg0b);
    bgHide(bg1b);

    // Copy only the actual decompressed data.
    dmaCopy(Scratch_Memory, bgGetGfxPtr(bg0b), tileSize);
    dmaCopy(kbdMapBuf, bgGetMapPtr(bg0b), mapSize);

    // Preserve the original map offset and copy size.
    dmaCopy((void *)bgGetMapPtr(bg0b) + 32 * 30 * 2,
            (void *)bgGetMapPtr(bg1b), 32 * 24 * 2);

    dmaCopy((void *)pal, (void *)BG_PALETTE_SUB, 256 * 2);

    bgShow(bg0b);
    bgShow(bg1b);

    memcpy(Scratch_Memory, (u8 *)0x06880000, 0x10000); // Restore previous RAM buffer

    unsigned short dmaVal = *(bgGetMapPtr(bg1b) + 24 * 32);
    dmaFillWords(dmaVal | (dmaVal << 16), (void *)bgGetMapPtr(bg1b), 32 * 24 * 2);

    DisplayStatusLine();
}

/*********************************************************************************
 * Init CPU for the current game
 ********************************************************************************/
void MoonLightSagaInitCPU(void)
{
    //  -----------------------------------------
    //  Init Main Memory and VDP Video Memory
    //  -----------------------------------------
    memset(RAM_Memory, 0x00, RAM_SIZE_KB * 1024);
    memset(VDP_Memory, 0x00, sizeof(VDP_Memory));

    // -----------------------------------------------
    // Init bottom screen do display correct overlay
    // -----------------------------------------------
    BottomScreenKeypad();

    // -----------------------------------------------------
    //  Load the correct Bios ROM for the given machine
    // -----------------------------------------------------
    msx_restore_bios();
}

// -----------------------------------------------------------------
// This is where we handle our pan/scan up and down as well as
// some basic vSYNC timing so that we lock the DS screen updates
// and the MSX screen updates to avoid tearing as much as possible.
// -----------------------------------------------------------------
ITCM_CODE void irqVBlank(void)
{
    int ydyBG = 0x0100; // Default to no screen scale
    int shifty = 0;
    int cyBG = 0;

    // Manage time and true vSync on display output to reduce tearing...
    vusCptVBL++;
    dsVSyncCount++;

    if (!bMainMenuActive) // If main menu active, don't shift anything...
    {
        // ---------------------------------------------------------------------
        // Are we in 192 scanline mode? If so, we don't need ANY screen panning.
        // ---------------------------------------------------------------------
        if (!(VDP[9] & 0x80)) // High bit set if we are in 212 mode
        {
            cyBG = 0;   // No shift
            shifty = 0; // No shift
        }
        else
        {
            cyBG = ((s16)myConfig.yOffset + temp_offset) << 8;
        }
    }

    REG_BG2Y = cyBG + shifty;
    REG_BG3Y = cyBG;

    REG_BG2PD = ydyBG;
    REG_BG3PD = ydyBG;

    if (temp_offset)
    {
        if (slide_dampen == 0)
        {
            if (temp_offset > 0)
            {
                temp_offset--;
            }
            else
            {
                temp_offset++;
            }
        }
        else
        {
            slide_dampen--;
        }
    }
}

/*********************************************************************************
 * Program entry point - check if an argument has been passed in probably from TWL++
 ********************************************************************************/
int main(int argc, char **argv)
{
    // Init sound so we get an intro jingle...
    consoleDemoInit();

    // Initialize the old FAT16 library for use with the SD card
    if (!fatInitDefault())
    {
        iprintf("Unable to initialize libfat!\n");
        return -1;
    }

    // -----------------------------------------------------------------
    // Allocate the Large DSi buffer for expanded RAM banking...
    // -----------------------------------------------------------------
    if (isDSiMode())
    {
        ROM_Memory = malloc(MAX_CART_SIZE_KB * 1024);
        RAM_Memory = malloc(RAM_SIZE_KB * 1024);
    }
    else // For older DS units... show warning that we can't run
    {
        ShowWarning(1);

        for (;;)
        {
            WAITVBL;
        }
    }

    // Clear out the ROM memory with all 0xFF values...
    memset(ROM_Memory, 0xFF, (MAX_CART_SIZE_KB * 1024));

    // ------------------------------------------------------
    // Both DS-Lite and DSi get a single Disk buffer of 720K
    // ------------------------------------------------------
    DISK_Memory = malloc(MAX_DISK_SIZE_KB * 1024);
    memset(DISK_Memory, 0xFF, (MAX_DISK_SIZE_KB * 1024));

    DISK_Memory1 = malloc(MAX_DISK_SIZE_KB * 1024);
    memset(DISK_Memory1, 0xFF, (MAX_DISK_SIZE_KB * 1024));

    DISK_CACHE[0] = malloc(MAX_DISK_SIZE_KB * 1024);
    DISK_CACHE[1] = malloc(MAX_DISK_SIZE_KB * 1024);
    DISK_CACHE[2] = malloc(MAX_DISK_SIZE_KB * 1024);

    // Make sure the SRAM is reset... This small buffer is pre-allocated.
    memset(SRAM_Memory, 0xFF, sizeof(SRAM_Memory));

    // ---------------------------------------------------------------
    // The main game action is on the top screen, keyboard on bottom
    // ---------------------------------------------------------------
    lcdMainOnTop();

    //  Init timer for frame management
    TIMER2_DATA = 0;
    TIMER2_CR = TIMER_ENABLE | TIMER_DIV_1024;

    // Install the sound driver for emulated AY/SCC sound
    SoundPause();
    dsInstallSoundEmuFIFO();

    //  Show the fade-away intro logo...
    intro_logo();

    SetYtrigger(180); // trigger 12 lines before true DS vsync

    irqSet(IRQ_VBLANK, irqVBlank);
    irqEnable(IRQ_VBLANK);

    // -------------------------------------------
    // Setup our DS VRAM - most banks go unused.
    // -------------------------------------------
    useVRAM();

    // ----------------------------------------
    // Get into the correct directory...
    // ----------------------------------------
    chdir("/roms/mls");

    // -----------------------------------------------------
    // Make sure we have true-ish random number generation
    // -----------------------------------------------------
    srand(time(NULL));

    //  ------------------------------------------------------------
    //  We run this loop forever until game exit is selected...
    //  ------------------------------------------------------------
    while (1)
    {
        MoonLightSagaInit();

        // Read in all the files into our various media slots...
        if (LoadGameIntoMedia(MEDIA_CART1, "msxdos2.rom") == 0)
        {
            if (LoadGameIntoMedia(MEDIA_CART1, "ascii_msxdos22.rom") == 0)
            {            
                ShowWarning(2);
                for (;;) {WAITVBL;}
            }
        }

        if (!ReadFileCarefully("MoonLightSaga-0.dsk", DISK_CACHE[0], (MAX_DISK_SIZE_KB * 1024), 0, &MyMedia[MEDIA_DISK].filecrc))
        {
            ShowWarning(2);
            for (;;) {WAITVBL;}
        }

        if (!ReadFileCarefully("MoonLightSaga-1.dsk", DISK_CACHE[1], (MAX_DISK_SIZE_KB * 1024), 0, NULL))
        {
            ShowWarning(2);
            for (;;) {WAITVBL;}
        }

        if (!ReadFileCarefully("MoonLightSaga-2.dsk", DISK_CACHE[2], (MAX_DISK_SIZE_KB * 1024), 0, NULL))
        {
            ShowWarning(2);
            for (;;) {WAITVBL;}
        }

        // Read in all the files into our various media slots...
        if (LoadGameIntoMedia(MEDIA_DISK, "MoonLightSaga-0.dsk") == 0)
        {
            ShowWarning(2);
            for (;;) {WAITVBL;}
        }

        while (1)
        {
            SoundPause();

            //  Choose carts and disks and options
            MoonLightSagaMainMenu();

            //  Run Machine
            MoonLightSagaInitCPU();
            MoonLightSagaRunEmu();
        }
    }
    return (0);
}

// -----------------------------------------------------------------------------------------------------------
// Used by the MSX handler to point to different 8K segments of memory as RAM and Carts are swapped in/out.
// This is one of the most important data structures in our system as it universally maps what to read/write.
// Please note: to gain an almost 5% speed-up, wherever these are written, we offset them by the appropriate
// amount. So MemoryMap[0] is offset by -0x0000 and MemoryMap[3] is offset by -0x6000, etc. This way we don't
// need to do any masking when we fetch bytes in the Z80 and those fetches happen a million times per second.
// -----------------------------------------------------------------------------------------------------------
u8 *MemoryMap[8] __attribute__((section(".dtcm"))) = {0, 0, 0, 0, 0, 0, 0, 0};

// ---------------------------------------
// The main Peripheral Interface IO Ports
// ---------------------------------------
u8 Port_PPI_A __attribute__((section(".dtcm"))) = 0x00;
u8 Port_PPI_B __attribute__((section(".dtcm"))) = 0x00;
u8 Port_PPI_C __attribute__((section(".dtcm"))) = 0x00;

// -------------------------------------
// Our venerable Z80 CPU structure!
// -------------------------------------
Z80 CPU __attribute__((section(".dtcm"))); // Put the entire CPU state into fast memory for speed!

// -----------------------------------------
// Some keyboard/joystick mapping vars...
// -----------------------------------------
u8 JoyMode __attribute__((section(".dtcm"))) = 0;   // Joystick Mode (1=Keypad, 0=Joystick)
u32 JoyState __attribute__((section(".dtcm"))) = 0; // Joystick State for P1 and P2

/*********************************************************************************
 * Keyboard Key Buffering Engine...
 ********************************************************************************/
u8 BufferedKeys[32];
u8 BufferedKeysWriteIdx = 0;
u8 BufferedKeysReadIdx = 0;
void BufferKey(u8 key)
{
    BufferedKeys[BufferedKeysWriteIdx] = key;
    BufferedKeysWriteIdx = (BufferedKeysWriteIdx + 1) % 32;
}

// Buffer a whole string worth of characters...
void BufferKeys(char *str)
{
    for (int i = 0; i < strlen(str); i++)
    {
        BufferKey((u8)str[i]);
    }
}

// ---------------------------------------------------------------------------------------
// Called every frame... so 1/50th or 1/60th of a second. We will virtually 'press' and
// hold the key for roughly a tenth of a second and be smart about shift keys...
// ---------------------------------------------------------------------------------------
void ProcessBufferedKeys(void)
{
    static u8 next_dampen_time = 5;
    static u8 dampen = 0;
    static u8 buf_held = 0;
    static u8 buf_shift = 0;
    static u8 buf_ctrl = 0;

    if (++dampen >= next_dampen_time) // Roughly 50ms... experimentally good enough for all systems.
    {
        if (BufferedKeysReadIdx != BufferedKeysWriteIdx)
        {
            buf_held = BufferedKeys[BufferedKeysReadIdx];
            BufferedKeysReadIdx = (BufferedKeysReadIdx + 1) % 32;
            if (buf_held == KBD_KEY_SHIFT)
            {
                buf_shift = 2;
            }
            else
            {
                if (buf_shift)
                {
                    buf_shift--;
                }
            }
            if (buf_held == KBD_KEY_CTRL)
            {
                buf_ctrl = 6;
            }
            else
            {
                if (buf_ctrl)
                {
                    buf_ctrl--;
                }
            }
            if (buf_held == 255)
            {
                buf_held = 0;
                next_dampen_time = 60;
            }
            else
            {
                next_dampen_time = 5;
            }
        }
        else
        {
            buf_held = 0;
        }
        dampen = 0;
    }

    // See if the shift key should be virtually pressed along with this buffered key...
    if (buf_held)
    {
        kbd_keys[kbd_keys_pressed++] = buf_held;
        if (buf_shift)
        {
            key_shift = 1;
        }
        if (buf_ctrl)
        {
            key_ctrl = 1;
        }
    }
}

/*********************************************************************************
 * Init MSX Emulation Engine for that game
 ********************************************************************************/
void msxInit(void)
{
    // We've got some debug data we can use for development... reset these.
    memset(debug, 0x00, sizeof(debug));
    DX = DY = 0;

    // Put up the proper bottom keyboard overlay/graphic
    BottomScreenKeypad();

    // -----------------------------------------------------------------
    // Change graphic mode to initiate emulation.
    // Here we can claim back 128K of VRAM which is otherwise unused
    // but we can use it for fast memory swaps and look-up-tables.
    // -----------------------------------------------------------------
    videoSetMode(MODE_5_2D | DISPLAY_BG2_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000); // Bank A starts at 0x06000000
    vramSetBankB(VRAM_B_MAIN_BG_0x06020000); // Bank B starts at 0x06020000

    REG_BG2CNT = BG_BMP8_256x256 | BG_BMP_BASE(0); // Uses Bank A
    REG_BG3CNT = BG_BMP8_256x256 | BG_BMP_BASE(8); // Uses Bank B

    REG_BG2PA = (1 << 8);
    REG_BG2PB = 0;
    REG_BG2PC = 0;
    REG_BG2PD = (1 << 8);
    REG_BG2X = 0;
    REG_BG2Y = 0;

    REG_BG3PA = (1 << 8);
    REG_BG3PB = 0;
    REG_BG3PC = 0;
    REG_BG3PD = (1 << 8);
    REG_BG3X = 0;
    REG_BG3Y = 0;

    memset((u8 *)0x06000000, 0x00, 0x40000); // Ensure screen is clear... (LCD_A and LCD_B)

    // Wipe RAM area for the MSX
    msxWipeRAM();

    // Perform a standard system RESET
    ResetMSX();
}

/*********************************************************************************
 * Update the screen for the current cycle. On the DSi this will generally
 * be called right after swiWaitForVBlank() in VDP9938a.c which will help
 * reduce visual tearing and other artifacts. It's not strictly necessary
 * and that does slow down the loop a bit... but DSi can handle it.
 ********************************************************************************/
void msxUpdateScreen(void)
{
    if (!skip_render)
    {
        overscan_trick = 0;
        dmaCopyWordsAsynch(2, (u32 *)XBuf[(drawn_frame_number + 1) & 1], (u32 *)DS_LCD_VRAM_1, 256*212);
    }

    skip_render = 0;
}

// -------------------------------------------------------------------------
// Return the master CRC32 used for high scores and Configuration. This is
// always the Cart 1 unless that's not loaded in which case it's the Disk.
// We never use Cart 2 as the master slot (only used for cart-combos).
// -------------------------------------------------------------------------
u32 GetMasterCRC(void)
{
    if (MyMedia[MEDIA_CART1].filecrc)
    {
        return MyMedia[MEDIA_CART1].filecrc;
    }
    else if (MyMedia[MEDIA_DISK].filecrc)
    {
        return MyMedia[MEDIA_DISK].filecrc;
    }

    return 0x00000000;
}

// -------------------------------------------------------------------------
// Return the master filename needed as the base filename for .sav and .srm
// Always the Cart 1 unless that's not loaded in which case it's the Disk.
// We never use Cart 2 as the master slot (only used for cart-combos).
// -------------------------------------------------------------------------
char *GetMasterFilename(void)
{
    if (MyMedia[MEDIA_CART1].filecrc)
    {
        return MyMedia[MEDIA_CART1].filename;
    }
    else if (MyMedia[MEDIA_DISK].filecrc)
    {
        return MyMedia[MEDIA_DISK].filename;
    }

    return "No File";
}

// -------------------------------------------------------------------------
// Return the master path needed as the directory needed for .sav and .srm
// Always the Cart 1 unless that's not loaded in which case it's the Disk.
// We never use Cart 2 as the master slot (only used for cart-combos).
// -------------------------------------------------------------------------
char *GetMasterPath(void)
{
    if (MyMedia[MEDIA_CART1].filecrc)
    {
        return MyMedia[MEDIA_CART1].filepath;
    }
    else if (MyMedia[MEDIA_DISK].filecrc)
    {
        return MyMedia[MEDIA_DISK].filepath;
    }

    return "";
}

/** LoadGameIntoMedia() **************************************************************/
/** Open a rom file from file system and load it into the appropriate buffer...     **/
/** After this call, the filename, file size and CRC32 are set for this media slot. **/
/*************************************************************************************/
u32 LoadGameIntoMedia(u8 media_id, char *filename)
{
    // Save the directory in which this file was found...
    getcwd(MyMedia[media_id].filepath, MAX_PATH_NAME_LEN);

    // Save the current game filename. We uppercase it so it's easier to search the title...
    strcpy(MyMedia[media_id].filename, filename);
    for (int i = 0; i < strlen(MyMedia[media_id].filename); i++)
    {
        MyMedia[media_id].filename[i] = toupper(MyMedia[media_id].filename[i]); // Uppercase string
    }

    // Now open and read the file... grab file size and CRC32 as well...
    if (media_id == MEDIA_CART1)
    {
        MyMedia[media_id].filesize = ReadFileCarefully(MyMedia[media_id].filename, ROM_Memory, (MAX_CART_SIZE_KB * 1024), 0, &MyMedia[media_id].filecrc);
    }
    else if (media_id == MEDIA_CART2)
    {
        MyMedia[media_id].filesize = ReadFileCarefully(MyMedia[media_id].filename, ROM_Memory + ((MAX_CART_SIZE_KB * 1024) / 2), (MAX_CART_SIZE_KB * 1024) / 2, 0, &MyMedia[media_id].filecrc);
    }
    else
    {
        strcpy(MyMedia[media_id].currdisk, MyMedia[media_id].filename); // This is the current disk name - since disks can be swapped.
        MyMedia[media_id].filesize = (MAX_DISK_SIZE_KB * 1024);
        memcpy(DISK_Memory, DISK_CACHE[0], MyMedia[media_id].filesize);
    }

    // --------------------------------------------------------
    // Load the Configuration for this new cart/disk layout...
    // --------------------------------------------------------
    FindConfig();

    return MyMedia[media_id].filesize;
}

// -------------------------------------------------------------------------
// For various machines, we have patched the BIOS so that we trap calls
// to various I/O routines: namely cassette access. We handle that here.
// -------------------------------------------------------------------------
void PatchZ80(register Z80 *r)
{
    // Not used at this time...
}

/** LoopZ80() *************************************************/
/** Z80 emulation calls this function periodically to run    **/
/** Z80 code for the loaded ROM. It runs code refreshing the **/
/** VDP and checking for interrupt requests.                 **/
/**************************************************************/
int H_BlankCycles = 114;
u32 LoopZ80(void)
{
    // Execute 1 scanline worth of CPU instructions
    u32 cycles_to_process = VDP9938_CLOCKS_PER_LINE + CPU.CycleDeficit; // Turbo-R boost
    H_BlankCycles = 114;

    if (turbo_r_boost) // 2.5x is about the best we can do...
    {
        cycles_to_process += ((2*VDP9938_CLOCKS_PER_LINE) + (VDP9938_CLOCKS_PER_LINE/2));
    }

    extern u16 turbo_timer;
    turbo_timer += 16;

    CPU.CycleDeficit = ExecZ80(cycles_to_process);

    // Run the FDC engine
    LoopFDC();

    // Run the VDP engine
    LoopVDP();

    // Refresh VDP
    Loop9938();

    // Generate an interrupt if called for...
    if (CPU.IRequest != INT_NONE)
    {
        IntZ80(&CPU, CPU.IRequest);
    }

    // Drop out unless end of screen is reached
    if (CurLine == VDP9938_END_LINE)
    {
        CPU.CycleDeficit = 0; // Start fresh at the VBlank boundary. Keeps frame timing consistent.
        return 0;
    }
    return 1;
}

// -----------------------------------------------------------------------
// A bit of memory reserved to store debug information... Reduce this
// when we are stable and don't have much need for it anymore.
// -----------------------------------------------------------------------

#define MAX_DPRINTF_STR_SIZE 128
#define MAX_DEBUG_BUF_SIZE (16 * 1024)

char DEBUG_BUFFER[MAX_DEBUG_BUF_SIZE];
u32 debug_len = 0;
extern char szName[]; // Reuse buffer which has no other in-game use

void debug_init()
{
    debug_len = 0;
    memset(DEBUG_BUFFER, 0x00, MAX_DEBUG_BUF_SIZE);
}

void debug_printf(const char *str, ...)
{
    if (debug_len >= (MAX_DEBUG_BUF_SIZE - MAX_DPRINTF_STR_SIZE))
    {
        return; // No more room!
    }

    va_list ap = {0};

    va_start(ap, str);
    vsnprintf(szName, MAX_DPRINTF_STR_SIZE, str, ap);
    va_end(ap);

    strcat(DEBUG_BUFFER + debug_len, szName);
    debug_len += strlen(szName);
}

void debug_save()
{
    if (debug_len > 0) // Only if we have debug data to write...
    {
        FILE *fp = fopen("debug.log", "w");
        if (fp)
        {
            fwrite(DEBUG_BUFFER, 1, debug_len, fp);
            fclose(fp);
        }
    }
}

// End of file
