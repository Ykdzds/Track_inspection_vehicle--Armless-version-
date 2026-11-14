/* Production F407 controller/status/link and F103 parser/controller, on a simulated wire. */
#define F103_PAIR_MAIN existing_pair_regression_main
#include "test_f103_f4_pair.c"

int main(int argc, char **argv)
{
    static const uint32_t diagnostics[] = {4U, 8U, 12U};
    static const int16_t targets[] = {0, 100, -100, 400, -400};
    unsigned d, mode, t, failures = 0U;
    (void)argv;
    setbuf(stdout, NULL);
    for (d = 0U; d < 3U; ++d) for (mode = 0U; mode < 4U; ++mode)
    for (t = 0U; t < 5U; ++t)
    {
        setup_pair(); acquire_sim();
        UpperControl_Intent(&controller, &sim, UPPER_INTENT_APPLY, targets[t], tick);
        run_for(12000U);
        telemetry.diagnostic = diagnostics[d];
        if (mode == 0U) UpperControl_Intent(&controller, &sim, UPPER_INTENT_RELEASE, 0, tick);
        else if (mode == 1U) UpperControl_Intent(&controller, &sim, UPPER_INTENT_QUICK_STOP, 0, tick);
        else if (mode == 2U) { k1 = true; run_for(10U); k1 = false; }
        else assert(app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_HMI, 2U, 600U, 99U, 0x10U));
        run_for(31000U);
        update_telemetry();
        printf("LCD_RELEASE diag=%lu mode=%u target=%d owner=%u life=%u phase=%u result=%u upper=%s err=%u\n",
            (unsigned long)diagnostics[d], mode, targets[t], telemetry.owner, telemetry.life,
            state.stop_phase, state.stop_result, UpperControl_StateName(controller.state), controller.detail);
        if (controller.state != SIM_STOPPED_RELEASED || controller.detail || telemetry.life != 1U ||
            telemetry.owner || state.authorized || state.motion_token || state.stop_phase != 4U || state.stop_result)
        { ++failures; continue; }
        assert(telemetry.diagnostic == diagnostics[d]);
        assert(feedback_axis[0].actual_speed_rpm == 0 && feedback_axis[1].actual_speed_rpm == 0);
        acquire_sim(); /* A retained LCD warning must not prevent a NEW explicit grant. */
        assert(controller.target_rpm == 0 && state.first_zero_done);
    }
    printf("LCD warning release/regrant: failures=%u / 60\n", failures);
    if(argc<=1) for(d=0U;d<3U;++d) for(mode=0U;mode<4U;++mode) for(t=0U;t<4U;++t) {
        setup_pair(); acquire_sim();
        UpperControl_Intent(&controller,&sim,UPPER_INTENT_APPLY,100,tick);run_for(5000U);
        telemetry.diagnostic=diagnostics[d];
        if(mode==0U)UpperControl_Intent(&controller,&sim,UPPER_INTENT_RELEASE,0,tick);
        else if(mode==1U)UpperControl_Intent(&controller,&sim,UPPER_INTENT_QUICK_STOP,0,tick);
        else if(mode==2U){k1=true;run_for(10U);k1=false;}
        else assert(app_control_arbiter_submit_global_stop(APP_CONTROL_OWNER_HMI,2U,600U,99U,0x10U));
        if(t==0U)telemetry.diagnostic|=16U;
        else if(t==1U)fresh_ok=false;
        else if(t==2U)app_control_arbiter_safety_takeover(APP_CONTROL_LOCK_SAFETY);
        else {feedback_axis[0].fault=1U;feedback_axis[0].fault_code=1U;}
        run_for(31000U);assert(controller.state!=SIM_STOPPED_RELEASED);
        UpperControl_Intent(&controller,&sim,UPPER_INTENT_AUTHORIZE,0,tick);run_for(1000U);
        assert(!controller.motion_token && !state.authorized);
    }
    if(argc<=1)puts("PASS LCD warnings plus real diagnostic/stale feedback/safety lock/servo fault: 48 cases still block release and reauthorization");
    return argc > 1 ? 0 : failures != 0U;
}
