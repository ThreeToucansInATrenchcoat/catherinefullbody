"""All Babel rigs in both player slots; native F9/F10 caps in the owned window."""
from pathlib import Path
import argparse,ctypes as C,json,re,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';sys.path.insert(0,str(BUILD/'perf/harness'))
import bench as b
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--run',required=True);p.add_argument('--last',type=int,default=12);p.add_argument('--first',type=int,default=0);p.add_argument('--p2-initial',type=int,default=1);args=p.parse_args();run=Path(args.run)
events=[json.loads(x)for x in (run/'events.jsonl').read_text().splitlines()];assert any(x.get('ready',{}).get('pid')==args.pid for x in events)
names=['Blue Cap','Red Cap','Vincent casual','Vincent nightwear','Katherine','Catherine','Rin','Orlando','Jonny','Toby','Erica','Boss','Joker'];profiles=[(14,30),(15,30),(2,31),(2,30),(4,30),(3,30),(20,30),(6,30),(5,30),(7,30),(8,30),(9,30),(19,30)]
memory=b.Memory(args.pid);exe=Path(memory.image);memory.close();probe=b.Probe(args.pid,exe);pad=b.Pad(args.pid);b.k.UnmapViewOfFile(pad.v);pad.v=b.k.MapViewOfFile(pad.h,2,0,0,0);assert pad.v
hwnds=[h for h in b.windows_of(args.pid)if b.wtext(h).startswith('Catherine Full Body')];assert len(hwnds)==1,hwnds;hwnd=hwnds[0]
text=(BUILD/(exe.stem+'.map')).read_text(errors='replace');pref=int(re.search(r'Preferred load address is\s+([0-9a-fA-F]+)',text)[1],16);line=next(s for s in text.splitlines()if ' catherine_babel_pose_counters 'in s);rva=int(line.split()[2],16)-pref
modules=(C.c_void_p*1024)();needed=C.c_uint32();assert b.k.K32EnumProcessModules(probe.m.handle,modules,C.sizeof(modules),C.byref(needed));counters=modules[0]+rva
def counts():return struct.unpack('<4Q',probe.m.read(counters,32))
def key(vk,delay=.7):pad.key(vk,.12);time.sleep(delay)
def second(button,delay=.7):
 C.memmove(pad.v+104,struct.pack('<QiiiiII',button,0,0,0,0,1,1),32);time.sleep(.12)
 C.memmove(pad.v+104,struct.pack('<QiiiiII',0,0,0,0,0,1,1),32);time.sleep(delay)
def wait(predicate,label,timeout=35):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if predicate():return
  time.sleep(.2)
 raise TimeoutError(label)
def capture(index,fps):
 directory=BUILD/'frames';before=set(directory.glob('*.ppm'));(BUILD/'dump_frames').write_text('1 1\n');end=time.monotonic()+12
 while time.monotonic()<end:
  new=set(directory.glob('*.ppm'))-before
  if new:
   time.sleep(.2);source=sorted(new)[-1]
   from PIL import Image
   im=Image.open(source);im.thumbnail((1500,1000));im.save(run/f'{index:02d}-pair-{fps}.png');source.unlink();return
  time.sleep(.1)
 raise TimeoutError('frame capture')
def fps(rate):
 b.u.SendMessageW(hwnd,0x100,0x78 if rate==30 else 0x79,1);b.u.SendMessageW(hwnd,0x101,0x78 if rate==30 else 0x79,1<<31)
 time.sleep(.8);assert re.search(r'^Fps='+str(rate)+r'$',(BUILD/'catherine-settings.ini').read_text(),re.M)
results=json.loads((run/'pair-results.json').read_text())if(run/'pair-results.json').exists()else[]
try:
 for index in range(args.first,args.last+1):
  name=names[index]
  if index:key(68)
  key(32,2)
  # P2 initially selects Red Cap; after that its last selection is retained.
  if index==0:
   for _ in range(args.p2_initial):second(1<<12)
  else:second(1<<14)
  second(1,2);key(32,1)
  wait(lambda:probe.puzzle()[0]==32,'pair stage playable')
  row={'index':index,'name':name,'samples':[]}
  for rate in (30,60):
   fps(rate);before=counts();target=run/f'{index:02d}-pair-{rate}-native.json'
   q=subprocess.run([sys.executable,str(ROOT/'Source/tools/babel_native_sample.py'),'--pid',str(args.pid),'--seconds','1.5','--out',str(target)],capture_output=True,text=True);assert q.returncode==0,q.stderr
   sample=json.loads(q.stdout);assert len(sample['players'])==2,sample
   assert all(tuple(x[3][:2])==profiles[index]for x in sample['players']),sample
   capture(index,rate)
   key(65,.35);key(68,.35);key(87,.35)
   # P2 moves independently through its explicit owned synthetic controller.
   C.memmove(pad.v+104,struct.pack('<QiiiiII',0,32767,0,0,0,1,1),32);time.sleep(.3)
   C.memmove(pad.v+104,struct.pack('<QiiiiII',0,-32767,0,0,0,1,1),32);time.sleep(.3)
   C.memmove(pad.v+104,struct.pack('<QiiiiII',0,0,0,0,0,1,1),32)
   after=counts();sample['fps']=rate;sample['counter_delta']=[b-a for a,b in zip(before,after)];row['samples'].append(sample)
   if rate==30:assert sample['counter_delta']==[0,0,0,0],sample
   else:assert sample['counter_delta'][0]>0 and sample['counter_delta'][1]>0,sample
  key(27,1);wait(lambda:probe.puzzle()[0]==33,'pair pause');key(83);key(83);key(32);key(87);key(32,3)
  wait(lambda:probe.puzzle()[1].get('status')!='valid','pair stage unload');key(16,2)
  results.append(row);(run/'pair-results.json').write_text(json.dumps(results,indent=2));print(json.dumps(row),flush=True)
finally:pad.close();probe.m.close()
