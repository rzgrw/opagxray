#!/usr/bin/env python3
"""Freeplay population probe: census and one recorded move, over the agent bridge.

  freeplay_probe.py census <endpoint> [--level <level>] --out <dir>
  freeplay_probe.py move <endpoint> --owner <owner_id> --site <smart> --out <dir> [--timeout 900]

census writes census.txt, owners.txt and census.json, prints the summary and the owner count per
kind and per writer, and exits 1 when the census is empty or an npc line has no owner.

move sends `ai.freeplay.wake <owner> probe`, waits until `ai.freeplay.owners` shows the owner idle
with a stored observation (non-empty sites=), sends `ai.freeplay.decide <owner> move_to <site>`,
then every 5 s records the server position of each squad member (lua, alife_object(id).position)
and reads the ledger, until an `arrived` or `blocked` event for that owner or the timeout. It writes
track.json and exits 0 only on `arrived` with the leader within 150 m of the smart.
"""
import argparse
import json
import math
from pathlib import Path
import sys
import time


KINDS = ('squad', 'pack', 'lone_stalker', 'lone_mutant')
ARRIVAL_RADIUS_M = 150.0
POLL_S = 5.0
BUSY_STATES = ('queued', 'awaiting_observation', 'awaiting_decision')


def parse_fields(tokens):
    fields = {}
    for token in tokens:
        key, sep, value = token.partition('=')
        if sep:
            fields[key] = value
    return fields


def as_int(text):
    try:
        return int(text)
    except (TypeError, ValueError):
        return None


def parse_census(text):
    """`npc <id> <section> key=value...` lines and the closing `summary ...` line."""
    npcs, summary = [], {}
    for line in text.splitlines():
        tokens = line.split()
        if not tokens:
            continue
        if tokens[0] == 'summary':
            summary = {k: as_int(v) for k, v in parse_fields(tokens[1:]).items()}
        elif tokens[0] == 'npc' and len(tokens) >= 3:
            fields = parse_fields(tokens[3:])
            npc = {'id': as_int(tokens[1]), 'section': tokens[2]}
            for key in ('owner', 'kind', 'binding', 'writer', 'level', 'objective', 'source', 'rev'):
                value = fields.get(key)
                npc[key] = as_int(value) if key in ('owner', 'rev') else value
            npcs.append(npc)
    return {'npcs': npcs, 'summary': summary}


def parse_owners(text):
    owners = []
    for line in text.splitlines():
        tokens = line.split()
        if len(tokens) < 2 or tokens[0] != 'owner':
            continue
        owner = {'id': as_int(tokens[1])}
        owner.update(parse_fields(tokens[2:]))
        for key in ('members', 'mrev', 'rev'):
            if key in owner:
                owner[key] = as_int(owner[key])
        sites = owner.get('sites', '-')
        owner['sites'] = [] if sites in ('', '-') else sites.split(',')
        owners.append(owner)
    return owners


def parse_log(text):
    entries = []
    for line in text.splitlines():
        fields = parse_fields(line.split())
        if 'serial' in fields:
            fields['serial'] = as_int(fields['serial'])
            fields['ownerId'] = as_int(fields.get('ownerId'))
            entries.append(fields)
    return entries


def is_transferable(owner):
    return owner.get('writer') == 'sim' and str(owner.get('binding', '')).startswith('group:')


def pick_owners(owners):
    picked = {kind: None for kind in KINDS}
    for owner in owners:
        kind = owner.get('kind')
        if kind in picked and picked[kind] is None and is_transferable(owner):
            picked[kind] = owner
    return picked


