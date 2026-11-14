#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_dual_axis_motion.h"
#include "app_motion_ramp.h"
#include "app_sync_params.h"

static servo_snapshot_t axes[2];
static remote_sync_status_t feedback;
static app_dual_axis_motion_t motion;
static uint32_t rng = 127U;
static float random_f(void) { rng = rng * 1664525U + 1013904223U; return (rng >> 8) / 16777216.0f; }
static void sample(unsigned axis, int16_t rpm, uint32_t tick)
{
    unsigned i;
    servo_snapshot_t *a = &axes[axis];
    for (i = 1; i < SERVO_SPEED_SAMPLE_CAPACITY; ++i) a->speed_samples[i-1] = a->speed_samples[i];
    a->speed_valid = a->fault_valid = 1;
    a->actual_speed_rpm = rpm; a->speed_sample_tick = tick; ++a->speed_sequence;
    a->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].valid = 1;
    a->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].sample_tick = tick;
    a->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].speed_sequence = a->speed_sequence;
    a->speed_samples[SERVO_SPEED_SAMPLE_CAPACITY-1].actual_speed_rpm = rpm;
}
static void begin(void)
{
    memset(axes,0,sizeof(axes));
    axes[0].speed_valid = axes[1].speed_valid = axes[0].fault_valid = axes[1].fault_valid = 1;
    app_remote_sync_reset(&feedback,&axes[0],&axes[1],0);
    app_dual_begin(&motion,1,42,3,0,0.15f,0.30f);
    assert(motion.initialized);
}
static void test_trajectory(void)
{
    unsigned n, step;
    for (n=0;n<10000;++n) {
        app_dual_trajectory_t t;
        float target, previous_a;
        memset(&t,0,sizeof(t));
        t.velocity = random_f()*100.0f-50.0f;
        t.acceleration = random_f()*3.0f-1.5f;
        target=random_f()*100.0f-50.0f;
        assert(app_dual_trajectory_plan(&t,target,2.0f,3.0f,4.0f));
        for(step=0;step<20000 && t.index<t.count;++step) {
            float dt=0.001f+random_f()*0.03f;
            previous_a=t.acceleration;
            app_dual_trajectory_step(&t,dt);
            assert(isfinite(t.velocity) && fabsf(t.acceleration)<=3.0001f);
            assert(fabsf(t.acceleration-previous_a)<=4.0f*dt+0.0001f);
            if(step%13==0 && step<100) {
                float v=t.velocity,a=t.acceleration;
                target=random_f()*100.0f-50.0f;
                assert(app_dual_trajectory_plan(&t,target,2.0f,3.0f,4.0f));
                assert(t.velocity==v && t.acceleration==a);
            }
        }
        assert(t.index==t.count && t.velocity==target && t.acceleration==0.0f);
    }
    puts("PASS analytic OTG: 10000 randomized moving-state replans, exact terminal and bounded jerk");
}
static void test_clock_and_alignment(void)
{
    unsigned t;
    float before;
    begin(); motion.s_mode=0;
    for(t=1;t<=1000;++t) {
        if(t%10==0) app_dual_target(&motion,40.0f+(t%30),t,t,0,t);
        else app_dual_advance(&motion,t);
    }
    assert(motion.integrated_ms==1000 && motion.discarded_ms==0);
    assert(fabsf(motion.trajectory.velocity-0.15f/0.0925f*0.991f)<0.002f);
    before=motion.trajectory.velocity;
    app_dual_advance(&motion,1120);
    assert(motion.clipped==1 && motion.discarded_ms==70);
    app_dual_advance(&motion,1121);
    assert(motion.integrated_ms==1051 && motion.trajectory.velocity>before);
    begin();
    sample(0,100,20); sample(0,140,60); sample(1,120,40);
    app_remote_sync_update(&feedback,&axes[0],&axes[1],100,1,60);
    assert(feedback.new_pair && feedback.aligned && fabsf(feedback.error_f)<0.001f);
    assert(feedback.alignment_sequence[0][0]==1 && feedback.alignment_sequence[0][1]==2);
    app_remote_sync_update(&feedback,&axes[0],&axes[1],100,1,61);
    assert(!feedback.new_pair && feedback.feedback_valid && feedback.correction_valid && feedback.active);
    app_remote_sync_update(&feedback,&axes[0],&axes[1],100,1,120);
    assert(!feedback.feedback_valid && !feedback.correction_valid && !feedback.active);
    puts("PASS clock continuity, bounded stall, bracketed interpolation, validity hold and original-age expiry");
}
static void test_allocator(void)
{
    unsigned t, axis;
    int16_t output[2]={0,0};
    float previous[2]={0,0}, previous_a[2]={0,0};
    begin();
    app_dual_target(&motion, app_dual_nominal_rpm_to_wheel(1600),1,0,0,0);
    for(t=1;t<50000;++t) {
        if(t%10==0 && t<12000) app_dual_target(&motion,
            app_dual_nominal_rpm_to_wheel(200.0f+random_f()*1400.0f),t,t,0,t);
        if(t==12000) app_dual_target(&motion,app_dual_nominal_rpm_to_wheel(1600),t,t,0,t);
        if(t==40000) app_dual_target(&motion,0,t,t,0,t);
        app_dual_advance(&motion,t);
        if(t%50) continue;
        sample(0, t<40000?105:20,t); sample(1,t<40000?100:10,t);
        assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],t,t,1,output));
        for(axis=0;axis<2;++axis) {
            float change=motion.command[axis]-previous[axis];
            assert(change<=motion.acceleration_limit*0.05f+0.00002f);
            assert(change>=-motion.deceleration_limit*0.05f-0.00002f);
            assert(output[axis]>=0 && output[axis]<=1600);
            if(t>=40000) assert(motion.command[axis]<=previous[axis]+0.00001f);
            if(motion.s_mode && t>50 && t<40000 && !(motion.constraints & DUAL_HARD_FALLBACK))
                assert(fabsf(motion.command_acceleration[axis]-previous_a[axis])<=motion.jerk_limit*0.05f+0.0003f);
            previous[axis]=motion.command[axis]; previous_a[axis]=motion.command_acceleration[axis];
        }
    }
    app_dual_execution(&motion,41,motion.batch,0,100,50000,1,1);
    assert(motion.acknowledged_mask==0);
    app_dual_execution(&motion,42,motion.batch-1,0,100,50000,1,1);
    assert(motion.acknowledged_mask==0);
    app_dual_execution(&motion,42,motion.batch,0,100,50000,0,1);
    app_dual_execution(&motion,42,motion.batch,0,90,50000,1,1);
    assert(motion.acknowledged_mask==1 && (motion.constraints & DUAL_WRITE_CHANGED));
    assert(fabsf(motion.command[0]-app_dual_motor_to_wheel(90,0))<0.00001f);
    app_dual_cancel(&motion);
    app_dual_execution(&motion,42,motion.batch,0,100,50001,1,1);
    assert(!motion.initialized && motion.acknowledged_mask==0);
    puts("PASS joint final-axis acceleration/jerk, saturation, random targets, zero monotonicity and stale generation");
}
static void test_wrap_expiry_and_reorder(void)
{
    uint32_t start=UINT32_MAX-20U;
    begin(); motion.s_mode=0;
    motion.service_ms=start;
    app_dual_target(&motion,10,1,start,start+100U,start);
    app_dual_advance(&motion,start+50U);
    assert(motion.integrated_ms==50 && motion.discarded_ms==0);
    sample(0,0,start+50U); sample(1,0,start+50U);
    app_dual_advance(&motion,start+100U);
    { int16_t output[2];
      assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],start+100U,start+100U,1,output));
      assert(motion.zeroing); }
    begin();
    sample(0,10,60); sample(1,10,60);
    app_remote_sync_update(&feedback,&axes[0],&axes[1],100,1,60);
    sample(0,999,59); /* increasing sequence with regressed timestamp */
    app_remote_sync_collect(&feedback,&axes[0],&axes[1],61);
    assert(feedback.pending_count[0]==0);
    app_remote_sync_update(&feedback,&axes[0],&axes[1],100,1,61);
    assert(!feedback.new_pair && feedback.feedback_valid);
    axes[1].fault=1;
    app_remote_sync_update(&feedback,&axes[0],&axes[1],100,1,62);
    assert(!feedback.feedback_valid && !feedback.correction_valid && !feedback.active);
    axes[1].fault=0; axes[0].feedback_degraded=1; feedback.zero_pairs=2;
    app_remote_sync_update(&feedback,&axes[0],&axes[1],100,1,63);
    assert(!feedback.have_pair && !feedback.zero_pairs);
    axes[0].feedback_degraded=0;
    app_remote_sync_update(&feedback,&axes[0],&axes[1],100,1,64);
    assert(!feedback.feedback_valid && !feedback.correction_valid);
    puts("PASS monotonic wrap, deadline, late batch, lower-layer changed write, timestamp disorder and fault invalidation");
}
static void test_serial_batch_correction(void)
{
    unsigned t;
    int16_t anchor[2] = {200,200}, output[2];
    app_sync_params_t original, tuned;
    float held_integral;
    app_sync_params_read(&original);
    tuned = original; tuned.pi = 0U; tuned.ki = tuned.kaw = 0.0f;
    assert(app_sync_params_submit(&tuned,app_sync_params_read(NULL)) == TUNE_PENDING);
    app_sync_params_service();
    begin(); motion.s_mode = 0U;
    app_dual_anchor(&motion,anchor,0U);
    app_dual_target(&motion,app_dual_nominal_rpm_to_wheel(200),1U,0U,0U,0U);
    for(t=1U;t<=600U;++t) {
        app_dual_advance(&motion,t);
        /* Independent fresh feedback throughout a two-transaction batch. */
        if(t%60U==0U) { sample(0,210,t); sample(1,200,t); }
        app_remote_sync_collect(&feedback,&axes[0],&axes[1],t);
        if(t%120U) continue;
        assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],t,t,1U,output));
        assert(feedback.new_pair && feedback.feedback_valid);
        { app_sync_timing_t timing;
          assert(app_sync_timing_read(1U,&timing));
          assert(timing.generation==42U && timing.batch==motion.batch && timing.observed_ms==t);
          assert(timing.calc_interval_ms==120U && timing.slew_dt_ms==feedback.slew_dt_ms);
          assert(timing.new_pair_count==feedback.new_pair_count);
          assert(fabsf(timing.applied-motion.applied/app_dual_nominal_rpm_to_wheel(1.0f))<0.0001f);
          assert(timing.flags&APP_SYNC_TIMING_NEW_PAIR); }
