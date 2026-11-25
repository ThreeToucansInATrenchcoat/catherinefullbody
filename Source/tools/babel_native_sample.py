"""Read-only snapshots of an owned Babel QA process's render palettes."""
from pathlib import Path
import argparse,ctypes as C,json,struct,sys,time
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Build/perf/harness'))
import bench as b
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--seconds',type=float,default=4);p.add_argument('--out',required=True);args=p.parse_args()
m=b.Memory(args.pid);start=time.perf_counter();rows=[];errors=0
try:
 while time.perf_counter()-start<args.seconds:
  try:
   registry=m.q(0x7000ccadf0);engine=m.q(0x7000cc9498);mapping=m.q(engine+0x6210);base=m.q(mapping)
   for player in range(2):
    actor=m.q(registry+8*player)
    if not actor:continue
    model=m.q(actor+0x20);parent=m.q(model+0x468);table=m.q(parent+0x138);count=struct.unpack('<I',m.read(table,4))[0]
    if not 0<count<=512:continue
    name=m.read(model+0x1c,96).split(b'\0')[0].decode(errors='replace');ids=struct.unpack('<4I',m.read(model+0x5a0,16))
    palettes=[]
    for slot in range(3):
     offset=m.q(table+8+8*slot)
     if offset<1<<30:
      try:palettes.append({'slot':slot,'offset':offset,'hex':m.read(base+offset,count*64).hex()})
      except OSError:pass
    rows.append({'t':time.perf_counter()-start,'player':player+1,'actor':actor,'model':model,'parent':parent,'table':table,'count':count,'name':name,'ids':ids,'position':struct.unpack('<3f',m.read(model+0x5d0,12)),'palettes':palettes})
  except OSError:errors+=1
  time.sleep(.004)
finally:m.close()
out=Path(args.out);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps({'pid':args.pid,'seconds':time.perf_counter()-start,'errors':errors,'rows':rows}))
print(json.dumps({'path':str(out),'samples':len(rows),'errors':errors,'players':list({(r['player'],r['name'],r['count'],tuple(r['ids']))for r in rows})}))
