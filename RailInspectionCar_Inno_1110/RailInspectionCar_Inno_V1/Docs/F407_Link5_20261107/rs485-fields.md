# USART1 RS485 diagnostic field map

Switch: `APP_SERVO_RS485_DIAG_ENABLED`. Source: `APP_LINK_RS485` (4).
No polling requests, command renewal, retry, queue capacity or protection deadline is added.
All new absolute timestamps use HAL milliseconds on ARM. Durations include preemption.
All printed words are hexadecimal; signed RPM occupies two's-complement 16 bits.

## Transaction records

| Kind | Six words a–f |
| --- | --- |
| 1, enqueue | transaction, transport epoch, enqueue ms, motion owner:8/protocol owner:8/tag:16, slave:8/axis:8/address:16, function:8/quantity:8/value:16 |
| 3, wire timing | transaction, enqueue ms, TX start ms, actual UART TC ms, last DMA byte capture ms, validity |
| 4, validation | transaction, epoch, motion owner:8/attempt:8/status:16, dequeue ms, validation ms, validity |
| 5, consumer | transaction, consume ms, accepted:16/identity matched:16, axis:16/tag:16, current epoch, status |
| 6, cancellation | active transaction, new epoch, safety mode, current motion owner, cancel ms, zero |
| 7, cancellation/drop | transaction, request epoch, current epoch, ms, safety mode, 0=dequeued stale / 1=response queue full |
| 0x10, zero feedback | transaction, previous RPM:16/acknowledged command RPM:16, command valid:8/axis:8/state:16, current owner:8/protocol owner:8/tag:16, validity:8/raw length:8/address:16, epoch |
| 0x11, speed command | transaction, epoch, motion owner:8/safety channel:8/tag:16, axis, written RPM, enqueue ms |
| 0x20, requested speed accepted | speed-write sequence, raw requested RPM:16/first health-limited RPM:16, current owner:8/internal stop:8/axis:16, arbiter generation, previous acknowledged RPM, internal CAN:16/degraded active:16 |
| 0x21, speed write submitted | transaction, value before final health check:16/actual submitted RPM:16, current owner:8/CAN pending valid:8/axis:16, arbiter generation, CAN batch if valid (otherwise 0), speed-write sequence |
| 0x22, servo feedback/health change | owner:8/selected item:8/axis:16, previous flags, current flags, pending stop mask:8/enable failures:8/fault failures:8/speed failures:8, selected feedback transaction, selected sample elapsed age ms |
| 0x23, independent safety change | previous flags, current flags, full 32-bit safety failure, safety generation, pending safety transaction, parameter mask:8/zero mask:8/pending axis:8/pending step:8 |

Bits of validity: `0x01`=enqueue identity matched; `0x02`=TX DMA start accepted;
`0x04`=actual UART TC callback; `0x08`=DMA bytes captured; `0x10`=validation completed;
`0x20`=consumer identity matched and accepted; `0x40`=selected normal transaction.
Validation bit does not imply success: inspect status.
TC is set only by the real UART transmit-complete callback, never RX/DMA completion.
Raw capture may contain noise or a rejected frame; status, length and validity disambiguate it.

One ordinary read or nonzero FC06 speed write is selected per 200 ms, keeping its stages together.
Other writes, zero writes, failures, >=40 ms transactions and first anomaly records bypass this sampling.
If a previously unselected transaction fails, first-record timing/raw captures its full available metadata.
Queue descriptor sizes are unchanged. Two fixed latest-enqueue sidecars serve normal and safety paths.
Several outstanding requests may displace the enqueue stamp: validity bit 0 clears, owner becomes `FF`,
and `unknown` increments. A timestamp from another transaction is never presented as known.

Kinds `0x20` and `0x21` connect the driver request, health limiting and the accepted Modbus transaction
even when the remote/CAN diagnostic switches are off. Match axis and speed-write sequence, then use
the transaction to join wire/consumer records. `0x20` records the input before the first health clamp;
`0x21` records the queued step before the final health recheck and the exact submitted value.
Unchanged nonzero targets are paired-sampled by speed-write sequence (sequence mod 8 = 1);
zero and any raw/limited mismatch bypass this sampling. They describe successful acceptance only; a rejected call does not imply a command was transmitted.
CAN batch is meaningful only when its validity field is set. RPM halves are signed 16-bit values.

