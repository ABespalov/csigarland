# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: Make the update certificates, their firmware header, and signed update files.
# Contributors: None
#
# Runs as a plain program under whichever Python has the `cryptography` package:
#
#   ota_sign_tool.py new-root <dir> [country] [years]     needs CSIGARLAND_ROOT_PASSPHRASE
#   ota_sign_tool.py new-signing <dir> [country] [years]  needs CSIGARLAND_ROOT_PASSPHRASE
#   ota_sign_tool.py throwaway <dir>                      a chain nobody keeps, for CI
#   ota_sign_tool.py root-header <dir> <out.h>
#   ota_sign_tool.py sign <dir> <effects.pack> <firmware.bin> <out.ota>
#   ota_sign_tool.py verify <root.crt> <file.ota>         prints what it holds, as JSON
#
# In `sign`, a dash stands for a part the update leaves out: firmware or effects stay as they
# are on the garland. The firmware holds the root certificate. Each update file carries the
# signing certificate and a signature made with its key, so a new signing key needs no new
# firmware. The root's
# key signs nothing else and can stay away from this machine between renewals. The file
# layout is the one src/ota_logic.h reads.

import datetime
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.x509.oid import ExtendedKeyUsageOID, ExtensionOID, NameOID, ObjectIdentifier

MAGIC = b"CGOT"
FORMAT = 3
NO_PART = "-"
HEADER_BYTES = 148
MAX_CERTIFICATE_BYTES = 1024
BYTES_PER_LINE = 12

# Yours, from deploy/new-signing-certificate.ps1; they end up in every update file.
ORGANIZATION = os.environ.get("CSIGARLAND_CERT_ORGANIZATION", "csiGarland owner")
PRODUCT = "csiGarland"
PERSON = os.environ.get("CSIGARLAND_CERT_PERSON", "csiGarland owner")
EMAIL = os.environ.get("CSIGARLAND_CERT_EMAIL", "")
ROOT_YEARS = 20
SIGNING_YEARS = 5

ROOT_KEY = "ota-root-ca.local.key"
ROOT_CERTIFICATE = "ota-root-ca.local.crt"
SIGNING_KEY = "ota-signing.local.key"
SIGNING_CERTIFICATE = "ota-signing.local.crt"



def name(common_name: str, country: str) -> x509.Name:
    parts = [x509.NameAttribute(NameOID.COUNTRY_NAME, country)] if country else []
    parts += [
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, ORGANIZATION),
        x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, PRODUCT),
        x509.NameAttribute(NameOID.COMMON_NAME, common_name),
    ]
    if EMAIL:
        parts.append(x509.NameAttribute(NameOID.EMAIL_ADDRESS, EMAIL))
    return x509.Name(parts)


def passphrase() -> bytes:
    value = os.environ.get("CSIGARLAND_ROOT_PASSPHRASE", "")
    if not value:
        raise SystemExit("The root key needs a passphrase: set CSIGARLAND_ROOT_PASSPHRASE.")
    return value.encode()


def save_key(path: Path, key: ec.EllipticCurvePrivateKey, password: bytes | None) -> None:
    protection = serialization.BestAvailableEncryption(password) if password else serialization.NoEncryption()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, protection))


def key_usage(signs_certificates: bool) -> x509.KeyUsage:
    return x509.KeyUsage(
        digital_signature=not signs_certificates,
        content_commitment=False,
        key_encipherment=False,
        data_encipherment=False,
        key_agreement=False,
        key_cert_sign=signs_certificates,
        crl_sign=signs_certificates,
        encipher_only=False,
        decipher_only=False,
    )


def make_root(folder: Path, country: str, years: int, password: bytes | None) -> None:
    key = ec.generate_private_key(ec.SECP256R1())
    subject = name(f"{PRODUCT} Root CA", country)
    now = datetime.datetime.now(datetime.timezone.utc)
    certificate = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + datetime.timedelta(days=365 * years))
        .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
        .add_extension(key_usage(True), critical=True)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
        .sign(key, hashes.SHA256())
    )
    save_key(folder / ROOT_KEY, key, password)
    (folder / ROOT_CERTIFICATE).write_bytes(certificate.public_bytes(serialization.Encoding.PEM))


def load_root(folder: Path, password: bytes | None) -> tuple[ec.EllipticCurvePrivateKey, x509.Certificate]:
    key = serialization.load_pem_private_key((folder / ROOT_KEY).read_bytes(), password=password)
    return key, load_certificate(folder / ROOT_CERTIFICATE)


def make_signing(folder: Path, country: str, years: int, password: bytes | None) -> None:
    root_key, root = load_root(folder, password)
    key = ec.generate_private_key(ec.SECP256R1())
    now = datetime.datetime.now(datetime.timezone.utc)
    certificate = (
        x509.CertificateBuilder()
        .subject_name(name(PERSON, country))
        .issuer_name(root.subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + datetime.timedelta(days=365 * years))
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(key_usage(False), critical=True)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CODE_SIGNING]), critical=True)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), critical=False)
        .sign(root_key, hashes.SHA256())
    )
    # Unprotected, as every build signs with it; the root can replace it.
    save_key(folder / SIGNING_KEY, key, None)
    (folder / SIGNING_CERTIFICATE).write_bytes(certificate.public_bytes(serialization.Encoding.PEM))


