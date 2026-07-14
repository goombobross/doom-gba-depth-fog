#ifndef GBA_FUNCTIONS_H
#define GBA_FUNCTIONS_H

#include <string.h>
#include "doomtype.h"
#include "m_fixed.h"

#ifdef GBA
    #include <tonc_bios.h>
    #include <tonc_memdef.h>
    #include <tonc_memmap.h>
#endif


inline static CONSTFUNC int IDiv32 (int a, int b)
{

    //use bios divide on gba.
#ifdef GBA
    return Div(a, b);
#else
    return a / b;
#endif
}

inline static void BlockCopy(void* dest, const void* src, const unsigned int len)
{
#ifdef GBA
    const int words = len >> 2;

    REG_DMA[3].cnt = 0;
    REG_DMA[3].src = src;
    REG_DMA[3].dst = dest;
    REG_DMA[3].cnt = DMA_DST_INC | DMA_SRC_INC | DMA_32NOW | words;
#else
    memcpy(dest, src, len & 0xfffffffc);
#endif
}

inline static void CpuBlockCopy(void* dest, const void* src, const unsigned int len)
{
#ifdef GBA
    const unsigned int words = len >> 2;

    CpuFastSet(src, dest, words);
#else
    BlockCopy(dest, src, len);
#endif
}

inline static void BlockSet(void* dest, volatile unsigned int val, const unsigned int len)
{
#ifdef GBA
    const int words = len >> 2;

    REG_DMA[3].cnt = 0;
    REG_DMA[3].src = (const void*)&val;
    REG_DMA[3].dst = dest;
    REG_DMA[3].cnt = DMA_DST_INC | DMA_SRC_FIXED | DMA_32NOW | words;
#else
    memset(dest, val, len & 0xfffffffc);
#endif
}

inline static void ByteCopy(byte* dest, const byte* src, unsigned int count)
{
    do
    {
        *dest++ = *src++;
    } while(--count);
}

inline static void ByteSet(byte* dest, byte val, unsigned int count)
{
    do
    {
        *dest++ = val;
    } while(--count);
}

inline static void* ByteFind(byte* mem, byte val, unsigned int count)
{
    do
    {
        if(*mem == val)
            return mem;

        mem++;
    } while(--count);

    return NULL;
}

inline static void SaveSRAM(const byte* eeprom, unsigned int size, unsigned int offset)
{
#ifdef GBA
    ByteCopy((byte*)(0xE000000 + offset), eeprom, size);
#endif
}

inline static void LoadSRAM(byte* eeprom, unsigned int size, unsigned int offset)
{
#ifdef GBA
    ByteCopy(eeprom, (byte*)(0xE000000 + offset), size);
#endif
}

//Cheap mul by 120. Not sure if faster.
#define ScreenYToOffset(x) ((x << 7) - (x << 3))

#endif // GBA_FUNCTIONS_H
