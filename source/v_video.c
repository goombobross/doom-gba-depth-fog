/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  Gamma correction LUT stuff.
 *  Color range translation support
 *  Functions to draw patches (by post) directly to screen.
 *  Functions to blit a block to the screen.
 *
 *-----------------------------------------------------------------------------
 */

#include "doomdef.h"
#include "r_main.h"
#include "r_draw.h"
#include "m_bbox.h"
#include "w_wad.h"   /* needed for color translation lump lookup */
#include "v_video.h"
#include "i_video.h"
#include "lprintf.h"

#include "global_data.h"
#include "gba_functions.h"

/*
 * V_DrawBackground tiles a 64x64 patch over the entire screen, providing the
 * background for the Help and Setup screens, and plot text betwen levels.
 * cphipps - used to have M_DrawBackground, but that was used the framebuffer
 * directly, so this is my code from the equivalent function in f_finale.c
 */
void V_DrawBackground(const char* flatname)
{
    /* erase the entire screen to a tiled background */
    const byte *src;
    int         lump;

    unsigned short *dest = _g->screens[0].data;

    // killough 4/17/98:
    src = W_CacheLumpNum(lump = _g->firstflat + R_FlatNumForName(flatname));

    for(unsigned int y = 0; y < SCREENHEIGHT; y++)
    {
        for(unsigned int x = 0; x < 240; x+=64)
        {
            unsigned short* d = &dest[ ScreenYToOffset(y) + (x >> 1)];
            const byte* s = &src[((y&63) * 64) + (x&63)];

            unsigned int len = 64;

            if( (240-x) < 64)
                len = 240-x;

            BlockCopy(d, s, len);
        }
    }
}



/*
 * This function draws at GBA resoulution (ie. not pixel doubled)
 * so the st bar and menus don't look like garbage.
 */

#if defined(GBA)

/*
 * Mode 4 VRAM only accepts useful halfword writes. Menu patches are drawn in
 * physical 240x160 coordinates, so every output pixel is a read/modify/write
 * of one byte in a halfword. The old loop recalculated a 16.16 source
 * coordinate and tested x parity for every pixel. The 200->160 scale is
 * exactly 5 source rows per 4 output rows, so process that fixed pattern
 * directly and select the low/high-byte writer once per output column.
 */
static inline __attribute__((always_inline))
void V_DrawScaledPostLow(volatile unsigned short* dest,
                         const byte* source,
                         unsigned int count)
{
    while (count >= 4)
    {
        unsigned int old;

        old = *dest;
        *dest = (unsigned short)((old & 0xff00u) | source[0]);
        dest += SCREENPITCH;

        old = *dest;
        *dest = (unsigned short)((old & 0xff00u) | source[1]);
        dest += SCREENPITCH;

        old = *dest;
        *dest = (unsigned short)((old & 0xff00u) | source[2]);
        dest += SCREENPITCH;

        old = *dest;
        *dest = (unsigned short)((old & 0xff00u) | source[3]);
        dest += SCREENPITCH;

        source += 5;
        count -= 4;
    }

    if (count)
    {
        unsigned int old = *dest;
        *dest = (unsigned short)((old & 0xff00u) | source[0]);

        if (count > 1)
        {
            dest += SCREENPITCH;
            old = *dest;
            *dest = (unsigned short)((old & 0xff00u) | source[1]);

            if (count > 2)
            {
                dest += SCREENPITCH;
                old = *dest;
                *dest = (unsigned short)((old & 0xff00u) | source[2]);
            }
        }
    }
}

static inline __attribute__((always_inline))
void V_DrawScaledPostHigh(volatile unsigned short* dest,
                          const byte* source,
                          unsigned int count)
{
    while (count >= 4)
    {
        unsigned int old;

        old = *dest;
        *dest = (unsigned short)((old & 0x00ffu) | ((unsigned int)source[0] << 8));
        dest += SCREENPITCH;

        old = *dest;
        *dest = (unsigned short)((old & 0x00ffu) | ((unsigned int)source[1] << 8));
        dest += SCREENPITCH;

        old = *dest;
        *dest = (unsigned short)((old & 0x00ffu) | ((unsigned int)source[2] << 8));
        dest += SCREENPITCH;

        old = *dest;
        *dest = (unsigned short)((old & 0x00ffu) | ((unsigned int)source[3] << 8));
        dest += SCREENPITCH;

        source += 5;
        count -= 4;
    }

    if (count)
    {
        unsigned int old = *dest;
        *dest = (unsigned short)((old & 0x00ffu) | ((unsigned int)source[0] << 8));

        if (count > 1)
        {
            dest += SCREENPITCH;
            old = *dest;
            *dest = (unsigned short)((old & 0x00ffu) | ((unsigned int)source[1] << 8));

            if (count > 2)
            {
                dest += SCREENPITCH;
                old = *dest;
                *dest = (unsigned short)((old & 0x00ffu) | ((unsigned int)source[2] << 8));
            }
        }
    }
}