def load_certificate(path: Path) -> x509.Certificate:
    return x509.load_pem_x509_certificate(path.read_bytes())


def throwaway(folder: Path) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    if not (folder / SIGNING_CERTIFICATE).is_file():
        make_root(folder, "", 1, None)
        make_signing(folder, "", 1, None)


# From a folder of certificates, or from a root certificate file alone, as a release publishes it.
def root_header(folder: Path, out_path: Path) -> None:
    raw = load_certificate(folder if folder.is_file() else folder / ROOT_CERTIFICATE).public_bytes(serialization.Encoding.DER)
    lines = [
        ", ".join(f"0x{byte:02X}" for byte in raw[i : i + BYTES_PER_LINE])
        for i in range(0, len(raw), BYTES_PER_LINE)
    ]
    text = "\n".join(
        [
            "// Generated by ota_sign_tool.py from the root certificate. Do not edit.",
            "#pragma once",
            "",
            "#include <cstdint>",
            "",
            "namespace garland {",
            "",
            "inline constexpr uint8_t kOtaRootCertificate[] = {",
            *(f"    {line}," for line in lines),
            "};",
            "",
            "}  // namespace garland",
            "",
        ]
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if not out_path.is_file() or out_path.read_text(encoding="utf-8") != text:
        out_path.write_text(text, encoding="utf-8")


def read_part(path: str) -> bytes:
    return b"" if path == NO_PART else Path(path).read_bytes()


def part_digest(part: bytes) -> bytes:
    return hashlib.sha256(part).digest() if part else bytes(32)


# expected_root, if given, is the SHA-256 of the root the image trusts: the root read here, and the chain
# checked against it, have to be that one, or what is signed would be refused by the image it goes with.
def sign(folder: Path, pack_path: str, image_path: str, out_path: Path, expected_root: str = "") -> None:
    key = serialization.load_pem_private_key((folder / SIGNING_KEY).read_bytes(), password=None)
    certificate = load_certificate(folder / SIGNING_CERTIFICATE)
    root = load_certificate(folder / ROOT_CERTIFICATE)
    if expected_root and root.fingerprint(hashes.SHA256()).hex() != expected_root.lower():
        raise SystemExit(f"The root in {folder} is not the one the image trusts ({expected_root}): it changed while this built. Build again.")
    der_certificate = certificate.public_bytes(serialization.Encoding.DER)
    if len(der_certificate) > MAX_CERTIFICATE_BYTES:
        raise SystemExit(f"The signing certificate is {len(der_certificate)} bytes; an update holds {MAX_CERTIFICATE_BYTES}.")
    # Checked here too, so a mismatched pair never produces a file to carry around.
    certificate.verify_directly_issued_by(root)
    pack = read_part(pack_path)
    image = read_part(image_path)
    if not pack and not image:
        raise SystemExit("An update holds an effects pack, a firmware image, or both.")
    signed = (
        MAGIC
        + struct.pack("<IIII", FORMAT, len(der_certificate), len(pack), len(image))
        + part_digest(pack)
        + part_digest(image)
    )
    digest = hashlib.sha256(signed).digest()
    # Deterministic, by RFC 6979: the same parts under the same key make the same file, so a release
    # can be built again to the byte. The nonce comes from the key and the digest, as secret as ever.
    prehashed = ec.ECDSA(utils.Prehashed(hashes.SHA256()), deterministic_signing=True)
    der = key.sign(digest, prehashed)
    certificate.public_key().verify(der, digest, prehashed)
    r, s = utils.decode_dss_signature(der)
    out_path.write_bytes(signed + r.to_bytes(32, "big") + s.to_bytes(32, "big") + der_certificate + pack + image)
    print(f"Signed update: {out_path} ({len(pack)} bytes of effects, {len(image)} bytes of image)")


# As mbedtls_x509_crt_check_extended_key_usage judges it, which is what the garland runs: a
# certificate naming no usage at all passes, and one naming others has to name this one or any.
def may_sign_code(certificate: x509.Certificate) -> bool:
    try:
        usages = certificate.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value
    except x509.ExtensionNotFound:
        return True
    return ExtendedKeyUsageOID.CODE_SIGNING in usages or ExtendedKeyUsageOID.ANY_EXTENDED_KEY_USAGE in usages


ANY_POLICY = ObjectIdentifier("2.5.29.32.0")

# Extensions mbedTLS reads in x509_get_crt_ext. Any other, if critical, it refuses outright.
MBEDTLS_READS = {
    ExtensionOID.BASIC_CONSTRAINTS,
    ExtensionOID.KEY_USAGE,
    ExtensionOID.EXTENDED_KEY_USAGE,
    ExtensionOID.SUBJECT_KEY_IDENTIFIER,
    ExtensionOID.AUTHORITY_KEY_IDENTIFIER,
    ExtensionOID.SUBJECT_ALTERNATIVE_NAME,
    ExtensionOID.CERTIFICATE_POLICIES,
    ObjectIdentifier("2.16.840.1.113730.1.1"),  # Netscape certificate type.
}


# What a signing certificate has to be for the garland to take it, beyond its issuer's signature.
# None of what it says is the time: the garland does not read a certificate's dates.
def certificate_problem(certificate: x509.Certificate) -> str | None:
    key = certificate.public_key()
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        return "its key is not P-256, which is the only signature an update has room for"
    if not may_sign_code(certificate):
        return "its certificate is not for signing code"
    for extension in certificate.extensions:
        if extension.critical and extension.oid not in MBEDTLS_READS:
            return f"its certificate has a critical extension mbedTLS does not know, {extension.oid.dotted_string}"
        # Of these mbedTLS knows only the policy that says nothing, and refuses others if critical.
        if extension.critical and extension.oid == ExtensionOID.CERTIFICATE_POLICIES:
            if any(policy.policy_identifier != ANY_POLICY for policy in extension.value):
                return "its certificate has a critical policy mbedTLS does not know"
    return None


# verify_directly_issued_by looks only at the name and the signature. The garland also wants the
# issuer to be a CA that, if it says what its key is for, may sign certificates.
def issuer_problem(root: x509.Certificate) -> str | None:
    try:
        if not root.extensions.get_extension_for_class(x509.BasicConstraints).value.ca:
            return "its issuer is not a CA"
    except x509.ExtensionNotFound:
        return "its issuer is not a CA"
    try:
        if not root.extensions.get_extension_for_class(x509.KeyUsage).value.key_cert_sign:
            return "its issuer's key is not for signing certificates"
    except x509.ExtensionNotFound:
        pass
    return None


# The whole file against a root, offline, by the rules of the garland's own check, as far as they
# are known here. Left out is what depends on the board: its slots, and an image being for its chip.
def verify(root_path: Path, ota_path: Path) -> None:
    data = ota_path.read_bytes()
    if len(data) < HEADER_BYTES or data[:4] != MAGIC:
        raise SystemExit(f"{ota_path} is not an update file.")
    file_format, certificate_bytes, pack_bytes, image_bytes = struct.unpack("<IIII", data[4:20])
    if file_format != FORMAT or len(data) != HEADER_BYTES + certificate_bytes + pack_bytes + image_bytes:
        raise SystemExit(f"{ota_path} is of another format, or incomplete.")
    if certificate_bytes == 0 or certificate_bytes > MAX_CERTIFICATE_BYTES:
        raise SystemExit(f"{ota_path} has a certificate of {certificate_bytes} bytes; an update holds 1 to {MAX_CERTIFICATE_BYTES}.")
    if pack_bytes == 0 and image_bytes == 0:
        raise SystemExit(f"{ota_path} holds neither effects nor firmware.")
    pack_start = HEADER_BYTES + certificate_bytes
    pack = data[pack_start : pack_start + pack_bytes]
    image = data[pack_start + pack_bytes :]
    if part_digest(pack) != data[20:52] or part_digest(image) != data[52:84]:
        raise SystemExit(f"{ota_path} holds parts its header does not name.")
    root = load_certificate(root_path)
    try:
        certificate = x509.load_der_x509_certificate(data[HEADER_BYTES:pack_start])
        certificate.verify_directly_issued_by(root)
        problem = certificate_problem(certificate) or issuer_problem(root)
        if problem is not None:
            raise ValueError(problem)
        r = int.from_bytes(data[84:116], "big")
        s = int.from_bytes(data[116:148], "big")
        certificate.public_key().verify(utils.encode_dss_signature(r, s), hashlib.sha256(data[:84]).digest(), ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    except Exception as error:
        raise SystemExit(f"{ota_path} is not signed under {root_path}: {error}")
    print(json.dumps({
        "pack_sha256": data[20:52].hex() if pack else None,
        "image_sha256": data[52:84].hex() if image else None,
        "signer": certificate.subject.rfc4514_string(),
        "root_sha256": root.fingerprint(hashes.SHA256()).hex(),
    }))


def main() -> None:
    args = sys.argv[1:]
    command = args[0] if args else ""
    if command == "new-root" and 2 <= len(args) <= 4:
        make_root(Path(args[1]), args[2] if len(args) > 2 else "", int(args[3]) if len(args) > 3 else ROOT_YEARS, passphrase())
    elif command == "new-signing" and 2 <= len(args) <= 4:
        make_signing(Path(args[1]), args[2] if len(args) > 2 else "", int(args[3]) if len(args) > 3 else SIGNING_YEARS, passphrase())
    elif command == "throwaway" and len(args) == 2:
        throwaway(Path(args[1]))
    elif command == "root-header" and len(args) == 3:
        root_header(Path(args[1]), Path(args[2]))
    elif command == "verify" and len(args) == 3:
        verify(Path(args[1]), Path(args[2]))
    elif command == "sign" and len(args) in (5, 6):
        sign(Path(args[1]), args[2], args[3], Path(args[4]), args[5] if len(args) == 6 else "")
    else:
        raise SystemExit("usage: see the top of this file")


main()
