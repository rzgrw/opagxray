#!/usr/bin/env python3
"""Strict experimental probe parser; deliberately independent of frame_time_report.

All distributions are conditional on the common probe and integer 501 limiter.
No variance filter, bare-arm distribution or absolute probe-cost estimate.
"""
import argparse
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
import json
import math
from pathlib import Path
import re
import statistics

BASE='e3f273f04bf85b4b265004cbbc8a35f19796cd31'
TARGET='45680801007e8c5a3fab7555eb4e4ed17a35ffe3'
HASHES={'probe_core_sha256','callsite_patch_sha256','source_snapshot_sha256','binary_sha256','user_ltx_sha256'}
COUNTS={'nwarmup','nmeasured','stamps','run_index','collector_rows','collector_capacity','collector_warmup',
        'collector_invalid','collector_dropped','collector_flags','collector_not_ready','collector_start_ns','collector_end_ns'}
KEYS=HASHES|COUNTS|{'role','revision','scene','save','level','vsync','frame_limit','resolution','renderer',
                  'content_manifest','overlay','clock_is_steady','clock_period_ns','truncated','machine','collector_truncated'}

class ProbeError(ValueError):pass

def require(condition,reason):
    if not condition:raise ProbeError(reason)

def uint(value):
    require(bool(re.fullmatch(r'0|[1-9][0-9]*',value)), 'noncanonical unsigned integer')
    result=int(value);require(result<=2**64-1,'uint64 overflow');return result

@dataclass
class Capture:
    metadata: dict
    rows: list
    def summary(self):
        values=sorted(row[2] for row in self.rows[self.metadata['nwarmup']:])
        return dict(p50_ns=values[math.ceil(.50*len(values))-1],p95_ns=values[math.ceil(.95*len(values))-1],
                    min_ns=values[0],max_ns=values[-1],count=len(values))

def parse(text):
    lines=text.splitlines();require(bool(lines) and lines[0]=='# xray-overhead-probe-v1','wrong schema')
    m={};i=1
    while i<len(lines) and lines[i].startswith('# '):
        for field in lines[i][2:].split():
            require(field.count('=')==1,'invalid metadata token');key,value=field.split('=')
            require(key in KEYS and key not in m,'unknown or duplicate metadata');m[key]=value
        i+=1
    require(set(m)==KEYS,'missing metadata')
    require(i<len(lines) and lines[i]=='loop_seq,start_ns,wall_ns','wrong columns');i+=1
    for name in HASHES:require(bool(re.fullmatch('[0-9a-f]{64}',m[name])),f'invalid {name}')
    require(m['role'] in ('base','off','on'),'unknown role')
    require(m['revision']==(BASE if m['role']=='base' else TARGET),'unpinned revision')
    for name in ('scene','save','level'):require(bool(re.fullmatch('[A-Za-z0-9_.-]{1,95}',m[name])),f'invalid {name}')
    require(bool(re.fullmatch('[A-Za-z0-9_.,-]{1,95}',m['machine'])),'invalid machine')
    for name,value in dict(vsync='off',frame_limit='501',renderer='renderer_r3',content_manifest='unknown',overlay='unknown',
                           clock_is_steady='true',truncated='false',collector_truncated='false').items():
        require(m[name]==value,f'invalid {name}')
    require(bool(re.fullmatch('[1-9][0-9]*x[1-9][0-9]*',m['resolution'])),'invalid resolution')
    try:period=Decimal(m['clock_period_ns'])
    except InvalidOperation as exc:raise ProbeError('invalid clock period') from exc
    require(period.is_finite() and period>0,'invalid clock period')
    for name in COUNTS:m[name]=uint(m[name])
    require(1<=m['run_index']<=5,'invalid run index')
    require(0<m['nmeasured'] and m['nwarmup']+m['nmeasured']<131072,'invalid count capacity')
    require(m['stamps']==m['nwarmup']+m['nmeasured']+1,'stamp count mismatch')
    rows=[]
    for line in lines[i:]:
        fields=line.split(',');require(len(fields)==3,'bad row');row=tuple(uint(v) for v in fields)
        seq,start,wall=row
        require(seq==len(rows),'non-contiguous sequence');require(wall>0 and start+wall<=2**64-1,'invalid interval')
        if rows:require(start==rows[-1][1]+rows[-1][2],'non-contiguous wall')
        rows.append(row)
    require(len(rows)==m['stamps']-1,'wrong row count')
    for name in ('collector_invalid','collector_dropped','collector_flags','collector_not_ready'):require(m[name]==0,name)
    if m['role']=='on':
        require(m['collector_rows']==m['nmeasured'],'collector row count')
        require(m['collector_capacity']==m['nmeasured']+1,'collector saturation risk')
        require(m['collector_warmup']==m['nwarmup'],'collector warmup')
        start=rows[m['nwarmup']][1];end=rows[-1][1]+rows[-1][2]
        require(start<=m['collector_start_ns']<start+rows[m['nwarmup']][2],'collector start outside first measured loop')
        require(m['collector_end_ns']==end,'collector final boundary mismatch')
    else:
        for name in ('collector_rows','collector_capacity','collector_warmup','collector_start_ns','collector_end_ns'):
            require(m[name]==0,'unexpected collector capture')
    return Capture(m,rows)

