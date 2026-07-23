#!/usr/bin/env python3
import hashlib
import json
import os
from pathlib import Path

owner = os.environ["GITHUB_REPOSITORY_OWNER"]
repository = os.environ["GITHUB_REPOSITORY"].split("/", 1)[1]
tag = os.environ["GITHUB_REF_NAME"]
version = tag.removeprefix("v")
asset_dir = Path(os.environ.get("ASSET_DIR", "release-assets"))

entries = {}
for binary in sorted(asset_dir.glob("*.bin")):
    device_type = binary.stem
    data = binary.read_bytes()
    entries[device_type] = {
        "version": version,
        "target": "esp32-c3",
        "minimum_hardware_revision": 1,
        "url": f"https://github.com/{owner}/{repository}/releases/download/{tag}/{binary.name}",
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
    }

manifest = {
    "schema_version": 1,
    "release": version,
    "firmware": entries,
}
(asset_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
