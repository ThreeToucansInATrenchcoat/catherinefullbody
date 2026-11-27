"""Run native Linux startup against an embedded dummy setup tool, not the game."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
out = ROOT / 'Build/launcher-linux-startup-test'
out.mkdir(exist_ok=True)
payload = out / 'setup.sh'
payload.write_text('#!/bin/sh\nprintf "ARG:%s\\n" "$@"\nprintf "progress 1 Fixture ready\\n"\nexit "${SETUP_RESULT:-0}"\n')
assembly = out / 'payload.s'
assembly.write_text(f'''.section .rodata
.globl catherine_asset_setup_start, catherine_asset_setup_end
catherine_asset_setup_start:
.incbin "{payload}"
catherine_asset_setup_end:
.globl catherine_game_icon_start, catherine_game_icon_end
catherine_game_icon_start:
.byte 0
catherine_game_icon_end:
.section .note.GNU-stack,"",@progbits
''')
source = out / 'startup.c'
source.write_text('''#include <wchar.h>
#include "launch_linux.inc"
int catherine_dev_save_isolate_startup(void){return 1;}
int catherine_guest_main(void){puts("GUEST-STARTED");return 77;}
int catherine_linux_run(int (*guest)(void)){return guest();}
int catherine_alt_build_mode(void){return 0;}
int asset_pak_initialize(void){return 1;}
int asset_pak_initialize_from_native_path(const char* path){return 1;}
int asset_pak_contains(const char* name){return 1;}
const char* asset_pak_error(void){return "fixture";}
int catherine_dev_loader_request(const CatherineDevLoadRequest* request){return 1;}
void catherine_dev_loader_set_startup_menu(int waiting){}
''')
exe = out / 'startup-test'
libraries = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2', 'cairo'], text=True).split()
subprocess.run(['clang', '-std=gnu17', '-O0', '-I'+str(ROOT/'Source/platform'),
                '-I'+str(ROOT/'Source/imports/base'), str(source), str(assembly),
                *libraries, '-o', str(exe)], check=True)
with tempfile.TemporaryDirectory(prefix='launcher paths ', dir=out) as temporary:
    base = Path(temporary)
    data, game, update = [base / name for name in ('data café', 'game extracted', 'update 1.0.1')]
    for path in (data, game, update): path.mkdir()
    env = {k:v for k,v in os.environ.items() if not k.startswith(('RECOMPLAUNCHER', 'CATHERINE_', 'NX_'))}
    env.update(RECOMPLAUNCHER_DATA=str(data), SETUP_RESULT='0')
    def run(args, code, environment=env, guest=False):
        result = subprocess.run([str(exe), *args], cwd=base, env=environment, capture_output=True, text=True, timeout=10)
        assert result.returncode == code, (args, result.returncode, result.stderr)
        assert ('GUEST-STARTED' in result.stdout) == guest
        return result.stdout
    output = run(['--prepare', '--game='+str(game), '--update='+str(update)], 0)
    for arg in ('--headless', '--launcher-progress', '--packed', str(data), str(game), str(update)):
        assert 'ARG:'+arg+'\n' in output, (arg, output)
    assert 'progress 1 Fixture ready' in output
    run(['--prepare'], 9, dict(env, SETUP_RESULT='9'))
    run(['--prepare', '--game', str(game)], 0)
    for args in (['--prepare','--game'], ['--game=missing','--prepare'], ['--prepare','--dev-menu'],
                 ['--prepare','--game='+str(game),'--game='+str(game)]):
        run(args, 2)
    run([], 77, guest=True)
    ordinary = dict(env); ordinary.pop('RECOMPLAUNCHER_DATA')
    run([], 77, ordinary, guest=True)
    assert (data/'cache/assets-setup-work').is_dir()
print('Linux launcher startup passed: setup arguments/progress, Unicode/spaces, failure codes and ordinary launch.')
