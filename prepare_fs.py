Import("env")

import os
import shutil


# ============================================================
# ESP8266 One Room Automation
# Root-based LittleFS preparation
#
# Web files are kept directly in repository root.
# They are copied into .piofs before PlatformIO builds LittleFS.
# ============================================================


PROJECT_DIR = env.get("PROJECT_DIR")

FILESYSTEM_DIR = os.path.join(PROJECT_DIR, ".piofs")


WEB_FILES = [
    "index.html",
    "settings.html",
    "app.js",
    "style.css",
]


def prepare_filesystem(source, target, env):
    print("")
    print("============================================================")
    print(" ESP8266 One Room Automation - Preparing LittleFS")
    print("============================================================")

    # --------------------------------------------------------
    # Remove old temporary filesystem
    # --------------------------------------------------------

    if os.path.exists(FILESYSTEM_DIR):
        shutil.rmtree(FILESYSTEM_DIR)

    os.makedirs(FILESYSTEM_DIR, exist_ok=True)

    # --------------------------------------------------------
    # Copy root web files
    # --------------------------------------------------------

    copied = 0

    for filename in WEB_FILES:

        source_file = os.path.join(PROJECT_DIR, filename)
        target_file = os.path.join(FILESYSTEM_DIR, filename)

        if not os.path.isfile(source_file):
            print(
                "[LittleFS] WARNING: missing file: {}".format(
                    filename
                )
            )
            continue

        shutil.copy2(source_file, target_file)

        size = os.path.getsize(target_file)

        print(
            "[LittleFS] Added: {} ({} bytes)".format(
                filename,
                size
            )
        )

        copied += 1

    # --------------------------------------------------------
    # Verify at least index.html exists
    # --------------------------------------------------------

    index_file = os.path.join(
        FILESYSTEM_DIR,
        "index.html"
    )

    if not os.path.isfile(index_file):

        print("")
        print(
            "[LittleFS] ERROR: index.html was not found."
        )

        raise RuntimeError(
            "index.html is required for LittleFS"
        )

    # --------------------------------------------------------
    # Summary
    # --------------------------------------------------------

    print("")
    print(
        "[LittleFS] {} web file(s) prepared.".format(
            copied
        )
    )

    print(
        "[LittleFS] Temporary filesystem prepared at: {}".format(
            FILESYSTEM_DIR
        )
    )

    print("============================================================")
    print("")


# Run before the build starts.
prepare_filesystem(None, None, env)
