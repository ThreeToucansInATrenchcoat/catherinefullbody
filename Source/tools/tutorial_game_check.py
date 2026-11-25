"""Hidden muted native tutorial QA, controlled only through the owned process map."""
from pathlib import Path
import ctypes as C,json,os,shutil,struct,subprocess,sys,time
R=Path(__file__).resolve().parents[2];B=R/'Build';OUT=B/'tutorial-text-audit/game-test';OUT.mkdir(exist_ok=True)
sys.path.insert(0,str(B/'perf/harness'));import bench as b
def main():
 for n in ['CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll','dev-checkpoints.pak','assets-required.json']:shutil.copyfile(B/n,OUT/n)
 exe=OUT/'Catherine.exe';shutil.copyfile(B/'catherine1.exe',exe);shutil.copyfile(B/'catherine1.map',OUT/'Catherine.map');shutil.copytree(B/'CathAssets',OUT/'CathAssets',dirs_exist_ok=True);b.BUILD=OUT
 if not(OUT/'catherine-settings.ini').exists():(OUT/'catherine-settings.ini').write_text('[Catherine]\nVersion=6\nFps=60\nButtonPrompts=0\n')
 command=OUT/'command.json';command.unlink(missing_ok=True);log=(OUT/'game.log').open('wb');env=dict(os.environ,NX_HEADLESS='1',NX_MUTE='1',CATHERINE_PERF_BACKGROUND='1');proc=subprocess.Popen([str(exe),'--assets-pak',str(B/'assets.pak')],cwd=OUT,env=env,stdout=log,stderr=subprocess.STDOUT);pad=probe=None
 def capture(name):
  frames=OUT/'frames';frames.mkdir(exist_ok=True);before={p:p.stat().st_mtime_ns for p in frames.glob('*.ppm')};(OUT/'dump_frames').write_text('1 1\n');end=time.monotonic()+12
  while time.monotonic()<end:
   found={p for p in frames.glob('*.ppm') if p.stat().st_mtime_ns!=before.get(p)}
   if found:
    time.sleep(.25);from PIL import Image
    for _ in range(30):
     try:Image.open(next(iter(found))).resize((1600,900)).save(OUT/(name+'.png'));return
     except OSError:time.sleep(.1)
    raise OSError('incomplete frame')
   time.sleep(.1)
  raise TimeoutError('capture '+name)
 try:
  end=time.monotonic()+120
  while 'for fullscreen movie'not in(OUT/'game.log').read_text(errors='replace'):assert proc.poll()is None and time.monotonic()<end;time.sleep(.1)
  pad=b.Pad(proc.pid);probe=b.Probe(proc.pid,exe);end=time.monotonic()+120
  while not probe.menu_ready():assert time.monotonic()<end;pad.key(32,.08);time.sleep(.15)
  if not os.environ.get('CATHERINE_TUTORIAL_QA_NEW_GAME'):
   time.sleep(1);pad.key(192,.1);time.sleep(.7);panel=b.windows_of(proc.pid,'CatherineDevDirectStart')[0];assert b.dev_select(panel,101,'All destinations');time.sleep(.2);assert b.dev_select(panel,103,b.SCENES['stage1']['label']);time.sleep(.2)
   choices=b.combo_items(b.u.GetDlgItem(panel,105));difficulty=next((x for x in ['Safety','Very Easy','Easy']if x in choices),'Normal');b.dev_select(panel,105,difficulty);b.u.SendMessageW(panel,0x111,107,int(b.u.GetDlgItem(panel,107)));time.sleep(.5);b.u.SendMessageW(panel,0x111,108,int(b.u.GetDlgItem(panel,108)))
   end=time.monotonic()+120
   while time.monotonic()<end:
    outer,actor=probe.puzzle()
    if (actor.get('stage_number'),actor.get('substage'))==(1,1)and outer in (9,32):break
    if outer in (8,18):pad.key(32,.1)
    time.sleep(.3)
  if os.environ.get('CATHERINE_TUTORIAL_QA_NEW_GAME'):time.sleep(3)
  if os.environ.get('CATHERINE_TUTORIAL_QA_AUTOMATE'):
   for _ in range(5):pad.key(32);time.sleep(1.5)
   time.sleep(4)
   for _ in range(8):
    outer,actor=probe.puzzle()
    if actor.get('stage_number')==1:break
    pad.key(27);time.sleep(.5);pad.key(17);time.sleep(10)
   time.sleep(40);pad.key(69);time.sleep(2)
   for _ in range(45):
    if 'Tutorial input snapshot:' in (OUT/'game.log').read_text(errors='replace'):break
    pad.key(32);time.sleep(1.2)
   capture('automated-flow-final')
   assert 'Tutorial input snapshot:' in (OUT/'game.log').read_text(errors='replace')
   capture('automated-grab-keyboard')
   for family in (1,2,3,4):
    C.memmove(pad.v,struct.pack('<QiiiiII',4,0,0,0,0,family,0),32);time.sleep(.2);C.memmove(pad.v,struct.pack('<Q',0),8);time.sleep(.5);capture('automated-grab-frozen-'+str(family))
   print('Native tutorial opened; switched all controller families without advancing',flush=True)
  capture('initial');print('Stage 1 ready for owned QA commands',flush=True);last=0;end=time.monotonic()+1800
  while time.monotonic()<end:
   assert proc.poll()is None
   outer,actor=probe.puzzle();(OUT/'state.json').write_text(json.dumps(dict(pid=proc.pid,outer=outer,actor=actor,last=last)))
   if command.exists():
    try:instructions=json.loads(command.read_text())
    except json.JSONDecodeError:time.sleep(.1);continue
    command.unlink()
    for item in instructions['actions']:
     if 'key'in item:pad.key(item['key'],item.get('hold',.1))
     if 'buttons'in item:
      C.memmove(pad.v,struct.pack('<QiiiiII',item['buttons'],0,0,0,0,item.get('family',1),0),32);time.sleep(item.get('hold',.15));C.memmove(pad.v,struct.pack('<Q',0),8)
     time.sleep(item.get('wait',.4))
     if 'capture'in item:capture(item['capture'])
    last=instructions['id'];print('Completed QA command',last,flush=True)
    if instructions.get('quit'):break
   time.sleep(.2)
 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if proc.poll()is None:proc.terminate();proc.wait(15)
  log.close()
if __name__=='__main__':main()
