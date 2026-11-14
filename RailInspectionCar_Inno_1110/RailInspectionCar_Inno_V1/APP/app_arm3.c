#include "app_arm3.h"
#include "app_link_diag.h"
#include <string.h>

#define TX_COUNT 8U
#define RESULT_COUNT 120U /* 32 normal and 8 stop operations, aggregate+two sides */
#define INPUT_COUNT 32U
#define PEER_REQUIRED ((1UL<<20U)|(1UL<<25U)|(1UL<<28U)|(1UL<<31U))
#define DETAIL(n) (0x01000000UL|(n))
typedef struct { can_protocol_record_t record; uint32_t built; uint8_t priority; bool used; } tx_t;
typedef struct {
    uint32_t operation,generation,started,last_result,pulse,last_pulse;
    uint32_t source_session;
    uint16_t budget;
    uint8_t opcode,mask,joint,grade,retries;
    int8_t direction;
    bool active,stopping,held,accepted;
    can_protocol_record_t request;
} active_t;
typedef struct { uint32_t session,sequence,seen,operation,generation;uint8_t source,result;bool used,pending;uint8_t content[48]; } input_cache_t;
typedef struct {
    can_arm3_result_t value;
    uint32_t stored;
    bool stop;
} result_cache_t;
typedef struct {
    uint32_t session,sequence,last_ms;
    bool seen;
} source_t;
typedef struct {
    uint32_t id,at;
    uint8_t content[48],response[96],length;
    bool used,pending;
} pc_query_t;
typedef struct {
    app_arm3_callbacks_t callbacks;
    app_arm3_platform_t platform;
    uint32_t lower,upper,generation,peer_caps,peer_features,arm_version;
    uint32_t peer_safety_version,owner_seq,owner_at,owner_live,last_manifest,last_features;
    uint32_t last_owner,last_safety,safety_seq,next_operation,next_query,snapshot_seq;
    uint32_t safety_tx_seq[4],safety_tx_ms[4],safety_tx_cursor;
    uint32_t reason,rejected,queue_full,release_op,release_gen,release_at,platform_hash;
    uint32_t pending_apply_at,next_read_at,active_usb_epoch[2];
    uint16_t token;
    uint8_t owner,state,permit,stopped_mask,pending_apply_mask,display_cursor,proof_cursor;
    bool ready,negotiated,work_lock,ever_enabled,manifest_sent,features_seen;
    bool platform_hash_seen,release_done,profile_previous;
    tx_t tx[TX_COUNT], urgent[2],safety_tx;
    can_protocol_record_t scratch;
    source_t sources[3];
    input_cache_t inputs[INPUT_COUNT];
    result_cache_t results[RESULT_COUNT];
    active_t active[2];
    can_arm3_config_t config[2];
    can_arm3_preset_t preset[2][4];
    can_arm3_pose_t pose[2];
    can_arm3_proof_t proof[2];
    uint32_t proof_query[2],proof_query_at[2];
    uint32_t summary_at[2],summary_age[2],summary_seq[2],summary_usb[2];
    uint8_t summary_state[2];
    uint16_t summary_flags[2];
    struct {can_protocol_record_t record;uint32_t id,at;uint8_t opcode,mask,selector,retries,received_mask;bool active;} query;
    pc_query_t pc_query[32];
    uint32_t pc_highwater;
} arm3_t;
static arm3_t s;
static uint32_t input_delay_ms;
#if APP_LINK_DIAG_ACTIVE && APP_CAN_ARM_DIAG_ENABLED
static void arm_diag(uint16_t kind,uint32_t why,uint32_t operation,uint32_t sequence,uint32_t now,bool freeze)
{
    uint32_t w[16];
    w[0]=now;w[1]=why;w[2]=operation;w[3]=sequence;w[4]=s.generation;
    w[5]=s.owner | ((uint32_t)s.state<<8U) | ((uint32_t)s.permit<<16U) | ((uint32_t)s.work_lock<<24U);
    app_link_diag_record(APP_LINK_CAN_ARM,kind,w[0],w[1],w[2],w[3],w[4],w[5]);
    if(!freeze)return;
    w[6]=s.arm_version;w[7]=s.platform.safety_version;w[8]=s.token;w[9]=now-s.owner_live;
    w[10]=input_delay_ms;w[11]=s.active[0].operation;w[12]=s.active[1].operation;
    w[13]=(uint32_t)s.ready | ((uint32_t)s.negotiated<<1U) | ((uint32_t)s.platform.profile_enabled<<2U) |
        ((uint32_t)s.platform.safety_config_approved<<3U) | ((uint32_t)s.platform.parameter_locked<<4U) |
        ((uint32_t)s.platform.recovery_locked<<5U) | ((uint32_t)s.platform.chassis_moving<<6U);
    w[14]=s.lower;w[15]=s.upper;
    app_link_diag_freeze(APP_LINK_CAN_ARM,kind,w,16U);
}
static uint8_t arm_diag_input_result(const app_arm3_intent_t *in,uint32_t now,
    uint32_t op,uint8_t result,uint16_t site)
{
    uint32_t identity=in?in->source | ((uint32_t)in->command<<8U) | ((uint32_t)in->mask<<16U) |
        ((uint32_t)in->held<<24U) | ((uint32_t)in->page_active<<25U):0U;
    app_link_diag_record(APP_LINK_CAN_ARM,0x200U,now,identity,in?in->session:0U,
        in?in->sequence:0U,op,(uint32_t)result | ((uint32_t)site<<8U));
    if(result)arm_diag(0x201U,(uint32_t)result | ((uint32_t)site<<8U),op,in?in->sequence:0U,now,true);
    return result;
}
#else
#define arm_diag(kind,why,operation,sequence,now,freeze) ((void)0)
#define arm_diag_input_result(in,now,op,result,site) ((void)(site),(result))
#endif
void app_arm3_input_delay(uint32_t elapsed){input_delay_ms=elapsed;}

static uint32_t age_add(uint32_t age,uint32_t elapsed)
{return age==UINT32_MAX || elapsed>UINT32_MAX-age ? UINT32_MAX:age+elapsed;}
static int source_index(uint8_t source)
{return source==1U?0:source==3U?1:source==4U?2:-1;}
static uint32_t next_nonzero(uint32_t *value)
{if(*value==UINT32_MAX)return 0U;return ++*value;}
static void invalidate_proofs(void)
{s.proof[0].valid=s.proof[1].valid=false;s.stopped_mask=0U;}
static void generation_change(void)
{if(!next_nonzero(&s.generation)){s.ready=false;s.generation=UINT32_MAX;}invalidate_proofs();++s.owner_seq;}
static bool lock_work(void)
{
    /* Existing transport lock cannot substitute for this atomic owner gate. */
    if(!s.callbacks.work_lock || !s.callbacks.work_lock(s.callbacks.context,true))return false;
    s.work_lock=true;return true;
}
static void force_transport_lock(void)
{
    if(s.callbacks.transport_lock){
        s.callbacks.transport_lock(s.callbacks.context);s.work_lock=true;
    }else if(!s.work_lock){
        /* Compatibility for isolated hosts; target port supplies transport_lock. */
        if(s.callbacks.work_lock && s.callbacks.work_lock(s.callbacks.context,true))s.work_lock=true;
    }
}
static bool transport_summary_valid(uint32_t now)
{
    unsigned i;
    for(i=0U;i<2U;++i)if(!s.summary_seq[i] || s.summary_state[i]!=0U ||
        (s.summary_flags[i]&0x8BU)!=0x8BU || (s.summary_flags[i]&0x14U) ||
        (uint32_t)(now-s.summary_at[i])>1000U ||
        age_add(s.summary_age[i],now-s.summary_at[i])>1000U)return false;
    return true;
}
static void transport_unknown(uint32_t now)
{
    if(!s.work_lock){
        arm_diag(0x205U,s.reason,0U,0U,now,true);
        /* A new unknown interval must not reuse a proof from before it. */
        generation_change();s.state=5U;s.query.active=false;s.next_read_at=now;
    }
    force_transport_lock();
    if(s.platform.chassis_moving && s.callbacks.vehicle_stop)
        s.callbacks.vehicle_stop(s.callbacks.context);
}
static bool enqueue(const can_protocol_record_t *record,uint8_t priority,uint32_t now)
{
    unsigned i;
    if(record->type==0x53U){s.safety_tx.record=*record;s.safety_tx.built=now;
        s.safety_tx.priority=priority;s.safety_tx.used=true;return true;}
    for(i=0;i<TX_COUNT;++i)if(!s.tx[i].used){s.tx[i].record=*record;
        s.tx[i].built=now;s.tx[i].priority=priority;s.tx[i].used=true;return true;}
    ++s.queue_full;return false;
}
static void discard_motion_queue(uint32_t now)
{
    unsigned i,j;
    for(i=0;i<TX_COUNT;++i)if(s.tx[i].used &&
       (s.tx[i].record.type==0x54U || s.tx[i].record.type==0x56U || s.tx[i].record.type==0x57U)){
        uint32_t op=can_protocol_read_u32(s.tx[i].record.payload);
        /* An unsent lower ID cannot follow a new stop. Preserve its history
         * reservation for the replay horizon even if RK never saw the intent. */
        for(j=0;j<96U;++j)if(s.results[j].value.operation==op && !s.results[j].value.present){
            s.results[j].value.flags=1U;s.results[j].stored=now;}
        s.tx[i].used=false;
    }
}
static uint32_t allocate_operation(void)
{
    if(s.next_operation>=0x7FFFFFFFUL){s.reason=DETAIL(8U);return 0U;}
    return ++s.next_operation;
}
static bool normal_result_capacity(uint32_t now,uint32_t reserve)
{
    unsigned i,j,free_count=0U;uint32_t candidate[32];unsigned count=0U;
    for(i=0;i<96U;++i){bool active=false;
        for(j=0;j<2U;++j)if(s.active[j].active && s.active[j].operation==s.results[i].value.operation)active=true;
        if(!s.results[i].value.operation || (!active &&
            (s.results[i].value.flags&1U) && (uint32_t)(now-s.results[i].stored)>=60000U)){
            if(count<32U)candidate[count++]=i;++free_count;
        }
    }
    if(free_count<3U)return false;
    if(reserve)for(i=0;i<3U;++i){result_cache_t *item=&s.results[candidate[i]];
        memset(item,0,sizeof(*item));item->value.operation=reserve;item->value.arm_id=(uint8_t)i;
        item->value.generation=s.generation;item->stored=now;}
    return true;
}
static uint32_t feature_version(void)
{return s.config[0].version==s.config[1].version ? s.config[0].version:0U;}
static bool configurations_ready(void)
{
    return s.features_seen && s.arm_version && s.config[0].valid && s.config[1].valid &&
        feature_version()==s.arm_version && s.peer_safety_version==s.platform.safety_version &&
        s.platform.safety_version && s.config[0].identity_crc!=s.config[1].identity_crc;
}
static bool arm_feedback_ready(uint32_t now)
{
    unsigned i;
    for(i=0;i<2U;++i)if(!s.summary_seq[i] || !s.active_usb_epoch[i] || (s.summary_flags[i]&3U)!=3U ||
        (s.summary_flags[i]&16U) || (uint32_t)(now-s.summary_at[i])>1000U ||
        age_add(s.summary_age[i],now-s.summary_at[i])>1000U)return false;
    return true;
}
static bool physical_ready(uint32_t now)
{
    const app_arm3_platform_t *p=&s.platform;
    return p->profile_enabled && p->safety_config_approved && p->safety_version &&
        p->config_flags==15U && p->mapped_input_mask==0x3FUL &&
        (p->hardware_bitmap&0x3FUL)==0x3FUL && (p->safety_flags&0x39U)==0x39U &&
        !(p->safety_flags&6U) && age_add(p->sample_age_ms,now-p->sample_tick_ms)<=20U &&
        p->chassis_idle && !p->chassis_moving && p->no_pending_writes &&
        p->chassis_zero_stable_ms>=500U && p->chassis_feedback_age_ms<=300U &&
        !p->parameter_locked && !p->recovery_locked;
}
static bool profile_ready(uint32_t now)
{return s.ready && s.negotiated && (s.peer_caps&PEER_REQUIRED)==PEER_REQUIRED &&
    configurations_ready() && arm_feedback_ready(now) && physical_ready(now) && s.platform.clock_valid;}
