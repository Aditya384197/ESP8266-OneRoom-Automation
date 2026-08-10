Import("env")
from pathlib import Path
import shutil
project = Path(env["PROJECT_DIR"])
staging = project / ".piofs"
if staging.exists():
    shutil.rmtree(staging)
staging.mkdir()
for name in ("index.html", "settings.html", "app.js", "style.css"):
    p = project / name
    if p.exists():
        shutil.copy2(p, staging / name)
