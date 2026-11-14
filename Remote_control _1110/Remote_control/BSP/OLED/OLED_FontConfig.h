#ifndef OLED_FONT_CONFIG_H
#define OLED_FONT_CONFIG_H
/* Product needs 6x8 ASCII and 12x12 Chinese only. Host compatibility tests
   enable archived modes explicitly; no legacy bitmap data enters firmware. */
#ifndef OLED_ENABLE_LEGACY_FONTS
#define OLED_ENABLE_LEGACY_FONTS 0
#endif
#endif
