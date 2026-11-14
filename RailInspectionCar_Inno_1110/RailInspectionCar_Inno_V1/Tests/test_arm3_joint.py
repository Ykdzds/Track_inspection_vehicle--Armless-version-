"""Host-only cross-language ARM3 manager/executor integration.

The C process links the real F407 manager and codecs. Records come directly
from each production implementation; only the platform and USB driver are
deterministic fixtures. This does not open CAN, USB, serial or any device.
"""
import collections
from pathlib import Path
import struct
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'RK3588_ARM3_20261106'))
import arm3_wire as w
from arm3_runtime import Arm3Session

PEER = sys.argv.pop(1)
BASE = [0, 0, 9000, 0, 0, 12000]


class Clock:
    now = 10.

    def __call__(self):
        return self.now


def configuration():
    arms = {}
    for side in (1, 2):
        arms[str(side)] = dict(identity_verified=True, stop_verified=True,
            mapping_verified=True, startup_motion_verified=True,
            independent_stop_verified=True, diagnostic_policy_verified=True,
            identity_crc=1000 + side, limits=[list(x) for x in w.LIMITS],
            enabled_joint_mask=63, jog_stop_margin_cdeg=[100] * 6,
            presets={str(p): dict(angles=list(BASE), revision=1, confirmed=True,
                                 transport_verified=p == 3) for p in range(1, 5)})
    return dict(business_limit_mm_s=100, arm_config_version=1,
                safety_config_version=1, arms=arms, poses={})


class Driver:
    """Deterministic source-time feedback and asynchronous write acknowledgments."""
    def __init__(self, clock):
        self.clock = clock
        self.latest = {}
        self.pending = []
        self.calls = []
        self.angles = {side: list(BASE) for side in (1, 2)}
        self.jogs = {}
        self.seq = 0

    def refresh(self):
        self.seq += 1
        for side, job in self.jogs.items():
            self.angles[side][job['joint']] += 5 * job['target']
        self.latest = {side: dict(sequence=self.seq, at=self.clock(),
            angles=list(self.angles[side]), valid=True, fault=0,
            usb_epoch=1, identity_crc=1000 + side, span=0)
            for side in (1, 2)}

    def submit(self, job):
        self.calls.append(('submit', job['id'], job['opcode'], job['mask']))
        for side, status in job['sides'].items():
            if job['opcode'] in (1, 6):
                self.angles[side] = list(status['target'])
            if job['opcode'] == 4:
                self.jogs[side] = job
            elif job['opcode'] == 3:
                self.jogs.pop(side, None)
            self.pending.append(dict(id=job['id'], side=side,
                                     at=self.clock(), epoch=job['epoch']))

    def pulse(self, job):
        self.calls.append(('pulse', job['id']))

    def stop(self, mask, reason):
        self.calls.append(('stop', mask, reason))
        for side in (1, 2):
            if mask & (1 << (side - 1)):
                self.jogs.pop(side, None)

    def finish(self, job):
        self.calls.append(('finish', job['id']))
        for side in job['sides']:
            self.jogs.pop(side, None)

    def samples(self):
        return self.latest

    def events(self):
        events, self.pending = self.pending, []
        return events

    def save(self, job, store, side, angles):
        raise AssertionError('Persistence is outside this transport test')


