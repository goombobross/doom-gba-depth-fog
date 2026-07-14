#include <stdarg.h>
#include <stdio.h>
#include <cstring>
#include <tonc.h>

#ifdef GBA

extern "C"
{
    #include "doomdef.h"
    #include "doomtype.h"
    #include "d_main.h"
    #include "d_event.h"
    #include "gba_functions.h"
    #include "global_data.h"
    #include "tables.h"
}

#include "i_system_e32.h"
#include "lprintf.h"
#include <maxmod.h>

#define VID_PAGE1 MEM_VRAM
#define VID_PAGE2 (MEM_VRAM + 0xA000)

// Preserve the existing 2/2/4 memory-clock configuration used by this fork.
#define REG_MEMCTRL (*(volatile u32*)0x04000800)

//*******************************************************************************
// Direct ARM-state VBlank handler. This avoids libgba's larger IRQ/console layer.
//*******************************************************************************
__attribute__((target("arm")))
void VBlankCallback()
{
    mmVBlank();
    mmFrame();

    // irq_init() installs this as the direct IRQ handler, so acknowledge here.
    REG_IFBIOS |= IRQ_VBLANK;
    REG_IF = IRQ_VBLANK;
}

void I_InitScreen_e32()
{
    irq_init(VBlankCallback);
    irq_enable(II_VBLANK);

    // Game Pak wait states and prefetch, expressed using libtonc definitions.
    REG_WAITCNT = WS_PREFETCH |
                  WS_ROM2_S1 | WS_ROM2_N2 |
                  WS_ROM1_S1 | WS_ROM1_N2 |
                  WS_ROM0_S1 | WS_ROM0_N2 |
                  WS_SRAM_2;

    REG_MEMCTRL = 0x0E000020;

    // Keep an emergency text console available without libgba's 4 KiB handle table.
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG0;
    tte_init_se_default(0, BG_CBB(0) | BG_SBB(31));

    key_poll();

    REG_TM2CNT_L = 65535 - 1872; // 1872 ticks = 1/35 second.
    REG_TM2CNT_H = TM_FREQ_256 | TM_ENABLE;
    REG_TM3CNT_H = TM_CASCADE | TM_ENABLE;
}

void I_BlitScreenBmp_e32()
{
}

void I_StartWServEvents_e32()
{
}

void I_PollWServEvents_e32()
{
    key_poll();

    const u16 current = key_curr_state();
    const u16 previous = key_prev_state();
    const u16 key_down = current & ~previous;
    const u16 key_up = previous & ~current;

    event_t ev;

    if (key_down)
    {
        ev.type = ev_keydown;

        if (key_down & KEY_UP)       { ev.data1 = KEYD_UP;     D_PostEvent(&ev); }
        else if (key_down & KEY_DOWN){ ev.data1 = KEYD_DOWN;   D_PostEvent(&ev); }

        if (key_down & KEY_LEFT)      { ev.data1 = KEYD_LEFT;   D_PostEvent(&ev); }
        else if (key_down & KEY_RIGHT){ ev.data1 = KEYD_RIGHT;  D_PostEvent(&ev); }

        if (key_down & KEY_SELECT) { ev.data1 = KEYD_SELECT; D_PostEvent(&ev); }
        if (key_down & KEY_START)  { ev.data1 = KEYD_START;  D_PostEvent(&ev); }
        if (key_down & KEY_A)      { ev.data1 = KEYD_A;      D_PostEvent(&ev); }
        if (key_down & KEY_B)      { ev.data1 = KEYD_B;      D_PostEvent(&ev); }
        if (key_down & KEY_L)      { ev.data1 = KEYD_L;      D_PostEvent(&ev); }
        if (key_down & KEY_R)      { ev.data1 = KEYD_R;      D_PostEvent(&ev); }
    }

    if (key_up)
    {
        ev.type = ev_keyup;

        if (key_up & KEY_UP)       { ev.data1 = KEYD_UP;     D_PostEvent(&ev); }
        else if (key_up & KEY_DOWN){ ev.data1 = KEYD_DOWN;   D_PostEvent(&ev); }

        if (key_up & KEY_LEFT)      { ev.data1 = KEYD_LEFT;   D_PostEvent(&ev); }
        else if (key_up & KEY_RIGHT){ ev.data1 = KEYD_RIGHT;  D_PostEvent(&ev); }

        if (key_up & KEY_SELECT) { ev.data1 = KEYD_SELECT; D_PostEvent(&ev); }
        if (key_up & KEY_START)  { ev.data1 = KEYD_START;  D_PostEvent(&ev); }
        if (key_up & KEY_A)      { ev.data1 = KEYD_A;      D_PostEvent(&ev); }
        if (key_up & KEY_B)      { ev.data1 = KEYD_B;      D_PostEvent(&ev); }
        if (key_up & KEY_L)      { ev.data1 = KEYD_L;      D_PostEvent(&ev); }
        if (key_up & KEY_R)      { ev.data1 = KEYD_R;      D_PostEvent(&ev); }
    }
}

