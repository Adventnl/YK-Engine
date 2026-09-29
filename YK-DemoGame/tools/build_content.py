"""Builds the demo's generated content and checks it with the engine's own tools.

    python3 tools/build_content.py [--art] [--yk <path to the yk executable>]

  --art   also regenerate the art and audio (tools/art/generate_all.py); slow and needs Pillow
  --yk    the `yk` command line built from the engine (default: $YK, else `yk` on PATH). When found,
          the written files are put in canonical form (`yk format`) and validated (`yk validate`).

Prefabs and the level are ordinary data files: after this runs they can be edited in the editor and
this script is not needed again. It exists so the demo can be regenerated and reviewed as code.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)


def find_yk(argv):
    if "--yk" in argv:
        return argv[argv.index("--yk") + 1]
    return os.environ.get("YK") or shutil.which("yk")


def main(argv):
    if "--art" in argv:
        subprocess.check_call([sys.executable, os.path.join(HERE, "art", "generate_all.py")])
    import build_prefabs
    import build_level
    build_prefabs.main()
    build_level.main()
    yk = find_yk(argv)
    if not yk:
        print("`yk` not found: skipping canonical formatting and validation (pass --yk <path>)")
        return 0
    subprocess.check_call([yk, "format", PROJECT])
    return subprocess.call([yk, "validate", PROJECT])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
