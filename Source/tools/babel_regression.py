"""Matched short old/new scene checks; hidden owned processes and restored saves."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time
p=argparse.ArgumentParser();p.add_argument('--fresh',action='store_true');args=p.parse_args()
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';report=BUILD/'logs/babel-regression.json';results=json.loads(report.read_text()).get('samples',[])if report.exists()and not args.fresh else[]
save=BUILD/'save.dat';settings=BUILD/'catherine-settings.ini';savebytes=save.read_bytes();settingsbytes=settings.read_bytes()
try:
 for scene in ('boss','bar'):
  for label,exe in [('previous','Catherine.exe'),('babel','catherine1.exe')]:
   if any(x['scene']==scene and x['label']==label and x['sha256']==hashlib.sha256((BUILD/exe).read_bytes()).hexdigest()for x in results):continue
   tag=f'babel-regression-{label}';before=set((BUILD/'perf/runs').iterdir())
   cmd=[sys.executable,str(ROOT/'Source/tools/eye_capture.py'),'--scene',scene,'--tag',tag,'--exe',exe,'--seconds','8','--settle','4','--env','NX_HEADLESS=1','--env','CATHERINE_PERF_BACKGROUND=1','--env','EYE_MEASURE_FRAME=1']
   if scene=='boss':cmd+=['--difficulty','Safety']
   q=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
   new=[p for p in set((BUILD/'perf/runs').iterdir())-before if tag in p.name]
   assert q.returncode==0,(cmd,q.stdout[-1000:],q.stderr[-1000:])
   assert len(new)==1,new
   data=json.loads((new[0]/'result.json').read_text());assert data.get('rendered_fps',0)>0,data
   row={'scene':scene,'label':label,'exe':exe,'sha256':hashlib.sha256((BUILD/exe).read_bytes()).hexdigest(),'fps':data['rendered_fps'],'result':str(new[0]/'result.json'),'native_save_changed':save.read_bytes()!=savebytes};results.append(row);print(json.dumps(row),flush=True)
   # Native stage loading can autosave system state. Each matched run starts
   # with the same user bytes, and neither autosave is retained.
   save.write_bytes(savebytes);settings.write_bytes(settingsbytes)
finally:
 save.write_bytes(savebytes);settings.write_bytes(settingsbytes)
 (BUILD/'logs/babel-regression.json').write_text(json.dumps({'samples':results,'save_restored':save.read_bytes()==savebytes,'settings_restored':settings.read_bytes()==settingsbytes},indent=2))
