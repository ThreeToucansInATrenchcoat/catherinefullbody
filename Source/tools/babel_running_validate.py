"""Validate movement-family transitions and actual ground placement smoothing."""
from pathlib import Path
import argparse,hashlib,json,struct
import numpy as np
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--partial',action='store_true');args=p.parse_args();run=args.run
cases=json.loads((run/'running-results.json').read_text());report=[]
for case in cases:
 folder=run/case['tag'];meta=json.loads((folder/'metadata.json').read_text());rows=json.loads((folder/'samples.json').read_text())
 assert meta['ids'][:2]==[14+case['index'],30]
 bind=np.linalg.inv(np.frombuffer(bytes.fromhex(meta['inverse_binds']),'<f4').reshape(-1,4,4)[0].T)
 anchors=np.array([np.frombuffer(bytes.fromhex(r['root']),'<f4').reshape(4,4)[3,0]for r in rows]);intermediate=matched=complete=discarded=0;previous=None;jumps=[]
 for row in rows:
  state=struct.unpack('<10I',bytes.fromhex(row['state']))[7]
  assert not state&0x80000000 and bool(state&128)==(case['phase']=='hanging')
  raw=np.frombuffer(bytes.fromhex(row['raw'][0]),'<f4').reshape(4,4).T;root=np.frombuffer(bytes.fromhex(row['root']),'<f4').reshape(4,4).T
  valid_first=False
  for palette in row['palettes']:
   matrices=np.frombuffer(bytes.fromhex(palette['hex']),'<f4').reshape(-1,4,4)
   if palette.get('stable_read')is False or not np.equal(matrices[:,3,:],[0,0,0,1]).all():discarded+=1;continue
   assert np.isfinite(matrices).all();complete+=1;world=matrices[0]@bind
   # Matching the current native pose's basis rules out mistaking an older
   # ring-buffer pose for a fractional placement. With the same pose, a
   # translation between all native placement endpoints is actual smoothing.
   if case['phase']!='hanging'and np.max(abs(world[:3,:3]-raw[:3,:3]))<1e-4:
    matched+=1;estimated=float(root[0,3]+world[0,3]-raw[0,3]);intermediate+=float(np.min(abs(anchors-estimated)))>3
   if palette['slot']==row['palettes'][0]['slot']:
    valid_first=True;value=float(world[0,3])
  if valid_first:
   current=float(raw[0,3])
   if previous:
    dx=current-previous[0];dy=value-previous[1]
    if abs(dx)>1 and dx*dy<0 and abs(dy)>40:jumps.append((row['t'],dx,dy))
   previous=(current,value)
  else:previous=None
 assert complete>len(rows)*2.8
 assert not jumps,(case['tag'],jumps)
 if case['phase']=='ground'and case['fps']==240:assert intermediate>=5,(case['tag'],intermediate)
 report.append({**case,'complete_palette_observations':complete,'incomplete_or_unstable_reads':discarded,'pose_matched_placement_samples':matched,'intermediate_placement_samples':intermediate,'large_opposite_jumps':jumps})
expected={(i,rate,phase)for i in(0,1)for rate in(30,60,240)for phase in('ground','hanging','ground-return')}
complete={(r['index'],r['fps'],r['phase'])for r in report}==expected
if not args.partial:assert complete
result={'complete':complete,'exe_sha256':hashlib.sha256(Path('C:/catherine/recomp/Build/catherine1.exe').read_bytes()).hexdigest(),'cases':report}
(run/'running-validation.json').write_text(json.dumps(result,indent=2));print(json.dumps({'complete':complete,'cases':len(report),'large_opposite_jumps':sum(len(r['large_opposite_jumps'])for r in report),'ground_240_intermediates':{r['name']:r['intermediate_placement_samples']for r in report if r['phase']=='ground'and r['fps']==240}}))
