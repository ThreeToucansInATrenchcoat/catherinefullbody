"""Package freshly compiled CI output without local game data or settings."""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import zipfile

root = Path(__file__).resolve().parents[2]
build = root / 'Build'
version = os.environ.get('RELEASE_VERSION', 'development')
platform = 'Windows' if sys.platform == 'win32' else 'Linux'
stage = build / 'release-stage'
stage.mkdir(exist_ok=True)
exe = 'Catherine.exe' if platform == 'Windows' else 'catherine'
shutil.copy2(build / exe, stage / exe)
shutil.copytree(build / 'CathAssets', stage / 'CathAssets', dirs_exist_ok=True)
shutil.copy2(build / 'assets-required.json', stage)
shutil.copy2(root / 'recomplauncher.json', stage)
# Direct destinations refer to fixed checkpoint entries and hashes. Refuse to
# publish a package whose supplied archive cannot satisfy the compiled catalog.
catalog = '\n'.join(p.read_text(encoding='utf-8') for p in
                    (root / 'Source/imports/base/dev-start/runtime').glob('*catalog.inc'))
checkpoints = set(re.findall(r'"(checkpoints/[A-F0-9]{64}\.dat)","([A-F0-9]{64})"', catalog))
assert checkpoints, 'No checkpoint references found in developer catalog'
with zipfile.ZipFile(root / 'Source/resources/dev-checkpoints.pak') as archive:
    assert archive.testzip() is None
    for entry, expected in checkpoints:
        data = archive.read(entry)
        assert len(data) == 0xd2000 and hashlib.sha256(data).hexdigest().upper() == expected
for name in ('dev-checkpoints.pak', 'DEV-MODE.txt', 'CUTSCENES.txt', 'cutscene-catalog.json'):
    shutil.copy2(root / 'Source/resources' / name, stage)
dev_launcher = 'Catherine DEV.cmd' if platform == 'Windows' else 'Catherine DEV.sh'
shutil.copy2(root / 'Source/resources' / dev_launcher, stage)
if platform == 'Linux':
    (stage / dev_launcher).chmod(0o755)
licenses = []
for path in sorted((root / 'Source/asset_setup/vendor/licenses').glob('*')):
    licenses.append(path.name + '\n\n' + path.read_text(encoding='utf-8-sig'))
(stage / 'ASSET-TOOLS-LICENSES.txt').write_text('\n\n'.join(licenses), encoding='utf-8')
if platform == 'Windows':
    prior = next((build / 'previous').glob('*Windows-x86_64.zip'))
    assert hashlib.sha256(prior.read_bytes()).hexdigest() == '3cb36758ac3294739d3179664b2c38117c1438c2ee16c476eb6fd5bf938ec6e9', 'Previous release checksum mismatch'
    with zipfile.ZipFile(prior) as archive:
        for name in ['CatherineInterpolation.dll', 'open_svpflow.dll', 'svpflow1_vs.dll', 'svpflow2_vs.dll', 'open-svpflow-LICENSE.txt']:
            (stage / name).write_bytes(archive.read(name))
    shutil.copy2(root / 'Source/resources/INSTALL-ASSETS.txt', stage)
else:
    shutil.copy2(build / 'catherine.png', stage)
    runtime = stage / 'ffmpeg-runtime'
    if runtime.exists():
        shutil.rmtree(runtime)
    shutil.copytree(build / 'ffmpeg-runtime', runtime)
    ffmpeg_source = root / 'Source/third_party/ffmpeg'
    sources = list(ffmpeg_source.glob('ffmpeg-*.tar.xz'))
    assert len(sources) == 1
    shutil.copy2(sources[0], runtime)
    shutil.copy2(ffmpeg_source / 'COPYING.LGPLv2.1', runtime)
    shutil.copy2(root / 'Source/tools/build_bundled_ffmpeg.sh', runtime)
    (runtime / 'README.txt').write_text(
        'FFmpeg is dynamically linked under the GNU LGPL v2.1 or later.\n'
        'The corresponding source archive and build script are included here.\n'
        'https://ffmpeg.org/\n'
    )
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
(stage / 'BUILD-INFO.txt').write_text(f'{version}\nCommit: {commit}\nPlatform: {platform} x86-64\nBuilt by GitHub Actions.\n')
readme = 'Catherine: Full Body PC Port - ' + version + '\n\n'
readme += 'Extract all files together. Supply your own decrypted Switch game and update 1.0.1.\n'
readme += 'Setup builds assets.pak and Vulkan shaders on first launch.\n'
readme += 'When updating, keep your existing assets.pak, save.dat and catherine-settings.ini.\n'
readme += 'Developer mode is disabled by default (devmenu=false). See DEV-MODE.txt to enable it.\n'
if platform == 'Windows':
    readme += 'Run Catherine.exe.\n'
else:
    readme += 'Run ./catherine. Requires an x86-64-v3 CPU, Vulkan, SDL2, Cairo, and OpenSSL. FFmpeg is bundled.\n'
    readme += 'Asset setup also requires SPIRV-Tools and KDialog or Zenity.\n'
(stage / 'README.txt').write_text(readme)
artwork = json.loads((stage / 'CathAssets/artwork-manifest.json').read_text())
for record in artwork['tutorials']:
    assert hashlib.sha256((stage / 'CathAssets' / record['path']).read_bytes()).hexdigest() == record['sha256']
for forbidden in ['assets.pak', 'save.dat', 'catherine-settings.ini']:
    assert not (stage / forbidden).exists()
out = build / 'release'
out.mkdir(exist_ok=True)
base = f'Catherine-Full-Body-PC-{version}-{platform}-x86_64'
if platform == 'Windows':
    package = out / (base + '.zip')
    with zipfile.ZipFile(package, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in sorted(stage.rglob('*')):
            if path.is_file(): z.write(path, path.relative_to(stage).as_posix())
    with zipfile.ZipFile(package) as z: assert z.testzip() is None
else:
    package = out / (base + '.tar.gz')
    with tarfile.open(package, 'w:gz') as tar:
        for path in sorted(stage.iterdir()): tar.add(path, arcname=path.name)
(package.with_name(package.name+'.sha256')).write_text(hashlib.sha256(package.read_bytes()).hexdigest()+'  '+package.name+'\n')
print(package)
