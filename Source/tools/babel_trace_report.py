"""Summarize bounded native/render node evidence without dumping matrices."""
from pathlib import Path
import argparse,collections,json,math,struct
p=argparse.ArgumentParser();p.add_argument('trace');p.add_argument('--out');args=p.parse_args()
data=Path(args.trace).read_bytes();groups={};last={}
for offset in range(0,len(data)-183,184):
 r=struct.unpack_from('<7Q32f',data,offset);frame,clock,actor,model,parent,node,ids=r[:7];raw=r[7:23];out=r[23:]
 key=(ids>>32,ids&0xffffffff);g=groups.setdefault(key,dict(records=0,frames=set(),actors=set(),nodes=set(),nonfinite=0,held_raw_intermediate=0,changed_output=0,max_translation_correction=0))
 g['records']+=1;g['frames'].add(frame);g['actors'].add(actor);g['nodes'].add(node);g['nonfinite']+=not all(math.isfinite(x)for x in raw+out)
 g['max_translation_correction']=max(g['max_translation_correction'],max(abs(raw[i]-out[i])for i in (3,7,11)))
 previous=last.get((actor,model,parent,node))
 if previous and frame==previous[0]+1:
  oldraw,oldout=previous[1:]
  held=all(abs(raw[i]-oldraw[i])<(.002 if i%4==3 else .00001)for i in range(12))
  changed=any(abs(out[i]-oldout[i])>(.002 if i%4==3 else .00001)for i in range(12))
  g['held_raw_intermediate']+=held and changed;g['changed_output']+=changed
 last[(actor,model,parent,node)]=(frame,raw,out)
result={f'{id}:{variant}':{**g,'frames':len(g['frames']),'actors':len(g['actors']),'nodes':len(g['nodes'])}for(id,variant),g in sorted(groups.items())}
text=json.dumps(result,indent=2);print(text)
if args.out:Path(args.out).write_text(text+'\n')
