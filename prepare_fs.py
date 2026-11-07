Import("env")

import os
import shutil

PROJECT_DIR = env.get("PROJECT_DIR")
FILESYSTEM_DIR = os.path.join(PROJECT_DIR, ".piofs")
WEB_FILES = ["index.html", "settings.html", "app.js", "style.css"]


def prepare_filesystem(source, target, env):
    print("\n[LittleFS] Preparing web assets from repository root")
    if os.path.exists(FILESYSTEM_DIR):
        shutil.rmtree(FILESYSTEM_DIR)
    os.makedirs(FILESYSTEM_DIR, exist_ok=True)

    missing = []
    for filename in WEB_FILES:
        source_file = os.path.join(PROJECT_DIR, filename)
        if not os.path.isfile(source_file):
            missing.append(filename)
            continue
        target_file = os.path.join(FILESYSTEM_DIR, filename)
        shutil.copy2(source_file, target_file)
        print("[LittleFS] Added: {} ({} bytes)".format(filename, os.path.getsize(target_file)))

    if missing:
        raise RuntimeError("Required LittleFS web file(s) missing: {}".format(", ".join(missing)))

    total = sum(os.path.getsize(os.path.join(FILESYSTEM_DIR, f)) for f in WEB_FILES)
    print("[LittleFS] {} files copied; {} bytes total".format(len(WEB_FILES), total))
    print("[LittleFS] Filesystem source: {}\n".format(FILESYSTEM_DIR))


prepare_filesystem(None, None, env)
