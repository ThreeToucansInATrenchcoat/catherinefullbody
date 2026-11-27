"""Exercise actual checkpoint extraction from a separate launcher data folder."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'Source/imports/base'
catalog = '\n'.join(p.read_text(encoding='utf-8') for p in (BASE / 'dev-start/runtime').glob('*catalog.inc'))
entries = sorted(set(re.findall(r'"(checkpoints/[A-F0-9]{64}\.dat)","([A-F0-9]{64})"', catalog)))
assert entries
with zipfile.ZipFile(ROOT / 'Source/resources/dev-checkpoints.pak') as archive:
    assert archive.testzip() is None
    for entry, expected in entries:
        data = archive.read(entry)
        assert len(data) == 0xd2000 and hashlib.sha256(data).hexdigest().upper() == expected

windows = sys.platform == 'win32'
manifest = json.loads((ROOT / 'Source/build-manifest.json').read_text())
cc = manifest['compiler'] if windows else 'clang'
cxx = manifest['linker'] if windows else 'clang++'
with tempfile.TemporaryDirectory(prefix='dev-checkpoint-check-') as folder:
    install = Path(folder) / 'install'
    data = Path(folder) / 'launcher data \u03bb'
    install.mkdir()
    data.mkdir()
    shutil.copy2(ROOT / 'Source/resources/dev-checkpoints.pak', install)
    harness = install / 'check.cpp'
    harness.write_text('''#include "dev_checkpoint_pack.h"
#include <cstdio>
int main(int argc,char**argv){
    if(argc!=3)return 2;
    char out[32768];
    if(!catherine_dev_checkpoint_materialize(argv[1],argv[2],nullptr,out,sizeof(out))){
        std::fprintf(stderr,"%s\\n",catherine_dev_checkpoint_error());return 1;
    }
    catherine_dev_checkpoint_release();return 0;
}
''', encoding='utf-8')
    includes = ['-I'+str(BASE), '-I'+str(BASE / 'dev-start/generated/include')]
    common = ['-O2'] + (['-fms-runtime-lib=static'] if windows else [])
    objects = []
    for name in ('miniz', 'miniz_tinfl', 'miniz_zip'):
        obj = install / (name + '.o')
        subprocess.run([cc, *common, '-DMINIZ_NO_ZLIB_APIS', '-DMINIZ_NO_DEFLATE_APIS', '-DMINIZ_NO_STDIO',
                        '-c', str(BASE / 'third_party/miniz' / (name+'.c')), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = install / ('check.exe' if windows else 'check')
    subprocess.run([cxx, *common, '-std=c++20', *includes, str(harness),
                    str(BASE / 'dev-start/runtime/dev_checkpoint_pack.cpp'), *objects,
                    '-lbcrypt' if windows else '-lcrypto', '-o', str(exe)], check=True)
    environment = dict(os.environ, RECOMPLAUNCHER_DATA=str(data))
    for entry, expected in entries:
        subprocess.run([str(exe), entry, expected], cwd=data, env=environment, check=True)
    assert not list(data.glob('checkpoint-*'))
print(f'All {len(entries)} developer checkpoints extracted and validated from a separate launcher data folder.')
