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
 *   the automap code
 *
 *-----------------------------------------------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "doomstat.h"
#include "st_stuff.h"
#include "r_main.h"
#include "p_setup.h"
#include "p_maputl.h"
#include "w_wad.h"
#include "v_video.h"
#include "p_spec.h"
#include "am_map.h"
#include "dstrings.h"
#include "lprintf.h"  // jff 08/03/98 - declaration of lprintf
#include "g_game.h"
#include "m_fixed.h"

#include "global_data.h"


static const int mapcolor_back = 247;    // map background
static const int mapcolor_wall = 23;    // normal 1s wall color
static const int mapcolor_fchg = 55;    // line at floor height change color
static const int mapcolor_cchg = 215;    // line at ceiling height change color
static const int mapcolor_clsd = 208;    // line at sector with floor=ceiling color
static const int mapcolor_rdor = 175;    // red door color  (diff from keys to allow option)
static const int mapcolor_bdor = 204;    // blue door color (of enabling one but not other )
static const int mapcolor_ydor = 231;    // yellow door color
static const int mapcolor_tele = 119;    // teleporter line color
static const int mapcolor_secr = 252;    // secret sector boundary color
static const int mapcolor_exit = 0;    // jff 4/23/98 add exit line color
static const int mapcolor_unsn = 104;    // computer map unseen line color
static const int mapcolor_flat = 88;    // line with no floor/ceiling changes
static const int mapcolor_sngl = 208;    // single player arrow color
static const int map_secret_after = 0;

#define AM_FRAME_WIDTH  (SCREENWIDTH * 2)
#define AM_FRAME_HEIGHT (SCREENHEIGHT - ST_SCALED_HEIGHT)

static const int f_w = AM_FRAME_WIDTH;
static const int f_h = AM_FRAME_HEIGHT; // to allow runtime setting of width/height



//jff 3/9/98 add option to not show secret sectors until entered
//jff 4/3/98 add symbols for "no-color" for disable and "black color" for black
#define NC 0
#define BC 247

// drawing stuff
#define FB    0


// how much the automap moves window per tic in frame-buffer coordinates
// moves 140 pixels in 1 second
#define F_PANINC  4
// how much zoom-in per tic
// goes to 2x in 1 second
#define M_ZOOMIN        ((int) (1.02*FRACUNIT))
// how much zoom-out per tic
// pulls out to 0.5x in 1 second
#define M_ZOOMOUT       ((int) (FRACUNIT/1.02))

#define PLAYERRADIUS    (16*(1<<MAPBITS)) // e6y

/* This expression is entirely compile-time constant. */
#define AM_MAX_SCALE_MTOF \
    ((fixed_t)(((long long)AM_FRAME_HEIGHT << (FRACBITS * 2)) / \
               (2 * PLAYERRADIUS)))

/*
 * Cached 16.16 slopes reduce clipping from as many as four divides to at
 * most two per line.  Set to 0 to restore the legacy edge-rounding path.
 */
#ifndef AM_FAST_CLIP_DIV
#define AM_FAST_CLIP_DIV 1
#endif

// translates between frame-buffer and map distances
#define FTOM(x) FixedMul(((x)<<16),_g->scale_ftom)
#define MTOF(x) (FixedMul((x),_g->scale_mtof)>>16)
// translates between frame-buffer and map coordinates
#define CXMTOF(x)  (MTOF((x)- _g->m_x))
#define CYMTOF(y)  ((f_h - MTOF((y)- _g->m_y)))

typedef struct
{
    mpoint_t a, b;
} mline_t;

//
// The vector graphics for the automap.
//  A line drawing of the player pointing right,
//   starting from the middle.
//
#define R ((8*PLAYERRADIUS)/7)
static const mline_t player_arrow[] =
{
  { { -R+R/8, 0 }, { R, 0 } }, // -----
  { { R, 0 }, { R-R/2, R/4 } },  // ----->
  { { R, 0 }, { R-R/2, -R/4 } },
  { { -R+R/8, 0 }, { -R-R/8, R/4 } }, // >---->
  { { -R+R/8, 0 }, { -R-R/8, -R/4 } },
  { { -R+3*R/8, 0 }, { -R+R/8, R/4 } }, // >>--->
  { { -R+3*R/8, 0 }, { -R+R/8, -R/4 } }
};
#undef R
#define NUMPLYRLINES (sizeof(player_arrow)/sizeof(mline_t))