#endif

#if defined(GBA)
__attribute__((optimize("O3"), noinline))
#endif
void V_DrawPatch(int x, int y, int scrn, const patch_t* patch)
{
    y -= patch->topoffset;
    x -= patch->leftoffset;

    /* Exact constants used by the original renderer. */
    const int DX  = (240 << FRACBITS) / 320;
    const int DXI = (320 << FRACBITS) / 240;
    const int DY  = ((SCREENHEIGHT << FRACBITS) + (FRACUNIT-1)) / 200;

    byte* const byte_topleft = (byte*)_g->screens[scrn].data;
    const int left   = (x * DX) >> FRACBITS;
    const int right  = ((x + patch->width) * DX) >> FRACBITS;
    const int bottom = ((y + patch->height) * DY) >> FRACBITS;

    /* Menu and option patches normally take this branch. */
    const boolean fully_visible =
        left >= 0 && right <= 240 && y >= 0 && (y + patch->height) <= 200;

    unsigned int colfrac = 0;

    for (int dc_x = left; dc_x < right; ++dc_x, colfrac += DXI)
    {
        const unsigned int colindex = colfrac >> FRACBITS;

        if (!fully_visible)
        {
            if (dc_x < 0)
                continue;
            if (dc_x >= 240)
                break;
        }

        const column_t* column =
            (const column_t*)((const byte*)patch + patch->columnofs[colindex]);

        const unsigned int odd = (unsigned int)dc_x & 1u;
        const int even_x = dc_x & ~1;

        while (column->topdelta != 0xff)
        {
            const byte* const source = (const byte*)column + 3;
            const int topdelta = column->topdelta;
            const int dc_yl = ((y + topdelta) * DY) >> FRACBITS;
            const int dc_yh =
                ((y + topdelta + column->length) * DY) >> FRACBITS;

            if (!fully_visible &&
                ((dc_yl >= SCREENHEIGHT) || (dc_yl > bottom)))
                break;

            const int count = dc_yh - dc_yl;

            if (count > 0)
            {
                volatile unsigned short* const dest =
                    (volatile unsigned short*)(byte_topleft +
                    ((ScreenYToOffset(dc_yl)) << 1) + even_x);

#if defined(GBA)
                if (odd)
                    V_DrawScaledPostHigh(dest, source, (unsigned int)count);
                else
                    V_DrawScaledPostLow(dest, source, (unsigned int)count);
#else
                /* Non-GBA builds keep the original fixed-point sampler. */
                byte* d = (byte*)dest + odd;
                fixed_t frac = 0;
                const fixed_t fracstep = (200 << FRACBITS) / SCREENHEIGHT;
                int remaining = count;

                while (remaining--)
                {
                    *d = source[frac >> FRACBITS];
                    d += SCREENPITCH * 2;
                    frac += fracstep;
                }
#endif
            }

            column = (const column_t*)((const byte*)column + column->length + 4);
        }
    }
}


// CPhipps - some simple, useful wrappers for that function, for drawing patches from wads

// CPhipps - GNU C only suppresses generating a copy of a function if it is
// static inline; other compilers have different behaviour.
// This inline is _only_ for the function below

/*
 * The embedded IWAD lookup in this fork is a reverse linear scan. Menus draw
 * the same named patches every frame, so cache those name->lump results in a
 * tiny 4-way table. This avoids rescanning the full WAD directory for every
 * menu item, title patch, thermometer piece, and cursor frame.
 */
typedef struct
{
    unsigned int key0;
    unsigned int key1;
    int lump_plus_one;
} v_patch_name_cache_entry_t;

#define V_PATCH_NAME_CACHE_SETS 8
#define V_PATCH_NAME_CACHE_WAYS 4

typedef struct
{
    unsigned int magic;
    v_patch_name_cache_entry_t
        entries[V_PATCH_NAME_CACHE_SETS * V_PATCH_NAME_CACHE_WAYS];
    byte victim[V_PATCH_NAME_CACHE_SETS];
} v_patch_name_cache_state_t;

#if defined(GBA)
static v_patch_name_cache_state_t v_patch_name_cache
    __attribute__((section(".ewram"), aligned(4)));
#else
static v_patch_name_cache_state_t v_patch_name_cache;
#endif

