Import("env")

from pathlib import Path
import shutil


# ---------------------------------------------------------
# ESP8266 One Room Automation
# Flat-Root LittleFS Preparation Script
#
# Root layout:
#   index.html
#   settings.html
#   app.js
#   style.css
#
# Temporary build directory:
#   .piofs/
#
# This directory is generated automatically.
# ---------------------------------------------------------

project_dir = Path(env["PROJECT_DIR"])
filesystem_dir = project_dir / ".piofs"


# ---------------------------------------------------------
# Clean previous temporary filesystem
# ---------------------------------------------------------

if filesystem_dir.exists():
    shutil.rmtree(filesystem_dir)


filesystem_dir.mkdir(
    parents=True,
    exist_ok=True
)


# ---------------------------------------------------------
# Web files which belong in LittleFS
# ---------------------------------------------------------

web_files = [
    "index.html",
    "settings.html",
    "app.js",
    "style.css",
]


# ---------------------------------------------------------
# Copy web assets from repository root
# to temporary LittleFS directory
# ---------------------------------------------------------

for filename in web_files:

    source = project_dir / filename
    destination = filesystem_dir / filename

    if source.is_file():

        shutil.copy2(
            source,
            destination
        )

        print(
            "[LittleFS] Added: {}".format(filename)
        )

    else:

        print(
            "[LittleFS] WARNING: {} not found".format(filename)
        )


print(
    "[LittleFS] Temporary filesystem prepared at: {}".format(
        filesystem_dir
    )
)
