"""Emit native function excerpts for config_menu_pointer_test.c.

Usage: python graphics/tests/prepare_config_menu_pointer_fixture.py OUTPUT.inc
Compile the test with CONFIG_MENU_NATIVE_FIXTURE set to that absolute file path.
The row-list seam stops f32c80 immediately before drawing; all descriptor selection
and the canonical relocation patch execute unchanged. The other native functions
are complete. ROM tables are loaded from main.image by the test executable.
"""
from pathlib import Path
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("config_pointer_timing_generator", ROOT / "base/guest-timing/generate.py")
generator = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = generator
spec.loader.exec_module(generator)
baseline = (ROOT / "base/generated/main/main_0001.c").read_text()
sites = generator.load_sites(ROOT / "base/guest-timing/timing-sites.patch")["main_0001.c"]

def function(name, patched=False):
    start = baseline.index("void " + name + "(Cpu *c) {")
    end = baseline.index("\n}", start) + 2
    result = baseline[start:end] + "\n"
    if patched:
        site = next(site for site in sites if site.function == name)
        result = generator.apply_hunks(result, site.changes)
    return "\n".join(line for line in result.splitlines() if "FN_ENTER(" not in line) + "\n"

parts = [function(name, patched=name in ("f_7000031d70", "f_70000320a0", "f_70000329d0")) for name in
         ("f_7000034100", "f_7000034720", "f_7000031d70", "f_70000320a0", "f_70000329d0")]
for patched, name in ((False, "baseline_list"), (True, "relocated_list")):
    source = function("f_7000032c80", patched)
    source = source[:source.index("    { uint64_t ea = x28 + 0x10ull;")]
    source += "    memcpy(listed, (void *)(uintptr_t)(sp_ + 0x80), 5);\n    c->sp = sp_ + 0x130; return;\n}\n"
    parts.append(source.replace("void f_7000032c80(", "static void " + name + "("))
Path(sys.argv[1]).write_text("\n".join(parts))
