"""Owned hidden bar/phone and first-stage artwork smoke check."""
from pathlib import Path
import ctypes as C,json,os,shutil,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];B=ROOT/'Build';OUT=B/'asset-setup/prompt-game-test'
sys.path.insert(0,str(B/'perf/harness'));import bench as b
def main():
 b.BUILD=OUT;shutil.copyfile(B/'save.dat',OUT/'save.dat');exe=OUT/'Catherine.exe'
 shutil.copyfile(B/'catherine1.exe',exe);shutil.copyfile(B/'catherine1.map',OUT/'Catherine.map');shutil.copytree(B/'CathAssets',OUT/'CathAssets',dirs_exist_ok=True)
 settings=OUT/'catherine-settings.ini';settings.write_text(settings.read_text().replace('ButtonPrompts=4','ButtonPrompts=0'))
 env={k:v for k,v in os.environ.items()if not k.startswith(('NX_','CATHERINE_'))};env.update(NX_HEADLESS='1',NX_MUTE='1',CATHERINE_PERF_BACKGROUND='1')
 log=(OUT/'art-game.log').open('wb');proc=subprocess.Popen([str(exe),'--assets-pak',str(B/'assets.pak')],cwd=OUT,env=env,stdout=log,stderr=subprocess.STDOUT);pad=probe=None
 def capture(name):
  frames=OUT/'frames';frames.mkdir(exist_ok=True);prior=set(frames.glob('*.ppm'));(OUT/'dump_frames').write_text('1 1\n');end=time.monotonic()+12
  while time.monotonic()<end:
   found=set(frames.glob('*.ppm'))-prior
   if found:
    time.sleep(.3);p=next(iter(found));p.replace(OUT/(name+'.ppm'));from PIL import Image
    im=Image.open(OUT/(name+'.ppm'));im.thumbnail((1600,1000));im.save(OUT/(name+'.png'));return
   time.sleep(.1)
  raise TimeoutError(name)
 try:
  end=time.monotonic()+90
  while 'for fullscreen movie'not in(OUT/'art-game.log').read_text(errors='replace'):assert proc.poll()is None and time.monotonic()<end;time.sleep(.1)
  pad=b.Pad(proc.pid);probe=b.Probe(proc.pid,exe)
  while not probe.menu_ready():assert time.monotonic()<end;pad.key(32,.08);time.sleep(.15)
  time.sleep(1);pad.key(192,.1);time.sleep(.7);panel=b.windows_of(proc.pid,'CatherineDevDirectStart')[0]
  b.dev_select(panel,101,'All destinations');time.sleep(.2);b.dev_select(panel,103,b.SCENES['bar']['label']);time.sleep(.2);b.u.SendMessageW(panel,0x111,107,int(b.u.GetDlgItem(panel,107)));time.sleep(.5);b.u.SendMessageW(panel,0x111,108,int(b.u.GetDlgItem(panel,108)))
  end=time.monotonic()+120
  while not probe.bar_ready()[0]:assert proc.poll()is None and time.monotonic()<end;time.sleep(.15)
  time.sleep(3);capture('art-bar')
  pad.key(ord('Z'),.12);time.sleep(3);capture('art-phone-keyboard')
  for family in [1,3,4,2]:
   C.memmove(pad.v,struct.pack('<QiiiiII',1<<4,0,0,0,0,family,0),32);time.sleep(.12);C.memmove(pad.v,struct.pack('<Q',0),8);time.sleep(.4);capture('art-phone-family-'+str(family))
  assert 'artwork recognized 11 ('in(OUT/'art-game.log').read_text(errors='replace'),'phone texture must use replacement artwork'
  (OUT/'phone-result.json').write_text(json.dumps(dict(passed=True,keyboard=True,auto_gamepad_families=[1,3,4,2],phone_atlas_recognized=True),indent=2))
  print('Captured hidden bar and phone input checks',flush=True)
 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if proc.poll()is None:proc.terminate();proc.wait(15)
  log.close()
if __name__=='__main__':main()