//
// AM_activateNewScale()
//
// Changes the map scale after zooming or translating
//
// Passed nothing, returns nothing
//
static void AM_activateNewScale(void)
{
    _g->m_x += _g->m_w/2;
    _g->m_y += _g->m_h/2;
    _g->m_w = FTOM(f_w);
    _g->m_h = FTOM(f_h);
    _g->m_x -= _g->m_w/2;
    _g->m_y -= _g->m_h/2;
    _g->m_x2 =  _g->m_x + _g->m_w;
    _g->m_y2 =  _g->m_y + _g->m_h;
}

//
// AM_findMinMaxBoundaries()
//
// Determines bounding box of all vertices,
// sets global variables controlling zoom range.
//
// Passed nothing, returns nothing
//
static void AM_findMinMaxBoundaries(void)
{
    int i;
    fixed_t a;
    fixed_t b;

    _g->min_x = _g->min_y =  INT_MAX;
    _g->max_x = _g->max_y = -INT_MAX;

    for (i=0;i<_g->numvertexes;i++)
    {
        if (_g->vertexes[i].x < _g->min_x)
            _g->min_x = _g->vertexes[i].x;
        else if (_g->vertexes[i].x > _g->max_x)
            _g->max_x = _g->vertexes[i].x;

        if (_g->vertexes[i].y < _g->min_y)
            _g->min_y = _g->vertexes[i].y;
        else if (_g->vertexes[i].y > _g->max_y)
            _g->max_y = _g->vertexes[i].y;
    }

    _g->max_w = (_g->max_x >>= FRACTOMAPBITS) - (_g->min_x >>= FRACTOMAPBITS);//e6y
    _g->max_h = (_g->max_y >>= FRACTOMAPBITS) - (_g->min_y >>= FRACTOMAPBITS);//e6y

    a = FixedDiv(f_w << FRACBITS, _g->max_w);
    b = FixedDiv(f_h << FRACBITS, _g->max_h);

    _g->min_scale_mtof = a < b ? a : b;
    _g->max_scale_mtof = AM_MAX_SCALE_MTOF;
}

//
// AM_changeWindowLoc()
//
// Moves the map window by the global variables m_paninc.x, m_paninc.y
//
// Passed nothing, returns nothing
//
static void AM_changeWindowLoc(void)
{
    if ( _g->m_paninc.x ||  _g->m_paninc.y)
    {
        _g->automapmode &= ~am_follow;
        _g->f_oldloc.x = INT_MAX;
    }

    _g->m_x +=  _g->m_paninc.x;
    _g->m_y +=  _g->m_paninc.y;

    if ( _g->m_x + _g->m_w/2 > _g->max_x)
        _g->m_x = _g->max_x - _g->m_w/2;
    else if ( _g->m_x + _g->m_w/2 < _g->min_x)
        _g->m_x = _g->min_x - _g->m_w/2;

    if ( _g->m_y + _g->m_h/2 > _g->max_y)
        _g->m_y = _g->max_y - _g->m_h/2;
    else if ( _g->m_y + _g->m_h/2 < _g->min_y)
        _g->m_y = _g->min_y - _g->m_h/2;

    _g->m_x2 =  _g->m_x + _g->m_w;
    _g->m_y2 =  _g->m_y + _g->m_h;
}


//
// AM_initVariables()
//
// Initialize the variables for the automap
//
// Affects the automap global variables
// Status bar is notified that the automap has been entered
// Passed nothing, returns nothing
//
static void AM_initVariables(void)
{
    static const event_t st_notify = { ev_keyup, AM_MSGENTERED, 0, 0 };

    _g->automapmode |= am_active;

    _g->f_oldloc.x = INT_MAX;

    _g->m_paninc.x =  _g->m_paninc.y = 0;

    _g->m_w = FTOM(f_w);
    _g->m_h = FTOM(f_h);


    _g->m_x = (_g->player.mo->x >> FRACTOMAPBITS) - _g->m_w/2;//e6y
    _g->m_y = (_g->player.mo->y >> FRACTOMAPBITS) - _g->m_h/2;//e6y
    AM_changeWindowLoc();

    // inform the status bar of the change
    ST_Responder(&st_notify);
}

