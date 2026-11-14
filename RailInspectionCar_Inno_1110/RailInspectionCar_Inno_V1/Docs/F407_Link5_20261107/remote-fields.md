# USART1 remote and synchronization diagnostic field map

Switches, both default `0` in `APP/app_config.h`:

- `APP_NRF_DIAGNOSTIC_ENABLED`: RF receive, publication, control consumption, batches, stop causes, driver health and telemetry delivery stages. Source `RF` / `APP_LINK_REMOTE` = 1.
- `APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED`: compact synchronization and position projections. Source `SYNC` / `APP_LINK_SYNC` = 6. This is optional curve evidence, not a replacement for the RF switch.

Both use the shared USART1 logger, its 256-byte formatting buffer, first-record slots and total 6000-byte/s budget. They add no hardware query, packet buffer, task, queue, runtime allocation, command renewal or protection deadline change. The former large CLI RF/SYNC printing paths are replaced rather than run in parallel.

All printed numeric words and kinds are hexadecimal. Below `lo16/hi16` always means low and high halves of the same 32-bit word. RPM halves are signed two's-complement `int16_t`; standalone signed values use the full 32-bit representation. Left is the low half and right is the high half of RPM pairs. Axis masks use bit 0 = left, bit 1 = right.

## Time and validity

`LD <at> <boot> <serial>` and RF `sample=` use `HAL_GetTick()` milliseconds. The event `at` is the time of the software hook, not the radio's physical receive instant. Radio/input ages and task ages come from unsigned FreeRTOS tick differences converted using `configTICK_RATE_HZ`; no HAL absolute timestamp is subtracted from an RTOS absolute timestamp.

The control task copies only seven words of feedback evidence from its existing servo snapshots: publication HAL ms, two speed sequences, two ages, packed actual RPM and flags. RF/SYNC projection ages add the unsigned HAL elapsed time since that publication, saturating at `UINT32_MAX`; the published raw speed and validity bits remain unchanged. Thus, a retained `valid` bit is **valid at publication**, not a promise that the sample is still fresh. Always check age, publication elapsed time and current owner. Early returns while another owner controls motion, disabled state or ESTOP can intentionally leave this projection old. The RS485 diagnostic is the all-owner transport view.

Before publication, the production control path clears unselected axes: zero value/sequence, valid/online/enabled = 0 and age = `UINT32_MAX`. A displayed zero with invalid flags is not evidence that a drive replied zero.

Periodic output can split a projection into `part=0` (words 0–11) and `part=1` (12–23). Each line carries its own `sample=`; periodic parts are observations, not an immutable transaction. A `FIRST` record is immutable across its two 8-word pages until both pages have been accepted by the serial transmitter. Serial acceptance does not prove that a PC saved the log.

## RF STATE, phase 0 (24 words)

| Word | Meaning |
| --- | --- |
| 0 | Current motion owner |
| 1 | Remote state bits 0–7; authorized bit 8; controlled stop bit 9; ESTOP latch bit 10; link valid bit 11 |
| 2 | Frame sequence bits 0–7; accepted event sequence bits 8–23 |
| 3–5 | Cumulative valid frames; current input age ms; radio recovery generation |
| 6 | Left vertical joystick lo16 / speed mode hi16 (0 joystick, 1 keys) |
| 7–8 | Common target RPM; key-mode target centi-km/h |
| 9–10 | Commanded RPM pair; observed feedback RPM pair |
| 11 | Left/right speed-valid bits 0/1; online bits 2/3; software enabled bits 4/5; selected axis mask bits 16–23 |
| 12–13 | Left and right speed sample sequences |
| 14–15 | Left and right speed sample age ms |
| 16–18 | Authorization generation; batch ID; batch generation |
| 19 | Required axis mask bits 0–7; success mask 8–15; written mask 16–23; active batch 24–31 |
| 20 | Current batch age ms, or 0 if inactive |
| 21 | Controlled stop state bits 0–7; controlled stop reason 8–15; retained stop reason 16–23 |
| 22 | Communication state bits 0–7; consecutive failures saturated to 255 in 8–15; speed submission failure count in 16–31 |
| 23 | HAL elapsed ms since the last feedback projection publication |

Remote states: 0 disabled, 1 arming, 2 armed, 3 running, 4 ESTOP, 5 stopping. Communication: 0 OK, 1 degraded, 2 failed. Stop reason: 0 none, 1 physical K5 (historic enum name K1), 2 RF link, 3 manual, 4 arming, 5 prepare, 6 enable, 7 servo state, 8 transaction, 9 speed submit, A synchronization error, B reverse timeout, C CAN/PB10.

## RF HEALTH phases 1–5

Phase 1 (24 words):

