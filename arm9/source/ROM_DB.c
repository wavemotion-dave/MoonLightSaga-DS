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

// ROM Database courtesy of https://romdb.vampier.net/

#include <nds.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MoonLightSaga-DS.h"
#include "MSX_generic.h"
#include "CRC32.h"

#include "printf.h"

#define TEENY_SHA1_IMPLEMENTATION
#include "teenysha1.h"

typedef struct {
    char *sha1;
    u8   mapper;
} ROM_DB_t;


const ROM_DB_t ROM_DB[] =
{
  {"42f4e3360eb134857b0efa82c1abedf342680124", MSXDOS2},  
  {"6dcf76a0076f8c7f025979d7d060367fb5ed56bc", MSXDOS2},
  {"9155145d3e8522daf33f821f1ab68ce165960c1a", MSXDOS2},  
  {"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", MIRRORED},
};

u8 RomDB_Lookup(u8 media_id)
{    
    char digest_string[41];
    u8 *CART_Memory = ROM_Memory + (media_id == MEDIA_CART1 ? 0 : ((MAX_CART_SIZE_KB/2)*1024));    
    int size = MyMedia[media_id].filesize;
    
    SHA1 s = {0};
    sha1_reset(&s);
    sha1_process_bytes(&s, CART_Memory, size);
    digest8_t digest;
    sha1_get_digest_bytes(&s, digest);
    
    sprintf(digest_string,"%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x",
        ((digest[0]>>4)  & 0xF),  ((digest[0]>>0) & 0xF),
        ((digest[1]>>4)  & 0xF),  ((digest[1]>>0) & 0xF),
        ((digest[2]>>4)  & 0xF),  ((digest[2]>>0) & 0xF),
        ((digest[3]>>4)  & 0xF),  ((digest[3]>>0) & 0xF),
        ((digest[4]>>4)  & 0xF),  ((digest[4]>>0) & 0xF),
        ((digest[5]>>4)  & 0xF),  ((digest[5]>>0) & 0xF),
        ((digest[6]>>4)  & 0xF),  ((digest[6]>>0) & 0xF),
        ((digest[7]>>4)  & 0xF),  ((digest[7]>>0) & 0xF),
        ((digest[8]>>4)  & 0xF),  ((digest[8]>>0) & 0xF),
        ((digest[9]>>4)  & 0xF),  ((digest[9]>>0) & 0xF),
        ((digest[10]>>4) & 0xF), ((digest[10]>>0) & 0xF),
        ((digest[11]>>4) & 0xF), ((digest[11]>>0) & 0xF),
        ((digest[12]>>4) & 0xF), ((digest[12]>>0) & 0xF),
        ((digest[13]>>4) & 0xF), ((digest[13]>>0) & 0xF),
        ((digest[14]>>4) & 0xF), ((digest[14]>>0) & 0xF),
        ((digest[15]>>4) & 0xF), ((digest[15]>>0) & 0xF),
        ((digest[16]>>4) & 0xF), ((digest[16]>>0) & 0xF),
        ((digest[17]>>4) & 0xF), ((digest[17]>>0) & 0xF),
        ((digest[18]>>4) & 0xF), ((digest[18]>>0) & 0xF),
        ((digest[19]>>4) & 0xF), ((digest[19]>>0) & 0xF));

    int idx = (digest[0] > 0x80) ? 5000:0; // Bisect the list for a bit of speed-up
    while (ROM_DB[idx].sha1[0] <= digest_string[0])
    {
        if (ROM_DB[idx].sha1[0] == digest_string[0])
        {
            if (strcmp(ROM_DB[idx].sha1, digest_string) == 0)
            {
                if ((ROM_DB[idx].mapper == MIRRORED) && (size == (64*1024)))
                    return LIN64;
                else
                    return ROM_DB[idx].mapper;
            }
        }
        idx++;
    }
    
    return ASC8; // If real MSXDOS2 not found, let's hope it's one of the ASC8 variants...
}

// End of File
