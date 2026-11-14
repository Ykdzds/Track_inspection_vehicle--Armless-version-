#include "can_arm3.h"
#include <string.h>

static const uint16_t lengths[9] = {64U,64U,64U,144U,64U,96U,96U,112U,144U};
static uint32_t sat_add(uint32_t a, uint32_t b)
{ return a == UINT32_MAX || b > UINT32_MAX-a ? UINT32_MAX : a+b; }
bool can_arm3_newer32(uint32_t value, uint32_t previous)
{ uint32_t d=value-previous; return d && d<0x80000000UL; }
uint32_t can_arm3_age(uint32_t age, uint32_t start, uint32_t commit, uint32_t now)
{
    if ((uint32_t)(commit-start)>200U || (uint32_t)(now-commit)>0x7FFFFFFFUL)
        return UINT32_MAX;
    return sat_add(sat_add(sat_add(age,commit-start),20U),now-commit);
}
void can_arm3_header(can_protocol_record_t *r, uint8_t kind, uint8_t status,
    uint32_t query, uint8_t owner, uint8_t arm, uint32_t gen,
    uint32_t version, uint32_t seq, uint32_t age, uint32_t flags)
{
    memset(r,0,sizeof(*r)); r->type=0x5FU;
    r->length=status || kind>8U ? 32U:lengths[kind];
    can_protocol_write_u32(r->payload,CAN_ARM3_SCHEMA);
    can_protocol_write_u32(r->payload+4U,query);
    r->payload[8]=kind;r->payload[9]=status;r->payload[10]=owner;r->payload[11]=arm;
    can_protocol_write_u32(r->payload+12U,gen);
    can_protocol_write_u32(r->payload+16U,version);
    can_protocol_write_u32(r->payload+20U,seq);
    can_protocol_write_u32(r->payload+24U,age);
    can_protocol_write_u32(r->payload+28U,flags);
}
void can_arm3_manifest(can_protocol_record_t *r, uint8_t role)
{
    uint8_t *p;
    can_arm3_header(r,0U,0U,0U,0U,0U,0U,0U,1U,0U,4U);p=r->payload;
    can_protocol_write_u16(p+32U,2U);p[34]=role;
    can_protocol_write_u32(p+36U,CAN_ARM3_FEATURES);
    can_protocol_write_u16(p+40U,1000U);can_protocol_write_u16(p+42U,300U);
    can_protocol_write_u16(p+44U,200U);can_protocol_write_u16(p+46U,100U);
    can_protocol_write_u16(p+48U,20U);can_protocol_write_u16(p+50U,200U);
    can_protocol_write_u16(p+52U,5000U);can_protocol_write_u16(p+54U,600U);
    can_protocol_write_u16(p+56U,32U);can_protocol_write_u16(p+58U,32U);
    p[60]=3U;p[61]=100U;can_protocol_write_u16(p+62U,30U);
}
bool can_arm3_manifest_matches(const can_protocol_record_t *r,uint8_t role)
{
    can_protocol_record_t expected;
    if (!can_arm3_response_valid(r) || r->payload[8] || r->payload[9])return false;
    can_arm3_manifest(&expected,role);
    return !memcmp(r->payload+32U,expected.payload+32U,24U) &&
        can_protocol_read_u16(r->payload+56U)>=32U &&
        can_protocol_read_u16(r->payload+58U)>=32U &&
        !memcmp(r->payload+60U,expected.payload+60U,4U);
}
void can_arm3_query(can_protocol_record_t *r,uint32_t query,uint8_t op,
    uint8_t owner,uint8_t mask,uint8_t selector,uint32_t gen,
    uint32_t operation,uint32_t version,uint32_t p0,uint32_t p1,
    uint32_t now,uint32_t safety)
{
    uint8_t *p;memset(r,0,sizeof(*r));r->type=0x5EU;r->length=48U;p=r->payload;
    can_protocol_write_u32(p,CAN_ARM3_SCHEMA);can_protocol_write_u32(p+4U,query);
    p[8]=op;p[9]=owner;p[10]=mask;p[11]=selector;
    can_protocol_write_u32(p+12U,gen);can_protocol_write_u32(p+16U,operation);
    can_protocol_write_u32(p+20U,version);can_protocol_write_u32(p+24U,p0);
    can_protocol_write_u32(p+28U,p1);can_protocol_write_u32(p+32U,now);
    can_protocol_write_u32(p+36U,safety);
}
bool can_arm3_response_valid(const can_protocol_record_t *r)
{
    const uint8_t *p;
    if (!r || r->type!=0x5FU || r->length<32U)return false;
    p=r->payload;
    return can_protocol_read_u32(p)==CAN_ARM3_SCHEMA && p[8]<=8U &&
        p[9]<=8U && (p[10]==0U || p[10]==1U || p[10]==3U || p[10]==4U) &&
        p[11]<=2U && !(can_protocol_read_u32(p+28U)&~7UL) &&
        r->length==(p[9] ? 32U:lengths[p[8]]);
}
bool can_arm3_decode_config(const can_protocol_record_t *r,can_arm3_config_t *c)
{
    const uint8_t *p;unsigned i;
    if (!c || !can_arm3_response_valid(r) || r->payload[8]!=3U || r->payload[9])return false;
    p=r->payload;
    if (!p[11] || p[32]!=p[11] || p[33]!=6U || p[34]!=3U || p[35]!=4U ||
        can_protocol_read_u32(p+36U)!=63U || !can_protocol_read_u32(p+16U) ||
        can_protocol_read_u16(p+112U)>100U || !can_protocol_read_u16(p+112U) ||
        can_protocol_read_u16(p+114U)!=200U || can_protocol_read_u16(p+116U)!=150U ||
        p[118]!=3U || (p[119]&~7U) || can_protocol_read_u16(p+128U)!=30U ||
        can_protocol_read_u32(p+120U)!=600U || can_protocol_read_u32(p+124U)!=20U ||
        p[130]!=3U || p[131] || can_protocol_read_u16(p+132U)!=100U ||
        can_protocol_read_u16(p+134U)!=150U ||
        can_protocol_crc32(p+32U,108U)!=can_protocol_read_u32(p+140U))return false;
    memset(c,0,sizeof(*c));c->version=can_protocol_read_u32(p+16U);
    c->enabled_mask=can_protocol_read_u32(p+36U);c->flags=p[119];
    c->identity_crc=can_protocol_read_u32(p+136U);c->payload_crc=can_protocol_read_u32(p+140U);
    for(i=0;i<6U;++i){c->lower[i]=(int32_t)can_protocol_read_u32(p+40U+i*4U);
        c->upper[i]=(int32_t)can_protocol_read_u32(p+64U+i*4U);
        if(c->lower[i]==INT32_MIN || c->upper[i]==INT32_MIN || c->lower[i]>=c->upper[i])return false;}
    c->valid=(c->flags==7U && c->identity_crc!=0U);return true;
}
bool can_arm3_decode_preset(const can_protocol_record_t *r,can_arm3_preset_t *x)
{
    unsigned i;const uint8_t *p;
    if(!x || !can_arm3_response_valid(r) || r->payload[8]!=4U || r->payload[9])return false;
    p=r->payload;if(!p[11] || p[32]<1U || p[32]>4U || (p[33]&~7U) || can_protocol_read_u16(p+34U))return false;
    memset(x,0,sizeof(*x));x->flags=p[33];x->revision=can_protocol_read_u32(p+36U);x->present=true;
    for(i=0;i<6U;++i){x->angle[i]=(int32_t)can_protocol_read_u32(p+40U+4U*i);
        if((p[33]&1U) && x->angle[i]==INT32_MIN)return false;}
    return !(p[33]&1U) || x->revision!=0U;
}
bool can_arm3_decode_pose(const can_protocol_record_t *r,uint32_t start,uint32_t commit,
    uint32_t now,can_arm3_pose_t *x)
{
    unsigned i;const uint8_t *p;
    if(!x || !can_arm3_response_valid(r) || r->payload[8]!=6U || r->payload[9] || !r->payload[11])return false;
    p=r->payload;if(can_protocol_read_u16(p+94U))return false;memset(x,0,sizeof(*x));
    x->sequence=can_protocol_read_u32(p+32U);x->angle_mask=can_protocol_read_u16(p+36U);
    x->speed_mask=can_protocol_read_u16(p+38U);x->span_ms=can_protocol_read_u16(p+92U);
    if((x->angle_mask|x->speed_mask)&~63U)return false;
    for(i=0;i<6U;++i){x->angle[i]=(int32_t)can_protocol_read_u32(p+40U+i*4U);
        x->speed[i]=(int32_t)can_protocol_read_u32(p+64U+i*4U);
        if(((x->angle[i]!=INT32_MIN)?1U:0U)!=((x->angle_mask>>i)&1U) ||
           ((x->speed[i]!=INT32_MIN)?1U:0U)!=((x->speed_mask>>i)&1U))return false;}
    x->age_ms=can_arm3_age(can_protocol_read_u32(p+24U),start,commit,now);
    x->received_ms=now;x->fault=can_protocol_read_u32(p+88U);
    x->valid=x->sequence && x->angle_mask==63U && x->span_ms<=100U && x->age_ms<=200U &&
        (can_protocol_read_u32(p+28U)&1U)!=0U;return true;
}
static bool result_consistent(uint8_t phase,uint8_t result,uint8_t flags,uint32_t seq)
{
    if(phase>3U || result>14U || (flags&~7U))return false;
    if(phase==2U)return (flags&1U) && result && (seq || result==14U);
    return !(flags&1U) && result==0U && seq!=0U;
}
bool can_arm3_decode_result(const can_protocol_record_t *r,uint32_t start,uint32_t commit,
    uint32_t now,can_arm3_result_t *x)
{
    const uint8_t *p;if(!r || !x)return false;p=r->payload;memset(x,0,sizeof(*x));
    if(r->type==0x55U && r->length==32U){
        x->operation=can_protocol_read_u32(p);x->generation=can_protocol_read_u32(p+4U);
        x->mask=p[8];x->phase=p[9];x->result=p[10];x->flags=p[11];
        x->detail=can_protocol_read_u32(p+12U);x->progress=can_protocol_read_u16(p+16U);
        if(can_protocol_read_u16(p+18U))return false;
        x->age_ms=can_arm3_age(can_protocol_read_u32(p+20U),start,commit,now);
        x->config_version=can_protocol_read_u32(p+24U);x->sequence=can_protocol_read_u32(p+28U);
    }else if(can_arm3_response_valid(r) && p[8]==2U && !p[9] && p[11]){
        x->arm_id=p[11];x->mask=(uint8_t)(1U<<(p[11]-1U));
        x->operation=can_protocol_read_u32(p+32U);x->sequence=can_protocol_read_u32(p+36U);
        x->phase=p[40];x->result=p[41];x->flags=p[42];x->detail=can_protocol_read_u32(p+44U);
        x->feedback_sequence=can_protocol_read_u32(p+48U);x->raw_fault=can_protocol_read_u32(p+52U);
        x->progress=can_protocol_read_u16(p+56U);if(can_protocol_read_u16(p+58U))return false;
        x->generation=can_protocol_read_u32(p+60U);x->config_version=can_protocol_read_u32(p+16U);
        x->age_ms=can_arm3_age(can_protocol_read_u32(p+24U),start,commit,now);
    }else return false;
    x->received_ms=now;x->present=true;
    return x->operation && x->mask>=1U && x->mask<=3U &&
        (x->progress<=100U || x->progress==65535U) &&
        result_consistent(x->phase,x->result,x->flags,x->sequence);
}
bool can_arm3_decode_proof(const can_protocol_record_t *r,uint32_t start,uint32_t commit,
    uint32_t now,can_arm3_proof_t *x)
{
    const uint8_t *p;unsigned i;
    if(!x || !can_arm3_response_valid(r) || r->payload[8]!=8U || r->payload[9] || !r->payload[11])return false;
    p=r->payload;memset(x,0,sizeof(*x));
    x->operation=can_protocol_read_u32(p+32U);x->operation_generation=can_protocol_read_u32(p+36U);
    x->generation=can_protocol_read_u32(p+40U);x->safety_version=can_protocol_read_u32(p+44U);
    x->usb_epoch=can_protocol_read_u32(p+48U);x->identity_crc=can_protocol_read_u32(p+52U);
    x->config_crc=can_protocol_read_u32(p+56U);x->preset_revision=can_protocol_read_u32(p+60U);
    x->first_sequence=can_protocol_read_u32(p+64U);x->last_sequence=can_protocol_read_u32(p+68U);
    x->window_ms=can_protocol_read_u16(p+72U);x->samples=can_protocol_read_u16(p+74U);
    x->max_interval_ms=can_protocol_read_u16(p+76U);x->span_ms=can_protocol_read_u16(p+78U);
    for(i=0;i<6U;++i){x->range[i]=can_protocol_read_u16(p+80U+i*2U);
        x->angle[i]=(int32_t)can_protocol_read_u32(p+92U+i*4U);}
    x->fault=can_protocol_read_u32(p+116U);x->id=can_protocol_read_u32(p+120U);
    x->flags=can_protocol_read_u32(p+132U);x->state=p[137];x->angle_mask=can_protocol_read_u16(p+138U);
    x->age_ms=can_arm3_age(can_protocol_read_u32(p+24U),start,commit,now);x->received_ms=now;
    if(p[136]!=3U || x->state>2U || (x->flags&~0x7FFUL) ||
       (x->angle_mask&~63U) || can_protocol_read_u32(p+140U) ||
       can_protocol_read_u32(p+12U)!=x->generation)return false;
    x->valid=true;return true;
}
bool can_arm3_stopped_proof_matches(const can_arm3_proof_t *x,const can_arm3_config_t *c,
    uint32_t gen,uint32_t safety,uint32_t op,uint32_t op_gen,uint32_t now)
{
    unsigned i;uint32_t required=op ? 0x3F3UL:0x7C3UL;
    if(!x || !c || !x->valid || !c->valid || !x->id || !x->usb_epoch || x->state<1U ||
       x->generation!=gen || x->safety_version!=safety || !safety ||
       x->operation!=op || x->operation_generation!=op_gen ||
       x->identity_crc!=c->identity_crc || x->config_crc!=c->payload_crc ||
       x->angle_mask!=63U || x->fault ||
       (x->flags&required)!=required || (op && (x->flags&0x400U)) ||
       x->window_ms<600U || x->samples<3U || !x->max_interval_ms ||
       x->max_interval_ms>100U || x->span_ms>100U || !x->first_sequence ||
       !can_arm3_newer32(x->last_sequence,x->first_sequence) ||
       sat_add(x->age_ms,now-x->received_ms)>200U)return false;
    for(i=0;i<6U;++i){
        if(x->range[i]>30U || x->angle[i]==INT32_MIN ||
           x->angle[i]<c->lower[i] || x->angle[i]>c->upper[i])return false;}
    return true;
}
bool can_arm3_proof_matches(const can_arm3_proof_t *x,const can_arm3_config_t *c,
    const can_arm3_preset_t *preset,uint32_t gen,uint32_t safety,uint32_t op,
    uint32_t op_gen,uint32_t now)
{
    unsigned i;
    if(!can_arm3_stopped_proof_matches(x,c,gen,safety,op,op_gen,now) || !preset ||
        !preset->present || preset->flags!=7U || x->state!=2U ||
        (x->flags&12U)!=12U || x->preset_revision!=preset->revision)return false;
    for(i=0;i<6U;++i){int64_t d=(int64_t)x->angle[i]-preset->angle[i];
        if(d>150 || d< -150)return false;}
    return true;
}
