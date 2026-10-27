#!/usr/bin/env python3
"""Read-only source/profile identity check; no matches or network requests."""
import hashlib
import json
from pathlib import Path

import yaml


def main():
    release = Path(__file__).resolve().parents[1] / "stage2"
    manifest = json.loads((release / "SOURCE_MANIFEST.json").read_text())
    actual = {
        str(p.relative_to(release)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted((release / "src").rglob("*")) if p.is_file()
    }
    if actual != manifest["files"]:
        raise SystemExit("Stage-two source differs from the frozen manifest")
    profile = release / manifest["profile"]["path"]
    if hashlib.sha256(profile.read_bytes()).hexdigest() != manifest["profile"]["sha256"]:
        raise SystemExit("Stage-two profile differs from the frozen manifest")
    params = yaml.safe_load(profile.read_text())["/**"]["ros__parameters"]
    if params != manifest["profile"]["parameters"]:
        raise SystemExit("Stage-two parsed parameters differ from the manifest")
    print(json.dumps({"source_files": len(actual), "profile_parameters": len(params),
                      "source_commit": manifest["source_commit"], "valid": True}))


if __name__ == "__main__":
    main()
