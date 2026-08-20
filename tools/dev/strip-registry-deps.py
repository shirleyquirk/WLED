#!/usr/bin/env python3
"""Drop declared dependencies from installed libraries' library.json.

The PlatformIO registry is unreachable in this container, so PlatformIO cannot resolve
a library's declared dependencies even when an identical copy is already installed from
git -- and it looks up framework builtins such as SPI by registry name too, which fails
the same way.

Every library WLED actually needs is pinned explicitly in platformio_override.ini, and
framework builtins are found by the library dependency finder through include scanning,
so removing the declarations is safe here. It is *not* safe as a general practice: it
silently un-declares real dependencies, which is how audioreactive lost arduinoFFT and
stopped linking until that was pinned explicitly too.

Usage: strip-registry-deps.py .pio/libdeps/<env>
"""
import glob
import json
import sys


def main(libdeps_dir):
    changed = []
    for path in glob.glob(libdeps_dir + "/*/library.json"):
        with open(path) as fh:
            data = json.load(fh)
        if data.pop("dependencies", None) is None:
            continue
        with open(path, "w") as fh:
            json.dump(data, fh, indent=2)
        changed.append(path)
    print("\n".join(changed) if changed else "no changes")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
