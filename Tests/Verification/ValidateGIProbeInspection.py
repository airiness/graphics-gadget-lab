"""Independent GPU compact extraction and same-frame atlas/unit checks."""
import argparse
import json
from pathlib import Path

import numpy as np


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(root):
    results = []
    for backend in json.loads((root / "backends.json").read_text(encoding="utf-8-sig")):
        for case in ("plane", "excluded", "fixed", "sunroom"):
            directory = root / backend / case
            resource = json.loads(next(directory.glob("*.resource-export.json")).read_text(encoding="utf-8"))
            frame = resource["frame"]
            result = frame["giProbeInspection"]
            target = next(t for t in resource["targets"] if t["address"]["resource"] == "GI.Field.ProbeInspection")
            raw = np.load(directory / target["array"], allow_pickle=False).reshape(-1)
            width = result["irradianceTileWidth"]
            require(raw.dtype == np.dtype("<u4") and raw.nbytes == (1 + width * width + 256) * 16 <= 20496,
                    "Native extraction is not the declared bounded uint32 record array")
            require(raw[0] == 0x47504931 and raw[1] == result["classification"] and raw[3] == width - 2,
                    "GPU extraction header and CPU result disagree")
            values = raw[4:].view(np.float32).reshape(-1, 4)
            require(np.array_equal(values[:width * width], np.asarray(result["irradiance"], np.float32)) and
                    np.array_equal(values[width * width:], np.asarray(result["distance"], np.float32)),
                    "CPU publication changed the fence-completed GPU payload")
            require(result["validity"] in ("Valid", "Inactive"), "Unexpected invalid inspection evidence")
            if case == "plane":
                require(result["validity"] == "Valid", "Above-plane probe must be active")
                x, y, z = result["request"]["coordinate"]
                for name, side, measured in (("Irradiance", width, values[:width * width]),
                                             ("Distance", 16, values[width * width:])):
                    atlas_target = next(t for t in resource["targets"] if t["address"]["resource"] == "GI.Field." + name)
                    atlas = np.load(directory / atlas_target["array"], allow_pickle=False)
                    tile = atlas[y, z * side:(z + 1) * side, x * side:(x + 1) * side].reshape(side * side, -1)
                    channels = 3 if name == "Irradiance" else 2
                    expected = tile[:, :channels].astype(np.float64) * (np.pi if name == "Irradiance" else 1)
                    require(np.allclose(measured[:, :channels], expected, rtol=2e-6, atol=2e-6),
                            "Compact tile orientation or scene-referred irradiance decoding differs from same-frame atlas")
                readiness_target = next(t for t in resource["targets"] if t["address"]["resource"] == "GI.Field.Readiness")
                readiness = np.load(directory / readiness_target["array"], allow_pickle=False)["record"]
                updates = np.ascontiguousarray(readiness[2::2, 3]).view(np.uint32)
                require(np.all(updates == result["observation"]["readinessUpdate"]),
                        "GPU and CPU observation update identities disagree")
            elif case == "excluded":
                require(result["validity"] == "Inactive", "Excluded furnace probe must retain inactive classification")
            elif case == "fixed":
                require(not frame["readiness"]["giField"]["enabled"] and
                        frame["readiness"]["giField"]["summary"]["completedMeasurement"] is None,
                        "Inspection must work without inventing disabled readiness measurements")
            elif case == "sunroom":
                require(width == 32 and raw.nbytes == 20496 and result["validity"] == "Valid",
                        "Largest supported angular profile lost its active observation or bound")
            results.append(dict(backend=backend, case=case, bytes=raw.nbytes, validity=result["validity"]))
    return dict(ok=True, cases=results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    result = validate(args.root)
    (args.root / "validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("Probe inspection GPU extraction/identity/unit checks passed")
