#ifndef OLED_CHINESE_FONT_H
#define OLED_CHINESE_FONT_H
#include <stdint.h>
#include "OLED_FontConfig.h"
#define OLED_CHINESE_COUNT 196U
extern const uint16_t OLED_ChineseCodepoints[OLED_CHINESE_COUNT];
extern const uint8_t OLED_ChineseData[OLED_CHINESE_COUNT][32];
extern const uint8_t OLED_F6x16[95][12];
#define OLED_SMALL_COUNT 184U
extern const uint16_t OLED_SmallCodepoints[OLED_SMALL_COUNT];
extern const uint8_t OLED_SmallData[OLED_SMALL_COUNT][8];
#define OLED_MEDIUM_COUNT 182U
extern const uint16_t OLED_MediumCodepoints[OLED_MEDIUM_COUNT];
extern const uint8_t OLED_MediumData[OLED_MEDIUM_COUNT][24];
#endif