#if APP_REMOTE_SERVO_SYNC_ENABLED
        assert(feedback.slew_dt_ms==50U && feedback.dt_ms==0U);
        if(t==120U) assert(fabsf(feedback.applied_correction-1.0f)<0.0001f);
        if(t==240U) assert(fabsf(feedback.applied_correction-2.0f)<0.0001f);
        assert(output[1]>output[0]);
#else
        assert(feedback.slew_dt_ms==0U && motion.applied==0.0f);
#endif
    }
    assert(motion.discarded_ms==0U);
    tuned.pi=1U; tuned.ki=0.02f; tuned.kaw=1.0f;
    assert(app_sync_params_submit(&tuned,app_sync_params_read(NULL)) == TUNE_PENDING);
    app_sync_params_service();
    begin(); motion.s_mode=0U;
    app_dual_anchor(&motion,anchor,0U);
    app_dual_target(&motion,app_dual_nominal_rpm_to_wheel(200),1U,0U,0U,0U);
    sample(0,210,50U); sample(1,200,50U);
    assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],50U,50U,1U,output));
    assert(feedback.dt_ms==0U && motion.integral==0.0f);
    sample(0,210,100U); sample(1,200,100U);
    assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],100U,100U,1U,output));
#if APP_REMOTE_SERVO_SYNC_ENABLED
    assert(feedback.dt_ms==50U && motion.integral!=0.0f);
