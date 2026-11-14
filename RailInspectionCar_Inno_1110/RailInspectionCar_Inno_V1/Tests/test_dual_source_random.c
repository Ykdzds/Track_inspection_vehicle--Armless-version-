/* Real source adapters, frequent updates and keepalive under serial writes. */
#include <stdint.h>
#define NORMAL_TIMING_TEST 1
#define main fixture_regression_main
#if TIMING_SOURCE == 1
#include "test_hmi_control.c"
#elif TIMING_SOURCE == 2
static uint32_t timing_delay,timing_due[2];
static int16_t timing_rpm[2];
#include "test_remote_servo.c"
#else
#include "test_can_control.c"
#endif
#undef main
#include "app_normal_ramp.h"
#include "app_sync_params.h"
int main(void)
{
    unsigned step,axis;
    app_normal_ramp_status_t before,after;
#if TIMING_SOURCE == 1
    uint16_t mailbox[24]; uint32_t session,sequence=1;
    reset_fixture();g_feedback_progress=1;timing_delay=0;
    make_mailbox(mailbox,TEST_CMD_ACQUIRE,0,sequence++,0,0);
    submit_service(mailbox);session=app_hmi_control_session();assert(session);
    for(step=0;step<20;++step){++g_tick;app_hmi_control_service();}
    app_hmi_control_ramp_status(&before);
#elif TIMING_SOURCE == 2
    nrf_remote_snapshot_t input;
    memset(axes,0,sizeof(axes));memset(&input,0,sizeof(input));memset(&debug,0,sizeof(debug));
    hold_response=prepare_fail_mask=enable_fail_mask=startup_hold=0;hold_ready=boot_ready=1;timing_delay=0;
    for(axis=0;axis<2;++axis){axes[axis].online=axes[axis].enabled=axes[axis].parameter_verified=1;axes[axis].control_mode=SERVO_CONTROL_RS485_VELOCITY;}
    input.link_up=1;reset_remote();arm(&input);assert(status().authorized);
    app_remote_servo_ramp_status(&before);
#else
    reset();acquire();timing_delay=0;
    app_can_control_ramp_status(&before);
#endif
    app_sync_params_init();
    timing_delay=60;
    for(step=1;step<=5000;++step){
        /* Nonlinear target sequence every 10ms; targets remain above ramp. */
        int target=700+(int)((step/10*67U)%800U);
        if(step==1700 || step==3300) {
            app_sync_params_t tuning;
            uint32_t version=app_sync_params_read(&tuning);
            tuning.kp=.6f;tuning.pi=step==1700?1U:0U;tuning.ki=.02f;tuning.kaw=1.0f;
            assert(app_sync_params_submit(&tuning,version)==TUNE_PENDING);
            app_sync_params_service();
        }
#if TIMING_SOURCE == 1
        ++g_tick;
        if(step%10==0){make_mailbox(mailbox,TEST_CMD_SPEED,session,sequence++,target,500);submit_service(mailbox);}
        else app_hmi_control_service();
        assert(app_hmi_control_session()==session);
#elif TIMING_SOURCE == 2
        ++ticks;if(step%10==0)input.left_vertical=joystick_for_rpm(target);feed(&input);
        assert(status().authorized);
#else
        if(step%10==0)assert(speed((int16_t)(target*app_can_control_rpm_mm_s(1,0)),300));
        advance(1,true);assert(state.authorized);
#endif
        if(step==3000) {
            app_sync_observation_t observation;
            uint8_t owner=TIMING_SOURCE==1?1U:TIMING_SOURCE==2?2U:5U;
            app_sync_observation_read(owner,&observation);
            assert(observation.used_version==2U);
        }
    }
#if TIMING_SOURCE == 1
    app_hmi_control_ramp_status(&after);
#elif TIMING_SOURCE == 2
    app_remote_servo_ramp_status(&after);
#else
    app_can_control_ramp_status(&after);
#endif
    assert(after.dual.parameter_version==3U && !after.dual.pi_mode);
    assert(after.dual.integral==0.0f);
    {
        app_sync_observation_t observation;
        app_sync_target_t accepted;
        uint8_t owner=TIMING_SOURCE==1?1U:TIMING_SOURCE==2?2U:5U;
        app_sync_observation_read(owner,&observation);app_sync_target_read(owner,&accepted);
        assert(observation.used_version==3U && accepted.seen);
        assert(observation.generation==accepted.generation);
    }
    assert(after.dual.integrated_ms-before.dual.integrated_ms==5000);
    assert(after.dual.discarded_ms==before.dual.discarded_ms);
    for(axis=0;axis<2;++axis)assert(after.dual.planned_rpm[axis]>150 && after.dual.planned_rpm[axis]<240);
    printf("PASS runtime P/PI on production source=%u; frequent nonlinear targets: 5000/5000ms integrated during 60ms serial writes\n",TIMING_SOURCE);
    return 0;
}