| Word | Meaning |
| --- | --- |
| 0–5 | Valid frames; estimated dropped frames; CRC errors; format errors; duplicate events; missed event batches |
| 6–11 | Link-loss count; recovery generation; maximum valid-frame gap ms; receive-service overrun count; current RF age ms; receive-service age ms |
| 12–17 | Driver SPI errors; FIFO errors; recovery attempts; DMA starts; DMA completions; DMA timeouts |
| 18–23 | Driver RX drops; invalid payload widths; configuration mismatches; last failure code; verify failures; RX packets |

Phase 2 (24 words) separates ACK preparation stages:

| Word | Meaning |
| --- | --- |
| 0–6 | ACK queued; loaded into NRF; replaced; flushed; failures; load timeouts; maximum ACK wait ms |
| 7–8 | Current pending software ACK age ms; current hardware FIFO ACK age ms, otherwise 0 |
| 9 | Software pending bit 0; hardware FIFO bit 1; last clear reason bits 8–15; ACK supported bit 16; SPI active bit 17; radio ready bit 18; initialized bit 19 |
| 10–15 | ACK clear count; MAX_RT clears; timeout clears; TX_FULL clears; recovery clears; driver event overwrites |
| 16–17 | Telemetry generated; telemetry queue failures |
| 18 | Telemetry session bits 0–7; last sequence 8–15; last type 16–23; alarm code 24–31 |
| 19–23 | Telemetry enabled; alarm-event priorities; fast-burst limits; fast-urgent priorities; telemetry compile-time enable |

`queued` is software queue acceptance and `loaded` is loading into the NRF FIFO. Neither proves reception or display by the remote handset. The NRF hardware automatic ACK does not provide an application-level confirmation for these payloads; the field map intentionally makes no such claim.

Phase 3 (16 words) is the driver's cached failure evidence: 0 failure HAL ms, 1 elapsed ms, 2–4 SPI CR1/CR2/SR, 5–6 RX/TX DMA remaining, 7–8 RX/TX DMA CR, 9–10 RX/TX flags, 11–12 RX/TX error flags, 13 valid/state/command/length in successive bytes, 14 active/done/error in successive bytes, 15 NRF config/status/FIFO-status/feature in successive bytes. This is the driver's last failure snapshot, not its own immutable history; new first control-stop evidence is separately retained by `FIRST`.

Phases 4 and 5 (20 words each): five type groups, each containing `generated, queued, maximum successful queue interval ms, overdue count`. Phase 4 covers types `0x81–0x85`; phase 5 covers `0x86–0x8A`. These counters come from the existing telemetry scheduler and do not alter scheduling or make physical-delivery claims. The first successful queue interval is recorded as 0.

## RF EVENT kinds (six words a–f)

| Kind | Hook | Words a–f |
| --- | --- | --- |
| 01 | Accepted RX | valid-frame counter; event-sequence bits 8–23/frame-sequence bits 0–7; recovery generation; left vertical; short-press lo16/long-press hi16; double-click lo16/new-event bit 16 |
| 02 | Parse rejection | parse result; CRC errors; format errors; valid-frame counter; recovery generation; RF age ms |
| 03 | Link down | RF age ms; link-loss count; valid-frame counter; recovery generation; 0=age expiry or 1=radio not ready; last frame sequence (expiry) or driver last failure (not ready) |
| 04 | Recovery generation changes | old generation; new generation; driver last failure; SPI errors; DMA timeouts; radio ready |
| 10 | Publish input | valid-frame counter; accepted event/frame sequences; recovery generation; received age ms; cumulative K5 short count; cumulative K5 long count |
| 11 | Consume input | valid-frame counter; accepted event/frame sequences; recovery generation; effective input age ms; publish-to-consume ms; remote state |
| 12 | State changes | before; after; owner; authorization generation; batch; last result. Before/after pack state/authorized/controlled-stop/communication in successive bytes |
| 20 | Batch starts | batch; generation; required mask; RPM command pair; 0=running or 1=controlled stop; valid-frame counter (running) or stop elapsed ms |
| 21 | Axis ACK accepted | batch; generation; axis; pending speed sequence; acknowledged RPM; batch elapsed ms |
| 22 | Batch completes | batch; generation; success mask; elapsed ms; two-axis response gap ms; gap valid |
| 23 | Axis write fails | batch; generation; axis; pending speed sequence; speed write result; submission failure count |
| 30 | Controlled stop begins | reason; deceleration mm/s²; authorization generation; batch; valid frames; computed stop timeout ms |
| 31 | Running stop upgraded | old reason; new reason; old/new deceleration mm/s²; stop generation; batch |
| 32 | Stop completes | reason; stop generation; first-command ms; zero ms; completion ms; zero-written mask |
| 33 | Immediate ESTOP | reason; state before revocation; controlled stop state; failed-axis mask; authorization generation; batch |