def schedule():
    roles=['base','off','on']
    return [(run,roles[(run-1+j)%3]) for run in range(1,6) for j in range(3)]

def aggregate(captures):
    slots={};runs=[];common=None;identities={}
    for c in captures:
        m=c.metadata;slot=(m['run_index'],m['role']);require(slot not in slots,'duplicate slot; select reruns explicitly')
        match=tuple(m[k] for k in ('scene','save','level','vsync','frame_limit','resolution','renderer','user_ltx_sha256',
                                    'machine','probe_core_sha256','nwarmup','nmeasured','clock_period_ns'))
        if common is None:common=match
        require(match==common,'unmatched campaign conditions')
        identity=tuple(m[k] for k in ('revision','callsite_patch_sha256','source_snapshot_sha256','binary_sha256'))
        require(m['role'] not in identities or identities[m['role']]==identity,'arm identity changed')
        identities[m['role']]=identity
        slots[slot]=c.summary();runs.append(dict(role=m['role'],run_index=m['run_index'],**c.summary()))
    result=dict(status='inconclusive',campaign_accepted=False,runs=runs,
                limitation='Conditional common-probe wall deltas; integer 1000/501=1 limiter can hide or reverse cost; no bare arm or absolute probe cost.')
    result['missing_slots']=[dict(run_index=r,role=a) for r,a in schedule() if (r,a) not in slots]
    if result['missing_slots']:return result
    for left,right,name in [('off','base','off_minus_base'),('on','off','on_minus_off')]:
        pairs=[dict(run_index=i,**{stat:slots[i,left][stat]-slots[i,right][stat] for stat in ('p50_ns','p95_ns')}) for i in range(1,6)]
        result[name]={stat:statistics.median(pair[stat] for pair in pairs) for stat in ('p50_ns','p95_ns')}
        result[name]['paired_runs']=pairs
    # Design gate is difference of medians of the five run statistics. Preserve
    # paired deltas separately: median of differences is not generally equal.
    arms={role:{stat:statistics.median(slots[i,role][stat] for i in range(1,6)) for stat in ('p50_ns','p95_ns')} for role in ('base','off','on')}
    result['arm_medians']=arms
    limits={'off_minus_base':{'p50_ns':max(50000,.01*arms['base']['p50_ns']),'p95_ns':max(100000,.03*arms['base']['p95_ns'])},
            'on_minus_off':{'p50_ns':100000,'p95_ns':250000}}
    result['gate_deltas']={name:{stat:arms[left][stat]-arms[right][stat] for stat in ('p50_ns','p95_ns')}
                          for left,right,name in [('off','base','off_minus_base'),('on','off','on_minus_off')]}
    result['thresholds_ns']=limits
    result['thresholds_pass']=all(result['gate_deltas'][name][stat]<=limit for name,stats in limits.items() for stat,limit in stats.items())
    result['status']='timing_pass' if result['thresholds_pass'] else 'timing_fail'
    result['acceptance_open']='Requires verified probe-specific provenance, loaded images, scene/traffic/log/exit gates for every selected slot.'
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('captures',type=Path,nargs='+');a=parser.parse_args()
    try:print(json.dumps(aggregate([parse(p.read_text()) for p in a.captures]),indent=2))
    except (ProbeError,OSError) as e:parser.exit(2,str(e)+'\n')