def count_by(items, key):
    counts = {}
    for item in items:
        counts[item.get(key)] = counts.get(item.get(key), 0) + 1
    return counts


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def census(bridge, level, out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    ok, text = bridge.request('ai.freeplay.census', level or '')
    (out / 'census.txt').write_text(text)
    if not ok:
        print(f'freeplay_probe: census failed: {text}', file=sys.stderr)
        return 1
    ok_owners, owners_text = bridge.request('ai.freeplay.owners', level or '')
    (out / 'owners.txt').write_text(owners_text if ok_owners else '')
    parsed = parse_census(text)
    owners = parse_owners(owners_text) if ok_owners else []

    # One record per owner from the npc lines; the owners listing adds names.
    names = {o['id']: o.get('name') for o in owners}
    by_owner = {}
    for npc in parsed['npcs']:
        if npc['owner'] is None:
            continue
        rec = by_owner.setdefault(npc['owner'], {'owner': npc['owner'], 'name': names.get(npc['owner']),
                                                 'kind': npc['kind'], 'writer': npc['writer'],
                                                 'binding': npc['binding'], 'level': npc['level'],
                                                 'objective': npc['objective'], 'members': []})
        rec['members'].append({'id': npc['id'], 'section': npc['section']})
    owner_records = sorted(by_owner.values(), key=lambda r: r['owner'])
    data = {
        'level': level or None,
        'summary': parsed['summary'],
        'npcs': parsed['npcs'],
        'owners_by_kind': count_by(owner_records, 'kind'),
        'owners_by_writer': count_by(owner_records, 'writer'),
        'transferable_by_kind': count_by([r for r in owner_records if is_transferable(r)], 'kind'),
        'script_owned': [r for r in owner_records
                         if r['binding'] and r['binding'].startswith('group:') and r['writer'] != 'sim'],
        'unbound_npcs': [n for n in parsed['npcs'] if n['binding'] and n['binding'].startswith('npc:')],
        'picked': {k: (v['id'] if v else None) for k, v in pick_owners(owners).items()},
    }
    write_json(out / 'census.json', data)

    summary_line = next((l for l in text.splitlines() if l.startswith('summary')), 'summary missing')
    print(summary_line)
    print('owners by kind: ' + ' '.join(f'{k}={v}' for k, v in sorted(data['owners_by_kind'].items())))
    print('owners by writer: ' + ' '.join(f'{k}={v}' for k, v in sorted(data['owners_by_writer'].items())))

    ownerless = [n for n in parsed['npcs'] if n['owner'] is None]
    if not parsed['npcs'] or not parsed['summary'].get('npcs'):
        print('freeplay_probe: census is empty', file=sys.stderr)
        return 1
    if ownerless:
        print(f'freeplay_probe: {len(ownerless)} npc lines have no owner', file=sys.stderr)
        return 1
    return 0


def position_query(group_id, site):
    """One-line Lua expression: squad, smart, leader and member server positions, `key=value;...`."""
    return ('(function() local sq=alife_object(%d) local sm=SIMBOARD.smarts_by_names[%s] '
            'local function p(o) if not o then return "nil" end local v=o.position '
            'return string.format("%%.1f,%%.1f,%%.1f",v.x,v.y,v.z) end '
            'local r={"squad="..p(sq),"smart="..p(sm)} '
            'if sq then r[#r+1]="leader="..tostring(sq:commander_id()) r[#r+1]="online="..tostring(sq.online) '
            'local function n(id) local o=id and alife_object(id) return o and o:name() or "nil" end '
            'r[#r+1]="target="..tostring(sq.scripted_target) r[#r+1]="action="..tostring(sq.current_action) '
            'r[#r+1]="assigned="..n(sq.smart_id) r[#r+1]="heading="..n(sq.current_target_id) '
            'for k in sq:squad_members() do r[#r+1]="m"..k.id.."="..p(alife_object(k.id)) end end '
            'return table.concat(r,";") end)()') % (group_id, json.dumps(site))


def parse_xyz(text):
    parts = (text or '').split(',')
    if len(parts) != 3:
        return None
    try:
        return [float(p) for p in parts]
    except ValueError:
        return None


def parse_positions(text):
    fields = dict(item.partition('=')[::2] for item in text.split(';') if '=' in item)
    sample = {'squad': parse_xyz(fields.get('squad')), 'smart': parse_xyz(fields.get('smart')),
              'leader': as_int(fields.get('leader')), 'online': fields.get('online'),
              'target': fields.get('target'), 'action': fields.get('action'),
              'assigned': fields.get('assigned'), 'heading': fields.get('heading'), 'members': {}}
    for key, value in fields.items():
        if key.startswith('m') and key[1:].isdigit():
            sample['members'][key[1:]] = parse_xyz(value)
    return sample


def leader_distance(sample):
    smart = sample.get('smart')
    if not smart:
        return None
    pos = sample['members'].get(str(sample.get('leader'))) or sample.get('squad')
    if not pos:
        return None
    return math.dist(pos, smart)


def max_serial(bridge):
    ok, text = bridge.request('ai.freeplay.log', '512')
    entries = parse_log(text) if ok else []
    return max((e['serial'] for e in entries), default=0)


def find_owner(bridge, owner_id):
    ok, text = bridge.request('ai.freeplay.owners', '')
    if not ok:
        return None
    return next((o for o in parse_owners(text) if o['id'] == owner_id), None)


def move(bridge, owner_id, site, out, timeout=900.0, sleep=time.sleep, clock=time.time):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    start = clock()
    track = {'owner': owner_id, 'site': site, 'group': None, 'name': None, 'members': [], 'samples': [],
             'events': [], 'event': None, 'decide': None, 'outcome': None, 'final_distance_m': None,
             'leader_within_m': False, 'arrival_radius_m': ARRIVAL_RADIUS_M, 'started_at': start}

    def finish(outcome, code):
        track['outcome'] = outcome
        track['elapsed_s'] = round(clock() - start, 1)
        write_json(out / 'track.json', track)
        print(f'owner={owner_id} site={site} outcome={outcome} elapsed={track["elapsed_s"]}s '
              f'distance={track["final_distance_m"]}')
        return code

    def sample():
        if track['group'] is None:
            return None
        ok, text = bridge.request('lua', position_query(track['group'], site))
        entry = {'t': round(clock() - start, 1)}
        if ok:
            entry.update(parse_positions(text))
            entry['distance_m'] = leader_distance(entry)
        else:
            entry['error'] = text
        track['samples'].append(entry)
        return entry

    owner = find_owner(bridge, owner_id)
    if owner is None:
        return finish('unknown_owner', 1)
    track['name'] = owner.get('name')
    track['kind'] = owner.get('kind')
    binding = owner.get('binding', '')
    if not binding.startswith('group:'):
        return finish('not_a_group', 1)
    track['group'] = int(binding.split(':', 1)[1])
    ok, text = bridge.request('ai.freeplay.census', '')
    track['members'] = [n['id'] for n in parse_census(text if ok else '')['npcs'] if n['owner'] == owner_id]
    track['owner_before'] = owner

    ok, text = bridge.request('ai.freeplay.wake', f'{owner_id} probe')
    if not ok:
        return finish(f'wake_failed:{text}', 1)
    deadline = start + timeout
    while True:
        owner = find_owner(bridge, owner_id)
        if owner and owner['sites'] and owner.get('state') not in BUSY_STATES:
            break
        if clock() >= deadline:
            return finish('no_observation', 1)
        sleep(POLL_S)
    track['sites_offered'] = owner['sites']
    track['owner_at_decide'] = owner

    baseline = max_serial(bridge)
    ok, text = bridge.request('ai.freeplay.decide', f'{owner_id} move_to {site}')
    track['decide'] = {'ok': ok, 'reply': text, 't': round(clock() - start, 1), 'after_serial': baseline}
    if not ok:
        return finish(f'rejected:{text}', 1)
    decided_at = clock()

    while True:
        last = sample()
        ok, text = bridge.request('ai.freeplay.log', '512')
        for entry in parse_log(text) if ok else []:
            if entry['serial'] > baseline and entry['ownerId'] == owner_id and \
                    entry not in track['events']:
                track['events'].append(entry)
        event = next((e for e in track['events'] if e.get('source') == 'event' and
                      e.get('wakeReason') in ('arrived', 'blocked')), None)
        if event or clock() >= deadline:
            break
        write_json(out / 'track.json', track)   # progress; rewritten with the outcome at the end
        sleep(POLL_S)

    if last and last.get('distance_m') is not None:
        track['final_distance_m'] = round(last['distance_m'], 3)
    track['leader_within_m'] = track['final_distance_m'] is not None and \
        track['final_distance_m'] <= ARRIVAL_RADIUS_M
    track['owner_after'] = find_owner(bridge, owner_id)
    if event is None:
        return finish('timeout', 1)
    track['event'] = event
    track['travel_s'] = round(clock() - decided_at, 1)
    if event['wakeReason'] == 'blocked':
        return finish('blocked', 1)
    return finish('arrived', 0 if track['leader_within_m'] else 1)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    c = sub.add_parser('census')
    c.add_argument('endpoint')
    c.add_argument('--level', default='')
    c.add_argument('--out', required=True)
    m = sub.add_parser('move')
    m.add_argument('endpoint')
    m.add_argument('--owner', type=int, required=True)
    m.add_argument('--site', required=True)
    m.add_argument('--out', required=True)
    m.add_argument('--timeout', type=float, default=900.0)
    args = parser.parse_args(argv)

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from agentctl import Bridge, EndpointError
    try:
        bridge = Bridge(args.endpoint, timeout=60, wait=10)
    except (OSError, EndpointError) as e:
        print(f'freeplay_probe: cannot connect to {args.endpoint}: {e}', file=sys.stderr)
        return 1
    with bridge:
        if args.command == 'census':
            return census(bridge, args.level, args.out)
        return move(bridge, args.owner, args.site, args.out, timeout=args.timeout)


if __name__ == '__main__':
    sys.exit(main())