Arming cancellation uses kinds 30/31 with a distinct arming layout: `reason, requested deceleration mm/s², state (=1), authorization generation, batch, valid frames`. Interpret these with the state transition evidence; this is not the running-stop-upgrade layout.

Ordinary RX/publication/consumption hooks sample every 16 accepted frames; new key event batches also emit RX/publication records and K5 count changes emit consumption records. `valid_frames` is the software correlation key across those hooks; the 8-bit wire frame sequence wraps. Event-pool congestion can drop ordinary records; inspect common queue/merged/drop counters. The comment that a new key batch is “retained” means the hook is emitted without sampling, not an unbounded lossless packet log.

## First stop, kind 40 (16 words)

Captured before authorization/target clearing and before servo emergency actions invalidate enable/verification evidence:

| Word | Meaning |
| --- | --- |
| 0–1 | Initial reason; deceleration mm/s² (0 for immediate ESTOP) |
| 2 | State bits 0–7; authorized bit 8; hold-stopping bit 9; servo snapshot present bit 10; current owner bits 16–23 |
| 3–4 | Initial non-cleared target RPM; commanded RPM pair |
| 5–9 | Authorization generation; valid frames; event/frame sequences; last observed RF age ms; batch ID |
| 10–11 | Left and right pending speed sequences |
| 12–13 | Chosen failing/selected axis speed and fault ages, or `FFFFFFFF` without a snapshot |
| 14 | Last poll result lo16 / last poll register address hi16, or `FFFFFFFF` without a snapshot |
| 15 | Failed-axis mask bits 0–7; speed-valid bit 8; fault-valid bit 9; online bit 10; fault code bits 16–31 |

The first cause is captured once per stop attempt. Repeated K5 requests and escalation to immediate ESTOP do not rewrite it. New authorization or a later independent stop permits the next first-cause capture, while the shared pending record remains immutable until transmitted; overlapping new records increment `merged`. Kind 41 is reserved and not emitted by this implementation. The common arbiter's separate `FIRST` record supplies global stop event, requester, owner, command and later upgrade evidence, including K5 while HMI/CAN owns motion.

## SYNC projections

Phase 0 calls the compact remote projection with internal phase `0x80` (24 words): target RPM, common commanded RPM, commanded RPM pair, observed RPM pair, enabled/active/state in successive bytes, batch, authorization generation, left/right paired sample sequences, raw error, effective error, signed integer correction, pair-valid bit 0/reject-reason bits 8–15/feedback-valid bit 16/correction-valid bit 17, skew ms, left/right pairing ages, feedback flags (same as RF word 11 low bits), current left/right feedback ages, state/authorized bit 8/hold-stopping bit 9, batch generation, valid-pair count, rejected-pair count, feedback publication elapsed ms.

Phase 1 (16 words) projects the published position monitor: baseline generation, pair valid, sample skew ms, authorization generation; then six words per axis: signed raw position, valid, published age ms, sequence, signed relative count high32, signed relative count low32. Its `sample=` uses HAL milliseconds, consistently with the other projections. Published position ages are not recomputed by this getter; use position sequence and sampling times when assessing freshness. A common `id=` associates phase 0 and phase 1 as one logical diagnostic group; every printed line still has its own observation time and the group is not an atomic speed/position snapshot.

## Memory and verification

Remote-only additional static cache is 28 bytes feedback + 4 bytes consumed frame counter + 1 byte stop-freeze flag = 33 bytes of symbols (alignment may occupy up to 36 bytes). SYNC alone only needs the 28-byte feedback projection. Driver/telemetry diagnostic getters reuse existing statistics and add no static buffers. The shared logger's memory is accounted once in the final Keil map, not once per macro.

`Tests/run_remote_link_diag_tests.ps1` compiles production control, receiver, NRF driver and telemetry paths under diagnostic/SYNC combinations 00/10/01/11 and driver/telemetry diagnostic 0/1. Coverage includes the existing motion safety regression, 150ms link boundary, recovery sequence rebaseline, CRC rejection, RX→publish→consume correlation, duplicate consumption suppression, first link-stop cause followed by K5 upgrade, stop completion, immediate right-axis fault evidence, current feedback ages, compact projections without control/driver/scheduler changes, ACK queued versus loaded, and all ten telemetry type counters.

Host regressions do not replace final Keil RAM/stack measurements, handset delivery observation or hardware stress testing. On hardware preserve pre-boot logs and exercise low speed, the previous failure workload, K5, link loss/recovery, multi-owner control and serial congestion while verifying all HMI complete replies remain below 50ms.
