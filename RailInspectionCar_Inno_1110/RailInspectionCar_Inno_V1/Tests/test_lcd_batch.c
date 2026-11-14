#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lcd.h"
static unsigned calls, bytes;
static unsigned char wire[2048];
void lcd_write_halfword(lcd_io *io, uint16_t data) { (void)io; (void)data; }
void lcd_io_rst(lcd_io *io, bool flag) { (void)io; (void)flag; }
void lcd_io_bl(lcd_io *io, bool flag) { (void)io; (void)flag; }
void lcd_delay(uint32_t ms) { (void)ms; }
void lcd_port_set_init_stage(lcd_init_stage_t stage) { (void)stage; }
void lcd_write_byte(lcd_io *io, uint8_t data) { (void)io; (void)data; }
void lcd_write_reg(lcd_io *io, uint8_t data) { (void)io; (void)data; }
void lcd_write_bulk(lcd_io *io, uint8_t *data, uint32_t len)
{ (void)io; assert(len <= 640U && bytes + len <= sizeof(wire)); memcpy(wire + bytes, data, len); bytes += len; ++calls; }
int main(void)
{
    struct { unsigned pre; uint16_t buffer[320]; unsigned post; } guarded;
    lcd d; lcd_hw hw; unsigned font, chr, idx, bit, n, width;
    memset(&d, 0, sizeof(d)); memset(&hw, 0, sizeof(hw)); hw.width = 320; hw.height = 172;
    d.hw = &hw; d.line_buffer = guarded.buffer; guarded.pre = 0x12345678U; guarded.post = 0xABCDEF01U;
    for (font = 0; font < 4U; ++font) for (chr = 32; chr < 127U; ++chr)
    {
        lcd_set_font(&d, (font_type)font, 0x1234U, 0xABCDU); calls = bytes = 0;
        lcd_show_char(&d, 0, 0, (uint16_t)chr); n = width = 0;
        for (idx = 0; idx < d.font.bytes; ++idx)
            for (bit = 0; bit < 8U; ++bit)
            {
                unsigned v = d.font.addr[(chr - 32U) * d.font.bytes + idx] & (1U << bit);
                assert(wire[n++] == (v ? 0x12U : 0xABU)); assert(wire[n++] == (v ? 0x34U : 0xCDU));
                if (++width == d.font.width) { width = 0; break; }
            }
        assert(n == bytes && calls <= 4U && guarded.pre == 0x12345678U && guarded.post == 0xABCDEF01U);
        bytes = calls = 0; lcd_show_char(&d, 319U, 171U, (uint16_t)chr); assert(!bytes && !calls);
    }
    puts("PASS LCD 380 glyphs: identical pixel order/color bytes, <=640-byte batches, buffer guards and clipping");
    return 0;
}