static uint8_t calculate_permit(uint32_t now)
{
    int idx=source_index(s.owner);
    if(!s.owner || idx<0 || s.state==3U || s.state==4U ||
       !profile_ready(now) || !s.work_lock || !s.sources[idx].seen ||
       (uint32_t)(now-s.sources[idx].last_ms)>=300U ||
       (uint32_t)(now-s.owner_live)>=1000U)return 0U;
    return 3U;
}
static void build_owner(can_protocol_record_t *r,uint32_t query,uint8_t status,uint32_t now)
{
    uint32_t remaining=0U,flags=0U;uint8_t active=0U;
    if(s.owner && (uint32_t)(now-s.owner_live)<1000U)remaining=1000U-(now-s.owner_live);
    if(s.active[0].active)active|=1U;if(s.active[1].active)active|=2U;
    if(s.work_lock)flags|=1U;if(s.stopped_mask==3U)flags|=2U;
    if(s.proof[0].valid && s.proof[1].valid && s.proof[0].state==2U && s.proof[1].state==2U)flags|=4U;
    if(s.active[0].stopping || s.active[1].stopping)flags|=8U;
    if(physical_ready(now))flags|=16U;
    can_arm3_header(r,1U,status,query,s.owner,0U,s.generation,s.arm_version,s.owner_seq,0U,query?0U:4U);
    if(status)return;
    r->payload[32]=s.state;r->payload[33]=s.owner;r->payload[34]=active;r->payload[35]=s.permit;
    can_protocol_write_u16(r->payload+36U,(uint16_t)remaining);
    can_protocol_write_u32(r->payload+40U,s.active[0].operation);
    can_protocol_write_u32(r->payload+44U,s.active[1].operation);
    can_protocol_write_u32(r->payload+48U,flags);can_protocol_write_u32(r->payload+52U,s.reason);
    can_protocol_write_u32(r->payload+56U,s.safety_seq);can_protocol_write_u32(r->payload+60U,s.owner_seq);
}
static void feature_record(can_protocol_record_t *r)
{
    memset(r,0,sizeof(*r));r->type=0x5CU;r->length=32U;
    can_protocol_write_u32(r->payload,CAN_ARM3_FEATURE_SCHEMA);
    can_protocol_write_u32(r->payload+4U,2U);
    can_protocol_write_u32(r->payload+8U,CAN_ARM3_LOCAL_FEATURES);
    can_protocol_write_u32(r->payload+12U,s.arm_version);
    can_protocol_write_u32(r->payload+16U,s.platform.safety_version);
    can_protocol_write_u16(r->payload+20U,1U);can_protocol_write_u16(r->payload+22U,5U);
    can_protocol_write_u16(r->payload+24U,300U);
}
static uint32_t stop_operation(uint8_t mask,uint32_t now)
{
    uint32_t op;unsigned i;can_protocol_record_t *r=&s.scratch;
    if(!s.ready)return 0U;
    op=allocate_operation();if(!op){s.state=4U;return 0U;}
    /* Stop history uses its independent 8-operation reserve. Pressure may
     * evict an old inactive stop, and never prevents the physical request. */
    {unsigned j,k,m,slot[3],count=0U;
        for(j=0;j<3U;++j){unsigned best=120U;uint32_t oldest=0U;
            for(k=96U;k<120U;++k){bool active=false,chosen=false;
                for(m=0;m<count;++m)if(slot[m]==k)chosen=true;
                for(m=0;m<2U;++m)if(s.active[m].active && s.active[m].operation==s.results[k].value.operation)active=true;
                if(chosen || active)continue;
                if(!s.results[k].value.operation){best=k;break;}
                if(best==120U || now-s.results[k].stored>oldest){best=k;oldest=now-s.results[k].stored;}
            }
            if(best<120U)slot[count++]=best;
        }
        for(j=0;j<count;++j){result_cache_t *item=&s.results[slot[j]];memset(item,0,sizeof(*item));
            item->value.operation=op;item->value.arm_id=(uint8_t)j;item->value.generation=s.generation;
            item->stored=now;item->stop=true;}
    }
    discard_motion_queue(now);memset(r,0,sizeof(*r));r->type=0x54U;r->length=32U;
    can_protocol_write_u32(r->payload,op);can_protocol_write_u32(r->payload+4U,s.generation);
    r->payload[8]=mask;r->payload[9]=3U;r->payload[10]=255U;
    can_protocol_write_u32(r->payload+24U,now);can_protocol_write_u32(r->payload+28U,s.arm_version);
    /* Each side has a dedicated urgent slot. A new stop may subsume an older
     * unsent stop, but never depends on a normal TX/result queue becoming free. */
    for(i=0;i<2U;++i)if(mask&(1U<<i)){
        unsigned j;
        for(j=0;j<96U;++j)if(s.results[j].value.operation==s.active[i].operation &&
            !s.results[j].value.present){s.results[j].value.flags=1U;s.results[j].stored=now;}
        s.urgent[i].record=*r;s.urgent[i].built=now;s.urgent[i].priority=0U;s.urgent[i].used=true;
        memset(&s.active[i],0,sizeof(s.active[i]));s.active[i].operation=op;
        s.active[i].generation=s.generation;s.active[i].opcode=3U;s.active[i].mask=mask;
        s.active[i].active=true;s.active[i].stopping=true;s.active[i].started=now;
        s.active[i].last_result=now;s.active[i].request=*r;s.proof[i].valid=false;
        s.stopped_mask&=(uint8_t)~(1U<<i);
    }
    return op;
}
static void begin_release(uint32_t now,uint32_t reason)
{
    unsigned i;bool had_permit=s.permit!=0U;
    if(s.state==3U || (s.state==4U && s.release_op && !s.release_done))return;
    arm_diag(0x202U,reason,s.release_op,s.owner_seq,now,true);
    for(i=0;i<96U;++i)if(s.results[i].value.operation && !s.results[i].value.present){
        s.results[i].value.flags=1U;s.results[i].stored=now;}
    s.owner=0U;s.permit=0U;s.pending_apply_mask=0U;s.state=3U;s.reason=reason;
    generation_change();s.release_at=now;s.release_done=false;
    s.release_op=stop_operation(3U,now);s.release_gen=s.generation;
    if(had_permit){app_arm3_build_safety(&s.scratch,now);(void)enqueue(&s.scratch,0U,now);s.last_safety=now;}
    s.query.active=false;s.next_read_at=now;
    if(!s.release_op && !s.ready)s.state=4U;
    build_owner(&s.scratch,0U,0U,now);(void)enqueue(&s.scratch,2U,now);
}
void app_arm3_init(const app_arm3_callbacks_t *callbacks)
{
    memset(&s,0,sizeof(s));input_delay_ms=0U;if(callbacks)s.callbacks=*callbacks;
    s.generation=s.owner_seq=s.snapshot_seq=1U;
    s.summary_state[0]=s.summary_state[1]=255U;
}
void app_arm3_set_platform(const app_arm3_platform_t *p)
{if(p)s.platform=*p;}
void app_arm3_link(uint32_t lower,uint32_t upper,uint16_t token,bool ready,
    uint32_t caps,uint32_t now)
{
    if(lower!=s.lower || upper!=s.upper || token!=s.token){
        if(s.ever_enabled){force_transport_lock();s.owner=0U;s.state=4U;}
        memset(s.tx,0,sizeof(s.tx));memset(s.urgent,0,sizeof(s.urgent));
        memset(s.active,0,sizeof(s.active));memset(s.results,0,sizeof(s.results));
        memset(s.config,0,sizeof(s.config));memset(s.preset,0,sizeof(s.preset));
        memset(s.pose,0,sizeof(s.pose));memset(s.proof,0,sizeof(s.proof));
        memset(s.sources,0,sizeof(s.sources));memset(s.inputs,0,sizeof(s.inputs));
        memset(s.summary_seq,0,sizeof(s.summary_seq));memset(s.summary_flags,0,sizeof(s.summary_flags));
        s.summary_state[0]=s.summary_state[1]=255U;s.owner=s.permit=0U;
        memset(s.pc_query,0,sizeof(s.pc_query));memset(s.safety_tx_seq,0,sizeof(s.safety_tx_seq));
        s.safety_tx.used=false;s.lower=lower;s.upper=upper;s.token=token;
        s.negotiated=s.features_seen=s.manifest_sent=false;s.arm_version=0U;
        s.peer_safety_version=s.peer_features=s.pc_highwater=0U;
        s.next_operation=s.next_query=0U;s.query.active=false;s.release_op=s.release_gen=0U;
        s.release_done=false;
        s.pending_apply_mask=0U;s.next_read_at=now;s.last_manifest=s.last_features=s.last_safety=now-1000U;
        memset(s.active_usb_epoch,0,sizeof(s.active_usb_epoch));
        generation_change();
    }
    if(s.ready && !ready && s.ever_enabled){s.owner=0U;s.permit=0U;s.state=4U;
        generation_change();force_transport_lock();}
    s.ready=ready && token && token!=65535U;s.peer_caps=caps;
}
bool app_arm3_negotiated(void){return s.negotiated;}
bool app_arm3_work_locked(void){return s.work_lock;}
bool app_arm3_profile_enabled(void){return s.platform.profile_enabled || s.ever_enabled;}
uint32_t app_arm3_capabilities(void)
{return (1UL<<26U)|(1UL<<27U)|(1UL<<31U);}
void app_arm3_transport_fault(uint32_t now)
{
    if(!s.ever_enabled)return;
    force_transport_lock();begin_release(now,DETAIL(15U));s.state=4U;s.permit=0U;
    if(s.callbacks.vehicle_stop)s.callbacks.vehicle_stop(s.callbacks.context);
}
void app_arm3_global_stop(uint32_t now)
{if(s.ever_enabled || s.owner || s.work_lock){force_transport_lock();begin_release(now,DETAIL(15U));}}

