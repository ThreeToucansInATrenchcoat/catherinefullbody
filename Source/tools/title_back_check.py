"""Owned hidden piano-menu QA; never uses the desktop mouse or starts a new game."""
from pathlib import Path
import ctypes as C,hashlib,json,os,re,shutil,struct,subprocess,sys,time
R=Path(__file__).resolve().parents[2];B=R/'Build';D=B/'title-back-test';D.mkdir(exist_ok=True)
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
  pointer(300,400);assert selected(0xb0)==1
  capture('main-before-back')
  config_address=symbol(' catherine_config_pc.llvm.')
  def config_open():return struct.unpack('<I',probe.m.read(config_address,4))[0]
  config_checks=[]
  for method in ('right_mouse','keyboard_back','controller_back'):
   pointer(300,650,True)
   end=time.monotonic()+10
   while not config_open():assert time.monotonic()<end;time.sleep(.05)
   time.sleep(.5);capture('config-'+method+'-open');print('Config test',method,flush=True)
   if method=='right_mouse':C.memmove(pad.v+28,struct.pack('<I',2),4)
   elif method=='keyboard_back':C.memmove(pad.v+28,struct.pack('<I',16),4)
   else:C.memmove(pad.v,struct.pack('<QiiiiII',1,0,0,0,0,1,0),32)
   time.sleep(.8);capture('config-'+method+'-cancel-question')
   # Native Config asks whether to discard. Confirm Yes with the mouse while
   # keeping Back held, then verify that same hold cannot escape the piano.
   pointer(960,385,True)
   capture('config-'+method+'-after-back')
   C.memset(pad.v,0,40)
   end=time.monotonic()+10
   while config_open():assert time.monotonic()<end;time.sleep(.05)
   time.sleep(2);assert page()==0xb0,('Config triggered a second Back',method,hex(page()))
   config_checks.append(method)
  capture('config-back-stopped-at-piano')
  tests=[]
  for method in ('right_mouse','keyboard_back','controller_back'):
   if method=='right_mouse':pad.key(2)
   elif method=='keyboard_back':pad.key(16)
   else:
    C.memmove(pad.v,struct.pack('<QiiiiII',1,0,0,0,0,1,0),32);time.sleep(.12);C.memset(pad.v,0,32)
   time.sleep(.10);capture(method+'-pan')
   time.sleep(3);capture(method+'-title')
   assert page()==0,('back did not reach title',method,hex(page()))
   time.sleep(.8);assert page()==0,('back input reopened menu',method)
   # Returning through the authored forward transition must restore hover.
   pad.key(13);wait_page(0xb0);time.sleep(1)
   pointer(300,400);assert selected(0xb0)==1
   from PIL import Image,ImageChops,ImageStat
   initial=Image.open(D/'main-before-back.png').convert('RGB')
   middle=Image.open(D/(method+'-pan.png')).convert('RGB')
   final=Image.open(D/(method+'-title.png')).convert('RGB')
   movement=sum(ImageStat.Stat(ImageChops.difference(initial,middle)).mean)
   destination=sum(ImageStat.Stat(ImageChops.difference(middle,final)).mean)
   assert movement>15 and destination>50,(method,movement,destination)
   tests.append(dict(method=method,mid_pan_difference=movement,title_difference=destination))
  capture('main-reentered')
  result=dict(passed=True,back_to_title=tests,reentry_mouse_works=True,config_back_stops_at_piano=config_checks)

 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if p.poll() is None:p.terminate();p.wait(15)
  log.close()
  for n,data in protected.items():assert (B/n).read_bytes()==data,n+' changed'
 if result:(D/'validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
if __name__=='__main__':main()
