"""Mixed ground/hanging coverage on owned QA sessions, with no desktop input."""
from pathlib import Path
import argparse,ctypes as C,json,re,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';sys.path.insert(0,str(BUILD/'perf/harness'))
import bench as b
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--run',type=Path,required=True);p.add_argument('--enter',action='store_true');p.add_argument('--last',type=int,default=1);args=p.parse_args();run=args.run
events=[json.loads(x)for x in(run/'events.jsonl').read_text().splitlines()];assert any(x.get('ready',{}).get('pid')==args.pid for x in events)
names=['Blue Cap','Red Cap','Vincent casual','Vincent nightwear','Katherine','Catherine','Rin','Orlando','Jonny','Toby','Erica','Boss','Joker']
m=b.Memory(args.pid);probe=b.Probe(args.pid,Path(m.image));pad=b.Pad(args.pid);results=[]
hwnd=next(h for h in b.windows_of(args.pid)if b.wtext(h).startswith('Catherine Full Body'))
def key(vk,delay=.4,hold=.12):pad.key(vk,hold);time.sleep(delay)
def wait(fn,label,timeout=45):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if fn():return
  time.sleep(.1)
 raise TimeoutError(label)
def playing():
 o,a=probe.puzzle();return o==32 and a.get('status')=='valid'and not a.get('state',0)&0x80000000
def cap(target):
 for _ in range(24):
  current=int(re.search(r'^Fps=(-?\d+)$',(BUILD/'catherine-settings.ini').read_text(),re.M)[1])
  if current==target:return
  vk=0x78 if current>target or current<0 else 0x79
  b.u.SendMessageW(hwnd,0x100,vk,1);b.u.SendMessageW(hwnd,0x101,vk,1<<31);time.sleep(.06)
 raise RuntimeError('frame cap')
def unpause():
 assert probe.puzzle()[0]==33;key(27,.08);wait(playing,'unpause')
def restart():
 assert probe.puzzle()[0]==33;key(83);key(32);key(87);key(32,.5);wait(playing,'retry');time.sleep(.3)
def capture(index,rate,phase):
 tag=f'{index:02d}-{phase}-{rate}';before=probe.puzzle()[1]
 seq='65:.3,0:.2,68:.45,0:.2,65:.3,0:.55'
 result=subprocess.run([sys.executable,str(ROOT/'Source/tools/babel_cling_capture.py'),'--pid',str(args.pid),'--run',str(run),'--tag',tag,'--seconds','2','--stride',str(8 if rate==240 else 2),'--sequence',seq,'--pause-after'],capture_output=True,text=True)
 assert result.returncode==0,result.stderr;wait(lambda:probe.puzzle()[0]==33,'capture pause')
 samples=json.loads((run/tag/'samples.json').read_text());states=[struct.unpack('<10I',bytes.fromhex(s['state']))[7]for s in samples]
 assert all(not s&0x80000000 and bool(s&128)==(phase=='hanging')for s in states),(tag,set(states))
 xs=[s['xyz'][0]for s in samples];assert max(xs)-min(xs)>149,(tag,min(xs),max(xs))
 row={'index':index,'name':names[index],'fps':rate,'phase':phase,'tag':tag,'samples':len(samples),'states':sorted(set(states)),'x_range':[min(xs),max(xs)],'before':before['xyz']}
 results.append(row);(run/'running-results.json').write_text(json.dumps(results,indent=2));print(json.dumps(row),flush=True)
try:
 if args.enter:
  key(83);key(32,2);key(32,2);key(27,.15);key(17,4)
 for index in range(args.last+1):
  if index:key(68)
  key(32,2);key(32,.5);wait(playing,'stage playable');time.sleep(.3)
  for rate in (30,60,240):
   cap(rate)
   if rate!=30:restart()
   capture(index,rate,'ground')
   unpause();key(87,.4)
   C.memmove(pad.v,struct.pack('<QiiiiI',32769,0,0,0,0,1),28);time.sleep(.6);C.memset(pad.v,0,40)
   wait(lambda:playing()and probe.puzzle()[1]['state']==128,'hanging',4)
   capture(index,rate,'hanging')
   unpause();key(87,.1)
   wait(lambda:playing()and not probe.puzzle()[1]['state']&128,'climb back onto block',4)
   capture(index,rate,'ground-return')
  key(83);key(83);key(32);key(87);key(32,2)
  wait(lambda:probe.puzzle()[1].get('status')!='valid','stage unload');time.sleep(2);key(16,2)
finally:C.memset(pad.v,0,40);pad.close();probe.m.close();m.close()
