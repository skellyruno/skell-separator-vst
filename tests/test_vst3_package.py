import json
import struct
import tempfile
import unittest
from pathlib import Path

from tools.verify_vst3_package import BUNDLE, MODULE, MODULEINFO, PackageError, load_moduleinfo, sha256, stage_package, strip_trailing_commas, validate_bundle


def fake_pe(machine=0x8664, magic=0x20B):
    data = bytearray(512)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = bytes((80, 69, 0, 0))
    struct.pack_into("<H", data, 0x84, machine)
    struct.pack_into("<H", data, 0x98, magic)
    return bytes(data)


def metadata(version="1.13.0", vendor="Skell Audio", duplicate=False):
    cid2 = "ABCDEF019182FAEB536B417553733031" if duplicate else "ABCDEF011234ABCD536B417553733031"
    return {
        "Name": "Skell Separator", "Version": version,
        "Factory Info": {"Vendor": vendor, "URL": "", "E-Mail": ""},
        "Classes": [
            {"CID": "ABCDEF019182FAEB536B417553733031", "Category": "Audio Module Class", "Name": "Skell Separator", "Vendor": "Skell Audio", "Version": version},
            {"CID": cid2, "Category": "Component Controller Class", "Name": "Skell Separator", "Vendor": "Skell Audio", "Version": version},
        ],
    }


def make_bundle(root, module=None, info=None, trailing=True):
    bundle = root / BUNDLE
    binary = bundle / MODULE
    binary.parent.mkdir(parents=True)
    binary.write_bytes(module if module is not None else fake_pe())
    path = bundle / MODULEINFO
    path.parent.mkdir(parents=True)
    raw = json.dumps(info if info is not None else metadata(), indent=2)
    if trailing:
        raw = raw.replace('"E-Mail": ""', '"E-Mail": "",')
        raw = raw.replace('"Version": "1.13.0"' + chr(10), '"Version": "1.13.0",' + chr(10))
    path.write_text(raw, encoding="utf-8")
    return bundle