//
// AM_LevelInit()
//
// Initialize the automap at the start of a new level
// should be called at the start of every level
//
// Passed nothing, returns nothing
// Affects automap's global variables
//
// CPhipps - get status bar height from status bar code
static void AM_LevelInit(void)
{
    AM_findMinMaxBoundaries();
    _g->scale_mtof = FixedDiv(_g->min_scale_mtof, (int)(0.7 * FRACUNIT));
    if (_g->scale_mtof > _g->max_scale_mtof)
        _g->scale_mtof = _g->min_scale_mtof;
    _g->scale_ftom = FixedDiv(FRACUNIT, _g->scale_mtof);
}

//
// AM_Stop()
//
// Cease automap operations, unload patches, notify status bar
//
// Passed nothing, returns nothing
//
void AM_Stop (void)
{
    static const event_t st_notify = { 0, ev_keyup, AM_MSGEXITED, 0 };

    _g->automapmode  = 0;
    ST_Responder(&st_notify);
    _g->stopped = true;
}

//
// AM_Start()
//
// Start up automap operations,
//  if a new level, or game start, (re)initialize level variables
//  init map variables
//  load mark patches
//
// Passed nothing, returns nothing
//
void AM_Start(void)
{
    if (!_g->stopped)
        AM_Stop();

    _g->stopped = false;
    if (_g->lastlevel != _g->gamemap || _g->lastepisode != _g->gameepisode)
    {
        AM_LevelInit();
        _g->lastlevel = _g->gamemap;
        _g->lastepisode = _g->gameepisode;
    }
    AM_initVariables();
}

//
// AM_minOutWindowScale()
//
// Set the window scale to the maximum size
//
// Passed nothing, returns nothing
//
static void AM_minOutWindowScale(void)
{
    _g->scale_mtof = _g->min_scale_mtof;
    _g->scale_ftom = FixedDiv(FRACUNIT, _g->scale_mtof);
    AM_activateNewScale();
}

//
// AM_maxOutWindowScale(void)
//
// Set the window scale to the minimum size
//
// Passed nothing, returns nothing
//
static void AM_maxOutWindowScale(void)
{
    _g->scale_mtof = _g->max_scale_mtof;
    _g->scale_ftom = FixedDiv(FRACUNIT, _g->scale_mtof);
    AM_activateNewScale();
}

