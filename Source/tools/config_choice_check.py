"""Owned hidden piano-menu QA; never uses the desktop mouse or starts a new game."""
from pathlib import Path
import ctypes as C,hashlib,json,os,re,shutil,struct,subprocess,sys,time
R=Path(__file__).resolve().parents[2];B=R/'Build';D=B/'config-choice-test';D.mkdir(exist_ok=True)
sys.path.insert(0,str(B/'perf/harness'));import bench as b

def main():
 protected={n:(B/n).read_bytes()for n in ('save.dat','catherine-settings.ini')}
 for n,data in protected.items():(D/(n+'.before')).write_bytes(data);(D/n).write_bytes(data)
 for n in ('assets-required.json','assets-verification.json','CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll'):shutil.copyfile(B/n,D/n)
 shutil.copyfile(B/('Catherine.exe' if '--baseline' in sys.argv else 'catherine1.exe'),D/'catherine1.exe');shutil.copyfile(B/('Catherine.map' if '--baseline' in sys.argv else 'catherine1.map'),D/'catherine1.map');shutil.copytree(B/'CathAssets',D/'CathAssets',dirs_exist_ok=True);b.BUILD=D
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
  text=(D/'catherine1.map').read_text();pref=int(re.search(r'Preferred load address is\s+([0-9a-fA-F]+)',text)[1],16)
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
  pointer(300,650,True);time.sleep(2)
  config_address=symbol(' catherine_config_pc.llvm.')
  def menu():return probe.m.q(config_address)
  def tab():return struct.unpack('<I',probe.m.read(menu()+8,4))[0]
  while tab()!=0:pad.key(ord('Q'));time.sleep(.7)
  capture('game-options-baseline')
  if '--baseline' in sys.argv:
   result=dict(passed=True,baseline=True)
  else:
   b.k.OpenProcess.argtypes=[C.c_uint32,C.c_int,C.c_uint32];b.k.OpenProcess.restype=C.c_void_p
   b.k.WriteProcessMemory.argtypes=[C.c_void_p,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
   h=b.k.OpenProcess(0x28,False,p.pid);assert h
   def write32(address,value):
    data=C.c_uint32(value);written=C.c_size_t();assert b.k.WriteProcessMemory(h,address,C.byref(data),4,C.byref(written))and written.value==4
   def u32(address):return struct.unpack('<I',probe.m.read(address,4))[0]
   preference=symbol(' ?state@?1??runtime@')+876
   game=probe.m.q(probe.m.q(0x7000cc9f60))
   def selection():return (u32(config_address+12),u32(menu()+16))
   original=(u32(game+0x9b58),u32(game+0x9b5c));initial=selection()
   for x in (775,920,1060,1190,1330):
    pointer(x,515);assert selection()==initial,('hover moved brush',x,selection(),initial)
   capture('button-prompts-hover-ps5')
   for i,x in enumerate((775,920,1060,1190,1330)):
    pointer(x,515,True);assert u32(preference)==i,('prompt click',i,u32(preference))
   pointer(840,515,True);assert u32(preference)==4,'slash activated an option'
   for x,y in ((435,300),(700,440)):
    pointer(x,y);assert selection()==initial
    pointer(x,y,True);assert (u32(game+0x9b58),u32(game+0x9b5c))==original
   capture('disabled-options-ignore-mouse')
   # Exercise the native active Game descriptor mode in this owned process only.
   write32(menu()+4,1);time.sleep(.6)
   pointer(435,300);assert selection()==initial;capture('active-safety-hover')
   for expected,x in ((3,435),(2,680),(1,920),(0,1170)):
    pointer(x,300,True);assert u32(game+0x9b58)==expected,('difficulty',expected,u32(game+0x9b58))
   before=selection();pointer(435,300);assert selection()==before,'active hover moved brush'
   capture('active-hover-keeps-hard-brush')
   pointer(710,440,True);assert u32(game+0x9b5c)==1,'Remix click'
   pointer(470,440,True);assert u32(game+0x9b5c)==0,'Classic click'
   capture('active-classic-click')
   write32(menu()+4,0);write32(game+0x9b58,original[0]);write32(game+0x9b5c,original[1]);b.k.CloseHandle(h)
   result=dict(passed=True,prompt_choices_clicked=5,slashes_inert=True,hover_preserves_brush=True,inactive_options_ignore_mouse=True,active_difficulty_choices_clicked=4,active_puzzle_types_clicked=2)

 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if p.poll() is None:p.terminate();p.wait(15)
  log.close()
  for n,data in protected.items():assert (B/n).read_bytes()==data,n+' changed'
 if result:(D/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
if __name__=='__main__':main()
