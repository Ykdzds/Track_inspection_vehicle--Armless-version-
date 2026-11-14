param([string]$Compiler = 'D:\Dev-C++软件--蓝桥杯比赛环境\Dev-Cpp\MinGW64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = Get-Content -LiteralPath (Join-Path $root 'APP/app_debug_cli.c') -Raw
$function = [regex]::Match($source, '(?s)static void app_cli_print_ramp\(void\).*?(?=\r?\nstatic bool app_cli_number)')
if (!$function.Success) { throw 'Missing production ramp command' }
$harness = @'
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "task.h"
#include "app_control_arbiter.h"
#include "app_normal_ramp.h"
#include "aimtor_servo.h"
static char output[2048];
static unsigned length, requests, snapshots;
static app_control_owner_t owner;
static int capture(const char *format, ...) {
    va_list args; int count;
    va_start(args, format); count = vsnprintf(output + length, sizeof(output) - length, format, args); va_end(args);
    assert(count > 0); length += count; assert(length < sizeof(output)); return count;
}
uint32_t HAL_GetTick(void) { return UINT32_MAX; }
app_control_owner_t app_control_arbiter_owner(void) { return owner; }
const char *app_cli_servo_control_owner(void) { return "hmi"; }
bool app_cli_servo_session_active(void) { return false; }
static void fill(app_normal_ramp_status_t *out) {
    memset(out, 0xFF, sizeof(*out)); requests++;
}
void app_hmi_control_ramp_status(app_normal_ramp_status_t *out) { assert(owner == APP_CONTROL_OWNER_HMI); fill(out); }
void app_remote_servo_ramp_status(app_normal_ramp_status_t *out) { assert(owner == APP_CONTROL_OWNER_REMOTE); fill(out); }
void app_can_control_ramp_status(app_normal_ramp_status_t *out) { assert(owner == APP_CONTROL_OWNER_CAN); fill(out); }
bool aimtor_get_snapshot_pair(servo_snapshot_t *l, servo_snapshot_t *r) {
    memset(l, 0xFF, sizeof(*l)); memset(r, 0xFF, sizeof(*r)); snapshots++; return true;
}
#define printf capture
'@
$main = @'
int main(void) {
    const app_control_owner_t owners[] = {APP_CONTROL_OWNER_NONE, APP_CONTROL_OWNER_HMI, APP_CONTROL_OWNER_REMOTE, APP_CONTROL_OWNER_CAN};
    unsigned i;
    for (i=0;i<4;i++) {
        owner=owners[i]; length=0; requests=snapshots=0;
        app_cli_print_ramp();
        assert(length < 1024 && snapshots == 1 && requests == (i ? 1U : 0U));
        assert(strstr(output,"source_authorized=") && strstr(output,"cli_authorized=") &&
            strstr(output,"batch_max=") && strstr(output,"R ack=") && strstr(output,"masks="));
    }
    puts("PASS production ramp status: bounded <1024 bytes, cached read only, correct source dispatch");
    return 0;
}
'@
$temporary = Join-Path $env:TEMP 'rail_ramp_diagnostic.c'
$binary = Join-Path $env:TEMP 'rail_ramp_diagnostic.exe'
[IO.File]::WriteAllText($temporary, $harness + "`n" + $function.Value + "`n" + $main)
Push-Location $root
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -ITests/mocks -IAPP -IAPP/HMI -IDevice $temporary -o $binary
    if ($LASTEXITCODE) { throw 'Ramp diagnostic compile failed' }
    & $binary
    if ($LASTEXITCODE) { throw 'Ramp diagnostic failed' }
} finally { Pop-Location }