//
// AM_Responder()
//
// Handle events (user inputs) in automap mode
//
// Passed an input event, returns true if its handled
//
boolean AM_Responder
( event_t*  ev )
{
    int rc;
    int ch;                                                       // phares

    rc = false;

    if (!(_g->automapmode & am_active))
    {
        if (ev->type == ev_keydown && ev->data1 == key_map)         // phares
        {
            AM_Start ();
            rc = true;
        }
    }
    else if (ev->type == ev_keydown)
    {
        rc = true;
        ch = ev->data1;                                             // phares

        if (ch == key_map_right)                                    //    |
            if (!(_g->automapmode & am_follow))                           //    V
                _g->m_paninc.x = FTOM(F_PANINC);
            else
                rc = false;
        else if (ch == key_map_left)
            if (!(_g->automapmode & am_follow))
                _g->m_paninc.x = -FTOM(F_PANINC);
            else
                rc = false;
        else if (ch == key_map_up)
            if (!(_g->automapmode & am_follow))
                _g->m_paninc.y = FTOM(F_PANINC);
            else
                rc = false;
        else if (ch == key_map_down)
            if (!(_g->automapmode & am_follow))
                _g->m_paninc.y = -FTOM(F_PANINC);
            else
                rc = false;
        else if (ch == key_map)
        {
            if(_g->automapmode & am_overlay)
                AM_Stop ();
            else
                _g->automapmode |= (am_overlay | am_rotate | am_follow);
        }
        else if (ch == key_map_follow && _g->gamekeydown[key_use])
        {
            _g->automapmode ^= am_follow;     // CPhipps - put all automap mode stuff into one enum
            _g->f_oldloc.x = INT_MAX;
            // Ty 03/27/98 - externalized
            _g->player.message = (_g->automapmode & am_follow) ? AMSTR_FOLLOWON : AMSTR_FOLLOWOFF;
        }                                                         //    |
        else if (ch == key_map_zoomout)
        {
            _g->mtof_zoommul = M_ZOOMOUT;
            _g->ftom_zoommul = M_ZOOMIN;
        }
        else if (ch == key_map_zoomin)
        {
            _g->mtof_zoommul = M_ZOOMIN;
            _g->ftom_zoommul = M_ZOOMOUT;
        }
        else                                                        // phares
        {
            rc = false;
        }
    }
    else if (ev->type == ev_keyup)
    {
        rc = false;
        ch = ev->data1;
        if (ch == key_map_right)
        {
            if (!(_g->automapmode & am_follow))
                _g->m_paninc.x = 0;
        }
        else if (ch == key_map_left)
        {
            if (!(_g->automapmode & am_follow))
                _g->m_paninc.x = 0;
        }
        else if (ch == key_map_up)
        {
            if (!(_g->automapmode & am_follow))
                _g->m_paninc.y = 0;
        }
        else if (ch == key_map_down)
        {
            if (!(_g->automapmode & am_follow))
                _g->m_paninc.y = 0;
        }
        else if ((ch == key_map_zoomout) || (ch == key_map_zoomin))
        {
            _g->mtof_zoommul = FRACUNIT;
            _g->ftom_zoommul = FRACUNIT;
        }
    }
    return rc;
}

//
// AM_rotate()
//
// Rotation in 2D.
// Used to rotate player arrow line character.
//
// Passed the coordinates of a point, and an angle
// Returns the coordinates rotated by the angle
//
// CPhipps - made static & enhanced for automap rotation

static inline void AM_rotatePoint(fixed_t* x, fixed_t* y,
                                  fixed_t sine, fixed_t cosine,
                                  fixed_t xorig, fixed_t yorig)
{
    const fixed_t relx = *x - xorig;
    const fixed_t rely = *y - yorig;

    *x = xorig + FixedMul(relx, cosine) - FixedMul(rely, sine);
    *y = yorig + FixedMul(relx, sine) + FixedMul(rely, cosine);
}

//
// AM_changeWindowScale()
//
// Automap zooming
//
// Passed nothing, returns nothing
//
static void AM_changeWindowScale(void)
{
    // Both zoom multipliers are maintained as an inverse pair by AM_Responder.
    // Updating both scales with FixedMul removes a FixedDiv on every zoom tic.
    _g->scale_mtof = FixedMul(_g->scale_mtof, _g->mtof_zoommul);
    _g->scale_ftom = FixedMul(_g->scale_ftom, _g->ftom_zoommul);

    // One reciprocal Newton step removes quantization drift without division:
    // y <- y * (2 - x*y), where x=scale_mtof and y=scale_ftom.
    _g->scale_ftom = FixedMul(
        _g->scale_ftom,
        (2 * FRACUNIT) - FixedMul(_g->scale_mtof, _g->scale_ftom));

    if (_g->scale_mtof < _g->min_scale_mtof)
        AM_minOutWindowScale();
    else if (_g->scale_mtof > _g->max_scale_mtof)
        AM_maxOutWindowScale();
    else
        AM_activateNewScale();
}

//
// AM_doFollowPlayer()
//
// Turn on follow mode - the map scrolls opposite to player motion
//
// Passed nothing, returns nothing
//
static void AM_doFollowPlayer(void)
{
    if (_g->f_oldloc.x != _g->player.mo->x || _g->f_oldloc.y != _g->player.mo->y)
    {
        _g->m_x = FTOM(MTOF(_g->player.mo->x >> FRACTOMAPBITS)) - _g->m_w/2;//e6y
        _g->m_y = FTOM(MTOF(_g->player.mo->y >> FRACTOMAPBITS)) - _g->m_h/2;//e6y
        _g->m_x2 =  _g->m_x + _g->m_w;
        _g->m_y2 =  _g->m_y + _g->m_h;
        _g->f_oldloc.x = _g->player.mo->x;
        _g->f_oldloc.y = _g->player.mo->y;
    }
}

