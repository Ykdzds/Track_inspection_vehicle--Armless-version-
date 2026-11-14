/* Real production startup, independent health guard and bus executor; hardware only is mocked. */
#define TEST_PRODUCTION_GUARDS 1
#define TEST_COLD_CLEAR 1
#include "test_core.c"
/* Startup fixture has no chassis owner; the real ARM3 arbiter is covered
 * by CAN/ARM3 joint tests. Preserve the production driver's lock dependency. */
uint8_t app_control_arbiter_arm_locked(void) { return 0U; }
#include "../Device/aimtor_servo.c"
static uint32_t cold_reset, cold_retained;
static unsigned clear_count[3], clear_ack_drop, clear_refuse, disable_refuse, disable_stuck;
static unsigned refresh_count, cold_transmits;
static TickType_t access_tick[3], max_gap, delay_until;
static uint8_t cold_audit(const uint8_t *data)
{
    unsigned slave=data[0], address=be16(data+2), value=be16(data+4);
    TickType_t gap=ticks-access_tick[slave];
    if(access_tick[slave] && gap>max_gap)max_gap=gap;
    access_tick[slave]=ticks;cold_transmits++;
    if(delay_until && (int32_t)(ticks-delay_until)<0)return 1;
    if(data[1]!=3 && !aimtor_cold_clear_ready()) {
        assert(data[1]==6);
        assert(address==AIMTOR_REG_FAULT_RESET || (address==AIMTOR_REG_SERVO_ENABLE && value==0));
        assert(registers[1][AIMTOR_REG_ACTUAL_SPEED]==0 && registers[2][AIMTOR_REG_ACTUAL_SPEED]==0);
    }
    if(data[1]==6 && address==AIMTOR_REG_FAULT_RESET) {
        assert(value==1 && registers[slave][AIMTOR_REG_SERVO_ENABLE]==0);
        assert(++clear_count[slave]==1);
        if(!clear_refuse)registers[slave][AIMTOR_REG_FAULT_CODE]=0;
        if(clear_ack_drop)return 1;
    }
    if(data[1]==3 && address==AIMTOR_REG_SERVO_ENABLE && disable_stuck)
        registers[slave][AIMTOR_REG_SERVO_ENABLE]=1;
    if(disable_refuse && data[1]==6 && address==AIMTOR_REG_SERVO_ENABLE && value==0)return 1;
    return 0;
}
uint32_t bsp_system_take_reset_reason(void){return cold_reset;}
void bsp_system_watchdog_refresh(void){refresh_count++;}
void bsp_system_fault_set_context(uint32_t task, uint32_t phase) { (void)task; (void)phase; }
void bsp_system_store_fault(uint32_t f,uint32_t d){(void)f;(void)d;}
void bsp_system_safety_store(uint32_t s,uint32_t d){cold_retained=s;(void)d;}
uint32_t bsp_system_safety_state(void){return cold_retained;}
uint32_t bsp_system_safety_detail(void){return 0;}
void bsp_system_safety_clear(void){cold_retained=0;}
void set_led(uint8_t a,uint8_t b){(void)a;(void)b;}
static servo_snapshot_t snapshot(servo_axis_t axis)
{servo_snapshot_t out;assert(aimtor_get_snapshot(axis,&out));return out;}
static void joint_seed(uint16_t left,uint16_t right,uint32_t flags,uint32_t marker)
{
    unsigned slave;
    memset(registers,0,sizeof(registers));memset(clear_count,0,sizeof(clear_count));
    memset(access_tick,0,sizeof(access_tick)); max_gap=0; delay_until=0;
    clear_ack_drop=clear_refuse=disable_refuse=disable_stuck=0;cold_transmits=0;
    response_fault=wdg_offline_slave=0;rx_length=rx_offset=0;
    cold_reset=flags;cold_retained=marker;
    for(slave=1;slave<=2;slave++) {
        registers[slave][AIMTOR_REG_WORD_ORDER]=1;
        registers[slave][AIMTOR_REG_FIRMWARE_VERSION]=558;
        registers[slave][AIMTOR_REG_NO2_STOP_MODE]=2;
        registers[slave][AIMTOR_REG_COMM_TIMEOUT]=1000;
    }
    registers[1][AIMTOR_REG_FAULT_CODE]=left;registers[2][AIMTOR_REG_FAULT_CODE]=right;
    assert(modbus_init()==MODBUS_OK);motion_init();app_health_init();aimtor_init();
    aimtor_startup_stop_init();aimtor_boot_zero_init(flags);aimtor_startup_hold_init();
    aimtor_cold_clear_init(flags,marker);
}
static void joint_pump(unsigned duration)
{
    TickType_t start=ticks,health=ticks;
    while(ticks-start<duration) {
        unsigned i;
        for(i=0;i<APP_HEARTBEAT_COUNT;i++)app_health_beat((app_heartbeat_t)i);
        if(ticks-health>=20U){app_health_watchdog_service();health=ticks;}
        aimtor_service();modbus_process();aimtor_handle_modbus_response();ticks++;
    }
}
static servo_cold_clear_status_t cold_status(void)
{servo_cold_clear_status_t out;assert(aimtor_cold_clear_get_status(0,&out));return out;}
static void assert_failed_at(unsigned line)
{(void)line;assert(cold_status().state==SERVO_COLD_CLEAR_FAILED);assert(!aimtor_startup_stop_ready());assert(!aimtor_motion_allowed());}
int main(void)
{
    unsigned i,before;const uint32_t por=1UL<<27;
    assert(APP_UNPROTECTED_MOTION_TEST_ENABLED==0);
    ticks=UINT32_MAX-300U;
    joint_seed(0x994,0x994,por,0);
    aimtor_set_motion_authorized(true);aimtor_set_hmi_control(true,3);aimtor_set_can_control(true,3);
    assert(!aimtor_motion_allowed() && !aimtor_hmi_control_active() && !aimtor_can_control_active());
    assert(aimtor_enable(0)==SERVO_NOT_READY);
    assert(aimtor_set_velocity_rpm(0,1600)!=SERVO_OK);
    assert(aimtor_set_control_mode(0,SERVO_CONTROL_RS485_VELOCITY)!=SERVO_OK);
    assert(cold_transmits==0);
    for(i=0;i<3;i++) {
        joint_seed(i?0x994:0,i==2?0:0x994,por,0);
        joint_pump(6000);
        assert(aimtor_cold_clear_ready() && aimtor_startup_stop_ready() && aimtor_startup_hold_ready());
        assert(modbus_motion_permitted() && !aimtor_motion_allowed());
        assert(registers[1][AIMTOR_REG_SPEED_COMMAND]==0 && registers[2][AIMTOR_REG_SPEED_COMMAND]==0);
        assert(clear_count[1]==(i!=0) && clear_count[2]==(i!=2));assert(max_gap<500);
        aimtor_set_motion_authorized(true);assert(aimtor_motion_allowed());
        registers[1][AIMTOR_REG_FAULT_CODE]=0x994;joint_pump(300);
        assert(clear_count[1]==(i!=0));
    }
    joint_seed(0,0,por,0);joint_pump(4000);assert(aimtor_startup_hold_ready() && clear_count[1]==0 && clear_count[2]==0);
    joint_seed(0x994,0x994,por,0);clear_ack_drop=1;joint_pump(6000);
    assert(aimtor_startup_hold_ready() && clear_count[1]==1 && clear_count[2]==1 && !cold_status().write_ack);
    joint_seed(0x994,0x994,por,0);registers[1][AIMTOR_REG_SERVO_ENABLE]=1;registers[2][AIMTOR_REG_SERVO_ENABLE]=1;
    joint_pump(6000);assert(aimtor_startup_hold_ready() && cold_status().disable_attempted);
    joint_seed(0x994,0,por,0);registers[1][AIMTOR_REG_ACTUAL_SPEED]=1600;registers[1][AIMTOR_REG_SERVO_ENABLE]=1;joint_pump(500);assert_failed_at(__LINE__);assert(clear_count[1]==0 && registers[1][AIMTOR_REG_SERVO_ENABLE]==1);
    joint_seed(0x123,0x994,por,0);joint_pump(1000);assert_failed_at(__LINE__);assert(clear_count[1]==0 && clear_count[2]==0);
    joint_seed(0x994,0x994,por,0);clear_refuse=1;joint_pump(1000);assert_failed_at(__LINE__);assert(clear_count[1]<=1 && clear_count[2]<=1);
    before=cold_transmits;joint_pump(1000);assert(cold_transmits>before);assert(clear_count[1]<=1);
    aimtor_cold_clear_init(por,0);assert_failed_at(__LINE__);
    joint_seed(0x994,0x994,por,0);registers[1][AIMTOR_REG_SERVO_ENABLE]=1;disable_refuse=1;joint_pump(1500);assert_failed_at(__LINE__);assert(clear_count[1]==0);
    joint_seed(0x994,0x994,por,0);delay_until=ticks+2000;joint_pump(8000);assert(aimtor_startup_hold_ready());
    joint_seed(0x994,0x994,por,0);wdg_offline_slave=2;joint_pump(32000);assert_failed_at(__LINE__);before=cold_transmits;joint_pump(300);assert(before==cold_transmits);
    joint_seed(0x994,0x994,por,0);registers[1][AIMTOR_REG_SERVO_ENABLE]=1;
    registers[2][AIMTOR_REG_SERVO_ENABLE]=1;
    while(!s_cold_online)joint_pump(10);
    /* Acknowledged disable with feedback remaining on must hit the fixed10s deadline. */
    disable_stuck=1;
    joint_pump(11000);assert_failed_at(__LINE__);assert(cold_status().failure==SERVO_COLD_FAIL_DEADLINE);
    joint_seed(0x994,0x994,por,0);
    while(s_cold_phase!=SERVO_COLD_CLEAR_VERIFYING)joint_pump(1);
    registers[2][AIMTOR_REG_FAULT_CODE]=0x994;joint_pump(300);assert_failed_at(__LINE__);
    joint_seed(0x994,0x994,por,0);joint_pump(60);ticks+=600;joint_pump(100);assert_failed_at(__LINE__);
    for(i=0;i<7;i++) {
        uint32_t flags=i==0?(1UL<<26):por|(1UL<<(i==1?28:i==2?29:30));
        if(i>=4)flags=por;
        joint_seed(0x994,0x994,flags,i>=4?i-3:0);joint_pump(1200);
        assert(clear_count[1]==0 && clear_count[2]==0 && cold_status().state==SERVO_COLD_CLEAR_SKIPPED);
    }
    joint_seed(0x994,0x994,por,0);
    { modbus_response_t response;
      aimtor_service();modbus_process();assert(modbus_get_response(&response,0));
      response.context.transaction_id++;aimtor_test_enqueue_response(&response);
      aimtor_handle_modbus_response();assert(s_pending_id && !s_cold_seen[0]);
      response.context.transaction_id--;response.address++;
      aimtor_test_enqueue_response(&response);aimtor_handle_modbus_response();
      assert(!s_cold_seen[0] && clear_count[1]==0); }
    joint_seed(0x994,0x994,por,0);
    joint_pump(30);app_health_request_driver_fallback(0x201);
    aimtor_service();assert_failed_at(__LINE__);assert(clear_count[1]==0);
    joint_seed(0x994,0x994,por,0);
    while(s_cold_phase!=SERVO_COLD_CLEAR_CLEARING)joint_pump(1);
    assert(aimtor_cold_clear_suppresses_fault(1));
    registers[2][AIMTOR_REG_SERVO_ENABLE]=1;
    s_axes[1].snapshot.enable_feedback=1;
    assert(!aimtor_cold_clear_suppresses_fault(1));
    joint_seed(0x994,0x994,por|(1UL<<26)|(1UL<<25),0);joint_pump(6000);
    assert(aimtor_startup_hold_ready() && clear_count[1]==1 && clear_count[2]==1);
    joint_seed(0x994,0x994,por,0);registers[1][AIMTOR_REG_NO2_STOP_MODE]=0;
    joint_pump(6000);assert(!modbus_motion_permitted());
    aimtor_set_motion_authorized(true);assert(!aimtor_motion_allowed());
    joint_seed(0x994,0x994,por,0);joint_pump(30);modbus_safety_takeover();aimtor_service();assert_failed_at(__LINE__);assert(clear_count[1]==0);
    puts("PASS real cold startup/guard/bus: ER994 single/dual/none, lost ACK, stopped disable, manual authorization, other fault/moving/refused/delayed/offline/stale, reset isolation, wrap, no preflight motion writes");
    return 0;
}
