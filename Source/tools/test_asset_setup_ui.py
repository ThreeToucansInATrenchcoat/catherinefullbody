"""Render and check setup layouts on a private desktop without user focus or input."""
from pathlib import Path
from test_asset_setup_clean_install import parent,ROOT
exe=ROOT/'Build/asset-setup/ui-check/bin/Release/net10.0-windows/UiCheck.exe'
parent(False,[str(exe),str(ROOT)])