//
// AM_Ticker()
//
// Updates on gametic - enter follow mode, zoom, or change map location
//
// Passed nothing, returns nothing
//
void AM_Ticker (void)
{
    if (!(_g->automapmode & am_active))
        return;

    if (_g->automapmode & am_follow)
        AM_doFollowPlayer();

    // Change the zoom if necessary
    if (_g->ftom_zoommul != FRACUNIT)
        AM_changeWindowScale();

    // Change x,y location
    if ( _g->m_paninc.x ||  _g->m_paninc.y)
        AM_changeWindowLoc();
}

//
// AM_clipMline()
//
// Automap clipping of lines.
//
// Based on Cohen-Sutherland clipping algorithm but with a slightly
// faster reject and precalculated slopes. If the speed is needed,
// use a hash algorithm to handle the common cases.
//
// Passed the line's coordinates on map and in the frame buffer performs
// clipping on them in the lines frame coordinates.
// Returns true if any part of line was not clipped
//
#if AM_FAST_CLIP_DIV
static inline int AM_applySlope(fixed_t slope, int delta)
{
    const long long product = (long long)slope * delta;

    // C integer division truncates toward zero.  Mirror that behavior instead
    // of relying on implementation-defined rounding for a negative right shift.
    return product < 0
        ? -(int)((-product) >> FRACBITS)
        :  (int)(product >> FRACBITS);
}
#endif

