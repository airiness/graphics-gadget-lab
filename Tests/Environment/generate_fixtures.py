"""Regenerate synthetic producer-owned strict-reader vectors."""
import copy
import json
from pathlib import Path
import tempfile
import test_environment as t

directory = Path(__file__).parent / "fixtures"
directory.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory() as temp:
    m = t.fixture(Path(temp))
variants = [("valid", m, None)]


def add(name, error, edit):
    value = copy.deepcopy(m)
    edit(value)
    variants.append((name, value, error))


add("unsupported-version", "unsupported-version", lambda v: v.update(manifestVersion=2))
add("escaped-path", "invalid-path", lambda v: v["members"][0].update(path="payload/../escape"))
add("wrong-identity", "identity-mismatch", lambda v: v.update(environmentId="sha256:" + "0" * 64))
add("wrong-role", "invalid-role", lambda v: v["roles"].update(tool="payload/unknown.exe"))
add("duplicate-member", "path-conflict", lambda v: v["members"].insert(1, v["members"][0]))
add("missing-tool", "missing-member", lambda v: v.update(members=[x for x in v["members"] if x["path"] != t.p.ROLES["tool"]]))
cases = []
for name, value, error in variants:
    (directory / (name + ".json")).write_bytes(t.p.canonical(value) + b"\n")
    cases.append({"file": name + ".json", "accept": error is None,
                  **({"error": error} if error else {"environmentId": value["environmentId"]})})
(directory / "index.json").write_text(json.dumps({"fixtureVersion": 1,
    "purpose": "Synthetic strict-reader vectors; no native compatibility claim", "cases": cases}, indent=2) + "\n")
