/* Retain the hardware/RTOS fixture, but execute only the new read-only path. */
#define main baseline_driver_main
#include "test_core.c"
#undef main
#include "app_remote_servo.h"
extern void aimtor_test_position_projection_seed(uint32_t now, unsigned variant);
extern void aimtor_test_pulse_disabled_entries(void);
/* The inherited, unexecuted pulse regression is linked by MinGW; the P0
 * projection test must never visit its interrupt fixture. */
void TIM1_BRK_TIM9_IRQHandler(void) { assert(!"pulse IRQ in position-only test"); }
void TIM1_TRG_COM_TIM11_IRQHandler(void) { assert(!"pulse IRQ in position-only test"); }

int main(void)
{
    servo_snapshot_t full[2];
    servo_position_monitor_axis_t narrow[2];
    unsigned variant, phase, axis;
    motion_init();
    assert(modbus_init() == MODBUS_OK);
    aimtor_init();
    assert(!aimtor_get_position_monitor_pair(NULL, &narrow[1]));
    assert(!aimtor_get_position_monitor_pair(&narrow[0], NULL));
    for (phase = 0U; phase < 2U; ++phase)
    {
        ticks = phase == 0U ? 10000U : 9U; /* Subtraction crosses tick wrap. */
        for (variant = 0U; variant < 4U; ++variant)
        {
            unsigned old_tx = tx_count, old_reads = read_count, old_writes = write_count;
            aimtor_test_position_projection_seed(ticks, variant);
            assert(aimtor_get_snapshot_pair(&full[0], &full[1]));
            memset(narrow, 0xA5, sizeof(narrow));
            assert(aimtor_get_position_monitor_pair(&narrow[0], &narrow[1]));
            for (axis = 0U; axis < 2U; ++axis)
            {
#define SAME(field) assert(narrow[axis].field == full[axis].field)
                SAME(position_age_ms); SAME(position_sequence); SAME(position_generation);
                SAME(speed_age_ms); SAME(actual_speed_rpm); SAME(online); SAME(fault);
                SAME(parameter_verified); SAME(control_mode); SAME(speed_valid);
                SAME(position_valid); SAME(busy);
#undef SAME
                assert(memcmp(narrow[axis].position_samples, full[axis].position_samples,
                              sizeof(narrow[axis].position_samples)) == 0);
            }
            assert(tx_count == old_tx && read_count == old_reads && write_count == old_writes);
        }
    }
    assert(sizeof(narrow) + sizeof(remote_position_gate_t) <= 240U);
    {
        unsigned old_tx = tx_count, old_reads = read_count, old_writes = write_count;
        aimtor_test_pulse_disabled_entries();
        assert(tx_count == old_tx && read_count == old_reads && write_count == old_writes);
    }
    puts("PASS production narrow position getter: selected/unselected, invalid/stale, signed, wrap, no I/O");
    return 0;
}
