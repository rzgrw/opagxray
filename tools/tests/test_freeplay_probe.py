"""Freeplay probe: census parsing, owner picking, the recorded move loop and watch, against a fake bridge."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from tools import freeplay_probe as probe


# FormatCensus output for the Task 1 kTwoGroups snapshot.
TWO_GROUPS_CENSUS = (
    'npc 601 sim_default_stalker_1 owner=1 kind=squad binding=group:500 writer=sim level=l01_escape '
    'objective=move_to:esc_smart_b source=bootstrap rev=1\n'
    'npc 602 sim_default_stalker_0 owner=1 kind=squad binding=group:500 writer=sim level=l01_escape '
    'objective=move_to:esc_smart_b source=bootstrap rev=1\n'
    'npc 611 dog_weak owner=2 kind=lone_mutant binding=group:510 writer=sim level=l01_escape '
    'objective=hold:- source=none rev=0\n'
    'npc 700 esc_m_trader owner=3 kind=lone_stalker binding=npc:700 writer=none level=l01_escape '
    'objective=hold:- source=none rev=0\n'
    'summary npcs=4 owners=3 transferable=2 script_owned=0 unbound=1\n'
)

TWO_GROUPS_OWNERS = (
    'owner 1 name=esc_stalkers kind=squad binding=group:500 writer=sim level=l01_escape members=2 mrev=1 '
    'state=idle objective=move_to:esc_smart_b source=bootstrap rev=1 sites=-\n'
    'owner 2 name=esc_dogs kind=lone_mutant binding=group:510 writer=sim level=l01_escape members=1 mrev=1 '
    'state=idle objective=hold:- source=none rev=0 sites=-\n'
    'owner 3 name=esc_m_trader kind=lone_stalker binding=npc:700 writer=none level=l01_escape members=1 mrev=1 '
    'state=idle objective=hold:- source=none rev=0 sites=-\n'
)


def owner_line(owner_id, name, kind, writer, sites='-', state='idle', binding=None, mrev=1):
    binding = binding or f'group:{500 + owner_id}'
    return (f'owner {owner_id} name={name} kind={kind} binding={binding} writer={writer} level=l01_escape '
            f'members=2 mrev={mrev} state={state} objective=hold:- source=none rev=0 sites={sites}\n')


def ledger_line(serial, owner_id, reason, source, verdict, site, verb='-', queue=0, latency=0):
    return (f'serial={serial} ownerId={owner_id} epoch=1 membershipRevision=1 wakeReason={reason} source={source} '
            f'verdict={verdict} verb={verb} site={site} provider=- model=- queueAgeMs={queue} latencyMs={latency}\n')


# Two transferable owners (a squad and a pack) and a script-owned squad that `watch` must not require.
WATCH_OWNERS = (owner_line(3, 'stalker_sim_squad_novice1', 'squad', 'sim')
                + owner_line(5, 'simulation_mix_dogs1', 'pack', 'sim')
                + owner_line(8, 'esc_script_squad', 'squad', 'script'))


class WatchWorld:
    """A fake game for `watch`: the n-th ledger poll returns batches[n] (the last batch repeats)."""

    def __init__(self, owners, batches):
        self.owners = owners
        self.batches = batches
        self.log_polls = 0

    def __call__(self, bridge, verb, payload):
        if verb == 'ai.freeplay.owners':
            return True, self.owners
        if verb == 'ai.freeplay.log':
            batch = self.batches[min(self.log_polls, len(self.batches) - 1)]
            self.log_polls += 1
            return True, ''.join(batch)
        return False, f'unexpected verb {verb}'


class FakeClock:
    def __init__(self):
        self.now = 1000.0

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeBridge:
    """Answers bridge verbs from a handler; records every request."""

    def __init__(self, handler):
        self.handler = handler
        self.calls = []

    def request(self, verb, payload=''):
        self.calls.append((verb, payload))
        return self.handler(self, verb, payload)

    def count(self, verb):
        return sum(1 for v, _ in self.calls if v == verb)


class MoveWorld:
    """A fake game for `move`: owner 7 (group 507) gains sites on a given owners poll; events come after decide."""

    def __init__(self, sites_on_poll=1, event=None, leader_xyz=(10.0, 0.0, 10.0)):
        self.sites_on_poll = sites_on_poll
        self.event = event
        self.leader_xyz = leader_xyz
        self.owner_polls = 0          # owners polls after the wake
        self.woken = False
        self.decided = False

    def __call__(self, bridge, verb, payload):
        if verb == 'ai.freeplay.owners':
            if self.woken:
                self.owner_polls += 1
            ready = self.woken and self.owner_polls >= self.sites_on_poll
            sites = 'esc_smart_terrain_2_12,esc_smart_terrain_3_16' if ready else '-'
            return True, owner_line(7, 'stalker_sim_squad_novice1', 'squad', 'sim', sites=sites,
                                    state='idle' if ready else 'queued')
        if verb == 'ai.freeplay.census':
            return True, ('npc 901 sim_default_stalker_1 owner=7 kind=squad binding=group:507 writer=sim '
                          'level=l01_escape objective=hold:- source=none rev=0\n'
                          'npc 902 sim_default_stalker_0 owner=7 kind=squad binding=group:507 writer=sim '
                          'level=l01_escape objective=hold:- source=none rev=0\n'
                          'summary npcs=2 owners=1 transferable=1 script_owned=0 unbound=0\n')
        if verb == 'ai.freeplay.wake':
            self.woken = True
            return True, 'queued'
        if verb == 'ai.freeplay.decide':
            self.decided = True
            return True, 'accepted move_to esc_smart_terrain_3_16'
        if verb == 'ai.freeplay.log':
            text = ledger_line(4, 7, 'arrived', 'event', 'event', 'esc_smart_terrain_2_12')   # an old arrival
            if self.decided:
                text += ledger_line(5, 7, 'spawn', 'recorded', 'accepted', 'esc_smart_terrain_3_16', 'move_to')
                if self.event:
                    text += ledger_line(6, 9, self.event, 'event', 'event', 'esc_smart_terrain_3_16')  # other owner
                    text += ledger_line(7, 7, self.event, 'event', 'event', 'esc_smart_terrain_3_16')
            return True, text
        if verb == 'lua':
            x, y, z = self.leader_xyz
            return True, (f'squad={x:.1f},{y:.1f},{z:.1f};smart=0.0,0.0,0.0;leader=901;online=false;'
                          f'target=esc_smart_terrain_3_16;action=0;m901={x:.1f},{y:.1f},{z:.1f};m902=300.0,0.0,0.0')
        return False, f'unexpected verb {verb}'


class FreeplayProbeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name) / 'out'

    def run_move(self, world, timeout=900):
        bridge = FakeBridge(world)
        clock = FakeClock()
        code = probe.move(bridge, 7, 'esc_smart_terrain_3_16', self.out, timeout=timeout,
                          sleep=clock.sleep, clock=clock.time)
        track = json.loads((self.out / 'track.json').read_text())
        return code, track, bridge

    def test_parse_census_fields_and_summary(self):
        census = probe.parse_census(TWO_GROUPS_CENSUS)
        self.assertEqual(4, len(census['npcs']))
        first = census['npcs'][0]
        self.assertEqual({'id': 601, 'section': 'sim_default_stalker_1', 'owner': 1, 'kind': 'squad',
                          'binding': 'group:500', 'writer': 'sim', 'level': 'l01_escape',
                          'objective': 'move_to:esc_smart_b', 'source': 'bootstrap', 'rev': 1}, first)
        self.assertEqual('npc:700', census['npcs'][3]['binding'])
        self.assertEqual({'npcs': 4, 'owners': 3, 'transferable': 2, 'script_owned': 0, 'unbound': 1},
                         census['summary'])

    def test_census_exits_1_when_empty(self):
        def empty(bridge, verb, payload):
            if verb == 'ai.freeplay.census':
                return True, 'summary npcs=0 owners=0 transferable=0 script_owned=0 unbound=0\n'
            return True, ''
        self.assertEqual(1, probe.census(FakeBridge(empty), 'l01_escape', self.out))
        self.assertTrue((self.out / 'census.txt').is_file())

        def ownerless(bridge, verb, payload):
            if verb == 'ai.freeplay.census':
                return True, 'npc 601 sim_default_stalker_1 kind=squad\nsummary npcs=1 owners=0 transferable=0 ' \
                             'script_owned=0 unbound=0\n'
            return True, ''
        self.assertEqual(1, probe.census(FakeBridge(ownerless), 'l01_escape', self.out))

        def good(bridge, verb, payload):
            return True, TWO_GROUPS_CENSUS if verb == 'ai.freeplay.census' else TWO_GROUPS_OWNERS
        bridge = FakeBridge(good)
        self.assertEqual(0, probe.census(bridge, 'l01_escape', self.out))
        self.assertIn(('ai.freeplay.census', 'l01_escape'), bridge.calls)
        data = json.loads((self.out / 'census.json').read_text())
        self.assertEqual(1, data['summary']['unbound'])
        self.assertEqual({'squad': 1, 'lone_mutant': 1, 'lone_stalker': 1}, data['owners_by_kind'])
        self.assertEqual({'sim': 2, 'none': 1}, data['owners_by_writer'])
        self.assertEqual(['esc_m_trader'], [n['section'] for n in data['unbound_npcs']])

    def test_pick_owners_one_per_kind_and_skips_script_owned(self):
        owners = probe.parse_owners(
            owner_line(1, 'esc_script_squad', 'squad', 'script')
            + owner_line(2, 'esc_companions', 'squad', 'companion')
            + owner_line(3, 'stalker_sim_squad_novice1', 'squad', 'sim', sites='a,b')
            + owner_line(4, 'stalker_sim_squad_novice2', 'squad', 'sim')
            + owner_line(5, 'simulation_mix_dogs1', 'pack', 'sim')
            + owner_line(6, 'trader', 'lone_stalker', 'none', binding='npc:700')
            + owner_line(8, 'simulation_boar1', 'lone_mutant', 'random'))
        self.assertEqual(['a', 'b'], owners[2]['sites'])
        self.assertEqual([], owners[3]['sites'])
        self.assertEqual('group:503', owners[2]['binding'])
        picked = probe.pick_owners(owners)
        self.assertEqual(3, picked['squad']['id'])
        self.assertEqual(5, picked['pack']['id'])
        self.assertIsNone(picked['lone_stalker'])   # npc-bound, writer none: not transferable
        self.assertIsNone(picked['lone_mutant'])    # writer random: script-owned

    def test_move_waits_for_sites_then_decides(self):
        world = MoveWorld(sites_on_poll=3, event='arrived')
        code, track, bridge = self.run_move(world)
        self.assertEqual(0, code)
        self.assertEqual(1, bridge.count('ai.freeplay.decide'))
        self.assertIn(('ai.freeplay.wake', '7 probe'), bridge.calls)
        self.assertIn(('ai.freeplay.decide', '7 move_to esc_smart_terrain_3_16'), bridge.calls)
        decide_at = bridge.calls.index(('ai.freeplay.decide', '7 move_to esc_smart_terrain_3_16'))
        wake_at = bridge.calls.index(('ai.freeplay.wake', '7 probe'))
        polls_before = sum(1 for v, _ in bridge.calls[wake_at:decide_at] if v == 'ai.freeplay.owners')
        self.assertEqual(3, polls_before)
        self.assertEqual('arrived', track['outcome'])
        self.assertEqual(7, track['owner'])
        self.assertEqual(507, track['group'])
        self.assertEqual('esc_smart_terrain_3_16', track['site'])
        self.assertEqual([901, 902], track['members'])
        self.assertTrue(track['samples'])
        self.assertEqual({'901': [10.0, 0.0, 10.0], '902': [300.0, 0.0, 0.0]}, track['samples'][0]['members'])
        self.assertAlmostEqual(14.142, track['final_distance_m'], places=2)
        self.assertEqual(7, track['event']['serial'])

    def test_move_reports_blocked_as_failure(self):
        code, track, bridge = self.run_move(MoveWorld(event='blocked'))
        self.assertEqual(1, code)
        self.assertEqual('blocked', track['outcome'])

    def test_move_times_out(self):
        code, track, bridge = self.run_move(MoveWorld(event=None), timeout=30)
        self.assertEqual(1, code)
        self.assertEqual('timeout', track['outcome'])
        self.assertEqual(1, bridge.count('ai.freeplay.decide'))
        self.assertGreaterEqual(len(track['samples']), 5)

    def test_move_far_arrival_fails(self):
        code, track, bridge = self.run_move(MoveWorld(event='arrived', leader_xyz=(400.0, 0.0, 0.0)))
        self.assertEqual(1, code)
        self.assertEqual('arrived', track['outcome'])
        self.assertFalse(track['leader_within_m'])

    def run_watch(self, world, minutes=1):
        bridge = FakeBridge(world)
        clock = FakeClock()
        with contextlib.redirect_stdout(io.StringIO()) as printed, contextlib.redirect_stderr(io.StringIO()):
            code = probe.watch(bridge, self.out, minutes=minutes, sleep=clock.sleep, clock=clock.time)
        summary = json.loads((self.out / 'summary.json').read_text())
        return code, summary, bridge, printed.getvalue()

    def test_watch_summarises_ledger_by_kind(self):
        world = WatchWorld(WATCH_OWNERS, [[
            ledger_line(1, 3, 'spawn', 'live', 'accepted', 'esc_a', 'move_to', queue=100, latency=900),
            ledger_line(2, 3, 'arrived', 'event', 'event', 'esc_a'),
            ledger_line(3, 3, 'arrived', 'live', 'accepted', 'esc_b', 'move_to', queue=300, latency=1500),
            ledger_line(4, 5, 'spawn', 'live', 'stale_membership', 'esc_c', 'move_to', queue=200, latency=1100),
            ledger_line(5, 5, 'spawn', 'hold', 'timeout', '-', queue=50, latency=2000),
        ]])
        code, summary, bridge, printed = self.run_watch(world)
        squad, pack = summary['kinds']['squad'], summary['kinds']['pack']
        self.assertEqual(2, squad['accepted_live'])
        self.assertEqual(1, squad['arrivals'])
        self.assertEqual({}, squad['rejections'])
        self.assertEqual({}, squad['holds'])
        self.assertEqual(0, pack['accepted_live'])
        self.assertEqual({'stale_membership': 1}, pack['rejections'])
        self.assertEqual({'timeout': 1}, pack['holds'])
        self.assertEqual({'median': 1200, 'max': 1500}, squad['latencyMs_live'])
        self.assertEqual({'median': None, 'max': None}, squad['latencyMs_hold'])
        self.assertEqual({'median': 200, 'max': 300}, squad['queueAgeMs'])
        self.assertEqual({'median': 1100, 'max': 1100}, pack['latencyMs_live'])   # the hold is not a reply
        self.assertEqual({'median': 2000, 'max': 2000}, pack['latencyMs_hold'])
        self.assertEqual({'median': 125, 'max': 200}, pack['queueAgeMs'])        # pooled: live and hold
        self.assertNotIn('latencyMs', squad)
        self.assertEqual(5, summary['lines'])
        self.assertEqual(1, summary['first_serial'])
        self.assertEqual(5, summary['last_serial'])
        self.assertTrue(summary['contiguous'])
        self.assertEqual({'squad': 1, 'pack': 1}, summary['transferable_by_kind'])
        self.assertIn(('ai.freeplay.log', '512'), bridge.calls)
        self.assertIn('squad', printed)

    def test_watch_requires_live_decision_and_arrival_per_kind(self):
        squad_ok = [ledger_line(1, 3, 'spawn', 'live', 'accepted', 'esc_a', 'move_to'),
                    ledger_line(2, 3, 'arrived', 'event', 'event', 'esc_a')]
        pack_decided = [ledger_line(3, 5, 'spawn', 'live', 'accepted', 'esc_b', 'move_to')]
        code, summary, _, _ = self.run_watch(WatchWorld(WATCH_OWNERS, [squad_ok + pack_decided]))
        self.assertEqual(1, code)
        self.assertEqual(['squad'], summary['kinds_proven'])
        self.assertEqual(['pack'], summary['kinds_missing'])
        self.assertFalse(summary['passed'])

        pack_arrived = pack_decided + [ledger_line(4, 5, 'arrived', 'event', 'event', 'esc_b')]
        self.out = Path(self.temp.name) / 'out2'
        code, summary, _, _ = self.run_watch(WatchWorld(WATCH_OWNERS, [squad_ok + pack_arrived]))
        self.assertEqual(0, code)
        self.assertEqual(['pack', 'squad'], summary['kinds_proven'])
        self.assertTrue(summary['passed'])

    def test_watch_deduplicates_ledger_lines_by_serial(self):
        first = [ledger_line(1, 3, 'spawn', 'live', 'accepted', 'esc_a', 'move_to')]
        second = first + [ledger_line(2, 3, 'arrived', 'event', 'event', 'esc_a')]
        code, summary, bridge, _ = self.run_watch(WatchWorld(WATCH_OWNERS, [first, second, second]))
        ledger = (self.out / 'ledger.txt').read_text().splitlines()
        self.assertEqual(['serial=1', 'serial=2'], [l.split()[0] for l in ledger])
        self.assertEqual(2, summary['lines'])
        self.assertEqual(1, summary['kinds']['squad']['accepted_live'])
        self.assertEqual(1, summary['kinds']['squad']['arrivals'])
        snapshots = (self.out / 'owners.jsonl').read_text().splitlines()
        self.assertEqual(bridge.count('ai.freeplay.owners'), len(snapshots))
        self.assertGreaterEqual(len(snapshots), 6)      # one per 10 s over a minute
        self.assertEqual([3, 5, 8], [o['id'] for o in json.loads(snapshots[0])['owners']])
        self.assertEqual({'squad': 1, 'pack': 1}, summary['transferable_by_kind'])   # 8 is script-owned

    def test_watch_reports_serial_gap(self):
        # The ledger ring rolled past serials 3..4 between polls.
        lines = [[ledger_line(1, 3, 'spawn', 'live', 'accepted', 'esc_a', 'move_to'),
                  ledger_line(2, 3, 'arrived', 'event', 'event', 'esc_a')],
                 [ledger_line(5, 5, 'spawn', 'hold', 'timeout', '-')]]
        code, summary, _, _ = self.run_watch(WatchWorld(WATCH_OWNERS, lines))
        self.assertFalse(summary['contiguous'])
        self.assertEqual([[3, 4]], summary['serial_gaps'])
        self.assertEqual(3, summary['lines'])

    def test_watch_writes_summary_when_the_game_goes_away(self):
        proven = [ledger_line(1, 3, 'spawn', 'live', 'accepted', 'esc_a', 'move_to'),
                  ledger_line(2, 3, 'arrived', 'event', 'event', 'esc_a'),
                  ledger_line(3, 5, 'spawn', 'live', 'accepted', 'esc_b', 'move_to'),
                  ledger_line(4, 5, 'arrived', 'event', 'event', 'esc_b')]
        world = WatchWorld(WATCH_OWNERS, [proven])

        def dying(bridge, verb, payload):
            if verb == 'ai.freeplay.log' and world.log_polls >= 2:
                raise ConnectionError('bridge closed the connection')
            return world(bridge, verb, payload)
        code, summary, bridge, _ = self.run_watch(dying, minutes=5)
        self.assertEqual(1, code)                      # a lost game never passes, even with every kind proven
        self.assertTrue(summary['bridge_lost'])
        self.assertEqual(['pack', 'squad'], summary['kinds_proven'])
        self.assertEqual(4, summary['lines'])
        self.assertEqual(3, bridge.count('ai.freeplay.log'))   # stops polling after the failure

    def test_watch_counts_observation_timeouts(self):
        # Engine shape (xrSimFreeplayBridge.cpp TakeTimedOutObservations): source unset -> `-`,
        # verdict=observation_timeout, wakeReason = the wake's reasons.
        lines = [
            'serial=1 ownerId=3 queueAgeMs=40 latencyMs=5000 epoch=1 membershipRevision=1 wakeReason=spawn '
            'source=- verdict=observation_timeout verb=- site=- provider=- model=-\n',
            'serial=2 ownerId=5 queueAgeMs=10 latencyMs=5000 epoch=1 membershipRevision=2 '
            'wakeReason=membership,arrived source=- verdict=observation_timeout verb=- site=- provider=- model=-\n',
            ledger_line(3, 5, 'spawn', 'hold', 'timeout', '-'),
        ]
        code, summary, _, _ = self.run_watch(WatchWorld(WATCH_OWNERS, [lines]))
        self.assertEqual(1, summary['kinds']['squad']['observation_timeouts'])
        self.assertEqual(1, summary['kinds']['pack']['observation_timeouts'])
        self.assertEqual(0, summary['kinds']['pack']['arrivals'])       # a wake reason, not an arrived event
        self.assertEqual({'timeout': 1}, summary['kinds']['pack']['holds'])

    def test_watch_counts_only_accepted_live_and_later_arrivals(self):
        lines = [
            ledger_line(1, 3, 'arrived', 'event', 'event', 'esc_a'),                 # before the decision
            ledger_line(2, 3, 'spawn', 'live', 'accepted', 'esc_a', 'move_to'),
            ledger_line(3, 5, 'spawn', 'live', 'kept', '-', 'keep'),
            ledger_line(4, 5, 'arrived', 'event', 'event', 'esc_b'),
            ledger_line(5, 5, 'spawn', 'live', 'constrained', 'esc_b', 'move_to'),
            ledger_line(6, 5, 'arrived', 'event', 'event', 'esc_b'),
            ledger_line(7, 5, 'spawn', 'recorded', 'accepted', 'esc_b', 'move_to'),
            ledger_line(8, 5, 'arrived', 'event', 'event', 'esc_b'),
        ]
        code, summary, _, _ = self.run_watch(WatchWorld(WATCH_OWNERS, [lines]))
        self.assertEqual(1, code)
        self.assertEqual([], summary['kinds_proven'])
        self.assertEqual(['pack', 'squad'], summary['kinds_missing'])
        self.assertEqual(0, summary['kinds']['pack']['accepted_live'])
        self.assertEqual(1, summary['kinds']['pack']['kept_live'])
        self.assertEqual({'constrained': 1}, summary['kinds']['pack']['rejections'])
        self.assertEqual(1, summary['kinds']['squad']['accepted_live'])


if __name__ == '__main__':
    unittest.main()
