#include "OLED_Text.h"
#ifndef OLED_TEXT_LAYOUT_ONLY
#include "OLED.h"
#endif
#include "OLED_ChineseFont.h"
#include <stddef.h>
extern uint8_t OLED_DisplayBuf[8][128];
uint32_t OLED_UTF8Next(const char *text, uint16_t length, uint16_t *offset)
{
    uint32_t cp, minimum;
    uint16_t i = *offset;
    uint8_t n, j, first;
    if (text == NULL || i >= length || !text[i]) return 0U;
    first = (uint8_t)text[i++]; *offset = i;
    if (first < 0x80U) return first;
    if (first >= 0xC2U && first <= 0xDFU) { n = 1U; cp = first & 31U; minimum = 0x80U; }
    else if (first >= 0xE0U && first <= 0xEFU) { n = 2U; cp = first & 15U; minimum = 0x800U; }
    else if (first >= 0xF0U && first <= 0xF4U) { n = 3U; cp = first & 7U; minimum = 0x10000U; }
    else return 0xFFFDU;
    for (j = 0U; j < n; j++) {
        if (i >= length || ((uint8_t)text[i] & 0xC0U) != 0x80U) return 0xFFFDU;
        cp = (cp << 6U) | ((uint8_t)text[i++] & 63U);
    }
    *offset = i;
    if (cp < minimum || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) return 0xFFFDU;
    return cp;
}
static const uint8_t *chinese(uint32_t cp, uint8_t mode)
{
#if OLED_ENABLE_LEGACY_FONTS
    const uint16_t *codes = mode==2U ? OLED_MediumCodepoints : (mode ? OLED_SmallCodepoints : OLED_ChineseCodepoints);
    uint16_t count = mode==2U ? OLED_MEDIUM_COUNT : (mode ? OLED_SMALL_COUNT : OLED_CHINESE_COUNT);
#else
    const uint16_t *codes=OLED_MediumCodepoints;
    uint16_t count=OLED_MEDIUM_COUNT;
    if(mode!=2U)return NULL;
#endif
    uint16_t low = 0U, high = count;
    while (low < high) {
        uint16_t mid = (uint16_t)(low + (high - low) / 2U);
        if (codes[mid] < cp) low = (uint16_t)(mid + 1U); else high = mid;
    }
    if (low >= count || codes[low] != cp) return NULL;
#if OLED_ENABLE_LEGACY_FONTS
    return mode==2U ? OLED_MediumData[low] : (mode ? OLED_SmallData[low] : OLED_ChineseData[low]);
#else
    return OLED_MediumData[low];
#endif
}
uint8_t OLED_HasGlyph(uint32_t cp)
{ return (uint8_t)((cp >= 32U && cp <= 126U) || chinese(cp,0U) != NULL || chinese(cp,1U) != NULL || chinese(cp,2U) != NULL); }
uint16_t OLED_TextWidthUTF8(const char *text, uint16_t length, uint8_t font)
{
    uint16_t offset = 0U;
    uint32_t cp, width = 0U;
    uint8_t ascii_width = font == 8U ? 8U : 6U;
    while (offset < length && text != NULL && text[offset]) {
        cp = OLED_UTF8Next(text, length, &offset);
        width += cp >= 32U && cp <= 126U ? ascii_width : (font == OLED_UTF8_12X12 ? 12U : (font == OLED_UTF8_SMALL ? 8U : 16U));
        if (width > 65535U) return 65535U;
    }
    return (uint16_t)width;
}
#ifndef OLED_TEXT_LAYOUT_ONLY
static void blit(int16_t x, int16_t y, uint8_t width, uint8_t height, const uint8_t *data)
{
    int16_t col, first, last, page;
    /* Compose whole columns with explicit masks. Handles negative origins,
       clipped final pages and arbitrary Y without touching adjacent rows. */
    if(y>=64 || (int32_t)y+height<=0 || x>=128 || (int32_t)x+width<=0)return;
    first=y<0?0:(int16_t)(y/8);
    last=(int16_t)(((int32_t)y+height-1)/8);if(last>7)last=7;
    for(col=0;col<width;col++) {
        uint32_t bits=0U;uint8_t src;
        if(x+col<0 || x+col>=128)continue;
        for(src=0U;src<(height+7U)/8U;src++)bits|=(uint32_t)data[src*width+col]<<(src*8U);
        bits&=(1UL<<height)-1UL;
        for(page=first;page<=last;page++) {
            int16_t shift=(int16_t)(page*8-y);
            uint32_t mask=(1UL<<height)-1UL, value=bits;
            if(shift>=0){mask>>=shift;value>>=shift;}
            else {mask<<=-shift;value<<=-shift;}
            OLED_DisplayBuf[page][x+col]=(uint8_t)((OLED_DisplayBuf[page][x+col]&~mask)|(value&mask));
        }
    }
}
void OLED_ShowUTF8(int16_t x, int16_t y, const char *text, uint16_t length, uint8_t font)
{
    static const uint8_t missing[32] = {
        0xFF,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0xFF,
        0xFF,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0xFF};
#if OLED_ENABLE_LEGACY_FONTS
    static const uint8_t missing_small[8] = {0x7F,0x41,0x41,0x41,0x41,0x41,0x7F,0};
#endif
    static const uint8_t missing_medium[24]={255,1,1,1,1,1,1,1,1,1,1,255,15,8,8,8,8,8,8,8,8,8,8,15};
    uint16_t offset = 0U;
    int32_t cursor = x;
#ifdef OLED_TEXT_TEST_TIMING
    extern void OLED_TestDrawCost(uint16_t bytes);
    OLED_TestDrawCost(length);
#endif
#if !OLED_ENABLE_LEGACY_FONTS
    if(font!=6U && font!=OLED_UTF8_12X12)return;
#endif
    while (text != NULL && offset < length && text[offset]) {
        uint32_t cp = OLED_UTF8Next(text, length, &offset);
        const uint8_t *data;
        uint8_t width = 16U, height = 16U;
        int32_t top=y;
        if (cp >= 32U && cp <= 126U) {
            width = font == 8U ? 8U : 6U;
            if (font == 6U || font == OLED_UTF8_SMALL || font == OLED_UTF8_12X12) { height = 8U; data = OLED_F6x8[cp - 32U]; }
#if OLED_ENABLE_LEGACY_FONTS
            else data = font == 8U ? OLED_F8x16[cp - 32U] : OLED_F6x16[cp - 32U];
#else
            else data=OLED_F6x8[cp-32U];
#endif
            if(font==OLED_UTF8_12X12)top=(int32_t)y+2;
        } else {
            uint8_t small = (uint8_t)(font == OLED_UTF8_SMALL);
            if (small) { width = 8U; height = 8U; }
            if(font==OLED_UTF8_12X12){width=12U;height=12U;small=2U;}
            data = chinese(cp,small);
#if OLED_ENABLE_LEGACY_FONTS
            if (data == NULL) data = small==2U ? missing_medium : (small ? missing_small : missing);
#else
            if(data==NULL)data=small==2U ? missing_medium : missing;
#endif
        }
        if (cursor >= 128) break;
        if (cursor + width > 0 && top < 64 && (int32_t)top + height > 0) blit((int16_t)cursor, (int16_t)top, width, height, data);
        cursor += width;
    }
}

#endif
