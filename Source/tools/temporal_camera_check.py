"""Check the real-camera reader without launching the game."""
from pathlib import Path
import json,shutil,subprocess,sys
root=Path(__file__).resolve().parents[2]
out=root/'Build/temporal-input-test';out.mkdir(parents=True,exist_ok=True)
source=root/'Source/tools/temporal_camera_check.cpp'
exe=out/('catherine2.exe'if sys.platform=='win32'else'catherine2')
if sys.platform=='win32':
 unit=next(u for u in json.loads((root/'Source/build-manifest.json').read_text())['units']if u['id']==191)
 recipe=unit['command'];command=[a for a in recipe[:recipe.index('-c')]if a!=unit['source']]
else:command=[shutil.which('clang++')or'c++','-std=c++20','-O2']
try:
 subprocess.run(command+['-UNDEBUG',str(source),'-o',str(exe)],cwd=root,check=True)
 subprocess.run([str(exe)],cwd=out,check=True)
finally:exe.unlink(missing_ok=True)
