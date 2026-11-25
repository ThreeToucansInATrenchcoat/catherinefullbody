"""Check complete hanging coverage and native/render hop continuity."""
from pathlib import Path
import argparse,hashlib,json,struct
import numpy as np
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--partial',action='store_true');a=p.parse_args();run=a.run
roster=json.loads((run/'cling-results.json').read_text());report=[]
for character in roster:
 for case in character['samples']:
  folder=run/case['capture'];meta=json.loads((folder/'metadata.json').read_text());rows=json.loads((folder/'samples.json').read_text())
  bind=np.linalg.inv(np.frombuffer(bytes.fromhex(meta['inverse_binds']),'<f4').reshape(-1,4,4)[0].T)
  previous=None;wrong_way=[];palettes=0;unstable=0;incomplete=0;invalid=[];native_motion=0
  for row in rows:
   state=struct.unpack('<10I',bytes.fromhex(row['state']))[7];assert state&128 and not state&0x80000000
   raw=np.frombuffer(bytes.fromhex(row['raw'][0]),'<f4').reshape(4,4).T
   first_valid=False
   for palette in row['palettes']:
    matrices=np.frombuffer(bytes.fromhex(palette['hex']),'<f4').reshape(-1,4,4)
    assert len(matrices)==meta['count']
    if palette.get('stable_read') is False:unstable+=1;continue
    # These are asynchronously populated CPU staging allocations, not a GPU
    # capture. A newly reused slot can still contain allocator metadata. Only
    # complete native affine palettes (including the authored bottom rows)
    # can be used as render-pose observations. Preserve rejected-read counts.
    if not np.equal(matrices[:,3,:],np.array([0,0,0,1],dtype=np.float32)).all():incomplete+=1;continue
    if not np.isfinite(matrices).all():invalid.append({'t':row['t'],'slot':palette['slot'],'repeated_read_available':'stable_read'in palette})
    palettes+=1
    if palette['slot']==row['palettes'][0]['slot']:first_valid=True
   if not first_valid:previous=None;continue
   out=np.frombuffer(bytes.fromhex(row['palettes'][0]['hex']),'<f4').reshape(-1,4,4)[0]@bind
   x=float(raw[0,3]);y=float(out[0,3])
   if previous:
    dx=x-previous[0];dy=y-previous[1]
    if abs(dx)>1:
     native_motion+=1
     # A render-ring slot can trail the native pose by several frames. Large
     # opposite movement inside a continuing native hop is the reported bug,
     # unlike ordinary latency or a direction change after release.
     if dx*dy<0 and abs(dy)>40:wrong_way.append({'t':row['t'],'native_delta':dx,'render_delta':dy})
   previous=(x,y)
  assert native_motion>5
  assert not wrong_way,(character['name'],case['fps'],wrong_way)
  assert palettes>len(rows)*2.8,'too few complete staging observations'
  report.append({'name':character['name'],'fps':case['fps'],'hanging_samples':len(rows),'palette_snapshots':palettes,'unstable_reads':unstable,'incomplete_staging_reads':incomplete,'nonfinite_snapshots':invalid,'bone_count':meta['count'],'large_opposite_jumps':wrong_way})
complete=len(roster)==13 and {r['index']for r in roster}==set(range(13)) and all({s['fps']for s in r['samples']}=={30,60}for r in roster)
if not a.partial:assert complete and not any(r['nonfinite_snapshots']for r in report),report
result={'complete':complete,'cases':report,'exe_sha256':hashlib.sha256((Path('C:/catherine/recomp/Build')/'catherine1.exe').read_bytes()).hexdigest()}
(run/'cling-validation.json').write_text(json.dumps(result,indent=2));print(json.dumps({'complete':complete,'characters':len(roster),'cases':len(report),'palette_snapshots':sum(r['palette_snapshots']for r in report),'large_opposite_jumps':sum(len(r['large_opposite_jumps'])for r in report)}))
