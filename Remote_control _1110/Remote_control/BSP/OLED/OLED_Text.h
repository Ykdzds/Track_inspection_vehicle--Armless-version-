#ifndef OLED_TEXT_H
#define OLED_TEXT_H
#include <stdint.h>
#define OLED_ASCII_6X16 7U
#define OLED_UTF8_SMALL 9U
#define OLED_UTF8_12X12 10U
uint16_t OLED_TextWidthUTF8(const char *text, uint16_t text_bytes, uint8_t ascii_font);
void OLED_ShowUTF8(int16_t x, int16_t y, const char *text, uint16_t text_bytes, uint8_t ascii_font);
uint32_t OLED_UTF8Next(const char *text, uint16_t length, uint16_t *offset);
uint8_t OLED_HasGlyph(uint32_t codepoint);
#endif
