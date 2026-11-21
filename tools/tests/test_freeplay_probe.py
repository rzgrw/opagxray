"""Freeplay probe: census parsing, owner picking and the recorded move loop, against a fake bridge."""
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


def ledger_line(serial, owner_id, reason, source, verdict, site, verb='-'):
    return (f'serial={serial} ownerId={owner_id} epoch=1 membershipRevision=1 wakeReason={reason} source={source} '
            f'verdict={verdict} verb={verb} site={site} provider=- model=- queueAgeMs=0 latencyMs=0\n')


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


if __name__ == '__main__':
    unittest.main()
