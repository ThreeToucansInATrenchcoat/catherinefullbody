"""Direct startup and live scene changes in an owned hidden game process."""
from pathlib import Path
import ctypes as C, hashlib, json, os, re, shutil, struct, subprocess, sys, time
R=Path(__file__).resolve().parents[2];B=R/'Build';D=B/'direct-level-test';D.mkdir(exist_ok=True)
sys.path.insert(0,str(B/'perf/harness'));import bench as b
def main():
 ids=([int(x)for x in sys.argv[1:]]if sys.argv[1:]!=['all']else[])or[10101,10906,5100,11004,10202]
 targets={}
 for line in (R/'Source/imports/base/dev-start/runtime/dev_loader_catalog.inc').read_text().splitlines():
  match=re.match(r'\{\{1,(\d+),(\d+),(\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),"([^"]+)"',line)
  if match:
   values=[int(x)for x in match.groups()[:-1]]+[match[10]]
   row=dict(zip(['id','kind','caps','slot','day','stage','floor','difficulty','route','label'],values));targets[row['id']]=row
 if sys.argv[1:]==['all']:ids=[i for i in targets if i!=5000]
 protected={n:hashlib.sha256((B/n).read_bytes()).hexdigest()for n in ('save.dat','catherine-settings.ini')}
 for n in ['catherine1.exe','catherine1.map','save.dat','catherine-settings.ini','assets-required.json','assets-verification.json','dev-checkpoints.pak','CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll']:shutil.copyfile(B/n,D/n)
 shutil.copytree(B/'CathAssets',D/'CathAssets',dirs_exist_ok=True);b.BUILD=D
 env={k:v for k,v in os.environ.items()if not k.startswith(('NX_','CATHERINE_'))};env.update(NX_HEADLESS='1',NX_MUTE='1',CATHERINE_PERF_BACKGROUND='1',CATHERINE_TRACE_BOOT='1',TEMP=str(D),TMP=str(D))
 exe=D/'catherine1.exe';log=(D/'game.log').open('wb');p=subprocess.Popen([str(exe),'--assets-pak',str(B/'assets.pak'),'--dev-level',str(ids[0])],cwd=D,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 pad=probe=None;results=[]
 try:
  start=time.monotonic();pad=b.Pad(p.pid);probe=b.Probe(p.pid,exe)
  for index,id in enumerate(ids):
   if index:
    if index==1 and os.environ.get('CATHERINE_DIRECT_QA_PAUSE'):
     pad.key(27);deadline=time.monotonic()+10
     while probe.puzzle()[0]!=33:
      assert time.monotonic()<deadline,'Puzzle Pause did not open'
      time.sleep(.1)
     print('Loading directly from native puzzle Pause',flush=True)
    pad.key(192);time.sleep(.4);panel=b.windows_of(p.pid,'CatherineDevDirectStart')[0]
    assert b.dev_select(panel,101,'All destinations');assert b.dev_select(panel,103,targets[id]['label'])
    b.u.SendMessageW(panel,0x111,107,int(b.u.GetDlgItem(panel,107)))
   entered=time.monotonic();trail=[];last=None
   while time.monotonic()-entered<100:
    assert p.poll()is None,('Game exited',p.returncode,id)
    status=probe.loader_state();outer,actor=probe.puzzle()
    try:root=probe.m.q(probe.m.q(0x7000d02610)+0x10);current=struct.unpack('<i',probe.m.read(root+4,4))[0]
    except OSError:current=None
    sample=(current,outer,actor.get('stage_number'),actor.get('substage'),status['phase'])
    if sample!=last:trail.append([round(time.monotonic()-entered,2),*sample]);last=sample;print(id,trail[-1],status['message'],flush=True)
    if status['phase']==7:raise AssertionError(status)
    if status['target']==id and status['phase']==6:
     if targets[id]['kind']==1:assert outer==32 and (actor.get('stage_number'),actor.get('substage'))==(targets[id]['stage'],targets[id]['floor']),(id,actor)
     break
    time.sleep(.15)
   else:raise TimeoutError((id,status,trail))
   results.append(dict(id=id,seconds=round(time.monotonic()-entered,2),trail=trail,piano_seen=probe.menu_ready()))
   (D/'validation.json').write_text(json.dumps(results,indent=2));time.sleep(.25)
 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if p.poll()is None:p.terminate();p.wait(15)
  log.close()
  for n,digest in protected.items():assert hashlib.sha256((B/n).read_bytes()).hexdigest()==digest,n+' changed'
  for n in ['catherine1.exe']:
   if (D/n).exists():(D/n).unlink()
 print('PASS',len(results),'destinations',flush=True)
if __name__=='__main__':main()
