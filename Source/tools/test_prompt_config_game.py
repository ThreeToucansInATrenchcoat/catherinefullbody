"""Fresh settings and live prompt-family checks on an owned hidden game only."""
from pathlib import Path
import ctypes as C,hashlib,json,os,re,shutil,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];B=ROOT/'Build';OUT=B/'asset-setup/prompt-game-test';OUT.mkdir(exist_ok=True)
sys.path.insert(0,str(B/'perf/harness'));import bench as b
def main():
 assert not(OUT/'catherine-settings.ini').exists(),'Fresh fixture required'
 for name in ['CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll','dev-checkpoints.pak','assets-required.json']:shutil.copyfile(B/name,OUT/name)
 exe=OUT/'Catherine.exe';shutil.copyfile(B/'catherine1.exe',exe);shutil.copyfile(B/'catherine1.map',OUT/'Catherine.map');shutil.copytree(B/'CathAssets',OUT/'CathAssets',dirs_exist_ok=True)
 before={n:(B/n).read_bytes()for n in ['save.dat','catherine-settings.ini']};env={k:v for k,v in os.environ.items()if not k.startswith(('NX_','CATHERINE_'))};env.update(NX_HEADLESS='1',NX_MUTE='1',CATHERINE_PERF_BACKGROUND='1');b.BUILD=OUT
 log=(OUT/'launch.log').open('wb');proc=subprocess.Popen([str(exe),'--assets-pak',str(B/'assets.pak')],cwd=OUT,env=env,stdout=log,stderr=subprocess.STDOUT);pad=probe=None;result={}
 def capture(name):
  frames=OUT/'frames';frames.mkdir(exist_ok=True);prior=set(frames.glob('*.ppm'));(OUT/'dump_frames').write_text('1 1\n');end=time.monotonic()+12
  while time.monotonic()<end:
   found=set(frames.glob('*.ppm'))-prior
   if found:
    time.sleep(.25);p=next(iter(found));p.replace(OUT/(name+'.ppm'));from PIL import Image
    im=Image.open(OUT/(name+'.ppm'));im.thumbnail((1600,1000));im.save(OUT/(name+'.png'));return
   time.sleep(.1)
  raise TimeoutError('capture '+name)
 try:
  start=time.monotonic();end=start+240
  while 'for fullscreen movie'not in(OUT/'launch.log').read_text(errors='replace'):
   assert proc.poll()is None,'game exited';assert time.monotonic()<end,'startup timeout';time.sleep(.1)
  pad=b.Pad(proc.pid);probe=b.Probe(proc.pid,exe)
  while not probe.menu_ready():assert time.monotonic()<end;pad.key(32,.08);time.sleep(.15)
  result['first_main_menu_seconds']=time.monotonic()-start
  # Resolve read-only native symbols from the exact candidate's map.
  text=(OUT/'Catherine.map').read_text(errors='replace');preferred=int(re.search(r'Preferred load address is\s+([0-9a-fA-F]+)',text)[1],16);mods=(C.c_void_p*1024)();need=C.c_uint32();assert b.k.K32EnumProcessModules(probe.m.handle,mods,C.sizeof(mods),C.byref(need));base=mods[0]
  def symbol(fragment):
   rows=[l for l in text.splitlines()if fragment in l];assert len(rows)==1,(fragment,len(rows));return base+int(re.match(r'\s*\S+\s+\S+\s+([0-9a-fA-F]+)',rows[0])[1],16)-preferred
  pc=symbol(' catherine_config_pc.llvm.');runtime=symbol(' ?state@?1??runtime@');art=symbol(' ?cached@?1??configuration@')
  def i(address):return struct.unpack('<i',probe.m.read(address,4))[0]
  values=runtime+112
  result['fresh_defaults']=dict(fps=i(values+72),resolution=i(values+80),anti_aliasing=i(values+84),fmv_interpolation=i(values+88),button_prompts=i(values+232),p1_device=probe.m.read(values+168,16).split(b'\0')[0].decode())
  assert result['fresh_defaults']==dict(fps=60,resolution=0,anti_aliasing=0,fmv_interpolation=0,button_prompts=0,p1_device='auto'),result
  holder=probe.m.q(0x7000cc9f60);save=probe.m.q(holder);result['brightness_native']=struct.unpack('b',probe.m.read(save+0x2fb,1))[0];assert result['brightness_native']==0,'brightness midpoint'
  time.sleep(1.5)
  # A confirm used to leave the title movie can enter Story on a fresh save.
  rows=symbol(' catherine_piano_mouse.4 ')
  if i(rows)==2:pad.key(16,.1);time.sleep(1)
  assert i(rows) in (5,6),('main menu rows',i(rows))
  if '--face-swap' in sys.argv:
   faces=[]
   for family in [1,3,4,2]:
    for physical,expected in [(1,1),(0,2)]:
     C.memmove(pad.v,struct.pack('<QiiiiII',1<<physical,0,0,0,0,family,0),32);time.sleep(.12)
     output=struct.unpack('<Q',C.string_at(pad.v+48,8))[0];assert output==expected,('face',family,physical,output,expected)
     C.memmove(pad.v,struct.pack('<Q',0),8);time.sleep(1.2);faces.append(dict(family=family,physical=physical,logical=output))
   result['face_swap']=faces
   assert i(rows) in (5,6),('returned to main',i(rows))
  C.memmove(pad.v+80,struct.pack('<iiIII',400,650,1,1,1),20);time.sleep(2);C.memmove(pad.v+96,struct.pack('<I',0),4);end=time.monotonic()+15
  while time.monotonic()<end:
   menu=probe.m.q(pc)
   if menu and i(menu+12)==4:break
   time.sleep(.1)
  else:
   capture('config-navigation-failure');print('Config pointer',hex(menu),'rows',i(rows),flush=True);raise TimeoutError('Config page')
  time.sleep(.6)
  for _ in range(5):
   if i(menu+8)==0:break
   pad.key(ord('Q'),.1);time.sleep(.4)
  assert i(menu+8)==0,('Game page',i(menu+8));capture('game-page-initial')
  serial=0
  def pointer(x,y,click=False):
   nonlocal serial;serial+=1;C.memmove(pad.v+80,struct.pack('<iiIII',x,y,serial,serial if click else 0,1),20);time.sleep(.15);C.memmove(pad.v+96,struct.pack('<I',0),4);time.sleep(.15)
  def gamepad(family):
   C.memmove(pad.v,struct.pack('<QiiiiII',1<<4,0,0,0,0,family,0),32);time.sleep(.15);C.memmove(pad.v,struct.pack('<Q',0),8);time.sleep(.25)
  cases=[]
  for choice in range(5):
   pointer(770+140*choice,520,True);assert i(values+232)==choice,('selection',choice,i(values+232))
   for family in [1,3,4,2]:
    gamepad(family);actual=i(art+4);expected=[family,1,3,4,2][choice];assert actual==expected,(choice,family,actual,expected);assert i(art+8)==family,'tutorial must ignore prompt override';cases.append(dict(preference=choice,active=family,rendered=actual,tutorial=i(art+8)))
   pad.key(ord('M'),.08);time.sleep(.25);assert i(art+4)==0,'keyboard must override forced gamepad prompts';assert i(art+8)==0,'keyboard tutorials follow actual input';cases.append(dict(preference=choice,active=0,rendered=0,tutorial=0))
  result['live_prompt_cases']=cases;capture('game-page-keyboard');gamepad(2);capture('game-page-ps5')
  def button(index):
   C.memmove(pad.v,struct.pack('<QiiiiII',1<<index,0,0,0,0,2,0),32);time.sleep(.12);C.memmove(pad.v,struct.pack('<Q',0),8);time.sleep(.35)
  button(13);assert i(pc+12)==0,'Dpad Up leaves added row'
  button(15);assert i(pc+12)==1,'Dpad Down reaches added row'
  button(12);assert i(values+232)==3,'Dpad Left changes preference'
  button(14);assert i(values+232)==4,'Dpad Right changes preference'
  result['dpad_navigation']=True
  # Footer Confirm saves the main Config draft through its normal native question.
  pointer(1760,1000,True);time.sleep(.4);pad.key(32,.1);time.sleep(.7)
  ini=OUT/'catherine-settings.ini'
  if ini.exists():result['saved_ini']=ini.read_text();assert 'ButtonPrompts=4'in result['saved_ini']
  else:raise AssertionError('Config did not persist the preference')
  result['passed']=True
 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if proc.poll()is None:proc.terminate();proc.wait(15)
  log.close();assert all((B/n).read_bytes()==v for n,v in before.items()),'Active save/settings changed'
 (OUT/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
if __name__=='__main__':main()
