"""Check the shipped installer with both main inputs, without rebuilding game data."""
from pathlib import Path
import copy,hashlib,json,shutil,subprocess,zipfile
R=Path(__file__).resolve().parents[2];B=R/'Build';D=B/'main-compatibility/installer';D.mkdir(parents=True,exist_ok=True)
EXE=B/'asset-setup/publish/CatherineAssetSetup.exe'
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
 manifest=json.loads((R/'Source/resources/assets-required.json').read_text())
 protected={n:(B/n).read_bytes()for n in ('save.dat','catherine-settings.ini')}
 source=Path('C:/catherine/go/exefs/main');source_before=(sha(source.read_bytes()),source.stat().st_mtime_ns)
 def run(folder,*args):
  return subprocess.run([str(EXE),'--root',str(folder),'--headless',*args],capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=600)
 full=D/'full-go-scan';full.mkdir(exist_ok=True);(full/'assets-required.json').write_text(json.dumps(manifest))
 result=run(full,'--scan-only','--scan','C:/catherine/go')
 assert result.returncode==0,(result.returncode,(full/'assets-errors.log').read_text()if(full/'assets-errors.log').exists()else result.stderr)
 assert not(full/'assets-missing.txt').exists() and not(full/'assets.pak').exists()
 print('All 3,826 files accepted directly from C:/catherine/go.',flush=True)
 mini=copy.deepcopy(manifest);mini.update(files=[f for f in manifest['files']if f['path'].startswith('exefs/')],romfsSize=0,romfsMetadata=[],shaders=[],packageSha256=[],canonicalPackageSha256='')
 packages=[]
 for name in ('primary','alternate'):
  folder=D/name;folder.mkdir(exist_ok=True);(folder/'assets-required.json').write_text(json.dumps(mini))
  pack=folder/'assets.pak'
  if pack.exists():pack.unlink()
  if name=='primary':
   assets=folder/'Assets';assets.mkdir(exist_ok=True);shutil.copyfile(B/'asset-setup/qa_full/Pending/exefs/main',assets/'main')
  result=run(folder,'--scan','C:/catherine/go/exefs');assert result.returncode==0,(name,result.returncode,result.stderr)
  with zipfile.ZipFile(pack)as z:
   assert z.testzip()is None
   for module in manifest['modules']:assert sha(z.read(module['name']+'.image'))==module['imageSha256'],module['name']
  packages.append(sha(pack.read_bytes()));assert not(folder/'Pending').exists()
 assert packages[0]==packages[1],'Equivalent inputs produced different module packages'
 invalid=D/'unrecognized';invalid.mkdir(exist_ok=True);bad=invalid/'Assets';bad.mkdir(exist_ok=True)
 data=bytearray(source.read_bytes());data[0x40]^=1;(bad/'main').write_bytes(data)
 negative=copy.deepcopy(mini);negative['files']=[f for f in negative['files']if f['path']=='exefs/main'];negative['modules']=[]
 (invalid/'assets-required.json').write_text(json.dumps(negative));result=run(invalid,'--scan-only')
 assert result.returncode==1 and 'exefs/main'in(invalid/'assets-missing.txt').read_text()
 assert (sha(source.read_bytes()),source.stat().st_mtime_ns)==source_before,'Extracted main changed'
 for n,data in protected.items():assert(B/n).read_bytes()==data,n+' changed'
 report=dict(passed=True,go_required_files_accepted=3826,both_main_inputs_supported=True,module_packages_identical=True,module_package_sha256=packages[0],all_four_canonical_module_hashes=True,unrecognized_input_rejected=True,extracted_main_unchanged=True,save_settings_unchanged=True)
 (D/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
if __name__=='__main__':main()
