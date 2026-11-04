# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Put the update root certificate in the build and sign each firmware.bin with the effects pack.
# Contributors: None
#
# The certificates and keys stay in cert/ at the repository root, where git ignores them, and
# deploy/new-signing-certificate.ps1 makes them. With CSIGARLAND_THROWAWAY_SIGNING set, as
# in CI, a chain nobody keeps stands in: that image accepts no real update. With
# CSIGARLAND_RELEASE_ROOT naming a release's root certificate, as deploy/rebuild-release.ps1
# sets it, the image holds that root and nothing is signed, to compare with the release. The
# `cryptography` package is in the Python environment PlatformIO makes for ESP-IDF, not
# necessarily in the one running this script, so the work happens in a separate program.
# PlatformIO makes that environment only after scripts like this have run, so on a clean
# machine the package has to be installed here.

Import("env")

import base64
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

_PROJECT_DIR = Path(env["PROJECT_DIR"])
_TOOL = _PROJECT_DIR / "scripts" / "ota_sign_tool.py"
_HEADER = _PROJECT_DIR / "generated" / "ota_root_certificate.h"
_PACK = _PROJECT_DIR / "generated" / "effects.pack"
_OWNED = _PROJECT_DIR.parent.parent / "cert"
_THROWAWAY = _PROJECT_DIR / "generated" / "throwaway-signing"
# The version ESP-IDF's environment holds today, so that a machine installing it signs with the
# same one. An upgrade is a change to the toolchain, made on purpose.
_CRYPTOGRAPHY = "cryptography==44.0.3"
_PINNED = _CRYPTOGRAPHY.split("==")[1]


# The version a Python holds, or None. Merely importing is not enough: an older one lacks what the
# tool calls, and signing would then depend on whatever this machine happened to have.
def _cryptography_version(python: str) -> "str | None":
    probe = subprocess.run([python, "-c", "import cryptography, sys; sys.stdout.write(cryptography.__version__)"],
                           capture_output=True, text=True)
    return probe.stdout.strip() if probe.returncode == 0 else None


# PlatformIO's own Python, which runs this, then ESP-IDF's, newest first: the first with the pinned
# version. With none, it is installed into PlatformIO's own, and never into a Python outside it.
# deploy/SigningPython.psm1 chooses the same way for the scripts.
def _find_python() -> str:
    environments = Path(env.subst("$PROJECT_CORE_DIR")) / "penv"
    candidates = [sys.executable]
    for root in sorted(environments.glob(".espidf-*"), reverse=True):
        candidates += [str(root / "Scripts" / "python.exe"), str(root / "bin" / "python")]
    for candidate in candidates:
        if Path(candidate).is_file() and _cryptography_version(candidate) == _PINNED:
            return candidate
    own = Path(sys.executable).resolve()
    if environments.resolve() not in own.parents:
        # PlatformIO installed with pip runs from that Python, and ESP-IDF's comes only later in the
        # build. The guides install both packages together, so a first build never stops here.
        raise RuntimeError(f"No Python of PlatformIO's holds {_CRYPTOGRAPHY}, and this one, {own}, is not PlatformIO's to install into. "
                           f"Install it beside PlatformIO: \"{own}\" -m pip install {_CRYPTOGRAPHY}")
    print(f"No Python of PlatformIO's holds {_CRYPTOGRAPHY}: installing it into {own}.")
    subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", _CRYPTOGRAPHY], check=False)
    if _cryptography_version(sys.executable) == _PINNED:
        return sys.executable
    raise RuntimeError(f"Could not install {_CRYPTOGRAPHY}. Try: {sys.executable} -m pip install {_CRYPTOGRAPHY}")


_PYTHON = _find_python()

# A release built again by anyone, with its root certificate, which every image holds anyway: the
# image and the pack come out as the release's, and with no key, nothing is signed.
_RELEASE_ROOT = os.environ.get("CSIGARLAND_RELEASE_ROOT", "")

if _RELEASE_ROOT:
    _FOLDER = Path(_RELEASE_ROOT)
    if not _FOLDER.is_file():
        raise RuntimeError(f"CSIGARLAND_RELEASE_ROOT names no file: {_RELEASE_ROOT}")
elif os.environ.get("CSIGARLAND_THROWAWAY_SIGNING"):
    _FOLDER = _THROWAWAY
    if (_OWNED / "ota-root-ca.local.crt").is_file():
        # deploy/flash-device.ps1 and ota-update.ps1 refuse this build: it trusts a root nobody keeps.
        print("WARNING: CSIGARLAND_THROWAWAY_SIGNING is set, so this is a test build under a throwaway chain,"
              " though cert/ holds this repository's own. No board or garland should be given it.")
    subprocess.run([_PYTHON, str(_TOOL), "throwaway", str(_FOLDER)], check=True)
else:
    _FOLDER = _OWNED
    if not (_FOLDER / "ota-signing.local.crt").is_file():
        raise RuntimeError("No update certificate. Make one: pwsh -File deploy\\new-signing-certificate.ps1")

subprocess.run([_PYTHON, str(_TOOL), "root-header", str(_FOLDER), str(_HEADER)], check=True)


# The root in the folder, as the bytes of its certificate, which the image holds as they are.
def _root_der(folder: Path) -> bytes:
    path = folder if folder.is_file() else folder / "ota-root-ca.local.crt"
    match = re.search(r"-----BEGIN CERTIFICATE-----(.+?)-----END CERTIFICATE-----", path.read_text(encoding="ascii"), re.S)
    if match is None:
        raise RuntimeError(f"{path} holds no certificate")
    return base64.b64decode("".join(match.group(1).split()))


# One update file for each thing a garland may need: both parts, or either alone. The root is the one in the
# image's own bytes: the header it was compiled from is shared by both profiles' builds, and a root replaced
# meanwhile, by new-signing-certificate.ps1 -Force, may have made it again since. Each signing holds the root
# it reads to that one, which it would otherwise sign under and the image refuse.
def _sign(target, source, env):
    image = Path(str(target[0]))
    der = _root_der(_FOLDER)
    if der not in image.read_bytes():
        print(f"The image does not hold the root in {_FOLDER}: it changed while this built. Build again.")
        return 1
    expected = hashlib.sha256(der).hexdigest()
    for pack, part, out in (
        (str(_PACK), str(image), image.with_suffix(".ota")),
        ("-", str(image), image.with_name("firmware-only.ota")),
        (str(_PACK), "-", image.with_name("effects.ota")),
    ):
        exit_code = subprocess.run([_PYTHON, str(_TOOL), "sign", str(_FOLDER), pack, part, str(out), expected]).returncode
        if exit_code != 0:
            return exit_code
    return 0


# The image does not hold the pack, but its update file does: new effects have to sign it again.
env.Depends("$BUILD_DIR/${PROGNAME}.bin", str(_PACK))
if not _RELEASE_ROOT:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _sign)
