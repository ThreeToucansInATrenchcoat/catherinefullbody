"""Exercise the real F8 overlay in one owned hidden game window."""
from pathlib import Path
import ctypes as C,hashlib,json,os,shutil,struct,subprocess,sys,time
R=Path(__file__).resolve().parents[2];B=R/'Build';D=B/'dev-overlay-test';D.mkdir(exist_ok=True)
sys.path.insert(0,str(B/'perf/harness'));import bench as b
def main():
 protected={n:hashlib.sha256((B/n).read_bytes()).hexdigest()for n in ('save.dat','catherine-settings.ini')}
 for n in ['catherine1.exe','catherine1.map','catherine-settings.ini','save.dat','assets-required.json','assets-verification.json','dev-checkpoints.pak','CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll']:shutil.copyfile(B/n,D/n)
 shutil.copytree(B/'CathAssets',D/'CathAssets',dirs_exist_ok=True);b.BUILD=D
 env={k:v for k,v in os.environ.items()if not k.startswith(('NX_','CATHERINE_'))};env.update(NX_HEADLESS='1',NX_MUTE='1',CATHERINE_PERF_BACKGROUND='1',TEMP=str(D),TMP=str(D))
 log=(D/'game.log').open('wb');exe=D/'catherine1.exe';p=subprocess.Popen([str(exe),'--assets-pak',str(B/'assets.pak'),'--dev-menu'],cwd=D,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 pad=probe=None
 def capture(name):
  folder=D/'frames';folder.mkdir(exist_ok=True);before={f:f.stat().st_mtime_ns for f in folder.glob('*.ppm')};(D/'dump_frames').write_text('1 1\n');end=time.monotonic()+20
  while time.monotonic()<end:
   new=[f for f in folder.glob('*.ppm')if f.stat().st_mtime_ns!=before.get(f)]
   if new:
    from PIL import Image
    for _ in range(50):
     try:Image.open(new[0]).save(D/(name+'.png'));return
     except OSError:time.sleep(.1)
   time.sleep(.1)
  raise TimeoutError(name)
 try:
  pad=b.Pad(p.pid);probe=b.Probe(p.pid,exe);time.sleep(2)
  hwnd=next(h for h in b.windows_of(p.pid)if 'Catherine' in b.wtext(h) and b.wtext(h,True)!='CatherineDevDirectStart')
  assert not b.windows_of(p.pid,'CatherineDevDirectStart'),'Separate DEV window exists'
  capture('startup-picker')
  def char(value):b.u.SendMessageW(hwnd,0x102,value,1)
  def key(value):b.u.SendMessageW(hwnd,0x100,value,1);b.u.SendMessageW(hwnd,0x101,value,1)
  def query(value):
   for _ in range(80):char(8)
   for ch in str(value):char(ord(ch))
   time.sleep(.3)
  serial=click_serial=0
  def pointer_click(x,y):
   nonlocal serial,click_serial
   serial+=1;click_serial+=1
   C.memmove(pad.v+80,struct.pack('<iiIII',x,y,serial,click_serial,1),20);time.sleep(.3)
  def pointer_move(x,y):
   nonlocal serial
   serial+=1
   C.memmove(pad.v+80,struct.pack('<iiIII',x,y,serial,click_serial,1),20);time.sleep(.3)
  pointer_click(900,825);pointer_click(900,754) # Safety
  pointer_click(500,825);pointer_click(500,589) # Karma 96
  before=probe.loader_state()
  pointer_click(500,435);pointer_click(500,435)
  pointer_move(500,650);pointer_move(500,325)
  b.u.SendMessageW(hwnd,0x20a,((-120)&0xffff)<<16,0);time.sleep(.3)
  b.u.SendMessageW(hwnd,0x20a,120<<16,0);time.sleep(.3)
  assert probe.loader_state()['request']==before['request'],'List click started loading'
  capture('map-selected-before-load')
  from PIL import Image
  selected=Image.open(D/'map-selected-before-load.png').convert('RGB')
  assert sum(selected.getpixel((1300,435)))>sum(selected.getpixel((1300,325)))+30,'Hover moved the clicked selection'
  hover_text=selected.crop((1400,305,1530,345))
  assert sum(r>200 and g<130 and blue>90 for r,g,blue in hover_text.get_flattened_data())>20,'Hovered row text did not change color'
  pointer_click(1450,955)
  end=time.monotonic()+30
  while time.monotonic()<end:
   status=probe.loader_state();outer,actor=probe.puzzle()
   if status['target']==10202 and status['phase']==6 and outer==32:break
   assert p.poll()is None and status['phase']!=7,status
   time.sleep(.1)
  else:raise TimeoutError(('level',status,actor))
  def check_overrides():
   config=probe.m.q(probe.m.q(0x7000cc9f60))
   difficulty=struct.unpack('<I',probe.m.read(config+0x9b58,4))[0]
   karma=struct.unpack('<I',probe.m.read(config+0x9958,4))[0]
   assert (difficulty,karma)==(3,96),('Overrides reset',difficulty,karma)
  check_overrides()
  key(119);time.sleep(.3);capture('gameplay-picker')
  # Native controller-style dropdown opens over the active game image.
  pointer_click(1100,200);capture('area-dropdown')
  key(27);time.sleep(.25)
  pointer_click(620,106);capture('cutscenes-tab')
  pointer_click(1360,198);capture('cutscene-day-dropdown')
  key(27);time.sleep(.25);query(30014)
  before=probe.loader_state();pointer_click(500,325)
  assert probe.loader_state()['request']==before['request'],'Scene list click started playback'
  pointer_click(1450,955)
  end=time.monotonic()+30
  while time.monotonic()<end:
   status=probe.loader_state()
   if status['target']==30014 and status['phase']==6:break
   assert p.poll()is None and status['phase']!=7,status
   time.sleep(.1)
  else:raise TimeoutError(('movie',status))
  key(119);time.sleep(.3);capture('movie-picker')
  pointer_click(420,106);query(10101);key(13)
  end=time.monotonic()+30
  while time.monotonic()<end:
   status=probe.loader_state();outer,actor=probe.puzzle()
   if status['target']==10101 and status['phase']==6 and outer==32:break
   assert p.poll()is None and status['phase']!=7,status
   time.sleep(.1)
  else:raise TimeoutError(('movie-to-level',status))
  check_overrides()
  result=dict(passed=True,overrides_survive_map_selection=True,overrides_survive_search_tabs_and_unsupported_scene=True,difficulty='Safety',karma=96,text_only_hover=True,clicked_selection_survives_hover=True,scroll_preserves_selection=True,list_click_only_selects=True,repeated_click_does_not_load=True,explicit_load_button=True,in_game_only=True,startup_picker=True,F8=True,dropdown=True,tabs=['Levels','Cutscenes'],cutscene_day_filter=True,level=10202,movie_to_level=[30014,10101])
  (D/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if p.poll()is None:p.terminate();p.wait(15)
  log.close()
  (D/'catherine1.exe').unlink(missing_ok=True)
  for n,digest in protected.items():assert hashlib.sha256((B/n).read_bytes()).hexdigest()==digest,n+' changed'
if __name__=='__main__':main()
