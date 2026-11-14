/* Historical implementation is a test oracle, never target firmware. */
#define remote_display_init reference_display_init
#define remote_display_keys reference_display_keys
#define remote_display_build reference_display_build
#include "english_baseline_3af92be/remote_display.c"
#include "english_reference.h"
void english_reference(const remote_status_model_t *model,const void *debug,uint8_t page,
                       uint32_t now,uint8_t ready,uint8_t capable,english_reference_output_t *out)
{
    remote_display_t display;remote_debug_view_t input;uint8_t i;
    memcpy(&input,debug,sizeof(input));
    reference_display_init(&display);display.requested_page=page;
    reference_display_build(&display,model,&input,now,ready,capable);
    memcpy(out->lines,display.lines,sizeof(out->lines));
    out->region_count=display.region_count;out->truncated=display.truncated;
    out->quality_any=display.quality_any;out->quality_old=display.quality_old;
    out->radio_live=display.radio_live;out->invert=display.invert;
    for(i=0U;i<out->region_count;i++) {
        out->regions[i].x=display.regions[i].x;out->regions[i].y=(uint8_t)(display.regions[i].row*8U);
        out->regions[i].width=display.regions[i].width;out->regions[i].height=8U;
    }
}