class Joint:
    def __init__(self):
        self.clock = Clock()
        self.driver = Driver(self.clock)
        self.tx = collections.deque()
        self.session = Arm3Session(lambda *_: None, configuration(), self.clock, self.driver)
        self.session.link, self.session.lower, self.session.boot = 66, 100, 200
        self.session.ready = True
        self.session.record_sink = lambda kind, payload, token, priority: self.tx.append((kind, bytes(payload)))
        self.session.records[2] = (struct.pack('<IIIIIBBBBII', 100, 200,
            (1 << 20) | (1 << 26) | (1 << 27) | (1 << 31), 0, 1, 1, 0, 0, 3, 0, 0), self.clock(), 1)
        self.arm = self.session.arm3
        self.process = subprocess.Popen([PEER], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True, bufsize=1)
        self.snap = None
        self.cached = None
        self.trace = collections.deque(maxlen=80)
        self.received = []
        self.sent = []
        self.next_sequence = collections.defaultdict(int)
        self.hold = None
        self.pc_client = None
        self.pc_sequence = 0
        self.crc_commissioned = False
        self.session.capabilities()
        self.step(0)

    def close(self):
        if self.process.poll() is None:
            self.process.communicate('QUIT\n', timeout=5)

    def command(self, line):
        self.trace.append('C <- ' + line)
        self.process.stdin.write(line + '\n')
        self.process.stdin.flush()
        records, result, accepted = [], None, None
        while True:
            output = self.process.stdout.readline().strip()
            if not output:
                raise AssertionError('C peer terminated: ' + self.process.stderr.read())
            if output == 'END':
                break
            self.trace.append('C -> ' + output)
            fields = output.split()
            if fields[0] == 'TX':
                records.append((int(fields[1]), bytes.fromhex(fields[2])))
            elif fields[0] == 'SNAP':
                self.snap = dict(zip(('owner', 'state', 'permit', 'locked', 'negotiated',
                    'configured', 'active', 'generation', 'reason', 'version'), map(int, fields[1:])))
            elif fields[0] == 'RESULT':
                result = tuple(map(int, fields[1:]))
            elif fields[0] == 'RXOK':
                accepted = bool(int(fields[1]))
            elif fields[0] == 'CACHED':
                self.cached = None if fields[1] == 'NONE' else tuple(map(int, fields[1:]))
        return records, result, accepted

    def pump(self, outgoing=()):
        pending = collections.deque(outgoing)
        iterations = 0
        while pending or self.tx:
            iterations += 1
            if iterations > 256:
                raise AssertionError('Protocol feedback loop\n' + '\n'.join(self.trace))
            while pending:
                kind, payload = pending.popleft()
                self.sent.append((kind, payload))
                self.arm.receive(kind, payload, self.clock(), self.clock())
                # This fixture commissioning step approves the body produced by
                # the C platform. The first actual check must reject it; a later
                # real query must pass after this explicit CRC approval.
                if kind == 0x5F and payload[8] == 5 and payload[9] == 0 and not self.crc_commissioned:
                    if self.arm.safety_config_valid:
                        raise AssertionError('Unapproved safety body was accepted')
                    self.session.config['safety_config_payload_crc32'] = w.crc(payload[32:])
                    self.crc_commissioned = True
            if self.tx:
                kind, payload = self.tx.popleft()
                if kind in (2, 0x48):
                    continue  # Existing link/joint displays are outside manager.
                records, _, accepted = self.command('RX %d %s' % (kind, payload.hex()))
                self.received.append((kind, payload, accepted))
                pending.extend(records)

    def step(self, milliseconds=50, alive=False):
        self.clock.now = round(self.clock.now + milliseconds / 1000, 6)
        if alive and self.snap and self.snap['owner'] in (1, 3):
            source = self.snap['owner']
            self.intent(source, 2, held=bool(self.hold), **(self.hold or {}))
        elif alive and self.snap and self.snap['owner'] == 4:
            self.pc('alive')
        records, _, _ = self.command('STEP %d' % round(self.clock() * 1000))
        self.pump(records)
        self.driver.refresh()
        self.arm.tick()
        self.pump()

    def advance(self, milliseconds, alive=False):
        for _ in range(milliseconds // 50):
            self.step(alive=alive)

    def intent(self, source, command, mask=1, joint=0, direction=0,
               grade=1, preset=1, budget=2000, held=False, sequence=None):
        if sequence is None:
            self.next_sequence[source] += 1
            sequence = self.next_sequence[source]
        flags = 1 | (2 if held else 0)
        line = 'I %d %d %d %d %d %d %d %d %d %d' % (
            source, command, mask, joint, direction, grade, preset, budget, flags, sequence)
        records, result, _ = self.command(line)
        self.pump(records)
        return result

    def pc(self, action, **params):
        if self.pc_client is None:
            self.pc_client = self.arm.pc('hello', {})['client_session']
        self.pc_sequence += 1
        value = self.arm.pc(action, dict(params, client_session=self.pc_client,
                                        input_seq=self.pc_sequence))
        self.pump()
        return value

    def ready(self):
        self.advance(3500)
        if not (self.snap['negotiated'] and self.snap['configured'] and
                not self.snap['locked'] and self.arm.safety_config_valid):
            raise AssertionError('Startup did not close: %r, RK safety=%r\n%s' %
                (self.snap, self.arm.safety_config_valid, '\n'.join(self.trace)))

    def apply(self, source=1):
        status, _ = self.intent(source, 1)
        if status:
            raise AssertionError('Owner apply rejected: %d\n%s' % (status, '\n'.join(self.trace)))
        self.step()
        if self.snap['owner'] != source or self.snap['permit'] != 3:
            raise AssertionError('Owner apply failed: %r\n%s' % (self.snap, '\n'.join(self.trace)))

    def result_accepted(self, operation, result=1):
        return any(kind == 0x55 and accepted and w.u32(p) == operation and
                   p[9] == 2 and p[10] == result for kind, p, accepted in self.received)


class JointTests(unittest.TestCase):
    def setUp(self):
        self.j = Joint()
        self.addCleanup(self.j.close)
        self.j.ready()

    def test_manifest_configuration_dual_transport_proof_and_owner(self):
        j = self.j
        self.assertTrue(j.arm.negotiated)
        self.assertIsNotNone(j.arm.time.bounds)
        kinds = {(p[8], p[11]) for kind, p, ok in j.received if kind == 0x5F and ok}
        self.assertTrue({(3, 1), (3, 2), (4, 1), (4, 2), (8, 1), (8, 2)} <= kinds)
        j.apply()
        self.assertEqual(j.intent(3, 1)[0], 5)  # HMI cannot preempt remote.

    def test_preset_six_axis_light_and_stop_real_results(self):
        j = self.j
        j.apply()
        for command, mask in ((6, 3), (7, 1), (8, 2), (9, 2)):
            status, operation = j.intent(1, command, mask=mask)
            self.assertEqual(status, 0, (command, status, '\n'.join(j.trace)))
            j.advance(250, alive=True)
            self.assertTrue(j.result_accepted(operation), (command, operation, '\n'.join(j.trace)))
            self.assertEqual(j.snap['active'], 0)
        status, operation = j.intent(3, 4, mask=3)  # Non-owner stop is legal.
        self.assertEqual(status, 0)
        j.advance(900, alive=True)
        self.assertTrue(j.result_accepted(operation), '\n'.join(j.trace))

    def test_remote_jog_pulses_and_input_loss_stop(self):
        j = self.j
        j.apply()
        j.hold = dict(mask=1, joint=0, direction=1, grade=2)
        status, operation = j.intent(1, 5, held=True, **j.hold)
        self.assertEqual(status, 0)
        j.advance(450, alive=True)
        self.assertTrue(any(x[0] == 'pulse' and x[1] == operation for x in j.driver.calls))
        self.assertFalse(j.arm.jobs[operation]['terminal'])
        j.hold = None
        j.advance(1300)
        self.assertTrue(j.arm.jobs[operation]['terminal'])
        self.assertEqual(j.snap['owner'], 0)
        self.assertEqual(j.snap['permit'], 0)
        self.assertTrue(any(x[0] == 'submit' and x[2] == 3 for x in j.driver.calls))

    def test_owner_release_requires_fresh_dual_transport_proofs(self):
        j = self.j
        j.apply()
        old_generation = j.snap['generation']
        status, operation = j.intent(1, 3, mask=3)
        self.assertEqual(status, 0)
        self.assertTrue(j.snap['locked'])
        j.advance(1800)
        self.assertTrue(j.result_accepted(operation), '\n'.join(j.trace))
        self.assertEqual((j.snap['owner'], j.snap['locked'], j.snap['permit']), (0, 0, 0),
                         '\n'.join(j.trace))
        self.assertGreater(j.snap['generation'], old_generation)
        proof_sides = {p[11] for kind, p, ok in j.received if kind == 0x5F and ok and
                       p[8] == 8 and p[9] == 0 and w.u32(p, 32) == operation}
        self.assertEqual(proof_sides, {1, 2})

    def test_pc_owner_query_uses_same_f407_arbiter(self):
        j = self.j
        self.assertFalse(j.pc('apply')['granted'])
        j.step()
        self.assertEqual((j.snap['owner'], j.snap['permit']), (4, 3), '\n'.join(j.trace))
        self.assertEqual(j.intent(1, 1)[0], 5)
        response = j.pc('six_axis', mask=1, angles=BASE, budget_ms=2000)
        operation = response['operation_id']
        self.assertGreaterEqual(operation, 0x80000001)
        j.advance(250, alive=True)
        self.assertEqual(j.arm.jobs[operation]['result'], 1, '\n'.join(j.trace))
        self.assertTrue(j.arm.jobs[operation]['terminal'])
        j.pc('release')
        j.advance(1800)
        self.assertEqual((j.snap['owner'], j.snap['locked']), (0, 0), '\n'.join(j.trace))

    def test_old_wire_stop_replay_does_not_interrupt_new_jog(self):
        j = self.j
        j.apply()
        status, stop_id = j.intent(3, 4, mask=1)
        self.assertEqual(status, 0)
        raw_stop = next(p for kind, p in j.sent if kind == 0x54 and w.u32(p) == stop_id)
        j.advance(900, alive=True)
        self.assertTrue(j.result_accepted(stop_id))
        j.hold = dict(mask=1, joint=0, direction=1, grade=2)
        status, jog_id = j.intent(1, 5, held=True, **j.hold)
        self.assertEqual(status, 0, '\n'.join(j.trace))
        j.advance(100, alive=True)
        writes = list(j.driver.calls)
        j.arm.receive(0x54, raw_stop, j.clock(), j.clock())
        j.pump()
        self.assertEqual(j.driver.calls, writes)
        self.assertFalse(j.arm.jobs[jog_id]['terminal'])
        self.assertIs(j.arm.active[1], j.arm.jobs[jog_id])

    def test_f407_terminal_cache_cannot_be_replaced_by_late_progress(self):
        j = self.j
        j.apply()
        status, operation = j.intent(1, 6, mask=1)
        self.assertEqual(status, 0)
        j.advance(250, alive=True)
        j.command('GET %d 1' % operation)
        before = j.cached
        self.assertIsNotNone(before)
        self.assertEqual(before[2:4], (2, 1))
        old_progress = next(p for kind, p, ok in j.received if kind == 0x5F and ok and
                            p[8] == 2 and p[11] == 1 and w.u32(p, 32) == operation and p[40] == 1)
        records, _, _ = j.command('RX 95 ' + old_progress.hex())
        j.pump(records)
        j.command('GET %d 1' % operation)
        self.assertEqual(j.cached, before)

    def test_one_failed_side_of_dual_motion_stops_both(self):
        j = self.j
        j.apply()
        status, operation = j.intent(1, 6, mask=3)
        self.assertEqual(status, 0)
        j.driver.pending.append(dict(id=operation, side=2, error='Fixture USB failure'))
        j.step(alive=True)
        self.assertTrue(j.result_accepted(operation, result=12), '\n'.join(j.trace))
        self.assertTrue(any(x[0] == 'stop' and x[1] == 3 for x in j.driver.calls))


if __name__ == '__main__':
    unittest.main(verbosity=2)
