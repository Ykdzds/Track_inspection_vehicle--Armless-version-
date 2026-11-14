#include "app_arm3.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "link_diag_capture.h"

static app_arm3_platform_t platform;
static app_arm3_snapshot_t snapshot;
static can_protocol_record_t record;
static uint32_t now,lock_acquire,lock_release,vehicle_stops,input_seq,forced_locks;
static uint32_t config_crc[2];
static bool allow_lock;
static uint8_t proof_state=2U;
static bool work_lock(void *context,bool acquire)
{
    (void)context;
    if(acquire){++lock_acquire;return allow_lock;}
    ++lock_release;return true;
}
static void transport_lock(void *context){(void)context;++forced_locks;}
static void vehicle_stop(void *context){(void)context;++vehicle_stops;}
static void fresh(uint32_t tick)
{
    now=tick;platform.sample_tick_ms=now;platform.sample_age_ms=0U;
    app_arm3_set_platform(&platform);
}
static void run(uint32_t tick){fresh(tick);app_arm3_service(now);app_arm3_snapshot(&snapshot,now);}
static bool take(uint8_t type,uint8_t opcode,can_protocol_record_t *out)
{
    can_protocol_record_t item;uint8_t priority;uint32_t built;bool found=false;
    while(app_arm3_take_record(&item,&priority,&built)){
        if(item.type==0x53U)app_arm3_record_transmitted(&item,now);
        if(item.type==type && (opcode==255U || (item.type==0x5EU?item.payload[8]:item.payload[9])==opcode)){
            if(out)*out=item;found=true;
        }
    }
    return found;
}
static void receive(can_protocol_record_t *r)
{assert(app_arm3_receive(r,now,now,now));}
static void upper_features(void)
{
    memset(&record,0,sizeof(record));record.type=0x5CU;record.length=32U;
    can_protocol_write_u32(record.payload,CAN_ARM3_FEATURE_SCHEMA);
    can_protocol_write_u32(record.payload+4U,2U);can_protocol_write_u32(record.payload+8U,0x3DU);
    can_protocol_write_u32(record.payload+12U,12U);can_protocol_write_u32(record.payload+16U,7U);
    can_protocol_write_u16(record.payload+20U,1U);can_protocol_write_u16(record.payload+22U,5U);
    can_protocol_write_u16(record.payload+24U,300U);receive(&record);
}
static void upper_summary(void)
{
    unsigned side;
    for(side=1U;side<=2U;++side){memset(&record,0,sizeof(record));record.type=0x47U;record.length=32U;
        record.payload[0]=(uint8_t)side;record.payload[1]=0U;can_protocol_write_u16(record.payload+2U,0x8BU);
        can_protocol_write_u32(record.payload+8U,1U);can_protocol_write_u32(record.payload+12U,0U);
        can_protocol_write_u32(record.payload+20U,12U);can_protocol_write_u16(record.payload+30U,6U);receive(&record);}
}
static void respond_query(const can_protocol_record_t *q)
{
    uint8_t side=q->payload[10]==1U?1U:2U,op=q->payload[8];unsigned i;
    uint32_t query=can_protocol_read_u32(q->payload+4U);
    if(op==5U){
        can_arm3_header(&record,3U,0U,query,0U,side,1U,12U,1U,0U,0U);
        record.payload[32]=side;record.payload[33]=6U;record.payload[34]=3U;record.payload[35]=4U;
        can_protocol_write_u32(record.payload+36U,63U);
        for(i=0;i<6U;++i){can_protocol_write_u32(record.payload+40U+i*4U,(uint32_t)-30000);
            can_protocol_write_u32(record.payload+64U+i*4U,30000U);}
        can_protocol_write_u16(record.payload+112U,100U);can_protocol_write_u16(record.payload+114U,200U);
        can_protocol_write_u16(record.payload+116U,150U);record.payload[118]=3U;record.payload[119]=7U;
        can_protocol_write_u32(record.payload+120U,600U);can_protocol_write_u32(record.payload+124U,20U);
        can_protocol_write_u16(record.payload+128U,30U);record.payload[130]=3U;
        can_protocol_write_u16(record.payload+132U,100U);can_protocol_write_u16(record.payload+134U,150U);
        can_protocol_write_u32(record.payload+136U,1000U+side);
        config_crc[side-1U]=can_protocol_crc32(record.payload+32U,108U);
        can_protocol_write_u32(record.payload+140U,config_crc[side-1U]);receive(&record);
    }else if(op==6U){
        can_arm3_header(&record,4U,0U,query,0U,side,1U,12U,1U,0U,0U);
        record.payload[32]=q->payload[11];record.payload[33]=7U;
        can_protocol_write_u32(record.payload+36U,10U+q->payload[11]);receive(&record);
    }else if(op==11U){
        can_arm3_header(&record,8U,0U,query,0U,side,snapshot.generation,12U,5U,0U,1U);
        can_protocol_write_u32(record.payload+32U,can_protocol_read_u32(q->payload+16U));
        can_protocol_write_u32(record.payload+36U,can_protocol_read_u32(q->payload+24U));
        can_protocol_write_u32(record.payload+40U,snapshot.generation);
        can_protocol_write_u32(record.payload+44U,7U);can_protocol_write_u32(record.payload+48U,1U);
        can_protocol_write_u32(record.payload+52U,1000U+side);can_protocol_write_u32(record.payload+56U,config_crc[side-1U]);
        can_protocol_write_u32(record.payload+60U,13U);can_protocol_write_u32(record.payload+64U,1U);
        can_protocol_write_u32(record.payload+68U,8U);can_protocol_write_u16(record.payload+72U,600U);
        can_protocol_write_u16(record.payload+74U,7U);can_protocol_write_u16(record.payload+76U,100U);
        can_protocol_write_u16(record.payload+78U,0U);can_protocol_write_u32(record.payload+116U,0U);
        can_protocol_write_u32(record.payload+120U,1U);
        can_protocol_write_u32(record.payload+132U,can_protocol_read_u32(q->payload+16U)?
            (proof_state==2U?0x3FFU:0x3F3U):(proof_state==2U?0x7CFU:0x7C3U));
        record.payload[136]=3U;record.payload[137]=proof_state;can_protocol_write_u16(record.payload+138U,63U);
        receive(&record);
    }else assert(0);
}
static void configure(void)
{
    app_arm3_callbacks_t cb={NULL,work_lock,vehicle_stop,transport_lock};can_protocol_record_t q;unsigned i;
    now=1000U;input_seq=0U;allow_lock=true;lock_acquire=lock_release=vehicle_stops=forced_locks=0U;
    memset(&platform,0,sizeof(platform));platform.profile_enabled=true;platform.safety_config_approved=true;
    platform.chassis_idle=platform.no_pending_writes=platform.clock_valid=true;
    platform.safety_version=7U;platform.hardware_bitmap=63U;platform.safety_flags=0x39U;
    platform.chassis_zero_stable_ms=500U;platform.config_flags=15U;platform.mapped_input_mask=63U;
    platform.wiring_crc=1U;platform.policy_crc=2U;platform.chassis_zero_threshold_mm_s=1U;
    app_arm3_init(&cb);fresh(now);app_arm3_link(100U,200U,123U,true,
        (1UL<<20U)|(1UL<<25U)|(1UL<<28U)|(1UL<<31U),now);
    run(now);assert(take(0x5FU,255U,NULL));
    can_arm3_manifest(&record,2U);receive(&record);upper_features();upper_summary();
    for(i=0;i<10U;++i){run(now+1U);assert(take(0x5EU,255U,&q));respond_query(&q);}
    run(now+1U);app_arm3_snapshot(&snapshot,now);assert(snapshot.negotiated&&snapshot.config_match);
    for(i=0;i<2U;++i){assert(take(0x5EU,11U,&q));respond_query(&q);run(now+1U);}
    assert(!snapshot.work_locked);
    (void)take(0xFFU,255U,NULL);
}
static app_arm3_intent_t intent(uint8_t source,uint8_t cmd,uint8_t mask)
{
    app_arm3_intent_t in;memset(&in,0,sizeof(in));in.source=source;in.command=cmd;
    in.mask=mask;in.session=source*100U;in.sequence=++input_seq;in.page_active=true;in.expected_version=12U;
    return in;
}
static uint32_t apply(uint8_t source)
{
    app_arm3_intent_t in=intent(source,APP_ARM3_APPLY,3U);uint32_t op;
    assert(app_arm3_submit(&in,now,&op)==0U);run(now+1U);assert(snapshot.owner==source&&snapshot.permit_mask==3U);
    (void)take(0xFFU,255U,NULL);return snapshot.generation;
}
static void result(uint32_t operation,uint32_t gen,uint8_t mask,uint8_t phase,uint8_t result,uint32_t seq)
{
    memset(&record,0,sizeof(record));record.type=0x55U;record.length=32U;
    can_protocol_write_u32(record.payload,operation);can_protocol_write_u32(record.payload+4U,gen);
    record.payload[8]=mask;record.payload[9]=phase;record.payload[10]=result;
    record.payload[11]=phase==2U?3U:0U;can_protocol_write_u16(record.payload+16U,result==1U?100U:65535U);
    can_protocol_write_u32(record.payload+20U,0U);can_protocol_write_u32(record.payload+24U,12U);
    can_protocol_write_u32(record.payload+28U,seq);receive(&record);
}
static void test_manifest_codec(void)
{
    can_protocol_record_t r;can_arm3_manifest(&r,2U);assert(can_arm3_manifest_matches(&r,2U));
    r.payload[52]=0xD0U;assert(!can_arm3_manifest_matches(&r,2U));
    assert(can_arm3_age(10U,100U,130U,140U)==70U);
    assert(can_arm3_age(UINT32_MAX,100U,100U,100U)==UINT32_MAX);
    assert(can_arm3_age(10U,0xFFFFFFF0U,10U,20U)==66U);
}
static void test_priority_and_motion(void)
{
    app_arm3_intent_t remote,hmi,jog;uint32_t op,gen;can_protocol_record_t tx;
    configure();remote=intent(1U,APP_ARM3_APPLY,3U);hmi=intent(3U,APP_ARM3_APPLY,3U);
    assert(app_arm3_submit(&hmi,now,&op)==0U);assert(app_arm3_submit(&remote,now,&op)==0U);
    run(now+1U);assert(snapshot.owner==1U);gen=snapshot.generation;
    jog=intent(1U,APP_ARM3_JOG,1U);jog.joint=2U;jog.direction=1;jog.grade=2U;jog.held=true;jog.budget_ms=30000U;
    assert(app_arm3_submit(&jog,now,&op)==0U&&op);assert(take(0x56U,4U,&tx));
    assert(tx.length==64U&&tx.payload[10]==2U&&can_protocol_read_u32(tx.payload+16U)==2U);
    result(op,gen,1U,0U,0U,1U);
    {app_arm3_intent_t alive=jog;alive.command=APP_ARM3_ALIVE;alive.sequence=++input_seq;
        fresh(now+100U);assert(app_arm3_submit(&alive,now,NULL)==0U);run(now);assert(take(0x56U,4U,&tx));
        assert(can_protocol_read_u32(tx.payload)==op&&can_protocol_read_u32(tx.payload+32U)==2U);
        /* Replay cannot refresh the physical held-input age. */
        run(now+301U);assert(snapshot.owner==0U&&snapshot.work_locked);assert(take(0x54U,3U,&tx));}
    /* The screen never steals an existing owner's arm. */
    assert(app_arm3_submit(&hmi,now,NULL)==5U); /* original denied intent */
}
static void test_requests_and_replay(void)
{
    app_arm3_intent_t in;uint32_t op,gen;can_protocol_record_t tx;
    configure();gen=apply(3U);
    in=intent(3U,APP_ARM3_PRESET,3U);in.preset=3U;in.budget_ms=10000U;
    assert(app_arm3_submit(&in,now,&op)==0U);assert(take(0x54U,1U,&tx));
    assert(tx.payload[8]==3U&&can_protocol_read_u32(tx.payload+12U)==3U);
    assert(app_arm3_submit(&in,now,NULL)==0U);assert(!take(0x54U,1U,NULL));
    in.preset=2U;assert(app_arm3_submit(&in,now,NULL)==14U);
    result(op,gen,3U,2U,1U,2U);
    in=intent(3U,APP_ARM3_SIX_AXIS,1U);in.budget_ms=30000U;in.angle[2]=5000;
    assert(app_arm3_submit(&in,now,&op)==0U);assert(take(0x56U,6U,&tx));
    assert(can_protocol_read_u32(tx.payload+48U)==5000U);result(op,gen,1U,2U,1U,1U);
    in=intent(3U,APP_ARM3_SAVE_TARGET,2U);in.preset=2U;in.angle[5]=18000;
    assert(app_arm3_submit(&in,now,&op)==0U);assert(take(0x57U,8U,&tx));
    assert(can_protocol_read_u32(tx.payload+60U)==18000U);
    in=intent(3U,APP_ARM3_LIGHT_ON,3U);in.light_level=128U;
    assert(app_arm3_submit(&in,now,&op)==0U);assert(take(0x56U,5U,&tx));assert(tx.payload[38]==128U&&tx.payload[39]==1U);
}
static void test_screen_bounded_and_stop_bypass(void)
{
    app_arm3_intent_t in,alive,stop;uint32_t op;can_protocol_record_t tx;
    configure();(void)apply(3U);
    in=intent(3U,APP_ARM3_JOG,2U);in.joint=5U;in.grade=1U;in.direction=-1;in.held=true;in.budget_ms=300U;
    assert(!app_arm3_submit(&in,now,&op));assert(take(0x56U,4U,&tx));
    alive=intent(3U,APP_ARM3_ALIVE,2U);alive.held=true;
    fresh(now+100U);assert(!app_arm3_submit(&alive,now,NULL));run(now);
    assert(!take(0x56U,4U,NULL));
    fresh(now+200U);alive.sequence=++input_seq;assert(!app_arm3_submit(&alive,now,NULL));run(now);
    assert(take(0x54U,3U,&tx)); /* bounded deadline, not extended by alive */
    platform.hardware_bitmap=0U;platform.safety_config_approved=false;platform.clock_valid=false;fresh(now);
    stop=intent(1U,APP_ARM3_STOP,3U);stop.page_active=false;stop.expected_version=0U;
    assert(!app_arm3_submit(&stop,now,&op));assert(take(0x54U,3U,&tx));
    assert(can_protocol_read_u32(tx.payload)==op);
    assert(!app_arm3_submit(&stop,now,NULL));assert(!take(0x54U,3U,NULL));
}
static void test_proof_validation(void)
{
    can_arm3_config_t c;can_arm3_preset_t preset;can_arm3_proof_t proof;unsigned i;
    memset(&c,0,sizeof(c));memset(&preset,0,sizeof(preset));memset(&proof,0,sizeof(proof));
    c.valid=true;c.identity_crc=1U;c.payload_crc=2U;preset.present=true;preset.flags=7U;preset.revision=3U;
    for(i=0;i<6U;++i){c.lower[i]=-10000;c.upper[i]=10000;}
    proof.valid=true;proof.state=2U;proof.id=1U;proof.usb_epoch=1U;proof.generation=4U;
    proof.safety_version=5U;proof.identity_crc=1U;proof.config_crc=2U;proof.preset_revision=3U;
    proof.first_sequence=1U;proof.last_sequence=8U;proof.window_ms=600U;proof.samples=7U;
    proof.max_interval_ms=100U;proof.angle_mask=63U;proof.flags=0x7CFU;proof.received_ms=1000U;
    assert(can_arm3_proof_matches(&proof,&c,&preset,4U,5U,0U,0U,1100U));
    proof.range[1]=31U;assert(!can_arm3_proof_matches(&proof,&c,&preset,4U,5U,0U,0U,1100U));
    proof.range[1]=30U;proof.last_sequence=1U;
    assert(!can_arm3_proof_matches(&proof,&c,&preset,4U,5U,0U,0U,1100U));
    proof.last_sequence=8U;assert(!can_arm3_proof_matches(&proof,&c,&preset,4U,5U,0U,0U,1201U));
    proof.operation=99U;proof.operation_generation=3U;proof.flags=0x3FFU;
    assert(can_arm3_proof_matches(&proof,&c,&preset,4U,5U,99U,3U,1000U));
    assert(!can_arm3_proof_matches(&proof,&c,&preset,4U,5U,99U,4U,1000U));
}
static void test_release_and_transport_unlock(void)
{
    app_arm3_intent_t in;can_protocol_record_t tx,q;uint32_t op,gen;unsigned i;
    configure();(void)apply(1U);
    in=intent(1U,APP_ARM3_RELEASE,3U);assert(!app_arm3_submit(&in,now,&op));
    assert(op&&take(0x54U,3U,&tx));gen=can_protocol_read_u32(tx.payload+4U);
    result(op,gen,3U,2U,1U,1U);run(now+1U);
    assert(snapshot.owner==0U&&snapshot.work_locked);
    for(i=0;i<2U;++i){assert(take(0x5EU,11U,&q));respond_query(&q);run(now+1U);}
    assert(!snapshot.work_locked&&snapshot.state==0U&&lock_release==2U);
    /* The same old release is only a cached outcome; no USB stop is reissued. */
    assert(!app_arm3_submit(&in,now,NULL));assert(!take(0x54U,3U,NULL));
}
static void test_startup_static_unlock_and_interlock(void)
{
    can_protocol_record_t q;app_arm3_intent_t in;uint32_t op;unsigned i;
    configure();run(now+1U);(void)q;(void)i;
    assert(!snapshot.work_locked&&snapshot.state==0U);
    platform.parameter_locked=true;fresh(now);
    in=intent(1U,APP_ARM3_APPLY,3U);assert(app_arm3_submit(&in,now,&op)==4U);
    platform.parameter_locked=false;platform.hardware_bitmap=0U;fresh(now);
    in.sequence=++input_seq;assert(app_arm3_submit(&in,now,&op)==4U);
    /* A false local lock callback cannot grant ownership. */
    platform.hardware_bitmap=63U;fresh(now);allow_lock=false;in.sequence=++input_seq;
    assert(!app_arm3_submit(&in,now,&op));run(now+1U);assert(!snapshot.owner);
}
static void test_stop_dedicated_queue(void)
{
    app_arm3_intent_t in;uint32_t op;unsigned i;can_protocol_record_t tx;
    configure();(void)apply(1U);
    for(i=0;i<12U;++i){in=intent(1U,APP_ARM3_LIGHT_ON,3U);in.light_level=128U;
        (void)app_arm3_submit(&in,now,&op);}
    in=intent(3U,APP_ARM3_STOP,3U);assert(!app_arm3_submit(&in,now,&op));
    {uint8_t priority;uint32_t built;assert(app_arm3_take_urgent(&tx,&priority,&built));
        assert(priority==0U&&tx.type==0x54U&&tx.payload[9]==3U&&can_protocol_read_u32(tx.payload)==op);}
}
static void test_pc_same_cycle_and_action_order(void)
{
    app_arm3_intent_t in;can_protocol_record_t q,tx;uint32_t a,b;uint8_t priority;uint32_t built;
    configure();app_arm3_snapshot(&snapshot,now);
    can_arm3_query(&q,1U,1U,4U,3U,0U,snapshot.generation,0U,12U,1000U,0U,now,snapshot.safety_sequence);
    receive(&q);in=intent(1U,APP_ARM3_APPLY,3U);assert(!app_arm3_submit(&in,now,NULL));
    run(now+1U);assert(snapshot.owner==1U);assert(take(0x5FU,255U,&tx));
    receive(&q);assert(take(0x5FU,255U,&tx)&&tx.payload[9]==4U); /* cached PC conflict */
    in=intent(1U,APP_ARM3_PRESET,1U);in.preset=1U;assert(!app_arm3_submit(&in,now,&a));
    in=intent(1U,APP_ARM3_JOG,2U);in.joint=0U;in.direction=1;in.grade=1U;in.held=true;in.budget_ms=30000U;
    assert(!app_arm3_submit(&in,now,&b)&&b>a);
    assert(app_arm3_take_record(&tx,&priority,&built)&&can_protocol_read_u32(tx.payload)==a);
    assert(app_arm3_take_record(&tx,&priority,&built)&&can_protocol_read_u32(tx.payload)==b);
    /* Dedicated stop slots preserve the operation high water across sides. */
    in=intent(3U,APP_ARM3_STOP,2U);assert(!app_arm3_submit(&in,now,&a));
    in=intent(3U,APP_ARM3_STOP,1U);assert(!app_arm3_submit(&in,now,&b));
    assert(app_arm3_take_record(&tx,&priority,&built)&&can_protocol_read_u32(tx.payload)==a);
    assert(app_arm3_take_record(&tx,&priority,&built)&&can_protocol_read_u32(tx.payload)==b);
    configure();app_arm3_snapshot(&snapshot,now);
    can_arm3_query(&q,1U,1U,4U,3U,0U,snapshot.generation,0U,12U,1000U,0U,now,snapshot.safety_sequence);
    receive(&q);run(now+1U);assert(snapshot.owner==4U&&snapshot.permit_mask==3U);
}
static void test_false_success_and_reconnect(void)
{
    app_arm3_intent_t in;uint32_t op,gen;
    configure();gen=apply(1U);in=intent(1U,APP_ARM3_PRESET,1U);in.preset=1U;
    assert(!app_arm3_submit(&in,now,&op));(void)take(0xFFU,255U,NULL);
    memset(&record,0,sizeof(record));record.type=0x55U;record.length=32U;
    can_protocol_write_u32(record.payload,op);can_protocol_write_u32(record.payload+4U,gen);
    record.payload[8]=1U;record.payload[9]=2U;record.payload[10]=1U;record.payload[11]=1U;
    can_protocol_write_u32(record.payload+24U,12U);can_protocol_write_u32(record.payload+28U,1U);
    assert(!app_arm3_receive(&record,now,now,now));app_arm3_snapshot(&snapshot,now);
    assert(!snapshot.owner&&snapshot.work_locked&&snapshot.state==3U);
    configure();(void)apply(1U);
    memset(&record,0,sizeof(record));record.type=0x47U;record.length=32U;record.payload[0]=1U;record.payload[1]=255U;
    can_protocol_write_u32(record.payload+8U,2U);can_protocol_write_u32(record.payload+20U,12U);receive(&record);
    app_arm3_snapshot(&snapshot,now);assert(!snapshot.owner&&snapshot.work_locked);
    /* An old last-known generation is permitted for an actual stop result. */
    assert(take(0x54U,3U,&record));op=can_protocol_read_u32(record.payload);
    result(op,0U,3U,2U,1U,1U);run(now+1U);assert(snapshot.state!=4U);
}
static void test_replay_capacity_keeps_stop_open(void)
{
    app_arm3_intent_t in;uint32_t op;unsigned i;
    configure();(void)apply(1U);
    for(i=0;i<31U;++i){in=intent(1U,APP_ARM3_LIGHT_ON,3U);in.light_level=128U;
        assert(!app_arm3_submit(&in,now,&op));(void)take(0xFFU,255U,NULL);}
    in=intent(1U,APP_ARM3_LIGHT_ON,3U);in.light_level=128U;assert(app_arm3_submit(&in,now,&op)==5U);
    in=intent(3U,APP_ARM3_STOP,3U);assert(!app_arm3_submit(&in,now,&op)&&op);
    assert(take(0x54U,3U,NULL));
}
static void test_stopped_unfolded_can_acquire_for_recovery(void)
{
    can_protocol_record_t q;unsigned i;
    configure();platform.policy_crc=3U;proof_state=1U;run(now+1U);upper_features();
    for(i=0;i<2U;++i){assert(take(0x5EU,11U,&q));respond_query(&q);run(now+1U);}
    assert(snapshot.work_locked&&!snapshot.transport_mask);
    /* The forced transport lock is not an owner gate: recheck it atomically. */
    {app_arm3_intent_t in=intent(1U,APP_ARM3_APPLY,3U);uint32_t before=lock_acquire;
        allow_lock=false;assert(!app_arm3_submit(&in,now,NULL));run(now+1U);
        assert(!snapshot.owner && snapshot.work_locked && lock_acquire==before+1U);
        allow_lock=true;
    }
    (void)apply(1U);
    assert(snapshot.owner==1U&&snapshot.permit_mask==3U&&snapshot.work_locked);proof_state=2U;
}
static void test_ordinary_result_capacity_and_stop_reserve(void)
{
    app_arm3_intent_t in;can_arm3_result_t value;uint32_t op,gen,seq;unsigned i;
    configure();gen=apply(1U);
    for(i=0;i<32U;++i){fresh(now+101U);in=intent(1U,APP_ARM3_ALIVE,3U);assert(!app_arm3_submit(&in,now,NULL));
        in=intent(1U,APP_ARM3_LIGHT_ON,3U);in.light_level=128U;
        {unsigned side;for(side=1U;side<=2U;++side){memset(&record,0,sizeof(record));record.type=0x47U;record.length=32U;
            record.payload[0]=(uint8_t)side;can_protocol_write_u16(record.payload+2U,0x8BU);
            can_protocol_write_u32(record.payload+8U,i+2U);can_protocol_write_u32(record.payload+20U,12U);receive(&record);}}
        assert(!app_arm3_submit(&in,now,&op));(void)take(0xFFU,255U,NULL);result(op,gen,3U,2U,1U,1U);}
    in=intent(1U,APP_ARM3_LIGHT_ON,3U);in.light_level=128U;assert(app_arm3_submit(&in,now,NULL)==5U);
    in=intent(3U,APP_ARM3_STOP,3U);assert(!app_arm3_submit(&in,now,&op));(void)take(0xFFU,255U,NULL);
    result(op,0U,3U,2U,1U,1U);assert(app_arm3_get_result(op,0U,now,&value)&&value.result==1U);
    seq=op;in=intent(3U,APP_ARM3_STOP,3U);assert(!app_arm3_submit(&in,now,&op)&&op>seq);
    (void)take(0xFFU,255U,NULL);result(op,0U,3U,2U,1U,1U);
    assert(app_arm3_get_result(seq,0U,now,&value)&&value.result==1U);
}
static void test_force_transport_and_urgent_revoke(void)
{
    app_arm3_callbacks_t cb={NULL,work_lock,vehicle_stop,transport_lock};
    can_protocol_record_t tx;uint8_t priority;uint32_t built;bool revoke=false,stop=false;unsigned i;
    forced_locks=lock_acquire=0U;allow_lock=false;
    app_arm3_init(&cb);memset(&platform,0,sizeof(platform));platform.profile_enabled=true;
    platform.chassis_moving=true;fresh(100U);app_arm3_service(now);
    app_arm3_snapshot(&snapshot,now);
    assert(snapshot.work_locked && forced_locks && !lock_acquire && !snapshot.owner);
    configure();(void)apply(1U);platform.hardware_bitmap=0U;run(now+1U);
    for(i=0U;i<4U && app_arm3_take_urgent(&tx,&priority,&built);++i){
        assert(priority==0U);
        if(tx.type==0x53U){assert(!tx.payload[8]);revoke=true;}
        if(tx.type==0x54U && tx.payload[9]==3U)stop=true;
    }
    assert(revoke && stop);
    /* Repeated transport errors cannot reallocate an ongoing fault stop. */
    app_arm3_transport_fault(now+1U);app_arm3_snapshot(&snapshot,now+1U);
    {uint32_t op=snapshot.side[0].operation,gen=snapshot.generation;
     app_arm3_transport_fault(now+2U);app_arm3_snapshot(&snapshot,now+2U);
     assert(snapshot.side[0].operation==op && snapshot.generation==gen);}
    /* Normal/config records cannot leak through the urgent-only API. */
    assert(!app_arm3_take_urgent(&tx,&priority,&built));
}
static void summary_one(uint8_t side,uint32_t seq,uint16_t flags,uint8_t state)
{
    memset(&record,0,sizeof(record));record.type=0x47U;record.length=32U;
    record.payload[0]=side;record.payload[1]=state;
    can_protocol_write_u16(record.payload+2U,flags);
    can_protocol_write_u32(record.payload+8U,seq);
    can_protocol_write_u32(record.payload+20U,12U);receive(&record);
}
static void test_idle_transport_expiry_and_usb_sequence_reset(void)
{
    app_arm3_intent_t in;uint32_t before;
    configure();before=snapshot.generation;
    assert(!snapshot.work_locked && !platform.chassis_moving);
    run(now+1001U);
    assert(snapshot.work_locked && !snapshot.owner && snapshot.state==5U);
    assert(snapshot.generation!=before && !vehicle_stops && !snapshot.transport_mask);
    run(now+1U);assert(snapshot.work_locked); /* Old proofs cannot unlock again. */
    summary_one(1U,1U,0x8BU,0U); /* An omitted offline report cannot deadlock a restart. */

    configure();summary_one(1U,200U,0x8BU,0U);before=snapshot.generation;
    summary_one(1U,0U,0U,255U);app_arm3_snapshot(&snapshot,now);
    assert(snapshot.work_locked && !snapshot.side[0].online && !snapshot.side[0].angle_mask);
    assert(!snapshot.transport_mask && snapshot.generation!=before);
    /* A reconnect's low source sequence is accepted but grants no permission. */
    summary_one(1U,1U,0x8BU,0U);
    in=intent(1U,APP_ARM3_APPLY,3U);assert(app_arm3_submit(&in,now,NULL)==4U);
    run(now+1U);assert(snapshot.work_locked && !snapshot.owner && !snapshot.permit_mask);

    configure();(void)apply(1U);summary_one(1U,0U,0U,255U);
    app_arm3_snapshot(&snapshot,now);
    assert(!snapshot.owner && !snapshot.permit_mask && snapshot.work_locked);
}
static void test_delayed_input_keeps_receive_age(void)
{
 app_arm3_intent_t in;uint32_t op;
 configure();in=intent(1U,APP_ARM3_APPLY,3U);app_arm3_input_delay(1U);
 assert(!app_arm3_submit(&in,now,&op));app_arm3_input_delay(0U);run(now+1U);
 assert(snapshot.owner==1U && snapshot.permit_mask==3U && snapshot.source_age_ms==0U);
 in=intent(1U,APP_ARM3_ALIVE,3U);app_arm3_input_delay(200U);
 assert(!app_arm3_submit(&in,now,&op));app_arm3_input_delay(0U);run(now+100U);
 assert(!snapshot.owner && !snapshot.permit_mask); /* reception was 300ms ago */
}
#if APP_LINK_DIAG_ACTIVE && APP_CAN_ARM_DIAG_ENABLED
static void test_link_diagnostic_evidence(void)
{
    app_arm3_intent_t in;uint32_t op;
    configure();diag_capture_reset();
    in=intent(1U,APP_ARM3_APPLY,3U);in.session=0U;
    assert(app_arm3_submit(&in,now,&op)==6U && !op);
    assert(diag_capture_count[0x200U]==1U && diag_capture_first_kind[APP_LINK_CAN_ARM]==0x201U);
    assert(diag_capture_first[APP_LINK_CAN_ARM][1]==(6U | (1U<<8U)));
    in.session=1U;in.command=255U;assert(app_arm3_submit(&in,now,NULL)==6U);
    assert(diag_capture_first[APP_LINK_CAN_ARM][1]==(6U | (1U<<8U)));
    configure();(void)apply(1U);diag_capture_reset();
    in=intent(1U,APP_ARM3_ALIVE,3U);in.held=true;
    assert(!app_arm3_submit(&in,now,NULL));
    assert(diag_capture_last[1]==(1U | (2U<<8U) | (3U<<16U) | (1U<<24U) | (1U<<25U)));
    assert(diag_capture_last[5]==0U);
    in=intent(1U,APP_ARM3_JOG,1U);in.joint=2U;in.direction=1;in.grade=2U;in.held=true;in.budget_ms=30000U;
    assert(!app_arm3_submit(&in,now,&op));
    in.command=APP_ARM3_ALIVE;in.sequence=++input_seq;in.joint=3U;
    assert(!app_arm3_submit(&in,now,NULL));
    assert(diag_capture_last[5]==(20U<<8U)); /* Success must not hide an implicit stop. */
    configure();(void)apply(1U);diag_capture_reset();
    in=intent(1U,APP_ARM3_ALIVE,3U);in.page_active=false;
    assert(!app_arm3_submit(&in,now,NULL));app_arm3_snapshot(&snapshot,now);
    assert(!(diag_capture_last[1]&(1U<<25U)));
    assert(!snapshot.owner && diag_capture_first_kind[APP_LINK_CAN_ARM]==0x202U);
    assert((diag_capture_first[APP_LINK_CAN_ARM][5]&255U)==1U); /* Freeze before owner clearing. */
    puts("PASS ARM diagnostic: invalid input branch, immutable evidence, release captured before owner reset");
}
#endif
int main(void)
{
#if APP_LINK_DIAG_ACTIVE && APP_CAN_ARM_DIAG_ENABLED
    test_link_diagnostic_evidence();
#endif
    test_delayed_input_keeps_receive_age();
    test_idle_transport_expiry_and_usb_sequence_reset();
    test_force_transport_and_urgent_revoke();
    test_manifest_codec();test_priority_and_motion();test_requests_and_replay();
    test_screen_bounded_and_stop_bypass();test_proof_validation();
    test_release_and_transport_unlock();test_startup_static_unlock_and_interlock();
    test_stop_dedicated_queue();
    test_pc_same_cycle_and_action_order();test_false_success_and_reconnect();
    test_replay_capacity_keeps_stop_open();
    test_stopped_unfolded_can_acquire_for_recovery();
    test_ordinary_result_capacity_and_stop_reserve();
    puts("PASS ARM3 revision2 manifest, motion, replay, owner, bounded HMI, stop and transport evidence");return 0;
}
