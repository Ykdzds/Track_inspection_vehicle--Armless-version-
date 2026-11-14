#define main original_unit_main
#include "test_f103_unit.c"
#undef main

static void Active(void)
{
    Setup(1000U); control.state=SIM_RUNNING; control.motion_token=23U;
    control.axes=3U; control.link_token=100U; control.lower_boot=42U; control.upper_boot=84U;
    control.stop_baseline=0U; control.motion_generation=1U;
    Record(3U,0x13U,40U,1000U);
}
static void Evidence(uint32_t now)
{
    UpperRecord_t *r=&protocol.snapshot.records[2], *s=&protocol.snapshot.records[3];
    protocol.snapshot.heartbeat_at=protocol.snapshot.basic[5].timestamp=now;
    protocol.snapshot.control_source=0U; protocol.snapshot.lifecycle=1U;
    r->timestamp=s->timestamp=now; Put32(r->payload,2U);
    r->payload[6]=0U; r->payload[7]=1U; r->payload[34]=4U; r->payload[35]=0U;
    Put16(r->payload+4U,0U); Put32(r->payload+12U,1028U);
    Put16(r->payload+32U,control.stop_sequence);
    Put32(s->payload,1U); s->payload[5]=4U; s->payload[6]=3U;
    protocol.snapshot.basic[6].timestamp=protocol.snapshot.basic[7].timestamp=now;
}
int main(void)
{
    unsigned mode, fault;
    /* A delayed control record is not a lost heartbeat/session. Completion
     * still requires two independently updated axis observations. */
    Active();
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_QUICK_STOP,0,1010U);
    Evidence(1020U); protocol.snapshot.records[2].timestamp=0U;
    UpperControl_Tick(&control,&protocol,1020U);
    Evidence(1600U); protocol.snapshot.records[3].timestamp=0U;
    UpperControl_Tick(&control,&protocol,1600U);
    assert(control.state==SIM_STOPPING && control.detail!=111U);
    Evidence(1610U); UpperControl_Tick(&control,&protocol,1610U);
    assert(control.cross_zero_observations==1U);
    Evidence(1620U); protocol.snapshot.basic[7].timestamp=1610U;
    UpperControl_Tick(&control,&protocol,1620U);
    assert(control.state==SIM_STOPPING && control.cross_zero_observations==1U);
    Evidence(1630U); UpperControl_Tick(&control,&protocol,1630U);
    assert(control.state==SIM_STOPPED_RELEASED && !control.detail && !control.motion_token);
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_AUTHORIZE,0,1640U);
    Evidence(1650U); UpperControl_Tick(&control,&protocol,1650U);
    assert(control.state==SIM_AUTH_REQUESTED);
    Active();
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_QUICK_STOP,0,1010U);
    UpperControl_Tick(&control,&protocol,1999U);
    assert(control.state==SIM_STOPPING && control.detail!=111U);
    UpperControl_Tick(&control,&protocol,2000U);
    assert(control.state==SIM_FAULT && control.detail==111U);
    puts("PASS PB10 delayed records wait; distinct zero observations release/regrant; real heartbeat expiry faults111");
    for(mode=0U;mode<3U;++mode) {
        UpperStopVerify_t frozen;
        Active();
        if(mode<2U) UpperControl_Intent(&control,&protocol,mode?UPPER_INTENT_QUICK_STOP:UPPER_INTENT_RELEASE,0,1010U);
        else { Evidence(1010U); UpperControl_Tick(&control,&protocol,1010U); }
        Evidence(1020U); protocol.snapshot.lifecycle=5U;
        UpperControl_Tick(&control,&protocol,1020U);
        Evidence(31010U); protocol.snapshot.lifecycle=5U;
        UpperControl_Tick(&control,&protocol,31010U);
        assert(control.state==SIM_FAULT && control.detail==(mode==2U?115U:114U));
        assert(control.stop_verify.mask&8U); assert(control.stop_verify.life==5U);
        assert(control.stop_verify.event==1U && !control.stop_verify.token);
        assert(control.stop_verify.phase==4U && !control.stop_verify.result);
        frozen=control.stop_verify;
        Evidence(31020U); Put16(protocol.snapshot.records[2].payload+4U,99U);
        UpperControl_Tick(&control,&protocol,31020U);
        assert(!memcmp(&frozen,&control.stop_verify,sizeof(frozen)));
    }
    for(fault=0U;fault<7U;++fault) {
        Active(); Evidence(1010U); UpperControl_Tick(&control,&protocol,1010U);
        Evidence(1020U);
        switch(fault) {
        case 0U: protocol.snapshot.records[2].timestamp=0U; break;
        case 1U: protocol.snapshot.records[3].timestamp=0U; break;
        case 2U: protocol.snapshot.basic[6].timestamp=0U; break;
        case 3U: Put16(protocol.snapshot.records[2].payload+4U,99U); break;
        case 4U: Put32(protocol.snapshot.records[2].payload+12U,1540U); break;
        case 5U: Put16(protocol.snapshot.basic[6].data+2U,1U); break;
        default: protocol.snapshot.basic[7].data[0]&=(uint8_t)~2U; break;
        }
        UpperControl_Tick(&control,&protocol,1020U);
        assert(control.state!=SIM_STOPPED_RELEASED && control.stop_verify.mask);
    }
    puts("PASS stop verification: normal/quick/cross 114/115 scene frozen; stale records/axes, token, safety lock, nonzero and disabled still block release");
    return 0;
}
