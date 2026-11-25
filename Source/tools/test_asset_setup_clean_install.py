"""Clean release installation on a private desktop, using only owned-window messages."""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,os,shutil,struct,subprocess,sys,time,zipfile
from ctypes import wintypes as W
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';OUT=BUILD/'asset-setup/clean-install-test'
u=C.WinDLL('user32',use_last_error=True);k=C.WinDLL('kernel32',use_last_error=True)
CALL=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
u.CreateDesktopW.argtypes=[W.LPCWSTR,W.LPCWSTR,C.c_void_p,W.DWORD,W.DWORD,C.c_void_p];u.CreateDesktopW.restype=W.HANDLE
u.CloseDesktop.argtypes=[W.HANDLE];u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
u.GetWindowTextW.argtypes=[W.HWND,W.LPWSTR,C.c_int];u.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
u.EnumWindows.argtypes=[CALL,W.LPARAM];u.EnumChildWindows.argtypes=[W.HWND,CALL,W.LPARAM]
u.GetDlgItem.argtypes=[W.HWND,C.c_int];u.GetDlgItem.restype=W.HWND
u.SendMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];u.SendMessageW.restype=C.c_ssize_t
u.SetWindowTextW.argtypes=[W.HWND,W.LPCWSTR];u.GetDlgCtrlID.argtypes=[W.HWND]
u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
u.GetParent.argtypes=[W.HWND];u.GetParent.restype=W.HWND;u.SetFocus.argtypes=[W.HWND];u.AttachThreadInput.argtypes=[W.DWORD,W.DWORD,W.BOOL]
k.GetCurrentThreadId.restype=W.DWORD
u.IsWindowVisible.argtypes=[W.HWND]
def text(h,cls=False):
 b=C.create_unicode_buffer(2048);(u.GetClassNameW if cls else u.GetWindowTextW)(h,b,len(b));return b.value
def windows(parent=None):
 found=[]
 @CALL
 def cb(h,_):
  pid=W.DWORD();u.GetWindowThreadProcessId(h,C.byref(pid));found.append(dict(hwnd=int(h),pid=pid.value,cls=text(h,True),text=text(h),id=u.GetDlgCtrlID(h),visible=bool(u.IsWindowVisible(h))));return True
 if parent:u.EnumChildWindows(parent,cb,0)
 else:u.EnumWindows(cb,0)
 return found
def event(kind,**data):
 with (OUT/'events.jsonl').open('a')as f:f.write(json.dumps(dict(time=time.time(),event=kind,**data))+'\n')
def snapshot(roots,label):
 result={}
 for root in roots:
  for p in sorted(root.rglob('*')):
   if not p.is_file():continue
   st=p.stat()
   with p.open('rb')as f:sha=hashlib.file_digest(f,'sha256').hexdigest()
   result[str(p)]=dict(size=st.st_size,sha256=sha,mtime_ns=st.st_mtime_ns,ctime_ns=st.st_ctime_ns,ino=st.st_ino,nlink=st.st_nlink,mode=st.st_mode)
   if len(result)%300==0:event('snapshot',phase=label,files=len(result))
 (OUT/(label+'.json')).write_text(json.dumps(result,indent=1));event('snapshot-complete',phase=label,files=len(result));return result
def drive_dialogs(proc,game,update,deadline):
 observed=set();selected=set();last_click={};progress=set();navigated=set();entered=set()
 while time.monotonic()<deadline and proc.poll()is None:
  for w in windows():
   title=w['text'];h=w['hwnd']
   if not w['visible']:continue
   if title in ['Catherine asset setup','Please Locate the Game Files','Please Locate the Update files']:
    children=windows(h)
    if (h,title)not in observed:event('dialog',title=title,controls=children);observed.add((h,title))
    if time.monotonic()-last_click.get(h,0)<1:continue
    if title=='Catherine asset setup':
     message=' '.join(c['text']for c in children if c['cls']=='Static');event('message',message=message)
     button=next((c['hwnd']for c in children if c['cls']=='Button'and c['text'].replace('&','')=='OK'),None);assert button,'message OK';u.PostMessageW(button,0xf5,0,0);last_click[h]=time.monotonic()
    else:
     edits=[c for c in children if c['cls']=='Edit' and c['visible']]
     if not edits:continue
     edit=next((c for c in edits if c['id']==1152),edits[-1]);folder=update if 'Update' in title else game
     button=u.GetDlgItem(h,1);assert button,'folder Select'
     if h not in navigated:
      tid=u.GetWindowThreadProcessId(edit['hwnd'],None);current=k.GetCurrentThreadId();assert u.AttachThreadInput(current,tid,True)
      try:
       u.SetFocus(edit['hwnd']);u.SendMessageW(edit['hwnd'],0xb1,0,-1)
       for char in str(folder):u.PostMessageW(edit['hwnd'],0x102,ord(char),1)
      finally:u.AttachThreadInput(current,tid,False)
      navigated.add(h);event('navigate-folder',title=title,path=str(folder))
     elif h not in entered:
      event('enter-folder',title=title,actual_text=text(edit['hwnd']));u.PostMessageW(edit['hwnd'],0x100,13,0x1c0001);u.PostMessageW(edit['hwnd'],0x101,13,0xc01c0001);entered.add(h)
     else:
      event('select-folder',title=title,path=str(folder));u.PostMessageW(button,0xf5,0,0);selected.add(title)
     last_click[h]=time.monotonic()
   elif 'Preparing Game Assets' in title:
    children=windows(h)
    for c in children:
     if c['text']in ['Checking game files','Preparing game modules','Generating Vulkan shaders','Building assets.pak','Verifying assets.pak']and c['text']not in progress:progress.add(c['text']);event('progress-stage',stage=c['text'])
  if (OUT/'stop').exists():raise RuntimeError('test stopped')
  yield selected,progress
  time.sleep(.1)
 if proc.poll()is not None:return
 raise TimeoutError('clean installation did not reach game startup')
