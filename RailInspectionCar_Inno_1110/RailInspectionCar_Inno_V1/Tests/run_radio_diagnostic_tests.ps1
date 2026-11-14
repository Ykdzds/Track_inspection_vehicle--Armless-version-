param([string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = Get-Content (Join-Path $root 'APP/app_debug_cli.c') -Raw
$function = [regex]::Match($source, '(?s)static void app_cli_radio_diagnostic_service\(void\).*?(?=\r?\nvoid app_cli_process\()')
if (!$function.Success) { throw '未找到生产诊断服务' }
$harness = @'
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "NRF24L01.h"
#include "nrf_status_telemetry.h"
#include "app_nrf_remote.h"
#include "debug_uart.h"
static uint32_t now, calls, snapshots, records;
static uint8_t busy;
uint32_t HAL_GetTick(void) { return now; }
void NRF24L01_GetStats(NRF24L01_Stats *value)
{ memset(value, 0, sizeof(*value)); snapshots++; }
void nrf_status_telemetry_get_stats(nrf_telemetry_stats_t *value)
{ memset(value, 0, sizeof(*value)); }
bool app_nrf_remote_get_snapshot(nrf_remote_snapshot_t *value)
{ memset(value, 0, sizeof(*value)); return true; }
bool NRF24L01_PopEvent(NRF24L01_Event *event)
{ memset(event, 0, sizeof(*event)); event->timestamp_ms = now; return true; }
uint8_t debug_uart_try_write(const uint8_t *data, uint16_t length, uint16_t reserve)
{
    assert(length > 0U && length <= 128U && strlen((const char *)data) == length);
    assert(reserve == 512U);
    calls++;
    if (busy) return 0U;
    records++;
    return 1U;
}
'@
$main = @'
int main(void)
{
    for (now = 0U; now < 6000U; now++) {
        busy = (uint8_t)(now >= 2000U && now < 4000U);
        app_cli_radio_diagnostic_service();
    }
#if APP_NRF_DIAGNOSTIC_ENABLED
    assert(snapshots == 6U && calls == 102U && records == 68U);
#else
    assert(calls == 0U && snapshots == 0U);
#endif
    puts("PASS F407 diagnostic: production service, one snapshot/second, bounded lines, busy drops, switch");
    return 0;
}
'@
$testSource = Join-Path $env:TEMP 'rxlost_radio_diagnostic.c'
[IO.File]::WriteAllText($testSource, $harness + "`n" + $function.Value + "`n" + $main)
Push-Location $root
try {
    foreach ($enabled in @(0,1)) {
        $binary = Join-Path $env:TEMP "rxlost_radio_diagnostic_$enabled.exe"
        & $Compiler -std=c99 -Wall -Wextra -Werror "-DAPP_NRF_DIAGNOSTIC_ENABLED=$enabled" `
            -ITests/mocks -IAPP -IDevice -IBSP/NRF24L01 -IBSP/DEBUG_UART $testSource -o $binary
        if ($LASTEXITCODE -ne 0) { throw 'F407诊断编译失败' }
        & $binary
        if ($LASTEXITCODE -ne 0) { throw 'F407诊断回归失败' }
    }
} finally { Pop-Location }
