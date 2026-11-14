#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app_sync_params.h"
static uint8_t bytes[0x40000],snapshot[0x40000];
static const uint32_t uid[3]={1,2,3};
static void start(void){app_sync_storage_test_attach(bytes,sizeof(bytes),uid);app_sync_storage_test_gate(1);app_sync_storage_test_fail_after(-1);app_sync_params_init();}
static void finish(void){unsigned i;for(i=0;i<40;i++)app_sync_storage_service();}
int main(void){app_sync_params_t a,b;app_sync_storage_status_t status;unsigned i;memset(bytes,255,sizeof(bytes));start();app_sync_params_read(&a);a.kp=.4f;
 assert(app_sync_storage_submit(&a,1,uid,9,10)==TUNE_PENDING);assert(app_sync_params_maintenance());app_sync_params_read(&b);assert(b.kp==.3f);finish();assert(!app_sync_params_maintenance());app_sync_storage_read(&status);assert(status.saved_sequence==1 && status.free_slots==2047 && status.transaction_request==10);start();app_sync_params_read(&b);assert(b.kp==.4f);memcpy(snapshot,bytes,sizeof(bytes));
 for(i=0;i<32;i++){memcpy(bytes,snapshot,sizeof(bytes));start();a.kp=.5f;assert(app_sync_storage_submit(&a,1,uid,9,20)==TUNE_PENDING);app_sync_storage_test_fail_after((int)i);finish();app_sync_storage_read(&status);assert(status.save_result==TUNE_FLASH && status.transaction_boot==9U && status.transaction_request==20U);app_sync_params_read(&b);assert(b.kp==.4f);start();app_sync_params_read(&b);assert(b.kp==.4f);}
 memcpy(bytes,snapshot,sizeof(bytes));start();a.kp=.4f;assert(app_sync_storage_submit(&a,1,uid,9,25)==TUNE_OK);app_sync_storage_read(&status);assert(status.free_slots==2047 && status.saved_sequence==1 && status.transaction_request==25);
 app_sync_storage_test_gate(0);a.kp=.6f;assert(app_sync_storage_submit(&a,2,uid,9,26)==TUNE_GATE);assert(!app_sync_params_maintenance());app_sync_storage_test_gate(1);assert(app_sync_storage_submit(&a,2,uid,9,26)==TUNE_PENDING);app_sync_storage_service();app_sync_storage_test_gate(0);finish();app_sync_params_read(&b);assert(b.kp==.4f);assert(!app_sync_params_maintenance());
 memcpy(bytes,snapshot,sizeof(bytes));start();for(i=128;i<sizeof(bytes);i+=128)memcpy(bytes+i,snapshot,128);start();app_sync_storage_read(&status);assert(status.free_slots==0);a.kp=.6f;assert(app_sync_storage_submit(&a,1,uid,9,27)==TUNE_FULL);
 memcpy(bytes,snapshot,sizeof(bytes));bytes[40]^=1;start();app_sync_storage_read(&status);assert(status.load_reason==2);app_sync_params_read(&b);assert(b.kp==.3f);
 memcpy(bytes,snapshot,sizeof(bytes));bytes[124]^=1;start();app_sync_storage_read(&status);assert(status.load_reason==2);
 memcpy(bytes,snapshot,sizeof(bytes));{const uint32_t other_uid[3]={9,8,7};app_sync_storage_test_attach(bytes,sizeof(bytes),other_uid);app_sync_params_init();app_sync_storage_read(&status);assert(status.load_reason==3);app_sync_params_read(&b);assert(b.kp==.3f);}
 memcpy(bytes,snapshot,sizeof(bytes));start();a.kp=.7f;{const uint32_t other_uid[3]={9,8,7};assert(app_sync_storage_submit(&a,1,other_uid,9,29)==TUNE_BOOT);}assert(app_sync_storage_submit(&a,2,uid,9,29)==TUNE_VERSION);
 assert(app_sync_storage_submit(&a,1,uid,9,29)==TUNE_PENDING);app_sync_storage_test_fail_after(2);finish();start();app_sync_storage_read(&status);assert(status.free_slots==2046);assert(app_sync_storage_submit(&a,1,uid,9,30)==TUNE_PENDING);finish();assert(bytes[256]!=255); /* append after the torn slot */
 memset(bytes,255,sizeof(bytes));bytes[0]=42;start();app_sync_storage_read(&status);assert(status.storage_state==5);assert(app_sync_storage_submit(&a,1,uid,9,28)==TUNE_STORAGE);
 memset(bytes,255,sizeof(bytes));start();app_sync_params_read(&a);a.s_mode=1;a.jerk=0;assert(!app_sync_params_valid(&a));a.jerk=.4f;assert(app_sync_params_submit(&a,1)==TUNE_PENDING);app_sync_params_service();app_sync_params_read(&b);assert(b.s_mode && b.jerk==.4f);
 puts("PASS storage: 32 word-failure positions, reboot restore, unchanged save, gate loss, full, CRC/commit corruption, UID/version mismatch, torn-slot skip, unknown region, S config");return 0;}