def driver(mini):
 import tempfile
 root=Path(tempfile.mkdtemp(prefix='mini-install-',dir=OUT))if mini else OUT/'install-with-progress'
 if not mini:assert not root.exists(),f'Fixture already exists: {root}';root.mkdir(parents=True)
 private={name:(BUILD/name).read_bytes()for name in ['save.dat','catherine-settings.ini']}
 if mini:
  game=OUT/'mini-game';update=OUT/'mini-update';game.mkdir(exist_ok=True);update.mkdir(exist_ok=True);rows=[]
  for i,(folder,name)in enumerate([(game,'game.bin'),(update,'update.bin')]):
   data=name.encode()*4;(folder/name).write_bytes(data);rows.append(dict(path='romfs/'+name,size=len(data),sha256=hashlib.sha256(data).hexdigest(),kind='update'if i else'game',romfsOffset=i*100))
  (root/'assets-required.json').write_text(json.dumps(dict(version=1,files=rows,modules=[],shaders=[],romfsSize=200,romfsMetadata=[],packageSha256=[],canonicalPackageSha256='')))
  command=[str(BUILD/'asset-setup/publish/CatherineAssetSetup.exe'),'--root',str(root)]
 else:
  game=Path('C:/catherine/extract');update=Path('C:/catherine/WithUpdate')
  with zipfile.ZipFile(BUILD/'Catherine-Release.zip')as z:
   for name in z.namelist():
    relative=Path(name);assert not relative.is_absolute()and '..'not in relative.parts and '\\'not in name
    dest=root/relative
    if name.endswith('/'):dest.mkdir(parents=True,exist_ok=True)
    else:dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(z.read(name))
  candidate=BUILD/('catherine1.exe'if(BUILD/'catherine1.exe').exists()else'Catherine.exe');exe=root/'Catherine.exe'
  if candidate.name=='catherine1.exe':exe.write_bytes(candidate.read_bytes())
  assert hashlib.sha256(exe.read_bytes()).digest()==hashlib.sha256(candidate.read_bytes()).digest()
  shutil.copyfile(BUILD/(candidate.stem+'.map'),root/'Catherine.map')
  command=[str(exe)]
 assert not(root/'assets.pak').exists()and not(root/'Assets').exists()and not(root/'Pending').exists()and not(root/'assets-sources.json').exists()
 event('clean-state',root=str(root),command=command)
 baseline=OUT/'before.json'
 if not mini and baseline.exists():
  before=json.loads(baseline.read_text());current={str(p):p.stat()for base in [game,update]for p in base.rglob('*')if p.is_file()};assert set(before)==set(current)
  assert all(s.st_size==before[p]['size']and s.st_mtime_ns==before[p]['mtime_ns']and s.st_ctime_ns==before[p]['ctime_ns']and s.st_ino==before[p]['ino']and s.st_nlink==before[p]['nlink']for p,s in current.items());event('baseline-reused',files=len(before))
 else:before=snapshot([game,update],'mini-before'if mini else'before')
 env={n:v for n,v in os.environ.items()if not n.startswith(('NX_','CATHERINE_'))};env.update(NX_MUTE='1',CATHERINE_PERF_BACKGROUND='1',LOCALAPPDATA=str(root/'AppData'),TEMP=str(root/'Temp'),TMP=str(root/'Temp'),DOTNET_BUNDLE_EXTRACT_BASE_DIR=str(root/'Bundle'))
 for folder in ['AppData','Temp','Bundle']:(root/folder).mkdir()
 log=(root/'launch.log').open('wb');proc=subprocess.Popen(command,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT);event('started',pid=proc.pid)
 selected=set();stages=set();pad=probe=None;menu=False
 try:
  deadline=time.monotonic()+1200
  for selected,stages in drive_dialogs(proc,game,update,deadline):
   if mini and proc.poll()is not None:break
   if not mini and 'for fullscreen movie'in(root/'launch.log').read_text(errors='replace'):
    sys.path.insert(0,str(BUILD/'perf/harness'));import bench as b
    b.BUILD=root;pad=b.Pad(proc.pid);probe=b.Probe(proc.pid,root/'Catherine.exe');end=time.monotonic()+100
    while not probe.menu_ready()and time.monotonic()<end:
     assert proc.poll()is None;pad.key(32,.08);time.sleep(.15)
    assert probe.menu_ready(),'game main menu';menu=True;event('main-menu',pid=proc.pid);break
  if mini:assert proc.wait(timeout=10)==0
  else:assert menu,'game did not reach main menu'
  assert selected=={'Please Locate the Game Files','Please Locate the Update files'},selected
  assert (root/'assets.pak').exists();assert not(root/'Pending').exists(),'staging directory unexpectedly created'
  required={'Checking game files','Verifying assets.pak'}if mini else{'Checking game files','Generating Vulkan shaders','Building assets.pak','Verifying assets.pak'}
  assert required<=stages,stages
 finally:
  if pad:pad.close()
  if probe:probe.m.close()
  if proc.poll()is None:proc.terminate();proc.wait(15)
  log.close()
  # Terminate only the embedded installer spawned by this exact owned game PID.
  subprocess.run(['powershell','-NoProfile','-Command',f"Get-CimInstance Win32_Process -Filter \"ParentProcessId={proc.pid} AND Name='CatherineAssetSetup.exe'\" | ForEach-Object {{ Stop-Process -Id $_.ProcessId }}"],capture_output=True)
 after=snapshot([game,update],'mini-after'if mini else'after');assert before==after,'Original source tree changed'
 assert all((BUILD/n).read_bytes()==data for n,data in private.items()),'Active installation save/settings changed'
 with (root/'assets.pak').open('rb')as f:sha=hashlib.file_digest(f,'sha256').hexdigest()
 if not mini:
  expected=json.loads((root/'assets-required.json').read_text())['canonicalPackageSha256'];assert sha==expected,(sha,expected)
 result=dict(passed=True,mini=mini,source_files_unchanged=len(before),source_roots=[str(game),str(update)],folder_prompts=sorted(selected),progress_stages=sorted(stages),package_sha256=sha,game_main_menu=menu,active_save_settings_unchanged=True,root=str(root))
 (OUT/('mini-result.json'if mini else'result.json')).write_text(json.dumps(result,indent=2));event('passed',**result)
