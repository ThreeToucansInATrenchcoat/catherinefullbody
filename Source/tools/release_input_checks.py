"""Small regression checks for the Alpha 2 fixes; no game assets or saves needed."""
import json
import subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[2]
m = json.loads((root / 'Source/build-manifest.json').read_text())
include = root / 'Source/imports/base/guest-timing/include'
piano = root / 'Source/imports/timing-audit/frame-advance-audit-2026-11-20/icon-babel-mouse-1795344580423916400'
for source in ['Source/tests/menu_mouse_test.c', 'Source/tests/puzzle_player_timing.cpp', 'Source/tests/frontend_timing_input.cpp', 'Source/tools/multiplayer_input_scope_check.cpp']:
    exe = root / 'Build' / (Path(source).stem + '.exe')
    compiler = m['compiler'] if source.endswith('.c') else m['linker']
    subprocess.run([compiler, '-O2', '-I'+str(include), '-I'+str(piano), str(root/source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
subprocess.run(['python', str(root/'Source/tests/question_animation_stack_test.py')], check=True)

subprocess.run(["python", str(root/"Source/tests/sheep_push_stack_test.py")], check=True)
subprocess.run(["python", str(root/"Source/tests/sheep_transition_handoff_test.py")], check=True)
subprocess.run(["python", str(root/"Source/tools/settings_ini_check.py")], check=True)
subprocess.run(["python", str(root/"Source/tools/native_renderer_checks.py")], check=True)

subprocess.run(["python", str(root/"Source/tests/launcher_startup_check.py")], check=True)
