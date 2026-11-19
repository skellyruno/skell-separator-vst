#!/usr/bin/env python3
"""Fail-closed Windows VST3 package staging and verification for Skell Separator."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
from pathlib import Path
from typing import Any

PRODUCT = "Skell Separator"
VENDOR = "Skell Audio"
VERSION = "1.13.0"
BUNDLE = f"{PRODUCT}.vst3"
MODULE = Path("Contents") / "x86_64-win" / BUNDLE
MODULEINFO = Path("Contents") / "Resources" / "moduleinfo.json"
CATEGORIES = {"Audio Module Class", "Component Controller Class"}
CID_RE = re.compile(r"^[0-9A-Fa-f]{32}$")


class PackageError(RuntimeError):
    pass


def strip_trailing_commas(text: str) -> str:
    """Remove JSON5 trailing commas without changing commas inside strings."""
    out = []
    quoted = escaped = False
    i = 0
    while i < len(text):
        ch = text[i]
        if quoted:
            out.append(ch)
            if escaped:
                escaped = False
            elif ch == chr(92):
                escaped = True
            elif ch == '"':
                quoted = False
            i += 1
            continue
        if ch == '"':
            quoted = True
            out.append(ch)
            i += 1
            continue
        if ch == ",":
            j = i + 1
            while j < len(text) and text[j].isspace():
                j += 1
            if j < len(text) and text[j] in "]}":
                i += 1
                continue
        out.append(ch)
        i += 1
    if quoted:
        raise PackageError("moduleinfo.json contains an unterminated string")
    return "".join(out)


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PackageError(f"moduleinfo.json contains duplicate key: {key}")
        result[key] = value
    return result


def load_moduleinfo(path: Path) -> tuple[dict[str, Any], str]:
    try:
        original = path.read_text(encoding="utf-8-sig")
        normalized = strip_trailing_commas(original)
        data = json.loads(normalized, object_pairs_hook=unique_object)
    except (OSError, json.JSONDecodeError, PackageError) as exc:
        raise PackageError(f"Invalid moduleinfo.json: {exc}") from exc
    if not isinstance(data, dict):
        raise PackageError("moduleinfo.json root must be an object")
    return data, normalized


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_pe32_plus_x64(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise PackageError("VST3 module is missing a valid DOS MZ header")
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if offset < 0x40 or offset + 26 > len(data):
        raise PackageError("VST3 module has an invalid PE header offset")
    if data[offset:offset + 4] != bytes((80, 69, 0, 0)):
        raise PackageError("VST3 module is missing the PE signature")
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    magic = struct.unpack_from("<H", data, offset + 24)[0]
    if machine != 0x8664:
        raise PackageError(f"Expected AMD64 PE machine 0x8664, got 0x{machine:04x}")
    if magic != 0x20B:
        raise PackageError(f"Expected PE32+ optional header 0x20b, got 0x{magic:04x}")


def validate_bundle(bundle: Path, expected_version: str) -> tuple[dict[str, Any], Path, str]:
    if bundle.name != BUNDLE or not bundle.is_dir() or bundle.is_symlink():
        raise PackageError(f"Expected VST3 bundle directory {BUNDLE}")
    if any(p.is_symlink() for p in bundle.rglob("*")):
        raise PackageError("VST3 bundle contains a symbolic link")
    info, normalized = load_moduleinfo(bundle / MODULEINFO)
    if info.get("Name") != PRODUCT or info.get("Version") != expected_version:
        raise PackageError("VST3 product name/version does not match the CMake project")
    factory = info.get("Factory Info")
    if not isinstance(factory, dict) or factory.get("Vendor") != VENDOR:
        raise PackageError("Factory Info.Vendor must be 'Skell Audio'")
    classes = info.get("Classes")
    if not isinstance(classes, list) or len(classes) != 2:
        raise PackageError("Expected exactly two VST3 classes")
    categories, cids = set(), []
    for item in classes:
        if not isinstance(item, dict):
            raise PackageError("VST3 class entry must be an object")
        if (item.get("Name"), item.get("Vendor"), item.get("Version")) != (PRODUCT, VENDOR, expected_version):
            raise PackageError("VST3 class identity does not match product metadata")
        category, cid = item.get("Category"), item.get("CID")
        if not isinstance(category, str) or not isinstance(cid, str) or not CID_RE.fullmatch(cid):
            raise PackageError("VST3 class category/CID is missing or malformed")
        categories.add(category)
        cids.append(cid.upper())
    if categories != CATEGORIES or len(set(cids)) != 2:
        raise PackageError("VST3 processor/controller categories or unique class IDs are invalid")
    module = bundle / MODULE
    modules = [p for p in bundle.rglob("*.vst3") if p.is_file()]
    if len(modules) != 1 or modules[0] != module:
        raise PackageError(f"Expected exactly one Windows module at {MODULE.as_posix()}")
    validate_pe32_plus_x64(module)
    return info, module, normalized


def stage_package(bundle: Path, output: Path, source_head: str, tested_commit: str,
                  run_id: str, expected_version: str,
                  repository_visibility: str = "unknown") -> dict[str, Any]:
    visibility = str(repository_visibility).strip().lower() or "unknown"
    if visibility not in {"public", "private", "internal", "unknown"}:
        raise PackageError(f"Unsupported repository visibility: {visibility}")
    upload_allowed = visibility == "private"
    artifact_access = {
        "private": "repository-restricted",
        "public": "public",
        "internal": "organization-internal",
        "unknown": "unknown",
    }[visibility]
    if upload_allowed:
        artifact_status = "owner-test preview; private repository; upload permitted"
    elif visibility == "public":
        artifact_status = "owner-test preview; public repository; upload withheld"
    elif visibility == "internal":
        artifact_status = "owner-test preview; internal repository; upload withheld"
    else:
        artifact_status = "owner-test preview; repository visibility unknown; upload withheld"
    _, source_module, normalized = validate_bundle(bundle, expected_version)
    source_info = bundle / MODULEINFO
    original_info = source_info.read_text(encoding="utf-8-sig")
    original_binary_hash = sha256(source_module)
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    staged_bundle = output / BUNDLE
    shutil.copytree(bundle, staged_bundle)
    (staged_bundle / MODULEINFO).write_text(normalized, encoding="utf-8")
    info, module, _ = validate_bundle(staged_bundle, expected_version)
    if source_info.read_text(encoding="utf-8-sig") != original_info or sha256(source_module) != original_binary_hash:
        raise PackageError("Staging modified the original compiled VST3 bundle")

    result = {
        "product": PRODUCT, "vendor": VENDOR, "version": expected_version,
        "format": "VST3", "platform": "Windows x86_64", "bundle": BUNDLE,
        "module": (Path(BUNDLE) / MODULE).as_posix(),
        "module_bytes": module.stat().st_size, "module_sha256": sha256(module),
        "moduleinfo_normalized": True,
        "class_ids": [x["CID"].upper() for x in info["Classes"]],
        "class_categories": sorted(x["Category"] for x in info["Classes"]),
        "source_head_sha": source_head, "tested_commit": tested_commit,
        "workflow_run_id": str(run_id),
        "repository_visibility": visibility,
        "artifact_access": artifact_access,
        "artifact_upload_allowed": upload_allowed,
        "release_status": "owner-test preview only; host acceptance not implied",
    }
    build_info = {
        "product": PRODUCT, "vendor": VENDOR, "version": expected_version,
        "format": "VST3", "platform": "Windows x86_64",
        "source_head_sha": source_head, "tested_commit": tested_commit,
        "workflow_run_id": str(run_id),
        "repository_visibility": visibility,
        "artifact_access": artifact_access,
        "artifact_upload_allowed": upload_allowed,
        "artifact_status": artifact_status,
    }
    nl = chr(10)
    (output / "vst3-verification.json").write_text(json.dumps(result, indent=2) + nl, encoding="utf-8")
    (output / "build-info.json").write_text(json.dumps(build_info, indent=2) + nl, encoding="utf-8")
    (output / "PREVIEW-ONLY.txt").write_text(nl.join([
        "SKELL SEPARATOR — OWNER-TEST WINDOWS VST3 PREVIEW",
        "Not an approved public or commercial release.",
        f"Repository visibility: {visibility}.",
        f"Product artifact upload permitted: {'yes' if upload_allowed else 'no'}.",
        "Product ZIP uploads are withheld unless repository visibility is private.",
        "Existing artifacts from public-repository runs remain publicly accessible; do not treat them as private.",
        "FL Studio acceptance, musical A/B, target-hardware CPU/latency, licensing and updater/rollback remain separate gates.",
        "",
    ]), encoding="utf-8")
    files = sorted(p for p in output.rglob("*") if p.is_file() and p.name != "SHA256SUMS.txt")
    checksum_lines = [f"{sha256(p)}  {p.relative_to(output).as_posix()}" for p in files]
    (output / "SHA256SUMS.txt").write_text(nl.join(checksum_lines) + nl, encoding="ascii")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--source-head", default="")
    parser.add_argument("--tested-commit", default="")
    parser.add_argument("--run-id", default="")
    parser.add_argument("--expected-version", default=VERSION)
    parser.add_argument("--repository-visibility",
                        choices=("public", "private", "internal", "unknown"),
                        default="unknown")
    args = parser.parse_args()
    try:
        result = stage_package(args.bundle, args.output_dir, args.source_head,
                               args.tested_commit, args.run_id, args.expected_version,
                               args.repository_visibility)
    except PackageError as exc:
        print(f"PACKAGE VERIFICATION FAILED: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
