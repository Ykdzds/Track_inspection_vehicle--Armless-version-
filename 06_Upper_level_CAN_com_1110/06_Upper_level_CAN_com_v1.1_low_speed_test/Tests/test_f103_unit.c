#include "can_upper_protocol.h"
#include "can_upper_control.h"
#include "can_upper_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static UpperProtocol_t protocol;
static UpperControl_t control;
static unsigned frames, nonzero, requests;
static uint8_t sent[8];
static uint16_t sent_id;
static uint8_t Send(void *context, uint16_t id, const uint8_t data[8])
{
    (void)context; frames++; sent_id = id; memcpy(sent, data, 8U);
    if (id == 0x110U) requests++;
    if (id == 0x111U && Upper_ReadU16(data+2U)) nonzero++;
    return 1U;
}
static void Put16(uint8_t *data, uint16_t value) { data[0]=(uint8_t)value; data[1]=(uint8_t)(value>>8); }
static void Put32(uint8_t *data, uint32_t value) { Put16(data,(uint16_t)value); Put16(data+2,(uint16_t)(value>>16)); }
static UpperRecord_t *Record(unsigned slot, uint8_t type, uint16_t length, uint32_t now)
{
    UpperRecord_t *record=&protocol.snapshot.records[slot];
    memset(record,0,sizeof(*record)); record->valid=1U;record->type=type;record->length=length;record->timestamp=now;
    return record;
}
static void Setup(uint32_t now)
{
    unsigned axis; UpperRecord_t *record;
    Upper_Init(&protocol,84U,Send,0,0); UpperControl_Init(&control,Send,0); UpperUi_Init();
    protocol.snapshot.state=UPPER_READY; protocol.snapshot.lower_boot_id=42U;protocol.snapshot.link_token=100U;
    protocol.snapshot.heartbeat_seen=1U;protocol.snapshot.heartbeat_at=now;protocol.snapshot.control_source=0U;
    protocol.snapshot.lifecycle=1U;protocol.snapshot.gates=31U;protocol.snapshot.axes=3U;protocol.capability_published=1U;
    protocol.snapshot.config_revision=7U;protocol.snapshot.basic[5].valid=1U;protocol.snapshot.basic[5].timestamp=now;
    record=Record(0,2U,32U,now);Put32(record->payload,42U);Put32(record->payload+4U,84U);
    record=Record(1,0x10U,80U,now);Put32(record->payload,7U);Put16(record->payload+34U,5811U);Put16(record->payload+78U,768U);
    record->payload[4]=3U;
    for(axis=0;axis<2U;axis++){Put32(record->payload+8U+axis*4U,185000U);Put16(record->payload+24U+axis*4U,8U);Put16(record->payload+26U+axis*4U,3U);}
    record=Record(2,0x11U,40U,now);Put32(record->payload+12U,1028U);record->payload[6]=0U;record->payload[7]=1U;record->payload[35]=255U;
    record->payload[11]=31U;
    for(axis=0;axis<2U;axis++){protocol.snapshot.basic[axis+6U].valid=1U;protocol.snapshot.basic[axis+6U].timestamp=now;protocol.snapshot.basic[axis+6U].data[0]=159U;protocol.snapshot.basic[axis+6U].data[1]=2U;}
    frames=nonzero=requests=0U;
}
#if UPPER_CAN_MOTION_ENABLED
static void Response(uint8_t phase,uint8_t result,uint32_t now)
{
    uint8_t data[8]={0};data[0]=control.request_sequence;data[1]=control.command;data[2]=phase;data[3]=result;Put16(data+4U,100U);
    UpperControl_Receive(&control,&protocol,0x121U,data,now,now);
}
#endif
static void GroupRecord(uint8_t type,const uint8_t *payload,uint16_t length,uint16_t sequence,uint32_t now,uint8_t corrupt)
{
    uint8_t header[8]={0},data[8]={0},joined[264];unsigned index;
    header[0]=type;header[1]=1U;Put16(header+2U,sequence);Put16(header+4U,length);Put16(header+6U,100U);
    memcpy(joined,header,8U);memcpy(joined+8U,payload,length);Upper_Receive(&protocol,0x300U,8U,header,now,now);
    for(index=0;index<length/4U;index++){Put16(data,sequence);Put16(data+2U,(uint16_t)index);memcpy(data+4U,payload+index*4U,4U);Upper_Receive(&protocol,0x301U,8U,data,now,now);}
    Put16(data,sequence);Put16(data+2U,length);Put32(data+4U,Upper_Crc32(joined,length+8U)^(corrupt?1U:0U));Upper_Receive(&protocol,0x302U,8U,data,now,now);
}
int main(void)
{
    {
        uint8_t unified[104] = {0};
        UpperUiView_t view;
        char text[32];
        unsigned page_index, line_index;
        Setup(1000U);
        Put32(unified, 123U); Put32(unified + 4U, 900U); Put32(unified + 8U, 42U);
        Put32(unified + 12U, 1U); unified[16] = 3U;
        Put16(unified + 24U, 100U); Put32(unified + 40U, 1U);
        Put32(unified + 44U, 1234U); Put32(unified + 52U, 5678U);
        Put32(unified + 68U, 1U); Put16(unified + 72U, 5U);
        Put32(unified + 100U, 10U);
        GroupRecord(0x25U, unified, 104U, 1U, 1000U, 0U);
        assert(protocol.snapshot.snapshot_valid && protocol.snapshot.snapshot_sequence == 123U);
        UpperUi_Publish(&control, &protocol, 1010U, &view);
        assert(view.snapshot_fresh && view.snapshot_age == 20U);
        assert(view.global_target == 100 && view.net_mm == 1234 && view.sync_error == 5);
        for (page_index = 0U; page_index < UPPER_UI_PAGE_COUNT; ++page_index) {
            view.page = (uint8_t)page_index;
            for (line_index = 0U; line_index < 8U; ++line_index) {
                UpperUi_Format(&view, (uint8_t)line_index, text, sizeof(text));
                assert(strlen(text) <= UPPER_OLED_LINE_CHARS);
            }
        }
        view.page = 3U;
        view.left = view.right = -32767;
        UpperUi_Format(&view, 1U, text, sizeof(text));
        assert(strcmp(text, "L:-32767 R:-32767 rpm") == 0);
        assert(strlen(text) == UPPER_OLED_LINE_CHARS);
        view.left = view.right = (int16_t)-32768;
        UpperUi_Format(&view, 1U, text, sizeof(text));
        assert(strcmp(text, "L:-- R:-- rpm") == 0);
        {
            uint8_t diagnostics[64] = {0};
            Put32(diagnostics, 123U); Put32(diagnostics + 56U, 42U);
            diagnostics[8] = 15U; diagnostics[10] = 1U;
            Put16(diagnostics + 12U, (uint16_t)-123);
            Put16(diagnostics + 20U, 2950U);
            GroupRecord(0x26U, diagnostics, 64U, 2U, 1020U, 0U);
            UpperUi_Publish(&control, &protocol, 1100U, &view);
            assert(view.diagnostic_fresh && (view.servo_valid[0] & 1U));
            assert(view.servo_stale[0] & 1U);
            Put32(unified + 48U, 1U);
            GroupRecord(0x25U, unified, 104U, 3U, 1110U, 0U);
            UpperUi_Publish(&control, &protocol, 1120U, &view);
            assert(!view.position_fresh && view.net_mm == INT32_MAX);
        }
        UpperUi_Publish(&control, &protocol, 1500U, &view);
        assert(!view.snapshot_fresh && !view.position_fresh && !view.sync_fresh);
        Upper_ResetSession(&protocol, 1500U);
        assert(!protocol.snapshot.snapshot_valid);
        puts("PASS unified snapshot: optional record, freshness, pages and reset");
    }
    uint8_t payload[32]={0},data[8]={0};int16_t speed;unsigned before,index;
    Setup(1000U);assert(UpperControl_Convert(&protocol.snapshot,50,&speed)&&speed==182);assert(UpperControl_Convert(&protocol.snapshot,-100,&speed)&&speed==-363);assert(!UpperControl_Convert(&protocol.snapshot,401,&speed));
    protocol.snapshot.records[1].payload[24]=0U;protocol.snapshot.records[1].payload[25]=0U;assert(!UpperControl_Convert(&protocol.snapshot,50,&speed));
    Setup(1000U);GroupRecord(0x53U,payload,32U,1U,1000U,0U);assert(protocol.snapshot.record_errors==0U);before=protocol.snapshot.records_received;GroupRecord(0x53U,payload,32U,2U,1010U,1U);assert(protocol.snapshot.records_received==before);assert(protocol.snapshot.record_errors==1U);
    Setup(1000U);Upper_Tick(&protocol,1500U,1U);assert(protocol.snapshot.link_token==100U);Upper_Tick(&protocol,2000U,1U);assert(protocol.snapshot.link_token==0U);
    Setup(1000U);protocol.capability_published=0U;protocol.snapshot.records[0].valid=0U;Upper_Tick(&protocol,1001U,1U);assert(!protocol.capability_active);protocol.snapshot.records[0].valid=1U;Upper_Tick(&protocol,1002U,1U);assert(protocol.capability_active);
    Setup(1000U);UpperControl_Tick(&control,&protocol,1000U);UpperControl_Intent(&control,&protocol,UPPER_INTENT_AUTHORIZE,0,1000U);
#if UPPER_CAN_MOTION_ENABLED
    assert(control.state==SIM_AUTH_PREPARING);protocol.snapshot.records[2].timestamp=1001U;UpperControl_Tick(&control,&protocol,1001U);assert(sent_id==0x110U&&sent[1]==4U&&Upper_ReadU16(sent+2U)==1U);
    data[0]=control.request_sequence;data[1]=4U;Put16(data+4U,100U);UpperControl_Receive(&control,&protocol,0x120U,data,1001U,1001U);assert(control.state==SIM_AUTH_REQUESTED);
    UpperControl_Intent(&control,&protocol,UPPER_INTENT_APPLY,100,1001U);assert(nonzero==0U);data[0]++;data[2]=3U;UpperControl_Receive(&control,&protocol,0x121U,data,1002U,1002U);assert(!control.completed);
    Response(3U,0U,1003U);assert(control.completed);UpperControl_Tick(&control,&protocol,1003U);assert(control.state==SIM_AUTH_REQUESTED&&control.motion_token==0U);
    Put16(protocol.snapshot.records[2].payload+4U,23U);protocol.snapshot.records[2].payload[6]=4U;protocol.snapshot.records[2].payload[10]=3U;Put32(protocol.snapshot.records[2].payload+12U,1036U);protocol.snapshot.records[2].timestamp=1004U;
    UpperControl_Tick(&control,&protocol,1004U);assert(control.state==SIM_AUTHORIZED_WAIT_ZERO&&Upper_ReadU16(sent+2U)==0U);UpperControl_Intent(&control,&protocol,UPPER_INTENT_APPLY,50,1005U);assert(nonzero==0U);
    protocol.snapshot.lower_boot_id++;UpperControl_Tick(&control,&protocol,1005U);assert(control.state==SIM_FAULT&&control.motion_token==0U);
    Setup(1000U);control.state=SIM_STOPPING;control.stop_event=7U;control.stop_at=1000U;control.link_token=100U;control.lower_boot=42U;control.upper_boot=84U;control.axes=3U;before=control.request_sequence;
    for(index=0;index<20U;index++)UpperControl_Intent(&control,&protocol,UPPER_INTENT_STOP,0,1001U+index);
    assert(control.stop_event==7U&&control.stop_at==1000U&&control.request_sequence==before);
    Setup(1000U);control.state=SIM_FAULT;UpperControl_Intent(&control,&protocol,UPPER_INTENT_AUTHORIZE,0,1001U);assert(control.state==SIM_FAULT);
    puts("PASS motion guard: ACK not grant, current token, zero-first, stale response, boot, repeat stop, invalid config");
#else
    (void)data;(void)index;assert(control.state==SIM_IDLE);assert(requests==0U);for(index=0;index<20U;index++){UpperControl_Intent(&control,&protocol,UPPER_INTENT_STOP,0,1000U+index);UpperControl_Intent(&control,&protocol,UPPER_INTENT_APPLY,100,1000U+index);}assert(requests==0U&&nonzero==0U);
    puts("PASS readonly guard: no authorization, speed or stop entry");
#endif

    Setup(3000U);UpperControl_Tick(&control,&protocol,3000U);
    { UpperUiView_t ui; char line[40]; unsigned index;
      for(index=0;index<2U;index++){
        uint32_t start=3000U+index*100U;
        UpperUi_ScanKeys(start,1U,&control,&protocol);UpperUi_ScanKeys(start+21U,1U,&control,&protocol);
        UpperUi_ScanKeys(start+40U,0U,&control,&protocol);UpperUi_ScanKeys(start+61U,0U,&control,&protocol);
      }
      UpperUi_Publish(&control,&protocol,3200U,&ui);assert(ui.page==2U&&ui.selected_rpm==0);
      UpperUi_ScanKeys(3220U,2U,&control,&protocol);UpperUi_ScanKeys(3241U,2U,&control,&protocol);
      UpperUi_ScanKeys(3260U,0U,&control,&protocol);UpperUi_ScanKeys(3281U,0U,&control,&protocol);
      UpperUi_Publish(&control,&protocol,3281U,&ui);assert(ui.selected_rpm==100);assert(requests==0U&&nonzero==0U);
      for(index=0;index<8U;index++){UpperUi_Format(&ui,(uint8_t)index,line,sizeof(line));assert(strlen(line)<sizeof(line));}
    }
    puts("PASS protocol/UI: config conversion, 0x53 CRC, timeout, capability gate, debounce, pages, select does not move");return 0;
}
