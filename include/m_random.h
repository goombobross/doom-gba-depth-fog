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
 *  Functions to return random numbers.
 *
 *-----------------------------------------------------------------------------*/


#ifndef __M_RANDOM__
#define __M_RANDOM__

#include "doomtype.h"


#include "doomtype.h"

// Returns a number from 0 to 255,
// from a lookup table.
int M_Random (void);

// As M_Random, but used only by the play simulation.
int P_Random (void);

/*
 * Exact fast remainders for P_Random(), whose result is guaranteed to be
 * in the range 0..255.  ARM7TDMI has no hardware divide, so the original
 * % operators call a software divider.  These multiply/shift reductions
 * are bit-identical for every possible RNG result and consume one random
 * value exactly like the original expressions.
 */
inline static unsigned int P_RandomMod3(void)
{
    const unsigned int x = (unsigned int)P_Random();
    const unsigned int q = (x * 171u) >> 9;
    return x - q * 3u;
}

inline static unsigned int P_RandomMod5(void)
{
    const unsigned int x = (unsigned int)P_Random();
    const unsigned int q = (x * 205u) >> 10;
    return x - q * 5u;
}

inline static unsigned int P_RandomMod6(void)
{
    const unsigned int x = (unsigned int)P_Random();
    const unsigned int q = (x * 171u) >> 10;
    return x - q * 6u;
}

inline static unsigned int P_RandomMod10(void)
{
    const unsigned int x = (unsigned int)P_Random();
    const unsigned int q = (x * 205u) >> 11;
    return x - q * 10u;
}

// Fix randoms for demos.
void M_ClearRandom (void);

#endif
