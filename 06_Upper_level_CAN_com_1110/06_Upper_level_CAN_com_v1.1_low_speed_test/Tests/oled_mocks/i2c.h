#include "main.h"
extern int hi2c1;
#define I2C_MEMADD_SIZE_8BIT 1
int HAL_I2C_Mem_Write(int *, unsigned, unsigned, unsigned, uint8_t *, unsigned, unsigned);
