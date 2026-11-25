"""Owned Babel hanging-hop coverage. No desktop input, save writes or focus."""
from pathlib import Path
import argparse,ctypes as C,json,re,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';sys.path.insert(0,str(BUILD/'perf/harness'))
import bench as b
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--run',required=True);p.add_argument('--first',type=int,default=0);p.add_argument('--last',type=int,default=12);p.add_argument('--enter',action='store_true');args=p.parse_args();run=Path(args.run)
events=[json.loads(x)for x in (run/'events.jsonl').read_text().splitlines()];assert any(x.get('ready',{}).get('pid')==args.pid for x in events)
names=['Blue Cap','Red Cap','Vincent casual','Vincent nightwear','Katherine','Catherine','Rin','Orlando','Jonny','Toby','Erica','Boss','Joker']
profiles=[(14,30),(15,30),(2,31),(2,30),(4,30),(3,30),(20,30),(6,30),(5,30),(7,30),(8,30),(9,30),(19,30)]
m=b.Memory(args.pid);probe=b.Probe(args.pid,Path(m.image));pad=b.Pad(args.pid)
hwnd=next(h for h in b.windows_of(args.pid)if b.wtext(h).startswith('Catherine Full Body'))
def key(vk,delay=.5,hold=.12):pad.key(vk,hold);time.sleep(delay)
def wait(fn,label,timeout=45):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if fn():return
  time.sleep(.1)
 raise TimeoutError(label)
def playing():
 o,a=probe.puzzle();return o==32 and a.get('status')=='valid' and not a.get('state',0)&0x80000000
def cap(rate):
 b.u.SendMessageW(hwnd,0x100,0x78 if rate==30 else 0x79,1);b.u.SendMessageW(hwnd,0x101,0x78 if rate==30 else 0x79,1<<31)
 time.sleep(.2);assert re.search(r'^Fps='+str(rate)+r'$',(BUILD/'catherine-settings.ini').read_text(),re.M)
def hang():
 # Climb one stair, then grab and step back off its front edge. Native bit7
 # confirms hanging; a standing or falling sample must never count as a pass.
 key(87,.4)
 C.memmove(pad.v,struct.pack('<QiiiiI',32769,0,0,0,0,1),28);time.sleep(.6);C.memset(pad.v,0,40)
 wait(lambda:playing() and probe.puzzle()[1]['state']==128,'native hanging pose',4)
def restart():
 key(27,.5);wait(lambda:probe.puzzle()[0]==33,'pause')
 key(83);key(32);key(87);key(32,1);wait(playing,'retry playable');time.sleep(.4)
results=json.loads((run/'cling-results.json').read_text())if(run/'cling-results.json').exists()else[]
try:
 if args.enter:
  key(83);key(32,2);key(32,2)
  # Only the Babel entry movie is skipped, before the character selector.
  key(27,.15);key(17,4)
 for index in range(args.first,args.last+1):
  if index:key(68)
  key(32,2);key(32,1);wait(playing,'stage playable');time.sleep(.4)
  actor=m.q(m.q(0x7000ccadf0));model=m.q(actor+0x20);ids=struct.unpack('<4I',m.read(model+0x5a0,16));assert ids[:2]==profiles[index],ids
  row={'index':index,'name':names[index],'ids':ids,'samples':[]}
  for rate in (30,60):
   cap(rate)
   if rate==60:restart()
   hang();before=probe.puzzle()[1]
   tag=f'{index:02d}-cling-{rate}'
   result=subprocess.run([sys.executable,str(ROOT/'Source/tools/babel_cling_capture.py'),'--pid',str(args.pid),'--run',str(run),'--tag',tag,'--seconds','3','--sequence','65:.25,0:.2,68:.25,0:.2,65:.25,0:.2,68:.25,0:.2,65:.25,0:.2,68:.25,0:.2'],capture_output=True,text=True)
   assert result.returncode==0,result.stderr
   after=probe.puzzle()[1];samples=json.loads((run/tag/'samples.json').read_text())
   states=[struct.unpack('<10I',bytes.fromhex(x['state']))[7]for x in samples]
   assert all(s&128 and not s&0x80000000 for s in states),'left hanging state'
   xs=[x['xyz'][0]for x in samples];assert max(xs)-min(xs)>=149,'no lateral hop occurred'
   row['samples'].append({'fps':rate,'capture':tag,'frames':len(list((run/tag).glob('*.png'))),'native_hanging_samples':len(samples),'states':sorted(set(states)),'x_range':[min(xs),max(xs)],'before':before['xyz'],'after':after['xyz']})
   print(json.dumps({'character':names[index],**row['samples'][-1]}),flush=True)
  key(27,1);wait(lambda:probe.puzzle()[0]==33,'stage-select pause');key(83);key(83);key(32);key(87);key(32,2)
  wait(lambda:probe.puzzle()[1].get('status')!='valid','stage unload');time.sleep(2);key(16,2)
  results.append(row);(run/'cling-results.json').write_text(json.dumps(results,indent=2))
finally:C.memset(pad.v,0,40);pad.close();probe.m.close();m.close()