static boolean AM_clipMline(mline_t* ml, fline_t* fl)
{
    enum
    {
        LEFT   = 1,
        RIGHT  = 2,
        BOTTOM = 4,
        TOP    = 8
    };

    int outcode1 = 0;
    int outcode2 = 0;
    int outside;
    fpoint_t tmp;
#if AM_FAST_CLIP_DIV
    int line_dx;
    int line_dy;
    fixed_t x_per_y = 0;
    fixed_t y_per_x = 0;
    boolean have_x_per_y = false;
    boolean have_y_per_x = false;
#else
    int dx;
    int dy;
#endif

#define DOOUTCODE(oc, mx, my) \
    (oc) = 0; \
    if ((my) < 0) (oc) |= TOP; \
    else if ((my) >= f_h) (oc) |= BOTTOM; \
    if ((mx) < 0) (oc) |= LEFT; \
    else if ((mx) >= f_w) (oc) |= RIGHT;

    // Map-space trivial rejection avoids transforming fully invisible lines.
    if (ml->a.y > _g->m_y2)
        outcode1 = TOP;
    else if (ml->a.y < _g->m_y)
        outcode1 = BOTTOM;

    if (ml->b.y > _g->m_y2)
        outcode2 = TOP;
    else if (ml->b.y < _g->m_y)
        outcode2 = BOTTOM;

    if (outcode1 & outcode2)
        return false;

    if (ml->a.x < _g->m_x)
        outcode1 |= LEFT;
    else if (ml->a.x > _g->m_x2)
        outcode1 |= RIGHT;

    if (ml->b.x < _g->m_x)
        outcode2 |= LEFT;
    else if (ml->b.x > _g->m_x2)
        outcode2 |= RIGHT;

    if (outcode1 & outcode2)
        return false;

    fl->a.x = CXMTOF(ml->a.x);
    fl->a.y = CYMTOF(ml->a.y);
    fl->b.x = CXMTOF(ml->b.x);
    fl->b.y = CYMTOF(ml->b.y);

    DOOUTCODE(outcode1, fl->a.x, fl->a.y)
    DOOUTCODE(outcode2, fl->b.x, fl->b.y)

    if (outcode1 & outcode2)
        return false;

#if AM_FAST_CLIP_DIV
    // The line slope never changes while clipping.  Compute each reciprocal
    // slope lazily and reuse it for every edge of the same orientation.
    line_dx = fl->b.x - fl->a.x;
    line_dy = fl->b.y - fl->a.y;
#endif

    while (outcode1 | outcode2)
    {
        outside = outcode1 ? outcode1 : outcode2;

#if AM_FAST_CLIP_DIV
        if (outside & (TOP | BOTTOM))
        {
            const int target_y = (outside & TOP) ? 0 : f_h;

            if (!line_dy)
                return false;

            if (!have_x_per_y)
            {
                x_per_y = FixedDiv(line_dx, line_dy);
                have_x_per_y = true;
            }

            tmp.x = fl->a.x + AM_applySlope(x_per_y, target_y - fl->a.y);
            tmp.y = (outside & TOP) ? 0 : f_h - 1;
        }
        else
        {
            const int target_x = (outside & RIGHT) ? f_w - 1 : 0;

            if (!line_dx)
                return false;

            if (!have_y_per_x)
            {
                y_per_x = FixedDiv(line_dy, line_dx);
                have_y_per_x = true;
            }

            tmp.y = fl->a.y + AM_applySlope(y_per_x, target_x - fl->a.x);
            tmp.x = target_x;
        }
#else
        if (outside & TOP)
        {
            dy = fl->a.y - fl->b.y;
            dx = fl->b.x - fl->a.x;
            tmp.x = fl->a.x + (dx * fl->a.y) / dy;
            tmp.y = 0;
        }
        else if (outside & BOTTOM)
        {
            dy = fl->a.y - fl->b.y;
            dx = fl->b.x - fl->a.x;
            tmp.x = fl->a.x + (dx * (fl->a.y - f_h)) / dy;
            tmp.y = f_h - 1;
        }
        else if (outside & RIGHT)
        {
            dy = fl->b.y - fl->a.y;
            dx = fl->b.x - fl->a.x;
            tmp.y = fl->a.y + (dy * (f_w - 1 - fl->a.x)) / dx;
            tmp.x = f_w - 1;
        }
        else
        {
            dy = fl->b.y - fl->a.y;
            dx = fl->b.x - fl->a.x;
            tmp.y = fl->a.y + (dy * -fl->a.x) / dx;
            tmp.x = 0;
        }
#endif

        if (outside == outcode1)
        {
            fl->a = tmp;
            DOOUTCODE(outcode1, fl->a.x, fl->a.y)
        }
        else
        {
            fl->b = tmp;
            DOOUTCODE(outcode2, fl->b.x, fl->b.y)
        }

        if (outcode1 & outcode2)
            return false;
    }

    return true;
}
#undef DOOUTCODE

//
// AM_drawMline()
//
// Clip lines, draw visible parts of lines.
//
// Passed the map coordinates of the line, and the color to draw it
// Color -1 is special and prevents drawing. Color 247 is special and
// is translated to black, allowing Color 0 to represent feature disable
// in the defaults file.
// Returns nothing.
//
static void AM_drawMline(mline_t* ml,int color)
{
    fline_t fl;

    if (color==-1)  // jff 4/3/98 allow not drawing any sort of line
        return;       // by setting its color to -1
    if (color==247) // jff 4/3/98 if color is 247 (xparent), use black
        color=0;

    if (AM_clipMline(ml, &fl))
        V_DrawLine(&fl, color); // draws it on frame buffer using fb coords
}

//
// AM_DoorColor()
//
// Returns the 'color' or key needed for a door linedef type
//
// Passed the type of linedef, returns:
//   -1 if not a keyed door
//    0 if a red key required
//    1 if a blue key required
//    2 if a yellow key required
//    3 if a multiple keys required
//
// jff 4/3/98 add routine to get color of generalized keyed door
//
static int AM_DoorColor(int type)
{
    if (GenLockedBase <= type && type< GenDoorBase)
    {
        type -= GenLockedBase;
        type = (type & LockedKey) >> LockedKeyShift;
        if (!type || type == 7)
            return 3;  // any or all keys

        --type;
        return type >= 3 ? type - 3 : type;
    }
    switch (type)  // closed keyed door
    {
    case 26: case 32: case 99: case 133:
        /*bluekey*/
        return 1;
    case 27: case 34: case 136: case 137:
        /*yellowkey*/
        return 2;
    case 28: case 33: case 134: case 135:
        /*redkey*/
        return 0;
    default:
        return -1; //not a keyed door
    }
}

