"""Owned QA movement capture with native hierarchy and final skin palettes."""
from pathlib import Path
import argparse,ctypes as C,json,struct,sys,time
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';sys.path.insert(0,str(BUILD/'perf/harness'))
import bench as b
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--run',required=True);p.add_argument('--tag',required=True);p.add_argument('--vk',type=int,default=65);p.add_argument('--hold',type=float,default=.3);p.add_argument('--seconds',type=float,default=2);p.add_argument('--retry',action='store_true');p.add_argument('--stride',type=int,default=2);p.add_argument('--pause-after',action='store_true');p.add_argument('--sequence',help='comma-separated virtual-key:seconds segments, 0 for release');args=p.parse_args();run=Path(args.run)
events=[json.loads(x)for x in (run/'events.jsonl').read_text().splitlines()];assert any(x.get('ready',{}).get('pid')==args.pid for x in events)
m=b.Memory(args.pid);pad=b.Pad(args.pid);folder=run/args.tag;folder.mkdir(exist_ok=True);rows=[]
try:
 if args.retry:
  probe=b.Probe(args.pid,Path(m.image));pad.key(32,.12);deadline=time.monotonic()+60
  while True:
   outer,act=probe.puzzle()
   if outer==32 and act.get('status')=='valid'and not act.get('state',0)&0x80000000:break
   if time.monotonic()>deadline:raise TimeoutError('retry did not become playable')
   time.sleep(.1)
  probe.m.close()
 actor=m.q(m.q(0x7000ccadf0));model=m.q(actor+0x20);parent=m.q(model+0x468);table=m.q(parent+0x138);count=struct.unpack('<I',m.read(table,4))[0]
 nodes=m.q(m.q(parent+0x30)+0x10);skin=m.q(parent+0x118);indices=m.q(m.q(skin+8)+0x10);binds=m.q(m.q(skin)+0x10)
 index=struct.unpack('<'+str(count)+'H',m.read(indices,2*count));pointers=[m.q(nodes+8*i)for i in index]
 ancestors={}
 for node in pointers:
  seen=set();current=node
  while current and current not in seen and len(seen)<256:
   seen.add(current)
   if current in ancestors:break
   ancestor=m.q(current+8);ancestors[current]=ancestor;current=ancestor
   if current not in pointers:break
 meta={'actor':actor,'model':model,'parent':parent,'ids':struct.unpack('<4I',m.read(model+0x5a0,16)),'count':count,'nodes':pointers,'parents':ancestors,'inverse_binds':m.read(binds,count*64).hex()}
 (folder/'metadata.json').write_text(json.dumps(meta))
 directory=BUILD/'frames';directory.mkdir(exist_ok=True);before=set(directory.glob('*.ppm'));(BUILD/'dump_frames').write_text(f'24 {args.stride}\n')
 segments=[];end=0
 for item in (args.sequence or f'{args.vk}:{args.hold},0:{max(0,args.seconds-args.hold)}').split(','):
  vk,duration=item.split(':');end+=float(duration);segments.append((end,int(vk)))
 start=time.perf_counter();C.memset(pad.v,0,40);last_key=None
 while time.perf_counter()-start<args.seconds:
  elapsed=time.perf_counter()-start
  vk=next((vk for end,vk in segments if elapsed<end),0)
  if vk!=last_key:C.memset(pad.v,0,40);C.memmove(pad.v+28,struct.pack('<I',vk),4);last_key=vk
  if m.q(m.q(0x7000ccadf0))!=actor:raise RuntimeError('actor changed during capture')
  engine=m.q(0x7000cc9498);base=m.q(m.q(engine+0x6210));state=m.read(actor+0x170,40)
  row={'t':elapsed,'xyz':struct.unpack('<3f',m.read(model+0x5d0,12)),'root':m.read(m.q(model+0x4a8)+0x20,64).hex(),'flags':struct.unpack('<I',m.read(actor+0x10,4))[0],'state':state.hex(),'raw':[m.read(node+0x20,64).hex()for node in pointers],'palettes':[]}
  for slot in range(3):
   offset=m.q(table+8+8*slot)
   if offset<1<<30:
    first=m.read(base+offset,count*64);second=m.read(base+offset,count*64)
    row['palettes'].append({'slot':slot,'hex':first.hex(),'repeat_hex':second.hex()if first!=second else None,'stable_read':first==second})
  rows.append(row);time.sleep(.008)
 C.memset(pad.v,0,40)
 if args.pause_after:pad.key(27,.08)
 time.sleep(.5)
 from PIL import Image
 for i,source in enumerate(sorted(set(directory.glob('*.ppm'))-before)):
  image=Image.open(source);image.thumbnail((1500,1000));image.save(folder/f'{i:03d}.png');source.unlink()
 (folder/'samples.json').write_text(json.dumps(rows));print(json.dumps({'path':str(folder),'frames':len(list(folder.glob('*.png'))),'samples':len(rows),'start':rows[0]['xyz'],'end':rows[-1]['xyz'],'flags':sorted({hex(x['flags'])for x in rows})}))
finally:C.memset(pad.v,0,40);pad.close();m.close()
