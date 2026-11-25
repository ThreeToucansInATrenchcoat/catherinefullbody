"""Owned hidden piano-menu QA; never uses the desktop mouse or starts a new game."""
from pathlib import Path
import ctypes as C,hashlib,json,os,re,shutil,struct,subprocess,sys,time
R=Path(__file__).resolve().parents[2];B=R/'Build';D=B/'piano-mouse-test';D.mkdir(exist_ok=True)
sys.path.insert(0,str(B/'perf/harness'));import bench as b

def main():
 protected={n:(B/n).read_bytes()for n in ('save.dat','catherine-settings.ini')}
 for n,data in protected.items():(D/(n+'.before')).write_bytes(data);(D/n).write_bytes(data)
 for n in ('assets-required.json','assets-verification.json','CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll'):shutil.copyfile(B/n,D/n)
 shutil.copyfile(B/'catherine1.exe',D/'catherine1.exe');shutil.copyfile(B/'catherine1.map',D/'catherine1.map');shutil.copytree(B/'CathAssets',D/'CathAssets',dirs_exist_ok=True);b.BUILD=D
 env={k:v for k,v in os.environ.items()if not k.startswith(('NX_','CATHERINE_'))};env.update(NX_HEADLESS='1',NX_MUTE='1',CATHERINE_PERF_BACKGROUND='1')
 log=(D/'game.log').open('wb');exe=D/'catherine1.exe';p=subprocess.Popen([str(exe),'--assets-pak',str(B/'assets.pak')],cwd=D,env=env,stdout=log,stderr=subprocess.STDOUT);pad=probe=None;serial=click_serial=0;result={}
 def capture(name):
  frames=D/'frames';frames.mkdir(exist_ok=True);before={f:f.stat().st_mtime_ns for f in frames.glob('*.ppm')};(D/'dump_frames').write_text('1 1\n');end=time.monotonic()+15
  while time.monotonic()<end:
   found=[f for f in frames.glob('*.ppm')if f.stat().st_mtime_ns!=before.get(f)]
   if found:
    from PIL import Image
    for _ in range(30):
     try:Image.open(found[0]).resize((1600,900)).save(D/(name+'.png'));return
     except OSError:time.sleep(.1)
   time.sleep(.1)
  raise TimeoutError(name)
 try:
  end=time.monotonic()+180
  while 'for fullscreen movie' not in (D/'game.log').read_text(errors='replace'):assert p.poll() is None and time.monotonic()<end;time.sleep(.1)
  pad=b.Pad(p.pid);probe=b.Probe(p.pid,exe);end=time.monotonic()+120
  while not probe.menu_ready():assert time.monotonic()<end;pad.key(32,.08);time.sleep(.15)
  time.sleep(3)
  # The native hover state exposes the active selection field, not a fabricated UI.
  text=(B/'catherine1.map').read_text();pref=int(re.search(r'Preferred load address is\s+([0-9a-fA-F]+)',text)[1],16)
  mods=(C.c_void_p*1024)();need=C.c_uint32();assert b.k.K32EnumProcessModules(probe.m.handle,mods,C.sizeof(mods),C.byref(need))
  def symbol(fragment):
   rows=[l for l in text.splitlines()if fragment in l];assert len(rows)==1,(fragment,rows[:2]);return mods[0]+int(re.match(r'\s*\S+\s+\S+\s+([0-9a-fA-F]+)',rows[0])[1],16)-pref
  page_address=symbol(' catherine_piano_mouse.5 ')
  def page():return struct.unpack('<I',probe.m.read(page_address,4))[0]
  def selected(offset):return struct.unpack('<h',probe.m.read(probe.m.q(probe.menu)+offset,2))[0]
  def wait_page(expected):
   end=time.monotonic()+12
   while time.monotonic()<end:
    if page()==expected:time.sleep(.4);return
    time.sleep(.1)
   capture('failure-page');raise AssertionError(('page',page(),expected))
  def pointer(x,y,click=False):
   nonlocal serial,click_serial
   serial+=1
   if click:click_serial+=1
   C.memmove(pad.v+80,struct.pack('<iiIII',x,y,serial,click_serial,1),20);time.sleep(.6)
  if page()==0xb2:pad.key(16);wait_page(0xb0)
  assert page()==0xb0,hex(page())
  pointer(300,290);assert selected(0xb0)==0;pointer(300,290,True);wait_page(0xb2)
  pointer(400,490);assert selected(0xb2)==0;pointer(400,490,True);wait_page(0xba)
  capture('difficulty-initial');hovers=[]
  for i,y in enumerate((420,575,745,940)):
   pointer(300,y);assert page()==0xba and selected(0xba)==i,('difficulty',i,selected(0xba));hovers.append(i)
  pointer(300,745);capture('difficulty-normal-hover')
  # Idle pointer must not override keyboard selection.
  pad.key(ord('W'));time.sleep(.5);assert selected(0xba)==1;time.sleep(.8);assert selected(0xba)==1
  # Clicking a different row must select that row before confirming it.
  pointer(300,940,True);wait_page(0xbc);assert selected(0xba)==3
  capture('type-initial');types=[]
  for i,y in ((1,575),(0,420),(1,575)):
   pointer(300,y);assert page()==0xbc and selected(0xbc)==i,('type',i,selected(0xbc));types.append(i)
  capture('type-remix-hover')
  pad.key(ord('W'));time.sleep(.5);assert selected(0xbc)==0;time.sleep(.8);assert selected(0xbc)==0
  pointer(300,575,True);time.sleep(.8);capture('hard-remix-confirm')
  assert selected(0xba)==3 and selected(0xbc)==1
  result=dict(passed=True,difficulty_hovers=hovers,puzzle_type_hovers=types,clicked_difficulty=3,clicked_type=1,stationary_pointer_preserves_keyboard=True)
  # Cancel the confirmation; never write a new game's earned state.
  pad.key(16);time.sleep(.8)
 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if p.poll() is None:p.terminate();p.wait(15)
  log.close()
  for n,data in protected.items():assert (B/n).read_bytes()==data,n+' changed'
 if result:(D/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
if __name__=='__main__':main()
