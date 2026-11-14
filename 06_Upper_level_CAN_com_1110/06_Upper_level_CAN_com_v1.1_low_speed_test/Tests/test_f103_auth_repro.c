#define main existing_unit_main
#include "test_f103_unit.c"
#undef main
int main(void)
{
    uint8_t result[8] = {0};
    Setup(1000U);
    protocol.capability_published = 0U;
    protocol.capability_active = protocol.capability_waiting = 1U;
    protocol.capability_attempts = 1U;
    protocol.capability_step = 9U;
    protocol.capability_sequence = 3U;
    Put16(result, 3U);
    Upper_TxResult(&protocol, 0x312U, result, 1U, 1001U);
    printf("REPRO first capability complete: published=%u expected=1\n", protocol.capability_published);
    fflush(stdout);
    assert(protocol.capability_published == 1U);
    puts("PASS first successful capability transport is retained");
    return 0;
}
