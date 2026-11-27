"""Exercise the real Windows startup code with stubbed game/setup entry points.

Builds only a small test harness, never Catherine or its asset installer.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
unit = next(u for u in json.loads((ROOT / 'Source/build-manifest.json').read_text())['units'] if u['id'] == 2)
out = ROOT / 'Build/launcher-startup-test'
out.mkdir(exist_ok=True)
source = out / 'startup.c'
source.write_text(r'''
#define main fixture_guest_main
#include "SOURCE"
#undef main
#include <assert.h>
int catherine_dev_loader_pending(void){return 0;}
int catherine_dev_loader_startup_menu(void){return 0;}
void catherine_dev_loader_set_startup_menu(int v){(void)v;}
int catherine_dev_loader_request(const CatherineDevLoadRequest* r){return 1;}
int catherine_dev_save_isolate_startup(void){return 1;}
unsigned catherine_internal_render_height(void){return 720;}
int asset_pak_initialize(void){return 1;}
int asset_pak_initialize_from(const wchar_t* p){return 1;}
const char* asset_pak_error(void){return "fixture";}
int catherine_asset_setup_launch(const wchar_t* pack,const wchar_t* game,const wchar_t* update,int prepare){
    assert(prepare == atoi(getenv("EXPECT_PREPARE")));
    const wchar_t* expected=_wgetenv(L"EXPECT_GAME");
    assert(expected ? !_wcsicmp(expected,game) : !*game);
    expected=_wgetenv(L"EXPECT_UPDATE");
    assert(expected ? !_wcsicmp(expected,update) : !*update);
    wchar_t cwd[32768];assert(GetCurrentDirectoryW(32768,cwd));
    assert(!_wcsicmp(cwd,_wgetenv(L"EXPECT_CWD")));
    if(getenv("RECOMPLAUNCHER_DATA"))assert(!strcmp(getenv("NX_DATA_ROOT"),"."));
    puts("SETUP-CHECKED");
    return atoi(getenv("SETUP_RESULT"));
}
int fixture_guest_main(void){puts("GUEST-STARTED");return 77;}
int main(void){return wWinMain(NULL,NULL,NULL,0);}
'''.replace('SOURCE', unit['source']), encoding='utf-8')
command = unit['command'][:unit['command'].index('-c')]
command = [arg for arg in command if arg not in ('-DNDEBUG', unit['source']) and not arg.startswith('-fprofile')]
exe = out / 'startup-test.exe'
subprocess.run(command + [str(source), '-luser32', '-lshell32', '-o', str(exe)], check=True, cwd=ROOT)
(out / 'assets-required.json').write_text('{}', encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='launcher paths ', dir=out) as temporary:
    base = Path(temporary)
    data, game, update = (base / name for name in ('data cafÃ©', 'game extracted', 'update 1.0.1'))
    for path in (data, game, update): path.mkdir()
    env = {k: v for k, v in os.environ.items() if not k.startswith(('RECOMPLAUNCHER', 'EXPECT_', 'CATHERINE_', 'NX_'))}
    env.update(EXPECT_PREPARE='1', EXPECT_CWD=str(data), RECOMPLAUNCHER_DATA=str(data), SETUP_RESULT='0')
    def run(args, code, environment=env, guest=False):
        result = subprocess.run([str(exe), *args], cwd=base, env=environment, capture_output=True, timeout=10)
        assert result.returncode == code, (args, result.returncode, result.stderr)
        assert (b'GUEST-STARTED' in result.stdout) == guest, result.stdout
        return result
    run(['--prepare'], 0)
    run(['--prepare', '--game=' + str(game), '--update=' + str(update)], 0,
        dict(env, EXPECT_GAME=str(game), EXPECT_UPDATE=str(update)))
    run(['--prepare', '--game', str(game)], 0, dict(env, EXPECT_GAME=str(game)))
    run(['--prepare'], 9, dict(env, SETUP_RESULT='9'))
    for args in (['--prepare', '--game'], ['--game=missing', '--prepare'],
                 ['--prepare', '--dev-menu'], ['--prepare', '--assets-pak', str(base / 'x.pak')],
                 ['--prepare', '--game=' + str(game), '--game=' + str(game)]):
        run(args, 2)
    run([], 77, dict(env, EXPECT_PREPARE='0'), guest=True)
    ordinary = dict(env, EXPECT_CWD=str(out), EXPECT_PREPARE='0')
    ordinary.pop('RECOMPLAUNCHER_DATA')
    run([], 77, ordinary, guest=True)
    assert (data / 'assets-required.json').read_text() == '{}'
    assert (data / 'cache').is_dir()
print('Launcher startup passed: Unicode/spaces, optional update, failure codes, data paths, setup-only and ordinary launch.')