## Changes, kinds 0x22 and 0x23 (6 words each)

Kind `0x22` runs once per existing servo task iteration, after response consumption. It observes
cached scalars and the existing health calculation, including age-based STOP; no query or full
servo/health snapshot is created. Its timestamp is observer time, not wire-change time.
Item 0=speed, 1=fault, 2=enable. The selected item identifies the available cause; inspect its
transaction, counts and validity instead of interpreting an old transaction as a new failed request.
The final age is elapsed time from the stored sample tick; when that item's validity bit is clear,
it is **not** the age of a valid sample.

| Axis flags bits | Meaning |
| --- | --- |
| 0–15 | fault code |
| 16 / 17 | reported enable / software enabled |
| 18 / 19 / 20 | speed valid / fault valid / enable feedback valid |
| 21 / 22 / 23 | online / stop latched / at least one feedback failure |
| 24–25 | existing health level: normal=0, degraded=1, STOP=2 |
| 26 / 27 | controlled safety stop active / offline |
| 31 | observer baseline present |

Only changes emit an EVENT. A new valid fault, actual feedback failures, unexpected enable loss,
established validity loss, rising health severity or controlled safety-stop entry also attempts
`FIRST kind=22` through the existing immutable first-record slot. Initial unknown feedback and
normal startup enable/validity establishment emit only EVENT, preserving the slot for a failure.
Recovery changes remain visible without repeatedly freezing the same stable state.

Kind `0x23` observes independent safety status once per existing watchdog service cycle. Its flags
contain phase in bits0–7, failed task in bits8–15, boot lock in bit16 and baseline present in bit31.
Its full failure word is compared without truncation or hashing. Phase 0=normal checks, 1=task-loss
stop, 2=silent fallback, 3=stopped awaiting reset, 4=silent startup, 5=watchdog recovery, 6=read-only
fault hold. Abnormal phases and failure changes attempt FIRST; a normal initial state does not.
The handler's failed task, generation and pending transaction remain distinguishable from the
current motion owner. Stable state produces no repeated event. The common logger retains an
earlier pending first record and counts merged later anomalies.

## First submit rejection, kind 0x14 (6 words)

1. transaction, or `FFFFFFFF` for a null request;
2. current transport cancellation epoch;
3. returned Modbus status:16 / request tag:16;
4. unknown actual motion source `FF`:8 / protocol owner:8 / axis:8 / request metadata valid:8;
5. slave:8 / function:8 / address:16;
6. quantity:16 / requested value:16.

The common frozen-record header supplies the refusal time. No enqueue time or accepted motion owner
exists for a refused request. Null requests have metadata-valid=0 and unknown identity fields.
Both ordinary and safety submissions freeze invalid arguments, unavailable transport and queue-full
rejections without changing their original return values. This record has no raw wire reply.

## Periodic phase 0: compact servo state (24 words)

Words 0–2: current motion owner, pending protocol transaction, cancelled:8/pending axis:8/tag:16.
Nine words per axis (left 3–11, right 12–20):

1. actual RPM:16 / acknowledged written RPM:16;
2. valid speed sequence;
3. speed sample age ms;
4. requested write sequence;
5. completed write sequence;
6. existing `speed_write_tick` **RTOS uptime ms** (not a new HAL timestamp);
7. write result:8 / acknowledged command valid:8 / speed valid:8 / stop latched:8;
8. status word:16 / fault code:16;
9. enable feedback valid:8 / enable feedback:8 / online:8 / software enabled:8.

Words 21–23: first queued step value left:16/right:16; action left:8/right:8/busy left:8/right:8;
consecutive failures left:16/right:16. Word 21 is a speed request only when the corresponding action is
`ACTION_SPEED=5` and busy is set; otherwise it is stale/non-speed sequence storage.
The getter reads existing narrow fields in a short critical section, never two full servo snapshots.

## Periodic phase 1: bus and Modbus health (24 words)

