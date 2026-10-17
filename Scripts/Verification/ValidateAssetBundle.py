"""Validate an installed, self-contained glTF bundle without authoring inputs."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tempfile


def revision(document):
    text = "gglab.asset-bundle.v1\n" + document["id"] + "\n" + document["entryPoint"] + "\n"
    text += "".join(item["path"] + "\0" + item["sha256"] + "\n" for item in document["files"])
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def validate(manifest):
    manifest = Path(manifest).resolve()
    root = manifest.parent
    document = json.loads(manifest.read_text(encoding="utf-8"))
    if document.get("schemaVersion") != 1 or not re.fullmatch(r"gglab\.[a-z0-9.-]+", document.get("id", "")):
        raise ValueError("Unsupported asset bundle schema or id")
    records = document["files"]
    names = [item["path"] for item in records]
    if not names or names != sorted(set(names)) or document["entryPoint"] not in names:
        raise ValueError("Bundle files must be unique, sorted and contain the entry point")
    for item in records:
        name = item["path"]
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name or str(path) != name:
            raise ValueError("Invalid bundle path: " + name)
        target = (root / name).resolve()
        if not target.is_relative_to(root) or not target.is_file():
            raise ValueError("Missing or escaping bundle file: " + name)
        if not re.fullmatch(r"[a-f0-9]{64}", item["sha256"]) or hashlib.sha256(target.read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError("SHA-256 mismatch: " + name)
    if revision(document) != document["revision"]:
        raise ValueError("Asset bundle revision does not match its file identities")
    gltf = json.loads((root / document["entryPoint"]).read_text(encoding="utf-8"))
    dependencies = [item.get("uri") for item in gltf.get("buffers", []) + gltf.get("images", [])]
    if any(not isinstance(uri, str) for uri in dependencies):
        raise ValueError("The installed bundle must use external buffers and images")
    expected = {document["entryPoint"], *dependencies}
    if set(names) != expected:
        raise ValueError("Manifest must describe the complete glTF dependency closure")
    return document


def self_test():
    with tempfile.TemporaryDirectory(prefix="gglab-asset-bundle-") as directory:
        root = Path(directory)
        (root / "scene.gltf").write_text(json.dumps({"asset": {"version": "2.0"}, "buffers": [{"uri": "scene.bin"}]}))
        (root / "scene.bin").write_bytes(b"geometry")
        data = {"schemaVersion": 1, "id": "gglab.test.bundle", "entryPoint": "scene.gltf", "files": [
            {"path": name, "sha256": hashlib.sha256((root / name).read_bytes()).hexdigest()}
            for name in ["scene.bin", "scene.gltf"]]}
        data["revision"] = revision(data)
        manifest = root / "bundle.json"
        manifest.write_text(json.dumps(data))
        validate(manifest)
        failures = 0
        for change in ["file", "revision", "escape", "closure", "duplicate"]:
            candidate = json.loads(json.dumps(data))
            if change == "file":
                (root / "scene.bin").write_bytes(b"changed")
            elif change == "revision":
                candidate["revision"] = "0" * 64
            elif change == "escape":
                candidate["files"][0]["path"] = "../scene.bin"
            elif change == "closure":
                candidate["files"] = candidate["files"][1:]
                candidate["revision"] = revision(candidate)
            else:
                candidate["files"].append(candidate["files"][0])
            manifest.write_text(json.dumps(candidate))
            try:
                validate(manifest)
            except (ValueError, KeyError):
                failures += 1
            (root / "scene.bin").write_bytes(b"geometry")
        if failures != 5:
            raise AssertionError("An invalid asset bundle was accepted")
    return {"ok": True, "rejectedFixtures": failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).resolve().parents[2] / "Assets/Models/GGLabCoastalRetreat/bundle.json")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        result = self_test() if args.self_test else {"ok": True, "bundle": validate(args.manifest)}
    except (ValueError, KeyError, OSError, TypeError) as error:
        result = {"ok": False, "error": str(error)}
    print(json.dumps(result, indent=2))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
