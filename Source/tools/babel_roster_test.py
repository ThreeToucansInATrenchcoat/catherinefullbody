"""Drive only a PID created by babel_audit.py; no desktop input or focus."""
from pathlib import Path
import argparse,json,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';sys.path.insert(0,str(BUILD/'perf/harness'))
import bench as b
p=argparse.ArgumentParser();p.add_argument('--pid',type=int,required=True);p.add_argument('--run',required=True);p.add_argument('--first',type=int,default=0);p.add_argument('--last',type=int,default=12);p.add_argument('--fps',type=int,required=True);args=p.parse_args()
run=Path(args.run);events=[json.loads(x)for x in (run/'events.jsonl').read_text().splitlines()]
assert any(x.get('ready',{}).get('pid')==args.pid for x in events),'PID is not owned by this audit session'
names=['Blue Cap','Red Cap','Vincent casual','Vincent nightwear','Katherine','Catherine','Rin','Orlando','Jonny','Toby','Erica','Boss','Joker']
pad=b.Pad(args.pid);probe=b.Probe(args.pid,Path(b.Memory(args.pid).image));results=[]
def key(vk,delay=.65):pad.key(vk,.12);time.sleep(delay)
def wait(predicate,label,timeout=35):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if predicate():return
  time.sleep(.2)
 raise TimeoutError(label)
def playing():
 outer,actor=probe.puzzle();return outer==32 and actor.get('status')=='valid'
def capture(index):
 directory=BUILD/'frames';directory.mkdir(exist_ok=True);before=set(directory.glob('*.ppm'));(BUILD/'dump_frames').write_text('1 1\n');end=time.monotonic()+12
 while time.monotonic()<end:
  new=set(directory.glob('*.ppm'))-before
  if new:
   time.sleep(.2);source=sorted(new)[-1]
   from PIL import Image
   image=Image.open(source);image.thumbnail((1500,1000));image.save(run/f'{index:02d}-render.png');source.unlink();return
  time.sleep(.1)
 raise TimeoutError('frame dump')
try:
 for index in range(args.first,args.last+1):
  if index==args.first:
   for _ in range(index):key(68)
  else:key(68)
  key(32,2);key(32,1);wait(playing,'Babel stage did not become playable')
  target=run/f'{index:02d}-{names[index].replace(" ","-")}-{args.fps}-native.json'
  sample=subprocess.run([sys.executable,str(ROOT/'Source/tools/babel_native_sample.py'),'--pid',str(args.pid),'--seconds','2','--out',str(target)],capture_output=True,text=True)
  assert sample.returncode==0,sample.stderr
  row=json.loads(sample.stdout);row.update(index=index,name=names[index],fps=args.fps);results.append(row)
  capture(index)
  key(65,.4);key(68,.4);key(87,.4)
  key(27,1);wait(lambda:probe.puzzle()[0]==33,'pause did not open')
  key(83);key(83);key(32);key(87);key(32,3)
  wait(lambda:probe.puzzle()[1].get('status')!='valid','stage did not unload')
  key(16,2)
  (run/'roster-results.json').write_text(json.dumps(results,indent=2));print(json.dumps(row),flush=True)
finally:pad.close();probe.m.close()
