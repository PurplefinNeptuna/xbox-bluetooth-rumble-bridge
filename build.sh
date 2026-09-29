#!/bin/sh
set -eu
cd "$(dirname "$0")"

mkdir -p dist
clang --target=aarch64-linux-android -fuse-ld=lld -nostdlib -static \
    -ffreestanding -fno-builtin -fno-stack-protector -Wl,-e,_start \
    -O2 -Wall -Wextra -Werror -o dist/ttmax_bridge src/ttmax_bridge.c

python3 - <<'PY'
from pathlib import Path
import hashlib
import re
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

module = Path("module")
properties = dict(
    line.split("=", 1)
    for line in (module / "module.prop").read_text().splitlines()
    if line and not line.startswith("#")
)
version = properties["version"]
if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", version):
    raise SystemExit(f"Invalid module version: {version}")

output = Path("dist") / f"ttmax-rumble-bridge-{version}.zip"
files = {
    "module.prop": module / "module.prop",
    "service.sh": module / "service.sh",
    "sepolicy.rule": module / "sepolicy.rule",
    "customize.sh": module / "customize.sh",
    "ttmax_bridge": Path("dist/ttmax_bridge"),
}
with ZipFile(output, "w") as archive:
    for name, path in files.items():
        entry = ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        entry.create_system = 3
        entry.external_attr = (0o100755 if name in {"service.sh", "ttmax_bridge"} else 0o100644) << 16
        entry.compress_type = ZIP_DEFLATED
        archive.writestr(entry, path.read_bytes())
digest = hashlib.sha256(output.read_bytes()).hexdigest()
output.with_name(output.name + ".sha256").write_text(f"{digest}  {output.name}\n")
print(output)
PY