class PackageVerifierTests(unittest.TestCase):
    def test_trailing_commas_are_removed_outside_strings_only(self):
        raw = '{"text":"literal ,} and ,]", "nested":[1,2,],}'
        self.assertEqual(strip_trailing_commas(raw), '{"text":"literal ,} and ,]", "nested":[1,2]}')

    def test_duplicate_json_keys_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "moduleinfo.json"
            path.write_text('{"Name":"A","Name":"B"}', encoding="utf-8")
            with self.assertRaisesRegex(PackageError, "duplicate key"):
                load_moduleinfo(path)

    def test_real_bundle_shape_and_juce_trailing_commas(self):
        with tempfile.TemporaryDirectory() as tmp:
            data, module, _ = validate_bundle(make_bundle(Path(tmp)), "1.13.0")
            self.assertEqual(data["Name"], "Skell Separator")
            self.assertTrue(module.is_file())

    def test_wrong_vendor_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(PackageError, "Vendor"):
                validate_bundle(make_bundle(Path(tmp), info=metadata(vendor="Other")), "1.13.0")

    def test_wrong_version_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(PackageError, "name/version"):
                validate_bundle(make_bundle(Path(tmp), info=metadata(version="1.12.0")), "1.13.0")

    def test_duplicate_class_ids_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(PackageError, "class IDs"):
                validate_bundle(make_bundle(Path(tmp), info=metadata(duplicate=True)), "1.13.0")

    def test_non_amd64_binary_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(PackageError, "AMD64"):
                validate_bundle(make_bundle(Path(tmp), module=fake_pe(machine=0x14C)), "1.13.0")

    def test_pe32_binary_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(PackageError, r"PE32\+"):
                validate_bundle(make_bundle(Path(tmp), module=fake_pe(magic=0x10B)), "1.13.0")

    def test_missing_module_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            bundle = make_bundle(Path(tmp))
            (bundle / MODULE).unlink()
            with self.assertRaises(PackageError):
                validate_bundle(bundle, "1.13.0")

    def test_duplicate_modules_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            bundle = make_bundle(Path(tmp))
            extra = bundle / "Contents" / "x86_64-win" / "duplicate.vst3"
            extra.write_bytes(fake_pe())
            with self.assertRaisesRegex(PackageError, "exactly one Windows module"):
                validate_bundle(bundle, "1.13.0")

    def test_staging_normalizes_copy_and_preserves_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bundle = make_bundle(root)
            original_info = (bundle / MODULEINFO).read_text(encoding="utf-8")
            original_hash = sha256(bundle / MODULE)
            stage = root / "release-stage"
            result = stage_package(bundle, stage, "source", "tested", "123", "1.13.0")
            self.assertEqual((bundle / MODULEINFO).read_text(encoding="utf-8"), original_info)
            self.assertEqual(sha256(bundle / MODULE), original_hash)
            self.assertTrue((stage / "build-info.json").is_file())
            self.assertTrue((stage / "vst3-verification.json").is_file())
            self.assertTrue((stage / "PREVIEW-ONLY.txt").is_file())
            self.assertFalse(any(stage.rglob("*.zip")))
            for line in (stage / "SHA256SUMS.txt").read_text(encoding="ascii").splitlines():
                digest, relative = line.split("  ", 1)
                self.assertEqual(digest, sha256(stage / relative))
            self.assertEqual(result["module_sha256"], original_hash)
            build_info = json.loads((stage / "build-info.json").read_text(encoding="utf-8"))
            self.assertEqual(build_info["repository_visibility"], "unknown")
            self.assertFalse(build_info["artifact_upload_allowed"])

    def test_public_repository_records_public_access_and_withholds_product_upload(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bundle = make_bundle(root)
            stage = root / "public-stage"
            result = stage_package(bundle, stage, "source", "tested", "123", "1.13.0",
                                   repository_visibility="public")
            build_info = json.loads((stage / "build-info.json").read_text(encoding="utf-8"))
            verification = json.loads((stage / "vst3-verification.json").read_text(encoding="utf-8"))
            preview = (stage / "PREVIEW-ONLY.txt").read_text(encoding="utf-8")
            self.assertEqual(result["repository_visibility"], "public")
            self.assertEqual(build_info["artifact_access"], "public")
            self.assertFalse(build_info["artifact_upload_allowed"])
            self.assertEqual(verification["artifact_access"], "public")
            self.assertFalse(verification["artifact_upload_allowed"])
            self.assertIn("public repository", build_info["artifact_status"])
            self.assertIn("withheld", build_info["artifact_status"])
            self.assertIn("Existing artifacts from public-repository runs remain publicly accessible", preview)

    def test_private_repository_allows_repository_restricted_owner_test_upload(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bundle = make_bundle(root)
            stage = root / "private-stage"
            result = stage_package(bundle, stage, "source", "tested", "123", "1.13.0",
                                   repository_visibility="private")
            build_info = json.loads((stage / "build-info.json").read_text(encoding="utf-8"))
            verification = json.loads((stage / "vst3-verification.json").read_text(encoding="utf-8"))
            self.assertEqual(result["artifact_access"], "repository-restricted")
            self.assertTrue(result["artifact_upload_allowed"])
            self.assertTrue(build_info["artifact_upload_allowed"])
            self.assertTrue(verification["artifact_upload_allowed"])
            self.assertIn("private repository", build_info["artifact_status"])

    def test_internal_repository_does_not_count_as_private_owner_test_delivery(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bundle = make_bundle(root)
            stage = root / "internal-stage"
            result = stage_package(bundle, stage, "source", "tested", "123", "1.13.0",
                                   repository_visibility="internal")
            self.assertEqual(result["artifact_access"], "organization-internal")
            self.assertFalse(result["artifact_upload_allowed"])

    def test_unsupported_repository_visibility_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bundle = make_bundle(root)
            with self.assertRaisesRegex(PackageError, "Unsupported repository visibility"):
                stage_package(bundle, root / "bad-stage", "source", "tested", "123",
                              "1.13.0", repository_visibility="shared")


if __name__ == "__main__":
    unittest.main(verbosity=2)