//
// Determines visible lines, draws them.
// This is LineDef based, not LineSeg based.
//
// jff 1/5/98 many changes in this routine
// backward compatibility not needed, so just changes, no ifs
// addition of clauses for:
//    doors opening, keyed door id, secret sectors,
//    teleports, exit lines, key things
// ability to suppress any of added features or lines with no height changes
//
// support for gamma correction in automap abandoned
//
// jff 4/3/98 changed mapcolor_xxxx=0 as control to disable feature
// jff 4/3/98 changed mapcolor_xxxx=-1 to disable drawing line completely
//
static void AM_drawWalls(void)
{
    int i;
    const boolean rotate = (_g->automapmode & am_rotate) != 0;
    const boolean allmap = _g->player.powers[pw_allmap] != 0;
    fixed_t rotation_sine = 0;
    fixed_t rotation_cosine = FRACUNIT;
    fixed_t rotation_x = 0;
    fixed_t rotation_y = 0;

    if (rotate)
    {
        const angle_t angle = ANG90 - _g->player.mo->angle;
        const unsigned int fineangle = angle >> ANGLETOFINESHIFT;

        rotation_sine = finesine[fineangle];
        rotation_cosine = finecosine[fineangle];
        rotation_x = _g->player.mo->x >> FRACTOMAPBITS;
        rotation_y = _g->player.mo->y >> FRACTOMAPBITS;
    }

    // Determine whether a line will actually draw before doing coordinate
    // conversion, trigonometry, clipping, or framebuffer work.
    for (i = 0; i < _g->numlines; ++i)
    {
        const line_t* line = &_g->lines[i];
        const unsigned int flags = line->flags;
        const boolean mapped = (_g->linedata[i].r_flags & ML_MAPPED) != 0;
        const sector_t* backsector;
        const sector_t* frontsector;
        unsigned int line_special;
        int color = -1;
        mline_t l;

        if ((!mapped && !allmap) || (flags & ML_DONTDRAW))
            continue;

        backsector = LN_BACKSECTOR(line);
        frontsector = LN_FRONTSECTOR(line);
        line_special = LN_SPECIAL(line);

        if (mapped)
        {
            int amd = -1;

            if (!(flags & ML_SECRET))
                amd = AM_DoorColor(line_special);

            if (amd != -1)
            {
                switch (amd)
                {
                case 0: color = mapcolor_rdor; break;
                case 1: color = mapcolor_bdor; break;
                case 2: color = mapcolor_ydor; break;
                default: color = mapcolor_clsd; break;
                }
            }
            else if (mapcolor_exit &&
                     (line_special == 11 || line_special == 52 ||
                      line_special == 197 || line_special == 51 ||
                      line_special == 124 || line_special == 198))
            {
                color = mapcolor_exit;
            }
            else if (!backsector)
            {
                if (mapcolor_secr &&
                    ((map_secret_after && P_WasSecret(frontsector) &&
                      !P_IsSecret(frontsector)) ||
                     (!map_secret_after && P_WasSecret(frontsector))))
                    color = mapcolor_secr;
                else
                    color = mapcolor_wall;
            }
            else if (mapcolor_tele && !(flags & ML_SECRET) &&
                     (line_special == 39 || line_special == 97 ||
                      line_special == 125 || line_special == 126))
            {
                color = mapcolor_tele;
            }
            else if (flags & ML_SECRET)
            {
                color = mapcolor_wall;
            }
            else if (mapcolor_clsd &&
                     ((backsector->floorheight == backsector->ceilingheight) ||
                      (frontsector->floorheight == frontsector->ceilingheight)))
            {
                color = mapcolor_clsd;
            }
            else if (mapcolor_secr &&
                     ((map_secret_after &&
                       ((P_WasSecret(frontsector) && !P_IsSecret(frontsector)) ||
                        (P_WasSecret(backsector) && !P_IsSecret(backsector)))) ||
                      (!map_secret_after &&
                       (P_WasSecret(frontsector) || P_WasSecret(backsector)))))
            {
                color = mapcolor_secr;
            }
            else if (backsector->floorheight != frontsector->floorheight)
            {
                color = mapcolor_fchg;
            }
            else if (backsector->ceilingheight != frontsector->ceilingheight)
            {
                color = mapcolor_cchg;
            }
        }
        else if (mapcolor_flat || !backsector ||
                 backsector->floorheight != frontsector->floorheight ||
                 backsector->ceilingheight != frontsector->ceilingheight)
        {
            color = mapcolor_unsn;
        }

        if (color < 0)
            continue;

        l.a.x = line->v1.x >> FRACTOMAPBITS;
        l.a.y = line->v1.y >> FRACTOMAPBITS;
        l.b.x = line->v2.x >> FRACTOMAPBITS;
        l.b.y = line->v2.y >> FRACTOMAPBITS;

        if (rotate)
        {
            AM_rotatePoint(&l.a.x, &l.a.y, rotation_sine, rotation_cosine,
                           rotation_x, rotation_y);
            AM_rotatePoint(&l.b.x, &l.b.y, rotation_sine, rotation_cosine,
                           rotation_x, rotation_y);
        }

        AM_drawMline(&l, color);
    }
}