static void intent_content(const app_arm3_intent_t *in,uint8_t p[48])
{
    unsigned i;memset(p,0,48U);p[0]=in->command;p[1]=in->mask;
    p[2]=in->joint;p[3]=in->grade;p[4]=in->preset;p[5]=in->light_level;p[6]=(uint8_t)in->direction;
    p[7]=(uint8_t)in->held;p[8]=(uint8_t)in->page_active;
    can_protocol_write_u32(p+12U,in->expected_version);can_protocol_write_u16(p+16U,in->budget_ms);
    for(i=0;i<6U;++i)can_protocol_write_u32(p+20U+i*4U,(uint32_t)in->angle[i]);
}
static void remember_input(const app_arm3_intent_t *in,const uint8_t content[48],uint8_t result,uint32_t op,uint32_t now)
{
    unsigned i,best=INPUT_COUNT;uint32_t oldest=0U;
    if(in->command==APP_ARM3_ALIVE)return;
    for(i=0;i<INPUT_COUNT;++i)if(s.inputs[i].used && s.inputs[i].source==in->source &&
        s.inputs[i].session==in->session && s.inputs[i].sequence==in->sequence){best=i;break;}
    if(best==INPUT_COUNT)
    for(i=0;i<INPUT_COUNT;++i){if(!s.inputs[i].used){best=i;break;}
        if((in->command==APP_ARM3_STOP || (!s.inputs[i].pending && now-s.inputs[i].seen>=1000U)) &&
           now-s.inputs[i].seen>=oldest){oldest=now-s.inputs[i].seen;best=i;}}
    if(best==INPUT_COUNT)return;
    s.inputs[best].source=in->source;s.inputs[best].session=in->session;
    s.inputs[best].sequence=in->sequence;memcpy(s.inputs[best].content,content,48U);s.inputs[best].result=result;
    s.inputs[best].operation=op;s.inputs[best].seen=now;s.inputs[best].used=true;
    s.inputs[best].generation=s.generation;
    s.inputs[best].pending=in->command==APP_ARM3_APPLY && !result && !s.owner;
}
bool app_arm3_get_input_result(uint8_t source,uint32_t session,uint32_t seq,
    app_arm3_input_result_t *out)
{
    unsigned i;if(!out)return false;
    memset(out,0,sizeof(*out));
    for(i=0;i<INPUT_COUNT;++i)if(s.inputs[i].used && s.inputs[i].source==source &&
        s.inputs[i].session==session && s.inputs[i].sequence==seq){
        out->request=seq;out->generation=s.inputs[i].generation;out->received_ms=s.inputs[i].seen;
        out->sequence=seq;out->source=source;out->result=s.inputs[i].result;
        out->present=true;out->pending=s.inputs[i].pending;return true;
    }return false;
}
static uint8_t motion_submit(const app_arm3_intent_t *in,uint32_t now,uint32_t *op)
{
    unsigned i;uint8_t opcode,type,feature;can_protocol_record_t *r=&s.scratch;uint32_t id;
    if(in->command==APP_ARM3_JOG){opcode=4U;type=0x56U;feature=2U;}
    else if(in->command==APP_ARM3_PRESET){opcode=1U;type=0x54U;feature=0U;}
    else if(in->command==APP_ARM3_SIX_AXIS){opcode=6U;type=0x56U;feature=4U;}
    else if(in->command==APP_ARM3_LIGHT_ON || in->command==APP_ARM3_LIGHT_OFF){opcode=5U;type=0x56U;feature=3U;}
    else {opcode=in->command==APP_ARM3_SAVE_ACTUAL?7U:8U;type=0x57U;feature=5U;}
    if(!(s.peer_features&(1UL<<feature)))return 2U;
    if(in->mask<1U || in->mask>3U || (in->mask==3U && opcode!=1U && opcode!=5U))return 6U;
    if(opcode!=5U)for(i=0;i<2U;++i)if((in->mask&(1U<<i)) &&
        (s.active[i].active || (s.active[i].stopping && s.state==4U)))return 5U;
    if(opcode==7U || opcode==8U)if(s.active[0].active || s.active[1].active || s.state==4U)return 5U;
    if(opcode==4U && (in->joint>5U || in->grade<1U || in->grade>5U ||
        (in->direction!=1 && in->direction!=-1) || !in->held ||
        in->budget_ms<100U || in->budget_ms>30000U ||
        (in->source==3U && in->budget_ms>300U)))return 6U;
    if(opcode==1U || opcode==7U || opcode==8U){
        if(in->preset<1U || in->preset>4U)return 6U;
        if(opcode==1U)for(i=0;i<2U;++i)if((in->mask&(1U<<i)) &&
            (!s.preset[i][in->preset-1U].present || (s.preset[i][in->preset-1U].flags&3U)!=3U))return 6U;
    }
    if(opcode==6U || opcode==8U){unsigned side=in->mask==1U?0U:1U;
        for(i=0;i<6U;++i)if(in->angle[i]==INT32_MIN || in->angle[i]<s.config[side].lower[i] ||
            in->angle[i]>s.config[side].upper[i])return 6U;}
    if(opcode==5U && in->command==APP_ARM3_LIGHT_ON && !in->light_level)return 6U;
    if(!normal_result_capacity(now,0U))return 5U;
    id=allocate_operation();if(!id)return 7U;
    memset(r,0,sizeof(*r));r->type=type;r->length=type==0x54U?32U:64U;
    can_protocol_write_u32(r->payload,id);can_protocol_write_u32(r->payload+4U,s.generation);
    r->payload[8]=in->mask;r->payload[9]=opcode;r->payload[10]=opcode==4U?in->joint:255U;
    if(opcode==1U || opcode==7U || opcode==8U)can_protocol_write_u32(r->payload+12U,in->preset);
    if(opcode==4U){can_protocol_write_u32(r->payload+12U,(uint32_t)(int32_t)in->direction);
        can_protocol_write_u32(r->payload+16U,in->grade);can_protocol_write_u32(r->payload+32U,1U);
        can_protocol_write_u16(r->payload+36U,in->budget_ms<300U?in->budget_ms:300U);}
    if(opcode==1U || opcode==4U || opcode==6U)can_protocol_write_u16(r->payload+20U,
        opcode==4U?in->budget_ms:in->budget_ms>=100U && in->budget_ms<=30000U?in->budget_ms:30000U);
    can_protocol_write_u32(r->payload+24U,now);can_protocol_write_u32(r->payload+28U,s.arm_version);
    if(opcode==5U){r->payload[38]=in->command==APP_ARM3_LIGHT_ON?in->light_level:0U;
        r->payload[39]=in->command==APP_ARM3_LIGHT_ON?1U:0U;}
    if(opcode==6U || opcode==8U)for(i=0;i<6U;++i)can_protocol_write_u32(r->payload+40U+i*4U,(uint32_t)in->angle[i]);
    if(!enqueue(r,opcode==4U?1U:2U,now))return 5U;
    (void)normal_result_capacity(now,id);
    for(i=0;i<2U;++i)if((in->mask&(1U<<i)) && opcode!=5U && opcode!=7U && opcode!=8U){
        active_t *a=&s.active[i];memset(a,0,sizeof(*a));a->operation=id;a->generation=s.generation;
        a->started=a->last_result=a->last_pulse=now;a->pulse=1U;a->opcode=opcode;a->mask=in->mask;
        a->joint=in->joint;a->grade=in->grade;a->direction=in->direction;a->budget=can_protocol_read_u16(r->payload+20U);
        a->source_session=in->session;a->active=true;a->held=in->held;a->request=*r;s.proof[i].valid=false;
        s.stopped_mask&=(uint8_t)~(1U<<i);
    }
    s.state=2U;*op=id;return 0U;
}
uint8_t app_arm3_submit(const app_arm3_intent_t *in,uint32_t now,uint32_t *operation)
{
    int idx;unsigned i;uint8_t result=0U,content[48];uint32_t op=0U;uint16_t site=0U;
    if(operation)*operation=0U;
    if(!in || (idx=source_index(in->source))<0 || !in->session || !in->sequence ||
       in->command<1U || in->command>12U)return arm_diag_input_result(in,now,0U,6U,1U);
    intent_content(in,content);
    for(i=0;i<INPUT_COUNT;++i)if(s.inputs[i].used && s.inputs[i].source==in->source &&
        s.inputs[i].session==in->session && s.inputs[i].sequence==in->sequence){
        if(memcmp(s.inputs[i].content,content,48U))return arm_diag_input_result(in,now,0U,14U,2U);
        if(operation)*operation=s.inputs[i].operation;return arm_diag_input_result(in,now,s.inputs[i].operation,s.inputs[i].result,3U);
    }
    if(s.sources[idx].session==in->session && s.sources[idx].seen &&
       in->sequence<=s.sources[idx].sequence)return arm_diag_input_result(in,now,0U,7U,4U);
    if(s.sources[idx].session!=in->session){
        if(s.owner==in->source)begin_release(now,DETAIL(2U));
        s.sources[idx].session=in->session;s.sources[idx].sequence=0U;
    }
    s.sources[idx].sequence=in->sequence;s.sources[idx].seen=true;
    if(in->page_active && in->command!=APP_ARM3_STOP)s.sources[idx].last_ms=now-input_delay_ms;
    if(in->command!=APP_ARM3_STOP && in->command!=APP_ARM3_ALIVE){
        bool available=false;
        for(i=0;i<INPUT_COUNT;++i)if(!s.inputs[i].used ||
            (!s.inputs[i].pending && now-s.inputs[i].seen>=1000U)){available=true;break;}
        /* Preserve the minimum replay horizon. A pressure rejection still
         * consumes the source sequence; the independent stop path stays open. */
        if(!available){++s.queue_full;++s.rejected;return arm_diag_input_result(in,now,0U,5U,5U);}
        /* Reserve before a session change may synthesize a stop. */
        remember_input(in,content,5U,0U,now);
    }
    if(in->command==APP_ARM3_STOP){
        if(in->mask<1U || in->mask>3U){result=6U;site=6U;}
        else {op=stop_operation(in->mask,now);if(!op){result=13U;site=7U;}}
    }else if(in->command==APP_ARM3_RELEASE){
        if(s.owner!=in->source){
            s.pending_apply_mask&=(uint8_t)~(1U<<idx);
            for(i=0;i<INPUT_COUNT;++i)if(s.inputs[i].pending && s.inputs[i].source==in->source){
                s.inputs[i].pending=false;s.inputs[i].result=4U;}
            result=3U;site=8U;
        }else {begin_release(now,0U);op=s.release_op;}
    }else if(in->command==APP_ARM3_ALIVE){
        if(!in->page_active)s.pending_apply_mask&=(uint8_t)~(1U<<idx);
        if(!in->page_active && s.owner==in->source)begin_release(now,DETAIL(2U));
        else if(s.owner==in->source){
            s.owner_live=now-input_delay_ms;
            /* Remote held evidence updates only an already active matching
             * immutable jog. Screen alive never renews its bounded jog. */
            if(in->source==1U)for(i=0;i<2U;++i){active_t *a=&s.active[i];
                if(a->active && a->opcode==4U){
                    if(!in->held || in->mask!=a->mask || in->joint!=a->joint ||
                       in->grade!=a->grade || in->direction!=a->direction){
                        site=20U; /* Accepted ALIVE that explicitly ends a held jog. */
                        (void)stop_operation((uint8_t)(1U<<i),now);
                    }else a->held=true;
                }
            }
        }
    }else if(in->command==APP_ARM3_APPLY){
        if(s.owner && s.owner!=in->source){result=5U;site=9U;}
        else if(s.state==3U || (s.state==4U && s.release_op && !s.release_done)){result=4U;site=10U;}
        else if((s.release_op && !s.release_done) || !profile_ready(now)){result=4U;site=11U;}
        else if(!in->page_active || in->held){result=6U;site=12U;}
        else if(s.owner==in->source)s.owner_live=now-input_delay_ms;
        else {s.pending_apply_mask|=(uint8_t)(1U<<idx);s.pending_apply_at=now;}
    }else if(in->command==APP_ARM3_READ_POSE){
        if(!s.negotiated || (in->mask!=1U && in->mask!=2U) || s.query.active){result=5U;site=13U;}
        else {uint32_t q=next_nonzero(&s.next_query);can_arm3_query(&s.query.record,q,8U,s.owner,in->mask,0U,
            s.generation,0U,s.arm_version,0U,0U,now,s.safety_seq);
            s.query.id=q;s.query.at=now;s.query.opcode=8U;s.query.mask=in->mask;s.query.retries=0U;
            s.query.active=enqueue(&s.query.record,3U,now);if(!s.query.active){result=5U;site=14U;}}
    }else if(s.owner!=in->source || !s.sources[idx].seen || !in->page_active){result=3U;site=15U;}
    else if(s.state==3U || s.state==4U){result=4U;site=16U;}
    else if(in->expected_version && in->expected_version!=s.arm_version){result=6U;site=17U;}
    else if(calculate_permit(now)!=3U){result=4U;site=18U;}
    else {result=motion_submit(in,now,&op);site=19U;}
    if(result)++s.rejected;remember_input(in,content,result,op,now);
    if(operation)*operation=op;return arm_diag_input_result(in,now,op,result,site);
}