def parent(mini,command=None):
 OUT.mkdir(parents=True,exist_ok=True)
 desktop=u.CreateDesktopW('CatherineAssetSetupQA',None,None,0,0x1ff,None);assert desktop,C.get_last_error()
 class SI(C.Structure):_fields_=[('cb',W.DWORD),('lpReserved',W.LPWSTR),('lpDesktop',W.LPWSTR),('lpTitle',W.LPWSTR),('dwX',W.DWORD),('dwY',W.DWORD),('dwXSize',W.DWORD),('dwYSize',W.DWORD),('dwXCountChars',W.DWORD),('dwYCountChars',W.DWORD),('dwFillAttribute',W.DWORD),('dwFlags',W.DWORD),('wShowWindow',W.WORD),('cbReserved2',W.WORD),('lpReserved2',C.c_void_p),('hStdInput',W.HANDLE),('hStdOutput',W.HANDLE),('hStdError',W.HANDLE)]
 class PI(C.Structure):_fields_=[('hProcess',W.HANDLE),('hThread',W.HANDLE),('dwProcessId',W.DWORD),('dwThreadId',W.DWORD)]
 k.CreateProcessW.argtypes=[W.LPCWSTR,W.LPWSTR,C.c_void_p,C.c_void_p,W.BOOL,W.DWORD,C.c_void_p,W.LPCWSTR,C.POINTER(SI),C.POINTER(PI)];k.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD];k.GetExitCodeProcess.argtypes=[W.HANDLE,C.POINTER(W.DWORD)];k.CloseHandle.argtypes=[W.HANDLE]
 argv=command or([sys.executable,str(Path(__file__).resolve()),'--driver']+(['--mini']if mini else[]))
 si=SI();si.cb=C.sizeof(si);si.lpDesktop='CatherineAssetSetupQA';pi=PI();cmd=C.create_unicode_buffer(subprocess.list2cmdline(argv))
 assert k.CreateProcessW(argv[0],cmd,None,None,False,0x08000000,None,str(ROOT),C.byref(si),C.byref(pi)),C.get_last_error();k.CloseHandle(pi.hThread)
 try:
  print('Private desktop test driver',pi.dwProcessId,flush=True)
  while k.WaitForSingleObject(pi.hProcess,1000)==258:pass
  code=W.DWORD();k.GetExitCodeProcess(pi.hProcess,C.byref(code));assert code.value==0,('driver failed',code.value)
 finally:k.CloseHandle(pi.hProcess);u.CloseDesktop(desktop)
 if command:print('Private desktop layout test passed',flush=True)
 else:print((OUT/('mini-result.json'if mini else'result.json')).read_text(),flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--driver',action='store_true');p.add_argument('--mini',action='store_true');a=p.parse_args()
 try:(driver if a.driver else parent)(a.mini)
 except Exception as e:
  OUT.mkdir(parents=True,exist_ok=True);import traceback
  with(OUT/'failure.txt').open('a')as f:f.write(traceback.format_exc())
  raise
