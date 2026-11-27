"""The sole game build entry point. All outputs stay under Build/."""
from pathlib import Path
import os,sys,argparse,json,subprocess,concurrent.futures,time,hashlib,functools,shutil
R=Path(__file__).resolve().parent;S=R/'Source';B=R/'Build';M=json.loads((S/'build-manifest.json').read_text());p=argparse.ArgumentParser();p.add_argument('--rebuild',action='store_true');p.add_argument('--check-dependencies',action='store_true');p.add_argument('--output');p.add_argument('--jobs',type=int,default=4);p.add_argument('--pgo',action='store_true',help='then rebuild with Build/perf/pgo/merged.profdata + ThinLTO (the release configuration)');a=p.parse_args()
if sys.platform == 'linux':
 sys.dont_write_bytecode=True
 from Source.tools.build_linux import run_linux
 run_linux(R,M,a)
 raise SystemExit(0)
a.output=a.output or 'catherine.exe';assert a.output.lower()in ['catherine.exe','catherine1.exe','catherine2.exe'];(B/'logs').mkdir(exist_ok=True);rows=[]
CREATE_NO_WINDOW={"creationflags":0x08004000} if os.name=='nt' else {}
STAMP=B/"obj/build-stamps.json";stamps=json.loads(STAMP.read_text())if STAMP.exists()else{}
@functools.lru_cache(None)
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def signature(row):return hashlib.sha256(json.dumps([row["command"],[(f,digest(f))for f in sorted(set(row["dependencies"]))]],sort_keys=True).encode()).hexdigest()
def run(row):
 c=list(row['command']);i=row['id'];log=B/'logs'/f'{i:03d}.log'
 if a.check_dependencies:
  c.remove('-c');n=c.index('-o');del c[n:n+2];c+=['-M','-MF',str(B/'logs'/f'{i:03d}.d'),'-MT','DEP']
 elif not a.rebuild and Path(row['object']).exists()and stamps.get(str(i))==signature(row):return dict(id=i,status='CACHED')
 q=subprocess.run(c,cwd=R,capture_output=True,text=True,**CREATE_NO_WINDOW);log.write_text(q.stdout+q.stderr);return dict(id=i,status='PASS'if not q.returncode else'FAIL',log=str(log))
with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs)as pool:
 for row in pool.map(run,M['units']):
  rows.append(row)
  if row['status']=='FAIL':print('FAIL',row,flush=True)
  elif len(rows)%25==0:print('Checked',len(rows),'/',len(M['units']),flush=True)
(B/'logs/last-build.json').write_text(json.dumps(rows,indent=2));assert all(x['status']!='FAIL'for x in rows),'Compile failed; see Build/logs'
STAMP.write_text(json.dumps({str(u['id']):signature(u)for u in M['units']},indent=2))if not a.check_dependencies else None
if a.check_dependencies:print('All source dependencies resolve.');raise SystemExit(0)
# Publish the embedded, self-contained installer and shader conversion tools.
local_dotnet=B/'toolchains/dotnet10/dotnet.exe'
dotnet=str(local_dotnet) if local_dotnet.is_file() else 'dotnet'
q=subprocess.run([dotnet,'publish',str(S/'asset_setup/AssetSetup.csproj'),'-c','Release','-o',str(B/'asset-setup/publish'),'--nologo','-v','quiet'],cwd=R,capture_output=True,text=True,**CREATE_NO_WINDOW)
(B/'logs/asset-setup-build.log').write_text(q.stdout+q.stderr);assert not q.returncode,'Asset setup build failed; see Build/logs/asset-setup-build.log'
required_manifest=S/'resources/assets-required.json'
if not (B/'assets-required.json').exists() or (B/'assets-required.json').read_bytes()!=required_manifest.read_bytes():
 (B/'assets-required.json').write_bytes(required_manifest.read_bytes())
res=M['resource'];q=subprocess.run([res['compiler'],'/fo',res['output'],res['source']],cwd=R,capture_output=True,text=True,**CREATE_NO_WINDOW);assert not q.returncode,q.stderr
shutil.copytree(S/'CathAssets',B/'CathAssets',dirs_exist_ok=True)
for resource_name in ('Catherine DEV.cmd','INSTALL-ASSETS.txt','CUTSCENES.txt','cutscene-catalog.json'):
 shutil.copyfile(S/'resources'/resource_name,B/resource_name)
# Build/ outputs may be hard links shared with archived copies in Old/. The linker
# rewrites its map in place, which would silently alter every linked archive copy.
# Move each existing output name aside first (a rename keeps the archived links
# intact), and keep catherine1/2 maps and PDBs separate from the primary ones.
primary=a.output.lower()=='catherine.exe';stem=Path(a.output).stem
aside={n:B/(n+'.prelink') for n in ['Catherine.map','Catherine.pdb',a.output]}
for n,t in aside.items():
 if t.exists():t.unlink()
 if (B/n).exists():os.replace(B/n,t)
q=subprocess.run([M['linker'],'@'+str(B/'Catherine.rsp'),'-o',str(B/a.output)],cwd=R,capture_output=True,text=True,**CREATE_NO_WINDOW);(B/'logs/link.log').write_text(q.stdout+q.stderr)
if not primary:
 for n in ['Catherine.map','Catherine.pdb']:
  if (B/n).exists():os.replace(B/n,B/(stem+Path(n).suffix))
for n,t in aside.items():
 if t.exists():
  if q.returncode or (not primary and n!=a.output):os.replace(t,B/n)
  else:t.unlink()
assert not q.returncode,q.stderr;print('Built',B/a.output,flush=True)
# --pgo: the release configuration. Profile-guided + ThinLTO objects go to Build/obj-pgo/use-lto (the training
# profile is Build/perf/pgo/merged.profdata; Build/perf/pgo/pgo_build.py gen retrains it). The result replaces the
# requested output by rename, so hard-linked archive copies are never written.
if a.pgo and not M.get('native_release'):
 q=subprocess.run([sys.executable,str(B/'perf/pgo/pgo_build.py'),'use',str(max(a.jobs,8)),'lto','catherine2'],cwd=R)
 assert q.returncode==0,'PGO build failed'
 stem=Path(a.output).stem;primary=a.output.lower()=='catherine.exe'
 for n,t in [('catherine2.exe',a.output),('catherine2.map','Catherine.map' if primary else stem+'.map'),('catherine2.pdb','Catherine.pdb' if primary else stem+'.pdb')]:
  os.replace(B/n,B/t)
 print('Built (PGO + ThinLTO)',B/a.output,flush=True)
if M.get('native_release'):
 report=dict(M['native_release'],exe_sha256=hashlib.sha256((B/a.output).read_bytes()).hexdigest(),compiled_units=len(M['units']),default_native_source=True)
 (B/(Path(a.output).stem+'.build.json')).write_text(json.dumps(report,indent=2))
 if primary:
  (B/'native-verification.json').write_text(json.dumps(report,indent=2))
  (B/'BUILD-INFO.txt').write_text('Catherine Full Body - default native source build\nSHA256 '+report['exe_sha256']+'\nO3 + ThinLTO + selected PGO profile.\nGPU skinning enabled by default; NativeGpuSkinning=false selects CPU fallback.\nAutomatic/manual shadow quality and the high-refresh sheep-push fix included.\n')