int V_GetPatchNumCached(const char* name)
{
    enum { V_PATCH_NAME_CACHE_MAGIC = 0x4d454e55u }; /* "MENU" */

    if (v_patch_name_cache.magic != V_PATCH_NAME_CACHE_MAGIC)
    {
        BlockSet(&v_patch_name_cache, 0, sizeof(v_patch_name_cache));
        v_patch_name_cache.magic = V_PATCH_NAME_CACHE_MAGIC;
    }

    unsigned int key0 = 0;
    unsigned int key1 = 0;

    for (unsigned int i = 0; i < 4; ++i)
    {
        const unsigned int c = (byte)name[i];
        key0 |= c << (i * 8);
        if (!c)
            break;
    }

    if ((key0 & 0xff000000u) != 0)
    {
        for (unsigned int i = 0; i < 4; ++i)
        {
            const unsigned int c = (byte)name[i + 4];
            key1 |= c << (i * 8);
            if (!c)
                break;
        }
    }

    const unsigned int set =
        (key0 ^ key1 ^ (key0 >> 16) ^ (key1 >> 16)) &
        (V_PATCH_NAME_CACHE_SETS - 1);
    const unsigned int base = set * V_PATCH_NAME_CACHE_WAYS;

    for (unsigned int way = 0; way < V_PATCH_NAME_CACHE_WAYS; ++way)
    {
        const v_patch_name_cache_entry_t* const entry =
            &v_patch_name_cache.entries[base + way];

        if (entry->lump_plus_one &&
            entry->key0 == key0 && entry->key1 == key1)
            return entry->lump_plus_one - 1;
    }

    const int lump = W_GetNumForName(name);
    const unsigned int slot = base +
        (v_patch_name_cache.victim[set]++ & (V_PATCH_NAME_CACHE_WAYS - 1));

    v_patch_name_cache.entries[slot].key0 = key0;
    v_patch_name_cache.entries[slot].key1 = key1;
    v_patch_name_cache.entries[slot].lump_plus_one = lump + 1;

    return lump;
}

void V_DrawNumPatch(int x, int y, int scrn, int lump,
         int cm, enum patch_translation_e flags)
{
    V_DrawPatch(x, y, scrn, W_CacheLumpNum(lump));
}

//
// V_SetPalette
//
// CPhipps - New function to set the palette to palette number pal.
// Handles loading of PLAYPAL and calls I_SetPalette

void V_SetPalette(int pal)
{
	I_SetPalette(pal);
}

//Colour corrected PLAYPAL lumps ~ Kippykip
void V_SetPalLump(int index)
{
    if(index < 0)
        index = 0;
    else if(index > 5)
        index = 5;

    char lumpName[9] = "PLAYPAL0";

    if(index == 0)
        lumpName[7] = 0;
    else
        lumpName[7] = '0' + index;

    _g->pallete_lump = W_CacheLumpName(lumpName);
}

//
// V_FillRect
//
// CPhipps - New function to fill a rectangle with a given colour
void V_FillRect(int x, int y, int width, int height, byte colour)
{
    byte* fb = (byte*)_g->screens[0].data;

    byte* dest = &fb[(ScreenYToOffset(y) << 1) + x];

    while (height--)
    {
        BlockSet(dest, colour, width);
        dest += (SCREENPITCH << 1);
    }
}



static void V_PlotPixel(int x, int y, int color)
{
    byte* fb = (byte*)_g->screens[0].data;

    byte* dest = &fb[(ScreenYToOffset(y) << 1) + x];

    //The GBA must write in 16bits.
    if((unsigned int)dest & 1)
    {
        //Odd addreses, we combine existing pixel with new one.
        unsigned short* dest16 = (unsigned short*)(dest - 1);

        unsigned short old = *dest16;

        *dest16 = (old & 0xff) | (color << 8);
    }
    else
    {
        unsigned short* dest16 = (unsigned short*)dest;

        unsigned short old = *dest16;

        *dest16 = ((color & 0xff) | (old & 0xff00));
    }
}

//
// WRAP_V_DrawLine()
//
// Draw a line in the frame buffer.
// Classic Bresenham w/ whatever optimizations needed for speed
//
// Passed the frame coordinates of line, and the color to be drawn
// Returns nothing
//
void V_DrawLine(fline_t* fl, int color)
{
    int x0 = fl->a.x;
    int x1 = fl->b.x;

    int y0 = fl->a.y;
    int y1 = fl->b.y;

    int dx =  D_abs(x1-x0);
    int sx = x0<x1 ? 1 : -1;

    int dy = -D_abs(y1-y0);
    int sy = y0<y1 ? 1 : -1;

    int err = dx + dy;

    while(true)
    {
        V_PlotPixel(x0, y0, color);

        if (x0==x1 && y0==y1)
            break;

        int e2 = 2*err;

        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}