#endif
    held_integral=motion.integral;
    assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],149U,149U,1U,output));
    assert(!feedback.new_pair && feedback.dt_ms==0U && motion.integral==held_integral);
    sample(0,210,1100U); sample(1,200,1100U);
    assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],1100U,1100U,1U,output));
    assert(feedback.new_pair && feedback.dt_ms==0U && motion.integral==held_integral);
    axes[0].fault=1U;
    assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],1101U,1101U,1U,output));
    assert(!feedback.correction_valid && motion.integral==0.0f);
    axes[0].fault=0U;
    sample(0,210,1150U); sample(1,200,1150U);
    assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],1150U,1150U,1U,output));
    assert(feedback.new_pair && feedback.dt_ms==0U && motion.integral==0.0f);
    assert(app_sync_params_submit(&original,app_sync_params_read(NULL)) == TUNE_PENDING);
    app_sync_params_service();
    puts("PASS 120ms serial allocator batches and separate startup/recovery/repeat PI integration time");
}

static void test_dynamic_model(void)
{
    unsigned t,axis;
    int16_t output[2]={0,0};
    float actual[2]={0,0};
    double sum=0; unsigned count=0;
    const char *path=getenv("DUAL_CURVE_FILE");
    FILE *csv=path?fopen(path,"w"):NULL;
    begin();
    if(csv) fprintf(csv,"time_ms,target_wheel_rad_s,reference_wheel_rad_s,front_motor_rpm,rear_motor_rpm,front_actual_wheel_rad_s,rear_actual_wheel_rad_s,error_wheel_rad_s,constraints\n");
    for(t=1;t<=30000;++t) {
        if(t==1 || t%1700==0) app_dual_target(&motion,app_dual_nominal_rpm_to_wheel(100+random_f()*500),t,t,0,t);
        app_dual_advance(&motion,t);
        /* First-order plants with unequal time constants and rear load droop;
         * actual velocity never assigned directly from command. */
        for(axis=0;axis<2;++axis) {
            float desired=app_dual_motor_to_wheel(output[axis],(uint8_t)axis)*(axis?0.992f:1.0f);
            actual[axis]+=(desired-actual[axis])*(0.001f/(axis?0.32f:0.10f));
        }
        if(t%20==0) { sample(0,app_motion_ramp_round_rpm(app_dual_wheel_to_motor(actual[0],0)),t); }
        if(t%20==10) { sample(1,app_motion_ramp_round_rpm(app_dual_wheel_to_motor(actual[1],1)),t); }
        app_remote_sync_collect(&feedback,&axes[0],&axes[1],t);
        if(t%50==0) {
            assert(app_dual_output(&motion,&feedback,&axes[0],&axes[1],t,t,1,output));
            sum+=(actual[0]-actual[1])*(actual[0]-actual[1]); ++count;
            if(csv) fprintf(csv,"%u,%.7f,%.7f,%d,%d,%.7f,%.7f,%.7f,%lu\n",t,motion.trajectory.target,motion.trajectory.velocity,output[0],output[1],actual[0],actual[1],actual[0]-actual[1],(unsigned long)motion.constraints);
        }
    }
    if(csv) fclose(csv);
    assert(isfinite(sum) && count==600);
    printf("MODEL ONLY sync=%u S=%u PI=%u wheel_error_RMS=%.7f rad/s samples=%u\n",APP_REMOTE_SERVO_SYNC_ENABLED,APP_DUAL_S_ENABLED,APP_DUAL_PI_ENABLED,sqrt(sum/count),count);
}
int main(void)
{
    assert(app_dual_config_valid());
    test_trajectory();test_clock_and_alignment();test_allocator();test_wrap_expiry_and_reorder();test_serial_batch_correction();test_dynamic_model();
    return 0;
}