void I_ClearWindow_e32()
{
}

unsigned short* I_GetBackBuffer()
{
    if (REG_DISPCNT & DCNT_PAGE)
        return (unsigned short*)VID_PAGE1;

    return (unsigned short*)VID_PAGE2;
}

unsigned short* I_GetFrontBuffer()
{
    if (REG_DISPCNT & DCNT_PAGE)
        return (unsigned short*)VID_PAGE2;

    return (unsigned short*)VID_PAGE1;
}

void I_CreateWindow_e32()
{
    // Bit 5 unlocks OAM during HBlank, matching the existing port.
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2 | DCNT_OAM_HBL;

    unsigned short* bb = I_GetBackBuffer();
    BlockSet(bb, 0, 240 * 160);
    I_FinishUpdate_e32(NULL, NULL, 0, 0);

    bb = I_GetBackBuffer();
    BlockSet(bb, 0, 240 * 160);
    I_FinishUpdate_e32(NULL, NULL, 0, 0);
}

void I_CreateBackBuffer_e32()
{
    I_CreateWindow_e32();
}

void I_FinishUpdate_e32(const byte* srcBuffer, const byte* pallete,
                        const unsigned int width, const unsigned int height)
{
    (void)srcBuffer;
    (void)pallete;
    (void)width;
    (void)height;
    REG_DISPCNT ^= DCNT_PAGE;
}

void I_SetPallete_e32(const byte* pallete)
{
    unsigned short* pal_ram = (unsigned short*)MEM_PAL;

    for (int i = 0; i < 256; ++i)
    {
        const unsigned int r = *pallete++;
        const unsigned int g = *pallete++;
        const unsigned int b = *pallete++;
        pal_ram[i] = RGB15(r >> 3, g >> 3, b >> 3);
    }
}

int I_GetVideoWidth_e32()
{
    return 120;
}

int I_GetVideoHeight_e32()
{
    return 160;
}

void I_ProcessKeyEvents()
{
    I_PollWServEvents_e32();
}

#define MAX_MESSAGE_SIZE 512

__attribute__((noreturn))
void I_Error(const char* error, ...)
{
    REG_DISPCNT = DCNT_MODE0 | DCNT_BG0;
    tte_init_se_default(0, BG_CBB(0) | BG_SBB(31));

    char msg[MAX_MESSAGE_SIZE];
    va_list v;
    va_start(v, error);
    vsnprintf(msg, MAX_MESSAGE_SIZE, error, v);
    va_end(v);
    msg[MAX_MESSAGE_SIZE - 1] = '\0';

    tte_write(msg);

    while (true)
        VBlankIntrWait();
}

extern "C"
{
    void __assert_func_stub(const char* file, int line, const char* fnct, const char* msg)
    {
        (void)file;
        (void)line;
        (void)fnct;
        I_Error("Assertion failed: %s", msg ? msg : "(no message)");
    }

    int printf(const char* format, ...)
    {
#ifdef NDEBUG
        // Release builds keep debug printing out of the gameplay path entirely.
        (void)format;
        return 0;
#else
        char msg[128];
        va_list v;
        va_start(v, format);
        const int result = vsnprintf(msg, sizeof(msg), format, v);
        va_end(v);
        msg[sizeof(msg) - 1] = '\0';
        tte_write(msg);
        return result;
#endif
    }
}

void I_Quit_e32()
{
}

#endif
