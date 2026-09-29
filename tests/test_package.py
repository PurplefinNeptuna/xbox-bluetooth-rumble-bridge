from pathlib import Path
import hashlib
import struct
import subprocess
import tempfile
import unittest
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
VERSION = next(
    line.split("=", 1)[1].strip()
    for line in (ROOT / "module/module.prop").read_text().splitlines()
    if line.startswith("version=")
)
PACKAGE = ROOT / "dist" / f"xbox-bluetooth-rumble-bridge-{VERSION}.zip"
FILES = {"module.prop", "service.sh", "sepolicy.rule", "customize.sh", "xbox_bridge"}


class PackageTest(unittest.TestCase):
    def test_package_files_and_permissions(self):
        with ZipFile(PACKAGE) as archive:
            self.assertEqual(set(archive.namelist()), FILES)
            for name in FILES:
                expected = 0o100755 if name in {"service.sh", "xbox_bridge"} else 0o100644
                self.assertEqual(archive.getinfo(name).external_attr >> 16, expected)
                source = ROOT / ("dist" if name == "xbox_bridge" else "module") / name
                self.assertEqual(archive.read(name), source.read_bytes())

    def test_arm64_elf(self):
        binary = (ROOT / "dist/xbox_bridge").read_bytes()
        self.assertEqual(binary[:5], b"\x7fELF\x02")
        self.assertEqual(struct.unpack_from("<H", binary, 18)[0], 183)  # AArch64
        self.assertEqual(struct.unpack_from("<H", binary, 16)[0], 2)  # ET_EXEC

    def test_checksum(self):
        digest = hashlib.sha256(PACKAGE.read_bytes()).hexdigest()
        checksum = PACKAGE.with_name(PACKAGE.name + ".sha256").read_text()
        self.assertEqual(checksum, f"{digest}  {PACKAGE.name}\n")

    def test_effect_logic(self):
        output = ROOT / "dist/core_test"
        subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-o", str(output), str(ROOT / "tests/core_test.c")], check=True)
        subprocess.run([str(output)], check=True)

    def test_installer_policies(self):
        original = (ROOT / "module/sepolicy.rule").read_text()
        for manager, expected in (("KSU", original),
                                  ("APATCH", original.replace("allow ksu ", "allow apatch ")),
                                  ("MAGISK", "")):
            with self.subTest(manager=manager), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                (path / "sepolicy.rule").write_text(original)
                script = f'''
                    set_perm() {{ :; }}
                    abort() {{ echo "$1" >&2; exit 1; }}
                    id() {{ echo u:r:apatch:s0; }}
                    ARCH=arm64 MODPATH='{path}' {manager}=true
                    . '{ROOT / "module/customize.sh"}'
                '''
                subprocess.run(["sh", "-c", script], check=True)
                self.assertEqual((path / "sepolicy.rule").read_text(), expected)


if __name__ == "__main__":
    unittest.main()
