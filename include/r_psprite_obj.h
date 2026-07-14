#ifndef __R_PSPRITE_OBJ_H__
#define __R_PSPRITE_OBJ_H__

#include "doomtype.h"
#include "r_defs.h"

typedef struct
{
    const patch_t *patch;
    const lighttable_t *colormap;
    fixed_t startfrac;
    fixed_t xiscale;
    short x;
    short y;
    short width;
    short height;
} psprite_obj_part_t;

void R_PSpriteOBJInit(void);
void R_PSpriteOBJVBlank(void);
void R_PSpriteOBJHide(void);
boolean R_PSpriteOBJSubmit(const psprite_obj_part_t *weapon,
                           const psprite_obj_part_t *flash);

#endif
