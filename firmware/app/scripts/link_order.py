# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Link the ESP-IDF components in one fixed order, so that a build made again is the same to the byte.
# Contributors: None
#
# PlatformIO links each component's library in the order CMake's file API lists the app's
# dependencies, and CMake lists them in no stable order: two builds of the same sources put
# ledc and i2c, say, in a different order, and the image differs. Builds link whatever the order,
# so the order does not decide what resolves. Here the component libraries are sorted by name in
# the places they hold among LIBS, and the system libraries keep theirs.

Import("env")

from pathlib import Path


def _is_component(lib) -> bool:
    # A component's library is a file of the build, under esp-idf/; the system's are names.
    return not isinstance(lib, str) and "esp-idf" in Path(str(lib)).parts


def fix_link_order() -> None:
    libs = list(env.get("LIBS", []))
    places = [i for i, lib in enumerate(libs) if _is_component(lib)]
    ordered = sorted((libs[i] for i in places), key=lambda lib: Path(str(lib)).as_posix())
    for place, lib in zip(places, ordered):
        libs[place] = lib
    env.Replace(LIBS=libs)
    print(f"Linking {len(places)} component libraries in a fixed order.")


fix_link_order()