0 RX bytes; 1 TX bytes; 2 RX overflow; 3 UART errors; 4 TX timeouts; 5 RX restarts;
6 DMA start failures; 7 RX abort failures; 8 UART error flags; 9 HAL error flags;
10 UART SR; 11 CR1; 12 CR3; 13 RX DMA CR; 14 DMA remaining:16/position:16;
15 DMA running:8/recovery pending:8/last position:16; 16 requests; 17 successful responses;
18 timeouts; 19 retries; 20 CRC errors; 21 frame errors; 22 exceptions; 23 request queue overflows.

## Periodic phase 2 and first failure

Words 0–15 are shared with first failure kind `0x12` and first >=40 ms transaction kind `0x13`:

| Word | Field |
| --- | --- |
| 0–1 | transaction, transport epoch |
| 2–8 | enqueue, dequeue, TX start, UART TC, RX capture, validation, consumer ms |
| 9 | tag:16 / address:16 |
| 10 | quantity:16 / written value:16 |
| 11 | actual motion owner at enqueue:8 / protocol owner:8 / axis:8 / slave:8 |
| 12 | function:8 / status:8 / attempt:8 / validity:8 |
| 13 | raw prefix length, maximum 8 bytes |
| 14–15 | raw prefix bytes in ARM little-endian word storage |

Phase 2 also has: 16 unknown enqueue count, 17 late/unmatched/rejected consumer count,
18 response queue drop count, 19 current transport epoch, 20 safety mode.
Only one active transaction is retained; cancellation/identity mismatches are explicit.
These periodic last values are not a history buffer.

## First zero feedback, kind 0x10 (16 words)

0 transaction; 1 observed HAL ms; 2 epoch; 3 previous RPM:16/acknowledged command RPM:16;
4 command valid:8/axis:8/state:16; 5 current owner:8/protocol owner:8/tag:16;
6–12 enqueue/dequeue/TX start/TC/RX/validation/consumer ms;
13 validity:8/raw length:8/address:16; 14–15 raw prefix.
For ordinary/cold/watchdog servo responses, state is software enabled:8/stop latched:8;
for independent safety responses it is the safety phase. Protocol owner `FE` identifies safety.
The trigger requires a successful FC03 one-register zero reply and either previous nonzero speed
or a valid nonzero acknowledged speed command. Invalid replies never become zero samples.
Normal, cold-start, watchdog and independent safety paths call this before updating their caches.
Seven bytes of a valid single-register reply include the entire CRC-bearing wire response.

An unmatched transaction sets timing/raw unavailable instead of borrowing another transaction.
The shared first-record service retains the first pending record until all output pages are accepted;
repeated anomalies merge into its counters. A later normal update does not overwrite pending evidence.

## Budget and validation

New static state: protocol 112 bytes, wire timestamps 16 bytes, per-axis observer flags 8 bytes,
and independent safety flags/full failure 8 bytes; total **144 bytes**.
No dynamic allocation, extra payload, descriptor, serial buffer or task is introduced.
The common logger owns first-record storage and formatting.

`Tests/run_rs485_diag_tests.ps1` links production Modbus and production IRQ/RS485 driver in separate
fixtures. It covers owner identity, cancellation, safety, displaced enqueue metadata, raw CRC/zero
evidence, whole-transaction sampling, slow transaction across tick wrap, timeout without stale RX,
real TC, missing TC and RX DMA separation. Its third fixture enables the macro while running the
existing production AIMtor core tests and checks the narrow getter creates no extra query.
It additionally checks queue-full/null-request rejection evidence and a production health clamp
from requested 200 RPM to 100 RPM, with acceptance/write-sequence and arbiter-generation evidence.
The driver observer fixture covers fault/enable changes, validity loss, first feedback degradation,
recovery, age-based STOP, no repeated stable event and pending first preservation. A fourth fixture
links production independent health/safety code and runs its full existing safety regressions with
diagnostics enabled, then checks normal baseline, phase/task-loss, full failure, no repetition and
first-record preservation.
`run_current_core_tests.ps1` and `run_current_watchdog_tests.ps1` passed with diagnostics disabled.
Hardware timing, servo behavior and the final all-enabled map remain release acceptance checks.