//
// AM_drawLineCharacter()
//
// Draws a vector graphic according to numerous parameters
//
// Passed the structure defining the vector graphic shape, the number
// of vectors in it, the scale to draw it at, the angle to draw it at,
// the color to draw it with, and the map coordinates to draw it at.
// Returns nothing
//
static void AM_drawLineCharacter(const mline_t* lineguy, int lineguylines,
                                 fixed_t scale, angle_t angle, int color,
                                 fixed_t x, fixed_t y)
{
    int i;
    mline_t l;
    fixed_t sine = 0;
    fixed_t cosine = FRACUNIT;
    boolean rotate = false;

    if (_g->automapmode & am_rotate)
        angle -= _g->player.mo->angle - ANG90;

    if (angle)
    {
        const unsigned int fineangle = angle >> ANGLETOFINESHIFT;
        sine = finesine[fineangle];
        cosine = finecosine[fineangle];
        rotate = true;
    }

    for (i = 0; i < lineguylines; ++i)
    {
        l.a = lineguy[i].a;
        l.b = lineguy[i].b;

        if (scale)
        {
            l.a.x = FixedMul(scale, l.a.x);
            l.a.y = FixedMul(scale, l.a.y);
            l.b.x = FixedMul(scale, l.b.x);
            l.b.y = FixedMul(scale, l.b.y);
        }

        if (rotate)
        {
            AM_rotatePoint(&l.a.x, &l.a.y, sine, cosine, 0, 0);
            AM_rotatePoint(&l.b.x, &l.b.y, sine, cosine, 0, 0);
        }

        l.a.x += x;
        l.a.y += y;
        l.b.x += x;
        l.b.y += y;

        AM_drawMline(&l, color);
    }
}

//
// AM_drawPlayers()
//
// Draws the player arrow in single player,
//
// Passed nothing, returns nothing
//
static void AM_drawPlayers(void)
{    
    AM_drawLineCharacter
            (
                player_arrow,
                NUMPLYRLINES,
                0,
                _g->player.mo->angle,
                mapcolor_sngl,      //jff color
                _g->player.mo->x >> FRACTOMAPBITS,//e6y
                _g->player.mo->y >> FRACTOMAPBITS);//e6y

}

//
// AM_Drawer()
//
// Draws the entire automap
//
// Passed nothing, returns nothing
//
void AM_Drawer (void)
{
    // CPhipps - all automap modes put into one enum
    if (!(_g->automapmode & am_active)) return;

    if (!(_g->automapmode & am_overlay)) // cph - If not overlay mode, clear background for the automap
        V_FillRect(0, 0, f_w, f_h, (byte)mapcolor_back); //jff 1/5/98 background default color

    AM_drawWalls();
    AM_drawPlayers();
}
