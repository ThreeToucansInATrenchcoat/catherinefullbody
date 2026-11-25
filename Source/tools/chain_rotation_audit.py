"""Audit cyclic chain motion in an opt-in rigid-motion trace from an owned game."""
import argparse,json
from pathlib import Path
import numpy as np

p=argparse.ArgumentParser();p.add_argument('trace',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
dtype=np.dtype([('meta','<u8',(7,)),('raw','<f4',(16,)),('out','<f4',(16,))])
rows=np.memmap(a.trace,dtype=dtype,mode='r',shape=(a.trace.stat().st_size//184,))
assert np.all(rows['meta'][:,0]==0x314e4f49544f4d52),'invalid motion trace'
report={'trace':str(a.trace),'assemblies':[]}
for geometry in ['5d85a105c9d1820f','74b7033ab5e79527','b423bd2d0d6de2e9','dde09f70b0bd2816']:
    r=rows[(rows['meta'][:,4]==int(geometry,16))&(rows['raw'][:,7]<5000)]
    if not len(r):continue
    _,indices=np.unique(r['meta'][:,2],return_index=True);r=r[np.sort(indices)]
    adjacent=np.diff(r['meta'][:,2])==1
    native=np.degrees(np.arctan2(r['raw'][:,8],-r['raw'][:,0]))
    rendered=np.degrees(np.arctan2(r['out'][:,8],-r['out'][:,0]))
    # The nine-sector geometry is invariant under 40-degree sector relabeling.
    step=(np.diff(rendered)+20)%40-20
    report['assemblies'].append(dict(geometry=geometry,samples=len(r),
        native_wraps=int(np.count_nonzero(adjacent&(np.diff(native)<-20))),
        large_reverse_frames=int(np.count_nonzero(adjacent&(step<-.1))),
        minimum_step_degrees=float(step[adjacent].min())if adjacent.any()else None))
text=json.dumps(report,indent=2);print(text)
if a.out:a.out.write_text(text+'\n')
