# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Holds ota_sign_tool.py's verify to what the garland refuses.
# Contributors: None
#
# Runs under whichever Python has the `cryptography` package, with a chain nobody keeps:
#
#   python firmware/app/scripts/test_ota_sign_tool.py

import datetime
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID, ObjectIdentifier

TOOL = Path(__file__).with_name("ota_sign_tool.py")
REPOSITORY = Path(__file__).resolve().parents[3]
ROOT_KEY = "ota-root-ca.local.key"
ROOT_CERTIFICATE = "ota-root-ca.local.crt"


def run_tool(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(TOOL), *args], capture_output=True, text=True)


def root_of_kind(folder: Path, is_ca: bool, usage: bool | None) -> None:
    folder.mkdir()
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "root under test")])
    now = datetime.datetime.now(datetime.timezone.utc)
    builder = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + datetime.timedelta(days=1))
        .add_extension(x509.BasicConstraints(ca=is_ca, path_length=None), critical=True)
    )
    if usage is not None:
        builder = builder.add_extension(
            x509.KeyUsage(digital_signature=True, content_commitment=False, key_encipherment=False, data_encipherment=False, key_agreement=False,
                          key_cert_sign=usage, crl_sign=False, encipher_only=False, decipher_only=False),
            critical=True,
        )
    (folder / ROOT_KEY).write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    (folder / ROOT_CERTIFICATE).write_bytes(builder.sign(key, hashes.SHA256()).public_bytes(serialization.Encoding.PEM))


