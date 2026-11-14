#ifndef ENGLISH_REFERENCE_H
#define ENGLISH_REFERENCE_H
#include "remote_status_model.h"
typedef struct {
    char lines[8][22];
    uint8_t region_count,truncated,quality_any,quality_old,radio_live,invert;
    struct {uint8_t x,y,width,height;} regions[4];
} english_reference_output_t;
void english_reference(const remote_status_model_t *model,const void *debug,uint8_t page,
                       uint32_t now,uint8_t ready,uint8_t capable,english_reference_output_t *out);
#endif