static void query_begin(uint8_t opcode,uint8_t mask,uint8_t selector,uint32_t now)
{
    uint32_t q=next_nonzero(&s.next_query),p0=0U,p1=0U,op=0U;
    if(!q || s.query.active)return;
    if(opcode==11U){p0=s.release_gen;p1=s.platform.safety_version;op=s.release_op;}
    can_arm3_query(&s.query.record,q,opcode,s.owner,mask,selector,s.generation,op,
        opcode==5U?0U:s.arm_version,p0,p1,now,s.safety_seq);
    s.query.id=q;s.query.at=now;s.query.opcode=opcode;s.query.mask=mask;
    s.query.selector=selector;s.query.retries=s.query.received_mask=0U;
    s.query.active=enqueue(&s.query.record,3U,now);
}
static void read_chain(uint32_t now)
{
    unsigned i,j;
    if(!s.negotiated || s.query.active || (int32_t)(now-s.next_read_at)<0)return;
    for(i=0;i<2U;++i){
        if(!s.config[i].valid || s.config[i].version!=s.arm_version){query_begin(5U,(uint8_t)(1U<<i),0U,now);return;}
        for(j=0;j<4U;++j)if(!s.preset[i][j].present){query_begin(6U,(uint8_t)(1U<<i),(uint8_t)(j+1U),now);return;}
    }
    if(s.work_lock && !s.owner && (s.state==3U || s.state==4U || s.state==5U) &&
       (!s.release_op || s.release_done)){
        for(i=0;i<2U;++i){unsigned side=(s.proof_cursor+i)&1U;
            if(!can_arm3_proof_matches(&s.proof[side],&s.config[side],&s.preset[side][2],
                s.generation,s.platform.safety_version,s.release_op,s.release_gen,now)){
                query_begin(11U,(uint8_t)(1U<<side),3U,now);s.proof_cursor=(uint8_t)(side+1U);return;
            }
        }
    }
    if(s.owner){uint8_t mask=(uint8_t)(1U<<(s.display_cursor++&1U));
        query_begin(8U,mask,0U,now);s.next_read_at=now+100U;}
}
static void safety_update(uint32_t now)
{
    uint8_t permit=calculate_permit(now);
    if(s.permit!=permit){
        if(s.permit && !permit && s.owner){begin_release(now,DETAIL(15U));permit=0U;}
        s.permit=permit;++s.owner_seq;
        app_arm3_build_safety(&s.scratch,now);(void)enqueue(&s.scratch,0U,now);s.last_safety=now;
    }
}
void app_arm3_service(uint32_t now)
{
    unsigned i;uint32_t platform_id;
    ++s.snapshot_seq;
    if(s.platform.profile_enabled && !s.ever_enabled){
        /* Deployment opt-in is latched by the platform configuration. */
        s.ever_enabled=true;force_transport_lock();s.state=5U;generation_change();
    }
    if(s.ever_enabled && !s.work_lock && s.state!=0U)force_transport_lock();
    {uint8_t key[12];can_protocol_write_u32(key,s.platform.safety_version);
        can_protocol_write_u32(key+4U,s.platform.wiring_crc);can_protocol_write_u32(key+8U,s.platform.policy_crc);
        platform_id=can_protocol_crc32(key,sizeof(key));}
    if(s.platform_hash_seen && platform_id!=s.platform_hash){
        if(s.owner)begin_release(now,DETAIL(4U));else generation_change();
        memset(s.active_usb_epoch,0,sizeof(s.active_usb_epoch));
        if(s.ever_enabled){force_transport_lock();if(!s.owner && s.state!=3U)s.state=5U;
            if(s.platform.chassis_moving && s.callbacks.vehicle_stop)s.callbacks.vehicle_stop(s.callbacks.context);}
        s.features_seen=false;s.last_features=now-1000U;
    }
    s.platform_hash=platform_id;s.platform_hash_seen=true;
    if(s.ever_enabled && !s.platform.profile_enabled && s.owner)begin_release(now,DETAIL(15U));
    if(s.ready && (s.peer_caps&(1UL<<31U)) && (uint32_t)(now-s.last_manifest)>=1000U){
        can_arm3_manifest(&s.scratch,1U);s.manifest_sent=enqueue(&s.scratch,3U,now);
        s.last_manifest=now;
    }
    if(s.ready && (s.peer_caps&(1UL<<31U)) && (uint32_t)(now-s.last_features)>=1000U){
        feature_record(&s.scratch);(void)enqueue(&s.scratch,3U,now);s.last_features=now;
    }
    for(i=0U;i<2U;++i)if(s.summary_seq[i] &&
        ((uint32_t)(now-s.summary_at[i])>1000U ||
         age_add(s.summary_age[i],now-s.summary_at[i])>1000U)){
        /* A reconnect may lose its transient offline report. Once the last
         * sequence expires, accept a new baseline only behind fresh proof. */
        s.summary_seq[i]=0U;s.summary_flags[i]=0U;s.summary_state[i]=255U;
        s.active_usb_epoch[i]=0U;s.pose[i].valid=false;s.proof[i].valid=false;
        if(s.query.active && (s.query.mask&(1U<<i)))s.query.active=false;
        s.next_read_at=now;
        if(s.owner)begin_release(now,DETAIL(15U));
        if(s.ever_enabled)transport_unknown(now);
    }
    for(i=0;i<3U;++i)if((s.pending_apply_mask&(1U<<i)) &&
        (!s.sources[i].seen || (uint32_t)(now-s.sources[i].last_ms)>=300U))
        s.pending_apply_mask&=(uint8_t)~(1U<<i);
    if(s.pending_apply_mask && s.state!=3U && (s.state!=4U || !s.release_op || s.release_done)){
        unsigned winner=(s.pending_apply_mask&1U)?0U:(s.pending_apply_mask&2U)?1U:2U;
        if(!s.owner && profile_ready(now) && lock_work()){
            s.owner=winner==0U?1U:winner==1U?3U:4U;s.state=1U;
            s.owner_at=now;s.owner_live=s.sources[winner].last_ms;s.reason=0U;generation_change();
            s.release_op=s.release_gen=0U;s.release_done=false;
        }
        for(i=0;i<INPUT_COUNT;++i)if(s.inputs[i].used && s.inputs[i].pending){
            s.inputs[i].pending=false;s.inputs[i].result=s.owner==s.inputs[i].source?0U:5U;
            s.inputs[i].generation=s.generation;
        }
        s.pending_apply_mask=0U;
    }
    for(i=0;i<INPUT_COUNT;++i)if(s.inputs[i].pending &&
        !(s.pending_apply_mask&(1U<<source_index(s.inputs[i].source)))){
        s.inputs[i].pending=false;s.inputs[i].result=4U;s.inputs[i].generation=s.generation;}
    for(i=0;i<32U;++i)if(s.pc_query[i].used && s.pc_query[i].pending && !(s.pending_apply_mask&4U)){
        pc_query_t *q=&s.pc_query[i];q->pending=false;
        build_owner(&s.scratch,q->id,s.owner==4U?0U:4U,now);
        q->length=(uint8_t)s.scratch.length;memcpy(q->response,s.scratch.payload,q->length);
        q->at=now;(void)enqueue(&s.scratch,2U,now);
    }
    if(s.owner){int idx=source_index(s.owner);
        if(idx<0 || (uint32_t)(now-s.sources[idx].last_ms)>=300U ||
           (uint32_t)(now-s.owner_live)>=1000U)begin_release(now,DETAIL(2U));
    }
    safety_update(now);
    if(s.ready && s.negotiated && (uint32_t)(now-s.last_safety)>=100U){
        app_arm3_build_safety(&s.scratch,now);(void)enqueue(&s.scratch,1U,now);s.last_safety=now;
    }
    for(i=0;i<2U;++i){active_t *a=&s.active[i];
        if(!a->active)continue;
        if(a->stopping){
            if((uint32_t)(now-a->started)>=5000U){a->active=false;s.state=4U;s.reason=DETAIL(11U);}
        }else if((uint32_t)(now-a->started)>=a->budget ||
            (uint32_t)(now-a->last_result)>=1000U){
            (void)stop_operation((uint8_t)(1U<<i),now);s.reason=DETAIL(11U);
        }else if(a->opcode==4U && s.owner==1U && a->held &&
                 (uint32_t)(now-a->last_pulse)>=100U){
            can_protocol_record_t *r=&a->request;
            if(a->pulse==UINT32_MAX){(void)stop_operation((uint8_t)(1U<<i),now);continue;}
            ++a->pulse;can_protocol_write_u32(r->payload+32U,a->pulse);
            can_protocol_write_u32(r->payload+24U,now);
            /* At most one not-started pulse per side; never mutate started TX. */
            {unsigned k;for(k=0;k<TX_COUNT;++k)if(s.tx[k].used && s.tx[k].record.type==0x56U &&
                can_protocol_read_u32(s.tx[k].record.payload)==a->operation)s.tx[k].used=false;}
            (void)enqueue(r,1U,now);a->last_pulse=now;
        }
        if(a->active && !a->accepted && (uint32_t)(now-a->last_result)>=200U){
            if(a->retries<2U){if(a->stopping){s.urgent[i].record=a->request;s.urgent[i].priority=0U;
                s.urgent[i].built=now;s.urgent[i].used=true;}else(void)enqueue(&a->request,2U,now);
                ++a->retries;a->last_result=now;
            }else if(!a->stopping)(void)stop_operation((uint8_t)(1U<<i),now);
        }
    }
    if(s.state==3U && !s.active[0].active && !s.active[1].active && s.release_done)s.state=5U;
    if(s.query.active && (uint32_t)(now-s.query.at)>=200U){
        if(s.query.retries<2U){(void)enqueue(&s.query.record,3U,now);++s.query.retries;s.query.at=now;}
        else{s.query.active=false;s.reason=DETAIL(11U);s.next_read_at=now+1000U;}
    }
    read_chain(now);
    if(s.work_lock && !s.owner && configurations_ready() && s.platform.safety_config_approved &&
       transport_summary_valid(now) &&
       can_arm3_proof_matches(&s.proof[0],&s.config[0],&s.preset[0][2],s.generation,
           s.platform.safety_version,s.release_op,s.release_gen,now) &&
       can_arm3_proof_matches(&s.proof[1],&s.config[1],&s.preset[1][2],s.generation,
           s.platform.safety_version,s.release_op,s.release_gen,now)){
        if(s.callbacks.work_lock && s.callbacks.work_lock(s.callbacks.context,false)){
            s.work_lock=false;s.state=0U;s.stopped_mask=3U;s.reason=0U;++s.owner_seq;
        }
    }
    /* Monitor idle transport too: otherwise a stale parked arm could retain a
     * chassis grant window until the first physical movement was observed. */
    if(s.ever_enabled && (!s.work_lock || s.platform.chassis_moving) &&
        !transport_summary_valid(now))transport_unknown(now);
    if(s.ready && s.negotiated && (uint32_t)(now-s.last_owner)>=(s.owner || s.work_lock?500U:1000U)){
        build_owner(&s.scratch,0U,0U,now);(void)enqueue(&s.scratch,2U,now);s.last_owner=now;
    }
}
void app_arm3_build_safety(can_protocol_record_t *r,uint32_t now)
{
    uint8_t *p;uint32_t age=age_add(s.platform.sample_age_ms,now-s.platform.sample_tick_ms);
    memset(r,0,sizeof(*r));r->type=0x53U;r->length=32U;p=r->payload;
    if(!next_nonzero(&s.safety_seq)){s.permit=0U;s.ready=false;}
    can_protocol_write_u32(p,s.safety_seq);can_protocol_write_u32(p+4U,s.generation);
    p[8]=s.permit;p[9]=s.owner;can_protocol_write_u16(p+10U,s.platform.safety_flags&0x3FU);
    can_protocol_write_u32(p+12U,s.platform.hardware_bitmap&0x3FUL);
    can_protocol_write_u32(p+16U,s.permit?0U:!s.owner?1U:5U);
    can_protocol_write_u32(p+20U,age);can_protocol_write_u16(p+24U,300U);
    can_protocol_write_u32(p+28U,s.platform.safety_version);
}
bool app_arm3_take_urgent(can_protocol_record_t *r,uint8_t *priority,uint32_t *built)
{
    unsigned i;tx_t *slot=NULL;
    if(!r)return false;
    for(i=0;i<2U;++i)if(s.urgent[i].used && (!slot ||
        can_protocol_read_u32(s.urgent[i].record.payload)<can_protocol_read_u32(slot->record.payload)))slot=&s.urgent[i];
    if(slot){uint32_t op=can_protocol_read_u32(slot->record.payload);
        for(i=0;i<2U;++i)if(&s.urgent[i]!=slot && s.urgent[i].used &&
            can_protocol_read_u32(s.urgent[i].record.payload)==op)s.urgent[i].used=false;
    }else if(s.safety_tx.used && s.safety_tx.priority==0U)slot=&s.safety_tx;
    if(!slot)return false;
    *r=slot->record;if(priority)*priority=slot->priority;
    if(built)*built=slot->built;slot->used=false;return true;
}
bool app_arm3_take_record(can_protocol_record_t *r,uint8_t *priority,uint32_t *built)
{
    unsigned i,best=TX_COUNT,first_action=TX_COUNT;uint8_t p=255U;tx_t *slot=NULL;
    uint32_t first_id=UINT32_MAX;
    if(!r)return false;
    if(app_arm3_take_urgent(r,priority,built))return true;
    if(s.safety_tx.used)slot=&s.safety_tx;
    else {for(i=0;i<TX_COUNT;++i)if(s.tx[i].used){
            if(s.tx[i].priority<p){best=i;p=s.tx[i].priority;}
            if((s.tx[i].record.type==0x54U || s.tx[i].record.type==0x56U || s.tx[i].record.type==0x57U) &&
               can_protocol_read_u32(s.tx[i].record.payload)<first_id){
                first_id=can_protocol_read_u32(s.tx[i].record.payload);first_action=i;}
        }
        if(best<TX_COUNT && first_action<TX_COUNT &&
           (s.tx[best].record.type==0x54U || s.tx[best].record.type==0x56U || s.tx[best].record.type==0x57U))best=first_action;
        if(best<TX_COUNT)slot=&s.tx[best];}
    if(!slot)return false;*r=slot->record;if(priority)*priority=slot->priority;
    if(built)*built=slot->built;slot->used=false;return true;
}
void app_arm3_record_transmitted(const can_protocol_record_t *r,uint32_t completed)
{
    if(r && r->type==0x53U && r->length==32U){unsigned i=s.safety_tx_cursor++%4U;
        s.safety_tx_seq[i]=can_protocol_read_u32(r->payload);s.safety_tx_ms[i]=completed;}
}