def certificate_for(folder: Path, usages: list | None, padding: int = 0, unknown_critical: bool | None = None, curve=None, policy: tuple[str, bool] | None = None):
    root_key = serialization.load_pem_private_key((folder / ROOT_KEY).read_bytes(), password=None)
    root = x509.load_pem_x509_certificate((folder / ROOT_CERTIFICATE).read_bytes())
    key = ec.generate_private_key(curve or ec.SECP256R1())
    now = datetime.datetime.now(datetime.timezone.utc)
    builder = (
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "test")]))
        .issuer_name(root.subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + datetime.timedelta(days=1))
    )
    if usages is not None:
        builder = builder.add_extension(x509.ExtendedKeyUsage(usages), critical=True)
    if unknown_critical is not None:
        builder = builder.add_extension(x509.UnrecognizedExtension(ObjectIdentifier("1.3.6.1.4.1.99999.1"), b"\x05\x00"), critical=unknown_critical)
    if policy is not None:
        policies = x509.CertificatePolicies([x509.PolicyInformation(ObjectIdentifier(policy[0]), None)])
        builder = builder.add_extension(policies, critical=policy[1])
    if padding > 0:
        builder = builder.add_extension(x509.SubjectAlternativeName([x509.DNSName("a" * 60 + ".test")] * (padding // 60 + 1)), critical=False)
    return key, builder.sign(root_key, hashes.SHA256()).public_bytes(serialization.Encoding.DER)


def digest_of(part: bytes) -> bytes:
    return hashlib.sha256(part).digest() if part else bytes(32)


def update_file(key, certificate: bytes, pack: bytes, image: bytes) -> bytes:
    signed = b"CGOT" + struct.pack("<IIII", 3, len(certificate), len(pack), len(image)) + digest_of(pack) + digest_of(image)
    der = key.sign(hashlib.sha256(signed).digest(), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der)
    return signed + r.to_bytes(32, "big") + s.to_bytes(32, "big") + certificate + pack + image


def main() -> None:
    (REPOSITORY / "temp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=REPOSITORY / "temp") as scratch:
        folder = Path(scratch) / "chain"
        other = Path(scratch) / "other"
        for chain in (folder, other):
            assert run_tool("throwaway", str(chain)).returncode == 0
        root = str(folder / ROOT_CERTIFICATE)
        out = Path(scratch) / "file.ota"

        def verdict(name: str, data: bytes, accepted: bool, against: str = root) -> list[str]:
            out.write_bytes(data)
            result = run_tool("verify", against, str(out))
            good = (result.returncode == 0) == accepted
            print(f"{'ok' if good else 'FAILED'}: {name}: {(result.stdout or result.stderr).strip()[:90]}")
            return [] if good else [name]

        code_signing = [ExtendedKeyUsageOID.CODE_SIGNING]
        pack = b"pack bytes"
        key, certificate = certificate_for(folder, code_signing)
        good_file = update_file(key, certificate, pack, b"")
        failed = verdict("a good file", good_file, True)
        failed += verdict("under another root", good_file, False, str(other / ROOT_CERTIFICATE))
        failed += verdict("firmware alone", update_file(key, certificate, b"", b"image"), True)

        key, wrong = certificate_for(folder, [ExtendedKeyUsageOID.SERVER_AUTH])
        failed += verdict("a certificate for something else", update_file(key, wrong, pack, b""), False)
        key, named_any = certificate_for(folder, [ExtendedKeyUsageOID.ANY_EXTENDED_KEY_USAGE])
        failed += verdict("a certificate for any use", update_file(key, named_any, pack, b""), True)
        key, none_named = certificate_for(folder, None)
        failed += verdict("a certificate naming no use, as mbedTLS passes it", update_file(key, none_named, pack, b""), True)

        key, extended = certificate_for(folder, code_signing, unknown_critical=True)
        failed += verdict("a critical extension mbedTLS does not know", update_file(key, extended, pack, b""), False)
        key, extended = certificate_for(folder, code_signing, unknown_critical=False)
        failed += verdict("the same, not critical, which it skips", update_file(key, extended, pack, b""), True)
        key, wide = certificate_for(folder, code_signing, curve=ec.SECP384R1())
        # P-384's own signature does not fit the file, so only the key can be the reason here.
        failed += verdict("a key that is not P-256", update_file(ec.generate_private_key(ec.SECP256R1()), wide, pack, b""), False)

        private_policy = "1.3.6.1.4.1.99999.2"
        key, policed = certificate_for(folder, code_signing, policy=(private_policy, True))
        failed += verdict("a critical policy mbedTLS does not know", update_file(key, policed, pack, b""), False)
        key, policed = certificate_for(folder, code_signing, policy=(private_policy, False))
        failed += verdict("the same, not critical, which it skips", update_file(key, policed, pack, b""), True)
        key, policed = certificate_for(folder, code_signing, policy=("2.5.29.32.0", True))
        failed += verdict("the critical policy it does know", update_file(key, policed, pack, b""), True)

        for name, is_ca, usage, accepted in (
            ("an issuer that is a CA, with a key for signing certificates", True, True, True),
            ("an issuer that is a CA and does not say what its key is for", True, None, True),
            ("an issuer that is not a CA", False, None, False),
            ("an issuer whose key is not for signing certificates", True, False, False),
        ):
            issuer = Path(scratch) / f"issuer-{is_ca}-{usage}"
            root_of_kind(issuer, is_ca, usage)
            key, issued = certificate_for(issuer, code_signing)
            failed += verdict(name, update_file(key, issued, pack, b""), accepted, str(issuer / ROOT_CERTIFICATE))

        key, large = certificate_for(folder, code_signing, padding=1200)
        assert len(large) > 1024
        failed += verdict("a certificate too large", update_file(key, large, pack, b""), False)

        key, certificate = certificate_for(folder, code_signing)
        failed += verdict("nothing in it", update_file(key, certificate, b"", b""), False)
        failed += verdict("no certificate", update_file(key, b"", pack, b""), False)
        failed += verdict("a certificate of noise", update_file(key, b"noise", pack, b""), False)
        failed += verdict("cut short", good_file[:-1], False)
        failed += verdict("too short for a header", good_file[:100], False)
        failed += verdict("not an update", b"X" + good_file[1:], False)
        failed += verdict("another format", good_file[:4] + struct.pack("<I", 2) + good_file[8:], False)
        failed += verdict("a part changed", good_file[:-1] + b"X", False)
        failed += verdict("the header changed", good_file[:20] + bytes(32) + good_file[52:], False)
        tampered = bytearray(good_file)
        tampered[100] ^= 1
        failed += verdict("the signature changed", bytes(tampered), False)

        # The tool signs deterministically: the same parts under the same key make the same file.
        pack_path, image_path = Path(scratch) / "pack.bin", Path(scratch) / "image.bin"
        pack_path.write_bytes(pack)
        image_path.write_bytes(b"image bytes")
        signed = []
        for n in range(2):
            path = Path(scratch) / f"again-{n}.ota"
            assert run_tool("sign", str(folder), str(pack_path), str(image_path), str(path)).returncode == 0
            signed.append(path.read_bytes())
        same = signed[0] == signed[1]
        print(f"{'ok' if same else 'FAILED'}: two signings of the same parts are the same file")
        failed += [] if same else ["deterministic signing"]
        failed += verdict("a file the tool signed", signed[0], True)

        # Held to the root the image trusts: the folder's own signs, another is refused and writes nothing.
        root_sha = hashlib.sha256(x509.load_pem_x509_certificate((folder / "ota-root-ca.local.crt").read_bytes()).public_bytes(serialization.Encoding.DER)).hexdigest()
        held = Path(scratch) / "held.ota"
        refused = Path(scratch) / "refused.ota"
        took = run_tool("sign", str(folder), str(pack_path), str(image_path), str(held), root_sha).returncode == 0
        refuses = run_tool("sign", str(folder), str(pack_path), str(image_path), str(refused), "00" * 32).returncode != 0 and not refused.exists()
        print(f"{'ok' if took and refuses else 'FAILED'}: signing is held to the root the image trusts")
        failed += [] if took and refuses else ["the image's root"]
    if failed:
        raise SystemExit(f"{len(failed)} cases went wrong.")
    print("verify judges every case as the garland does.")


main()
