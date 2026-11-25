"""Hidden, muted startup check using an owned PID and per-process input only."""
from pathlib import Path
import argparse,hashlib,os,subprocess,sys,time,json
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';sys.path.insert(0,str(BUILD/'perf/harness'))
import bench as b
def main():
 p=argparse.ArgumentParser();p.add_argument('--pack',default=str(BUILD/'assets.pak'));p.add_argument('--scenes',action='store_true');a=p.parse_args();out=BUILD/'asset-setup/game-test';out.mkdir(exist_ok=True)
 protected={f:(BUILD/f).read_bytes() for f in ['save.dat','catherine-settings.ini']};env={k:v for k,v in os.environ.items() if not k.startswith(('NX_','CATHERINE_'))};env.update(NX_HEADLESS='1',NX_MUTE='1',CATHERINE_PERF_BACKGROUND='1')
 proc=None;pad=None;probe=None
 try:
  with (out/'startup.log').open('wb') as log:
   exe=BUILD/'catherine1.exe';proc=subprocess.Popen([str(exe),'--assets-pak',str(Path(a.pack).resolve())],cwd=BUILD,env=env,stdout=log,stderr=subprocess.STDOUT);end=time.monotonic()+180
   while time.monotonic()<end:
    assert proc.poll() is None,('early exit',proc.returncode,(out/'startup.log').read_text(errors='replace')[-3000:])
    text=(out/'startup.log').read_text(errors='replace')
    if 'for fullscreen movie' in text:break
    time.sleep(.2)
   else:raise TimeoutError('startup movie')
   pad=b.Pad(proc.pid);probe=b.Probe(proc.pid,exe)
   while time.monotonic()<end and not probe.menu_ready():pad.key(32,.08);time.sleep(.15)
   assert probe.menu_ready(),'main menu not reached';time.sleep(2)
   scenes=[]
   if a.scenes:
    for scene in ['bar','boss']:
     pad.key(192,.08);time.sleep(.5);panels=b.windows_of(proc.pid,'CatherineDevDirectStart');assert panels,'DEV panel';panel=panels[0]
     b.dev_select(panel,101,'All destinations');time.sleep(.2);b.dev_select(panel,103,b.SCENES[scene]['label']);time.sleep(.2)
     if scene=='boss':
      choices=b.combo_items(b.u.GetDlgItem(panel,105));difficulty=next((s for s in ['Safety','Very Easy','Easy']if s in choices),'Normal');b.dev_select(panel,105,difficulty)
     b.u.SendMessageW(panel,0x111,107,int(b.u.GetDlgItem(panel,107)));time.sleep(.5);b.u.SendMessageW(panel,0x111,108,int(b.u.GetDlgItem(panel,108)))
     end=time.monotonic()+150;last=0;skip=0
     while time.monotonic()<end:
      assert proc.poll() is None,'game exited in scene'
      if scene=='bar':ready,_=probe.bar_ready()
      else:
       ready,outer,actor=probe.boss_active()
       if (actor.get('stage_number'),actor.get('substage'))==(9,6):
        if outer==9 and time.monotonic()-skip>4:pad.key(27,.1);time.sleep(.15);pad.key(17,.1);skip=time.monotonic()
        if outer in (8,18) and time.monotonic()-last>1.2:pad.key(32,.1);last=time.monotonic()
      if ready:break
      time.sleep(.15)
     else:raise TimeoutError('scene '+scene)
     time.sleep(5);scenes.append(scene);print('Loaded hidden scene',scene,flush=True)
     frames=BUILD/'frames';frames.mkdir(exist_ok=True);before=set(frames.glob('*.ppm'));(BUILD/'dump_frames').write_text('1 1\n');end=time.monotonic()+10
     while time.monotonic()<end:
      captures=set(frames.glob('*.ppm'))-before
      if captures:
       time.sleep(.3);source=next(iter(captures));source.replace(out/(scene+'.ppm'));break
      time.sleep(.1)
   (out/'result.json').write_text(json.dumps(dict(passed=True,pid=proc.pid,pack=a.pack,main_menu=True,scenes=scenes),indent=2));print('Hidden game validation passed',flush=True)
 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if proc and proc.poll() is None:proc.terminate();proc.wait(15)
  for f,data in protected.items():
   if (BUILD/f).read_bytes()!=data:(BUILD/f).write_bytes(data)
if __name__=='__main__':main()
