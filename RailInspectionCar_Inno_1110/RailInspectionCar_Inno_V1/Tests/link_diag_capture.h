#ifndef TEST_LINK_DIAG_CAPTURE_H
#define TEST_LINK_DIAG_CAPTURE_H
#include "app_link_diag.h"
#include <assert.h>
#include <string.h>
#if APP_LINK_DIAG_ACTIVE
static unsigned diag_capture_count[0x400],diag_capture_total;
static uint32_t diag_capture_last[6],diag_capture_first[APP_LINK_CHANNELS][16];
static uint16_t diag_capture_first_kind[APP_LINK_CHANNELS];
static uint8_t diag_capture_first_count[APP_LINK_CHANNELS];
void app_link_diag_record(uint8_t source,uint16_t kind,uint32_t a,uint32_t b,
    uint32_t c,uint32_t d,uint32_t e,uint32_t f)
{
    assert(source<APP_LINK_CHANNELS && kind<0x400U);++diag_capture_count[kind];++diag_capture_total;
    diag_capture_last[0]=a;diag_capture_last[1]=b;diag_capture_last[2]=c;
    diag_capture_last[3]=d;diag_capture_last[4]=e;diag_capture_last[5]=f;
}
void app_link_diag_freeze(uint8_t source,uint16_t kind,const uint32_t *w,uint8_t n)
{
    assert(source<APP_LINK_CHANNELS && w && n && n<=16U);
    if(diag_capture_first_count[source])return;
    memcpy(diag_capture_first[source],w,n*sizeof(*w));diag_capture_first_count[source]=n;
    diag_capture_first_kind[source]=kind;
}
static inline void diag_capture_reset(void)
{
    memset(diag_capture_count,0,sizeof(diag_capture_count));diag_capture_total=0U;
    memset(diag_capture_first_count,0,sizeof(diag_capture_first_count));
}
#endif
#endif