bool app_arm3_prepare_record(can_protocol_record_t *r,uint32_t constructed,uint32_t now)
{
    uint32_t elapsed=now-constructed,offset;
    if(!r || elapsed>0x7FFFFFFFUL)return false;
    if(r->type==0x53U){
        if(elapsed>20U)return false;
        offset=20U;
    }else if(r->type==0x5FU && r->length>=32U)offset=24U;
    else return true;
    can_protocol_write_u32(r->payload+offset,age_add(can_protocol_read_u32(r->payload+offset),elapsed));
    return true;
}
static bool store_result(const can_arm3_result_t *value,uint32_t now)
{
    unsigned i,first,last,best=RESULT_COUNT;uint32_t age=0U;
    bool stop=false;
    for(i=0;i<2U;++i)if(s.active[i].operation==value->operation && s.active[i].stopping)stop=true;
    for(i=96U;i<120U;++i)if(s.results[i].value.operation==value->operation)stop=true;
    first=stop?96U:0U;last=stop?120U:96U;
    for(i=0;i<RESULT_COUNT;++i)if(s.results[i].value.operation &&
        s.results[i].value.operation==value->operation && s.results[i].value.arm_id==value->arm_id){
        can_arm3_result_t *old=&s.results[i].value;
        if(value->sequence==0U)return false;
        if(!old->present){*old=*value;s.results[i].stored=now;return true;}
        if((old->flags&1U) && !(value->flags&1U))return false;
        if((old->flags&1U) && (value->phase!=old->phase || value->result!=old->result ||
            value->detail!=old->detail || value->generation!=old->generation ||
            value->config_version!=old->config_version || value->mask!=old->mask))return false;
        if(value->sequence==old->sequence){
            if(value->phase!=old->phase || value->result!=old->result || value->detail!=old->detail)return false;
            if(value->age_ms<age_add(old->age_ms,now-old->received_ms))return false;
            *old=*value;return true;
        }
        if(!can_arm3_newer32(value->sequence,old->sequence))return false;
        *old=*value;s.results[i].stored=now;return true;
    }
    if(value->sequence==0U)return false;
    for(i=first;i<last;++i){if(!s.results[i].value.operation){best=i;break;}
        if((s.results[i].value.flags&1U) && (stop || (uint32_t)(now-s.results[i].stored)>=60000U) &&
            now-s.results[i].stored>=age){age=now-s.results[i].stored;best=i;}}
    if(best==RESULT_COUNT)return false;
    s.results[best].value=*value;s.results[best].stored=now;s.results[best].stop=stop;return true;
}
bool app_arm3_get_result(uint32_t op,uint8_t arm,uint32_t now,can_arm3_result_t *out)
{
    unsigned i;
    if(!out || arm>2U)return false;
    for(i=0;i<RESULT_COUNT;++i)if(s.results[i].value.present &&
        s.results[i].value.operation==op && s.results[i].value.arm_id==arm){
        *out=s.results[i].value;out->age_ms=age_add(out->age_ms,now-out->received_ms);
        out->received_ms=now;return true;
    }memset(out,0,sizeof(*out));return false;
}
bool app_arm3_request_result(uint32_t op,uint8_t mask,uint32_t now)
{
    uint32_t id;
    if(!op || mask<1U || mask>3U || !s.negotiated || s.query.active)return false;
    id=next_nonzero(&s.next_query);if(!id)return false;
    can_arm3_query(&s.query.record,id,4U,s.owner,mask,0U,s.generation,op,0U,0U,0U,now,s.safety_seq);
    s.query.id=id;s.query.at=now;s.query.opcode=4U;s.query.mask=mask;
    s.query.selector=s.query.retries=s.query.received_mask=0U;
    s.query.active=enqueue(&s.query.record,3U,now);return s.query.active;
}
static bool consume_result(const can_protocol_record_t *r,uint32_t start,uint32_t commit,uint32_t now)
{
    can_arm3_result_t value;unsigned i;
    if(!can_arm3_decode_result(r,start,commit,now,&value))return false;
#if APP_LINK_DIAG_ACTIVE && APP_CAN_ARM_DIAG_ENABLED
    app_link_diag_record(APP_LINK_CAN_ARM,0x203U,start,commit,now,value.operation,
        value.generation,(uint32_t)value.phase | ((uint32_t)value.result<<2U) |
        ((uint32_t)value.flags<<6U) | ((uint32_t)value.mask<<9U) | ((uint32_t)r->type<<16U));
#endif
    if((value.flags&1U) && value.result==1U && (!(value.flags&2U) || value.age_ms>200U)){
        for(i=0;i<2U;++i)if(s.active[i].active && s.active[i].operation==value.operation &&
            (s.active[i].stopping || s.active[i].generation==value.generation) && (value.mask&(1U<<i))){
            if(s.active[i].stopping){s.active[i].active=false;s.state=4U;s.reason=DETAIL(11U);}
            else begin_release(now,DETAIL(11U));
            return false;
        }
    }
    if(!store_result(&value,now))return false;
    if(r->type==0x55U && (value.flags&1U))for(i=0;i<96U;++i)
        if(s.results[i].value.operation==value.operation && !s.results[i].value.present){
            s.results[i].value.flags=1U;s.results[i].stored=now;
        }
    /* Per-side records display progress but never complete a multi-side
     * operation before the matching aggregate result establishes its outcome. */
    if(r->type==0x5FU)return true;
    for(i=0;i<2U;++i){active_t *a=&s.active[i];
        if(!a->active || a->operation!=value.operation || (!a->stopping &&
           (a->generation!=value.generation || value.config_version!=s.arm_version)) ||
           !(value.mask&(1U<<i)))continue;
        a->last_result=now;a->accepted=true;
        if(value.flags&1U){
            bool stopping=a->stopping;
            a->active=false;
            if(stopping){
                if(value.result==1U && (value.flags&2U) && value.age_ms<=200U){
                    s.stopped_mask|=(uint8_t)(1U<<i);
                    if(s.release_op==value.operation && s.stopped_mask==3U)s.release_done=true;
                }else {s.state=4U;s.reason=value.detail?value.detail:DETAIL(11U);}
            }else if(value.result!=1U && value.result!=11U){
                begin_release(now,value.detail?value.detail:DETAIL(15U));
            }
        }
    }
    if(s.owner && s.state!=4U && !s.active[0].active && !s.active[1].active)s.state=1U;
    return true;
}
static bool safety_challenge(uint32_t seq,uint32_t now)
{
    unsigned i;if(!seq)return false;
    for(i=0;i<4U;++i)if(s.safety_tx_seq[i]==seq && (uint32_t)(now-s.safety_tx_ms[i])<=200U)return true;
    return false;
}
static void build_safety_config(can_protocol_record_t *r,uint32_t query,uint32_t now)
{
    uint8_t *p;
    can_arm3_header(r,5U,s.platform.safety_config_approved?0U:3U,query,s.owner,0U,
        s.generation,s.arm_version,++s.owner_seq,0U,0U);
    if(r->payload[9])return;p=r->payload;
    can_protocol_write_u32(p+32U,s.platform.safety_version);can_protocol_write_u32(p+36U,0x3FU);
    can_protocol_write_u32(p+40U,s.platform.raw_active_high_mask);
    can_protocol_write_u32(p+44U,s.platform.mapped_input_mask);
    can_protocol_write_u16(p+48U,s.platform.debounce_ms);can_protocol_write_u16(p+50U,10U);
    can_protocol_write_u16(p+52U,500U);can_protocol_write_u16(p+54U,300U);
    can_protocol_write_u16(p+56U,s.platform.chassis_zero_threshold_mm_s);
    can_protocol_write_u16(p+58U,5000U);can_protocol_write_u16(p+60U,25U);
    can_protocol_write_u16(p+62U,2000U);can_protocol_write_u16(p+64U,200U);
    can_protocol_write_u16(p+66U,1U);can_protocol_write_u32(p+68U,s.platform.config_flags);
    can_protocol_write_u32(p+72U,s.platform.wiring_crc);can_protocol_write_u32(p+76U,s.platform.policy_crc);
    (void)now;
}
static bool receive_query(const can_protocol_record_t *r,uint32_t now)
{
    const uint8_t *p=r->payload;uint32_t id;unsigned i,best=32U;uint8_t op,status=0U;
    if(r->length!=48U || can_protocol_read_u32(p)!=CAN_ARM3_SCHEMA ||
        !s.negotiated || can_protocol_read_u32(p+40U) || can_protocol_read_u32(p+44U))return false;
    id=can_protocol_read_u32(p+4U);op=p[8];if(!id || (op!=1U && op!=2U && op!=3U && op!=7U && op!=9U))return false;
    for(i=0;i<32U;++i){if(s.pc_query[i].used && s.pc_query[i].id==id){
        if(!memcmp(s.pc_query[i].content,p,48U)){
            if(s.pc_query[i].pending){arm_diag(0x206U,0x100U|op,id,0U,now,false);return true;}
            memset(&s.scratch,0,sizeof(s.scratch));s.scratch.type=0x5FU;s.scratch.length=s.pc_query[i].length;
            memcpy(s.scratch.payload,s.pc_query[i].response,s.scratch.length);
            can_protocol_write_u32(s.scratch.payload+24U,age_add(can_protocol_read_u32(s.scratch.payload+24U),now-s.pc_query[i].at));
            (void)enqueue(&s.scratch,2U,now);arm_diag(0x206U,0x200U|op,id,0U,now,false);return true;}
        can_arm3_header(&s.scratch,op==7U?5U:1U,8U,id,s.owner,0U,s.generation,s.arm_version,s.owner_seq,0U,0U);
        (void)enqueue(&s.scratch,2U,now);arm_diag(0x206U,0x800U|op,id,0U,now,true);return true;
    }if(!s.pc_query[i].used || (!s.pc_query[i].pending && (uint32_t)(now-s.pc_query[i].at)>=1000U))best=i;}
    if(id<=s.pc_highwater)status=5U;
    else {s.pc_highwater=id;if(best==32U)status=4U;}
    if(!status && op<=3U){
        if(p[9]!=4U || p[10]!=3U || p[11] || can_protocol_read_u32(p+16U) ||
           can_protocol_read_u32(p+24U)!=(op==3U?0U:1000U) || can_protocol_read_u32(p+28U))status=2U;
        else if(can_protocol_read_u32(p+12U)!=s.generation ||
            !safety_challenge(can_protocol_read_u32(p+36U),now))status=5U;
        else if(op==1U){
            if(s.owner && s.owner!=4U)status=4U;
            else if(s.state==3U || (s.state==4U && s.release_op && !s.release_done) ||
                (s.release_op && !s.release_done) || !profile_ready(now))status=3U;
            else {s.sources[2].session=s.upper;s.sources[2].sequence=id;s.sources[2].last_ms=now;s.sources[2].seen=true;
                if(!s.owner){
                    /* Resolve PC with local candidates at the same service
                     * boundary, independent of their arrival order. */
                    s.pending_apply_mask|=4U;s.pending_apply_at=now;
                    s.pc_query[best].id=id;memcpy(s.pc_query[best].content,p,48U);
                    s.pc_query[best].at=now;s.pc_query[best].length=0U;
                    s.pc_query[best].used=s.pc_query[best].pending=true;arm_diag(0x206U,op,id,0U,now,false);return true;
                }else s.owner_live=now;
            }
        }else if(s.owner!=4U){if(op==3U)s.pending_apply_mask&=(uint8_t)~4U;status=4U;}
        else if(op==2U){s.owner_live=now;s.sources[2].last_ms=now;}
        else begin_release(now,0U);
    }
    if(op==7U){
        if(!status && (p[10]!=3U || p[11] || can_protocol_read_u32(p+16U) ||
            can_protocol_read_u32(p+24U) || can_protocol_read_u32(p+28U)))status=2U;
        if(status)can_arm3_header(&s.scratch,5U,status,id,s.owner,0U,s.generation,s.arm_version,s.owner_seq,0U,0U);
        else build_safety_config(&s.scratch,id,now);
    }else build_owner(&s.scratch,id,status,now);
    (void)enqueue(&s.scratch,2U,now);
    if(best<32U && id==s.pc_highwater){s.pc_query[best].id=id;memcpy(s.pc_query[best].content,p,48U);
        s.pc_query[best].at=now;s.pc_query[best].length=(uint8_t)s.scratch.length;
        memcpy(s.pc_query[best].response,s.scratch.payload,s.scratch.length);
        s.pc_query[best].used=true;s.pc_query[best].pending=false;}
    arm_diag(0x206U,(uint32_t)op | ((uint32_t)status<<8U),id,0U,now,status!=0U);
    return true;
}
static bool receive_record(const can_protocol_record_t *r,uint32_t start,uint32_t commit,uint32_t now)
{
    const uint8_t *p;unsigned side,bit,i;uint32_t query;
    if(!r || !s.ready || (uint32_t)(commit-start)>200U)return false;p=r->payload;
    if(r->type==0x5CU){uint32_t version,safety;
        if(r->length!=32U || can_protocol_read_u32(p)!=CAN_ARM3_FEATURE_SCHEMA ||
           !can_protocol_read_u32(p+4U) || can_protocol_read_u16(p+20U)!=1U ||
           can_protocol_read_u16(p+22U)!=5U || can_protocol_read_u16(p+24U)!=300U ||
           can_protocol_read_u32(p+28U))return false;
        version=can_protocol_read_u32(p+12U);safety=can_protocol_read_u32(p+16U);
        if(s.arm_version && version!=s.arm_version){
            if(s.owner)begin_release(now,DETAIL(4U));else generation_change();
            memset(s.active_usb_epoch,0,sizeof(s.active_usb_epoch));
            if(s.ever_enabled){force_transport_lock();if(!s.owner && s.state!=3U)s.state=5U;
                if(s.platform.chassis_moving && s.callbacks.vehicle_stop)s.callbacks.vehicle_stop(s.callbacks.context);}
            memset(s.config,0,sizeof(s.config));memset(s.preset,0,sizeof(s.preset));
            s.query.active=false;
        }
        s.peer_features=can_protocol_read_u32(p+8U)&CAN_ARM3_LOCAL_FEATURES;
        s.arm_version=version;s.peer_safety_version=safety;s.features_seen=true;return true;
    }
    if(r->type==0x5EU)return receive_query(r,now);
    if(r->type==0x55U)return s.negotiated && consume_result(r,start,commit,now);
    if(r->type==0x47U){
        if(r->length!=32U || p[0]<1U || p[0]>2U || (p[1]>6U && p[1]!=255U) ||
           (can_protocol_read_u16(p+2U)&0xFF00U))return false;
        side=p[0]-1U;
        if(!can_protocol_read_u32(p+8U) ||
           (can_protocol_read_u16(p+2U)&3U)!=3U ||
           (can_protocol_read_u16(p+2U)&16U) ||
           can_protocol_read_u32(p+20U)!=s.arm_version){
            /* USB absence uses feedback_seq=0; invalidate before comparing
             * sample sequence and reopen that baseline only for new proof. */
            s.summary_seq[side]=0U;s.summary_state[side]=255U;
            s.summary_flags[side]=0U;s.summary_at[side]=now;s.summary_age[side]=UINT32_MAX;
            s.active_usb_epoch[side]=0U;s.pose[side].valid=false;s.proof[side].valid=false;
            if(s.query.active && (s.query.mask&(1U<<side)))s.query.active=false;
            s.next_read_at=now;
            if(s.owner)begin_release(now,DETAIL(15U));
            if(s.ever_enabled)transport_unknown(now);
            return true;
        }
        if(s.summary_seq[side] && !can_arm3_newer32(can_protocol_read_u32(p+8U),s.summary_seq[side]))return false;
        s.summary_seq[side]=can_protocol_read_u32(p+8U);s.summary_state[side]=p[1];
        s.summary_flags[side]=can_protocol_read_u16(p+2U);s.summary_at[side]=now;
        s.summary_age[side]=can_arm3_age(can_protocol_read_u32(p+12U),start,commit,now);
        if((s.summary_flags[side]&3U)!=3U || (s.summary_flags[side]&16U) ||
           can_protocol_read_u32(p+20U)!=s.arm_version){
            s.active_usb_epoch[side]=0U;s.pose[side].valid=false;s.proof[side].valid=false;
            if(s.owner)begin_release(now,DETAIL(15U));
        }
        if(p[1]!=0U || (s.summary_flags[side]&0x14U) || can_protocol_read_u32(p+20U)!=s.arm_version){
            s.proof[side].valid=false;
            if(s.ever_enabled && !s.work_lock)transport_unknown(now);
        }return true;
    }
    if(!can_arm3_response_valid(r))return false;
    if(p[8]==0U){s.negotiated=s.manifest_sent && can_arm3_manifest_matches(r,2U);return s.negotiated;}
    if(!s.negotiated)return false;
    if(p[8]==2U){
        bool accepted=consume_result(r,start,commit,now);
        if(accepted && s.query.active && s.query.opcode==4U &&
           can_protocol_read_u32(p+4U)==s.query.id && p[11]){
            s.query.received_mask|=(uint8_t)(1U<<(p[11]-1U));
            if((s.query.received_mask&s.query.mask)==s.query.mask)s.query.active=false;
        }return accepted;
    }
    query=can_protocol_read_u32(p+4U);side=p[11]?p[11]-1U:2U;bit=side<2U?1U<<side:0U;
    if(!s.query.active || query!=s.query.id || !(s.query.mask&bit))return false;
    if(p[9]){s.query.active=false;s.next_read_at=now+1000U;return true;}
    if(can_protocol_read_u32(p+16U)!=s.arm_version)return false;
    if(p[8]==3U && s.query.opcode==5U){
        if(!can_arm3_decode_config(r,&s.config[side]))return false;
    }else if(p[8]==4U && s.query.opcode==6U && p[32]==s.query.selector){
        can_arm3_preset_t preset;if(!can_arm3_decode_preset(r,&preset))return false;
        for(i=0;i<6U && (p[33]&1U);++i)if(preset.angle[i]<s.config[side].lower[i] ||
            preset.angle[i]>s.config[side].upper[i])return false;
        s.preset[side][p[32]-1U]=preset;
    }else if(p[8]==6U && s.query.opcode==8U){
        can_arm3_pose_t pose;if(!can_arm3_decode_pose(r,start,commit,now,&pose))return false;
        if(s.pose[side].sequence && !can_arm3_newer32(pose.sequence,s.pose[side].sequence))return false;
        s.pose[side]=pose;
    }else if(p[8]==8U && s.query.opcode==11U){
        can_arm3_proof_t proof;if(!can_arm3_decode_proof(r,start,commit,now,&proof))return false;
        if(s.proof[side].id && proof.id<s.proof[side].id)return false;
        s.proof[side]=proof;
        if(can_arm3_stopped_proof_matches(&proof,&s.config[side],s.generation,
            s.platform.safety_version,s.release_op,s.release_gen,now)){
            s.active_usb_epoch[side]=proof.usb_epoch;
            s.pose[side].sequence=proof.last_sequence;s.pose[side].age_ms=proof.age_ms;
            s.pose[side].received_ms=now;s.pose[side].angle_mask=63U;s.pose[side].speed_mask=0U;
            s.pose[side].fault=proof.fault;s.pose[side].span_ms=proof.span_ms;s.pose[side].valid=true;
            for(i=0;i<6U;++i){s.pose[side].angle[i]=proof.angle[i];s.pose[side].speed[i]=INT32_MIN;}
        }
    }else return false;
    s.query.received_mask|=(uint8_t)bit;
    if((s.query.received_mask&s.query.mask)==s.query.mask)s.query.active=false;
    if(p[8]==8U && (!s.proof[0].valid || s.proof[0].state!=2U || !s.proof[1].valid || s.proof[1].state!=2U) &&
       can_arm3_stopped_proof_matches(&s.proof[0],&s.config[0],s.generation,
           s.platform.safety_version,s.release_op,s.release_gen,now) &&
       can_arm3_stopped_proof_matches(&s.proof[1],&s.config[1],s.generation,
           s.platform.safety_version,s.release_op,s.release_gen,now))s.next_read_at=now+500U;
    return true;
}
bool app_arm3_receive(const can_protocol_record_t *r,uint32_t start,uint32_t commit,uint32_t now)
{
    bool accepted=receive_record(r,start,commit,now);
#if APP_LINK_DIAG_ACTIVE && APP_CAN_ARM_DIAG_ENABLED
    app_link_diag_record(APP_LINK_CAN_ARM,0x204U,start,commit,now,
        r?(uint32_t)r->type | ((uint32_t)r->length<<8U):0U,accepted?1U:0U,s.reason);
    if(!accepted)arm_diag(0x207U,r?(uint32_t)r->type | ((uint32_t)r->length<<8U):0U,
        r && r->length>=8U?can_protocol_read_u32(r->payload+4U):0U,commit-start,now,true);
#endif
    return accepted;
}
void app_arm3_snapshot(app_arm3_snapshot_t *out,uint32_t now)
{
    unsigned i,j;int idx;
    if(!out)return;memset(out,0,sizeof(*out));out->lower_boot=s.lower;out->upper_boot=s.upper;
    out->generation=s.generation;out->arm_version=s.arm_version;out->safety_version=s.platform.safety_version;
    out->safety_sequence=s.safety_seq;out->owner_sequence=s.owner_seq;out->reason=s.reason;out->now_ms=now;
    out->snapshot_sequence=s.snapshot_seq;out->source_age_ms=age_add(s.platform.sample_age_ms,now-s.platform.sample_tick_ms);
    out->owner=s.owner;out->state=s.state;out->permit_mask=s.permit;out->negotiated=s.negotiated;
    out->config_match=configurations_ready();out->link_ready=s.ready;out->work_locked=s.work_lock;
    out->parameter_locked=s.platform.parameter_locked;out->chassis_busy=!s.platform.chassis_idle || !s.platform.no_pending_writes;
    out->safety_valid=physical_ready(now);out->feature_mask=s.peer_features;out->rejected=s.rejected;out->queue_full=s.queue_full;
    if(s.owner && (uint32_t)(now-s.owner_live)<1000U)out->owner_remaining_ms=(uint16_t)(1000U-(now-s.owner_live));
    idx=source_index(s.owner);if(idx>=0 && (uint32_t)(now-s.sources[idx].last_ms)<300U)
        out->input_remaining_ms=(uint16_t)(300U-(now-s.sources[idx].last_ms));
    for(i=0;i<2U;++i){app_arm3_side_snapshot_t *x=&out->side[i];can_arm3_result_t *result=NULL;
        x->operation=s.active[i].operation;x->generation=s.active[i].generation;x->version=s.config[i].version;
        x->active=s.active[i].active;x->stopping=s.active[i].stopping;x->state=s.summary_state[i];
        x->online=(s.summary_flags[i]&1U) && (uint32_t)(now-s.summary_at[i])<=1000U;
        x->feedback_age_ms=age_add(s.pose[i].age_ms,now-s.pose[i].received_ms);
        x->angle_mask=s.pose[i].valid && x->feedback_age_ms<=200U?s.pose[i].angle_mask:0U;
        x->speed_mask=x->angle_mask?s.pose[i].speed_mask:0U;x->sequence=s.pose[i].sequence;
        x->raw_fault=x->angle_mask?s.pose[i].fault:UINT32_MAX;
        x->usb_epoch=s.proof[i].usb_epoch;x->identity_crc=s.config[i].identity_crc;
        x->config_crc=s.config[i].payload_crc;x->preset3_revision=s.preset[i][2].revision;
        x->feedback_real=x->angle_mask==63U && s.pose[i].sequence!=0U;
        x->diagnostic_valid=x->feedback_real && s.pose[i].fault==0U;
        x->identity_valid=s.active_usb_epoch[i]!=0U && s.active_usb_epoch[i]==s.proof[i].usb_epoch;
        x->config_valid=s.config[i].valid && s.config[i].version==s.arm_version;
        x->transport_approved=s.preset[i][2].present && s.preset[i][2].flags==7U;
        x->proof_id=s.proof[i].id;x->proof_state=0U;
        if(can_arm3_stopped_proof_matches(&s.proof[i],&s.config[i],s.generation,
            s.platform.safety_version,s.release_op,s.release_gen,now))x->proof_state=1U;
        if(can_arm3_proof_matches(&s.proof[i],&s.config[i],&s.preset[i][2],s.generation,
            s.platform.safety_version,s.release_op,s.release_gen,now)){x->proof_state=2U;out->transport_mask|=(uint8_t)(1U<<i);}
        for(j=0;j<6U;++j){x->angle[j]=(x->angle_mask&(1U<<j))?s.pose[i].angle[j]:INT32_MIN;
            x->speed[j]=(x->speed_mask&(1U<<j))?s.pose[i].speed[j]:INT32_MIN;}
        for(j=0;j<4U;++j)if(s.preset[i][j].present && (s.preset[i][j].flags&3U)==3U)x->preset_mask|=(uint8_t)(1U<<j);
        for(j=0;j<RESULT_COUNT;++j)if(s.results[j].value.present &&
            s.results[j].value.operation==x->operation &&
            (!s.results[j].value.arm_id || s.results[j].value.arm_id==i+1U)){
            if(!result || can_arm3_newer32(s.results[j].value.sequence,result->sequence))result=&s.results[j].value;}
        if(result){x->phase=result->phase;x->result=result->result;x->result_flags=result->flags;
            x->result_sequence=result->sequence;x->detail=result->detail;}else{x->phase=x->result=255U;}
        if(x->active)out->active_mask|=(uint8_t)(1U<<i);
        if(x->angle_mask==63U)out->feedback_mask|=(uint8_t)(1U<<i);
    }
}
