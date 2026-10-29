"""Validate the initial stateless F1/F2 one-bounce reference, not DDGI or Final."""
import argparse
import json
from pathlib import Path
import struct
import zlib

import numpy as np

DEFAULTS = {"fixture": 0, "samples": 64, "seed": 61051, "albedo": 0, "ev100": 10}
ROOM_FACES = {
    "F2_Interior": (4, (2, 3)), "F2_Floor": (0, (4, 5)),
    "F2_Ceiling": (1, (6, 7)), "F2_West": (2, (10, 11)),
    "F2_East": (3, (8, 9)), "F2_Front": (5, (0, 1)), "F2_Roof": (1, (4, 5)),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def measurement_mask(case, values, camera):
    position = values["GI.Reference.Position"][..., :3]
    identity = values["GI.Reference.Identity"]
    covered = values["GI.Receiver.FaceNormal"][..., 3] == 1
    if case["fixture"] == 0:
        to_camera = np.asarray(camera["position"]) - position
        to_camera /= np.maximum(np.linalg.norm(to_camera, axis=-1)[..., None], 1e-12)
        normal = values["GI.Receiver.ShadingNormal"][..., :3]
        return covered & (identity[..., 0] == 0) & (np.sum(normal * to_camera, axis=-1) >= 0.5)
    if case["fixture"] == 1:
        return covered & (identity[..., 0] == 0) & np.isin(identity[..., 1], [0, 1]) & \
            (np.abs(position[..., 0]) <= 1) & (np.abs(position[..., 2]) <= 1)
    instance, primitives = ROOM_FACES[case["view"]]
    mask = covered & (identity[..., 0] == instance) & np.isin(identity[..., 1], primitives)
    x, y, z = (position[..., axis] for axis in range(3))
    if case["view"] in ("F2_Floor", "F2_Ceiling", "F2_Roof"):
        radius = 1 if case["view"] == "F2_Roof" else 1.5
        mask &= (np.abs(x) <= radius) & (np.abs(z) <= radius)
    elif case["view"] in ("F2_West", "F2_East"):
        mask &= (np.abs(z) <= 1.5) & (y >= 0.5) & (y <= 3.5)
    else:
        mask &= (np.abs(x) <= 1.5) & (y >= 0.5) & (y <= 3.5)
    return mask


def inspect(root, case):
    directory = root / case["directory"]
    records = list(directory.glob("*.resource-export.json"))
    require(len(records) == 1, f"Expected one export in {directory}")
    record = json.loads(records[0].read_text(encoding="utf-8-sig"))
    frame = record["frame"]
    require(frame["camera"]["referenceView"] == case["view"], "Wrong locked reference view")
    require(frame["content"]["labId"] == "gglab.lab.gi_reference", "Wrong Lab")
    require(frame["temporal"]["status"] == "disabled" and frame["temporal"]["jitterPixels"] == [0, 0] and
            frame["temporal"]["renderExtent"] == frame["temporal"]["displayExtent"], "Non-Native/temporal receivers")
    require(not frame["temporal"]["gtao"]["enabled"] and not frame["sceneInputs"]["postProcess"]["bloomEnabled"],
            "Fixture GTAO/bloom must be off")
    actual = DEFAULTS.copy()
    for value in frame["parameters"]["values"]:
        if value["id"].startswith("gi_reference."):
            actual[value["id"].split(".", 1)[1]] = value["value"]
    require(all(actual[key] == case[key] for key in DEFAULTS), "Frozen fixture/sample/seed/albedo/exposure differs")
    require(frame["sceneInputs"]["exposure"]["effectiveEV100"] == case["ev100"], "Exposure override differs")
    require(frame["sceneInputs"]["sun"] is None, "F1/F2 sun must be off")
    values = {}
    for target in record["targets"]:
        array = np.load(directory / target["array"], allow_pickle=False)
        require(array.dtype == np.dtype("<f4") and array.ndim == 4 and array.shape[0] == 1 and
                array.shape[-1] == 4 and np.isfinite(array).all(), "Invalid/non-finite reference export")
        values[target["address"]["resource"]] = array[0]
    e = values["GI.Reference.Irradiance"]
    receiver_class = values["GI.Receiver.FaceNormal"][..., 3]
    covered = receiver_class == 1
    require(np.isin(receiver_class, [0, 1]).all(), "Unexpected unsupported fixture receiver class")
    require(covered.any() and np.all(e[..., 3][covered] == 1), "Invalid/unmatched receivers cannot be dropped")
    require(np.all(e[..., 3][~covered] == 0), "Reference coverage differs from raster receivers")
    require(np.all(e[..., :3] >= 0), "Negative irradiance")
    diagnostics = values["GI.Reference.Diagnostics"]
    require(np.all(diagnostics >= 0) and np.all(diagnostics.sum(-1) <= 1.00001) and
            np.all(diagnostics[..., 3] == 0), "Invalid secondary sample or denominator")
    if case["fixture"] != 0:
        face_normal = values["GI.Receiver.FaceNormal"][..., :3]
        shading_normal = values["GI.Receiver.ShadingNormal"][..., :3]
        require(np.all(np.sum(face_normal * shading_normal, axis=-1)[covered] >= 0.999999),
                "Flat fixture inherits a normal-map or quantized fallback tilt")
    mask = measurement_mask(case, values, frame["camera"])
    require(mask.sum() >= 32, f"Empty or unusable mask: {case['view']}")
    lit = case["fixture"] != 2 or case["view"] == "F2_Roof"
    expected = np.pi if lit else 0.0
    mean = e[..., :3][mask].mean(axis=0, dtype=np.float64)
    error = float(np.max(np.abs(mean / np.pi - 1))) if lit else float(np.max(e[..., :3][mask]))
    require(error <= (0.01 if lit else 0.0), f"Irradiance gate failed ({case['view']}): {error}")
    if case["fixture"] == 2 and not lit:
        require(np.all(diagnostics[..., 0][mask] == 0), "Sealed interior rays incorrectly became sky misses")
        # A ray exactly on a touching-box seam can commit the adjacent box's backface
        # (e.g. front-wall top against ceiling bottom). It is still an occluded HIT,
        # never sky. Require every sample to hit; retain/report backfaces explicitly.
        require(np.all(diagnostics[..., 1:3].sum(-1)[mask] == 1), "Closed shell lost a blocked sample")
    albedo = np.array([0.5] * 3 if case["fixture"] == 2 else
                      ([0.2, 0.5, 0.8] if case["albedo"] else [1.0] * 3))
    require(np.max(np.abs(values["GI.Receiver.HitAlbedo"][mask, :3] - albedo)) <= 1e-6,
            "Fixture albedo does not match the frozen linear material")
    radiance = values["GI.Reference.LambertRadiance"][mask, :3]
    require(np.max(np.abs(radiance - e[mask, :3] * albedo / np.pi)) < 1e-5, "Receiver albedo applied incorrectly")
    require(np.max(np.abs(radiance.mean(0, dtype=np.float64) - albedo * expected / np.pi)) <= 0.01,
            "Lambert diagnostic is not albedo * L")
    graph = json.loads(next(directory.glob("*.graph.json")).read_text(encoding="utf-8-sig"))
    executed = {item["name"]: item for item in graph["compile"]["passes"] if not item["culled"]}
    require("GI.Reference.OneBounce" in executed and "GI.Receiver" in executed,
            "Reference/receiver graph work is missing")
    reference = executed["GI.Reference.OneBounce"]
    require(executed["GI.Receiver"]["executionOrder"] < reference["executionOrder"],
            "Reference must follow receiver production")
    require(not any(name.startswith("TAA.") for name in executed), "Temporal work is active")
    normal = values["GI.Receiver.ShadingNormal"][..., :3]
    image = np.clip((normal * 0.5 + 0.5) * 0.45, 0, 1)
    image[mask] = image[mask] * 0.35 + np.array([0.0, 0.6, 0.1])
    pixels = np.uint8(image * 255)
    height, width = pixels.shape[:2]
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + row.tobytes() for row in pixels)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    (directory / "measurement-mask.png").write_bytes(png)
    return {"maskPixels": int(mask.sum()), "meanIrradiance": mean.tolist(), "error": error,
            "validReceivers": int(covered.sum()), "expectedIrradiance": expected,
            "backfaceSamples": int(np.rint(diagnostics[..., 2][mask].sum() * case["samples"]))}, e, mask


def validate(root):
    cases = json.loads((root / "cases.json").read_text(encoding="utf-8-sig"))
    results = {}
    arrays = {}
    for case in cases:
        key = case["directory"]
        results[key], arrays[key], _ = inspect(root, case)
    for backend in sorted({case["directory"].split("/")[0] for case in cases}):
        for fixture in (0, 1):
            prefix = f"{backend}/f{fixture}"
            base = arrays[f"{prefix}-base"]
            for variant in ("white-ev0", "white-ev15", "tint-ev0", "tint-ev10", "tint-ev15"):
                require(np.array_equal(base, arrays[f"{prefix}-{variant}"]), "E depends on receiver albedo or exposure")
            if fixture == 0:
                require(not np.array_equal(base, arrays[f"{prefix}-independent"]),
                        "Independent reference seed did not change sphere sample outcomes")
    if any(case["directory"].startswith("vulkan/") for case in cases):
        for case in cases:
            if case["directory"].startswith("dx12/"):
                other = case["directory"].replace("dx12/", "vulkan/", 1)
                left, right = arrays[case["directory"]], arrays[other]
                require(np.array_equal(left[..., 3], right[..., 3]), "Cross-RHI receiver validity differs")
                require(np.max(np.abs(left[..., :3] - right[..., :3])) <= 0.05,
                        "Cross-RHI irradiance disagreement exceeds one 64-sample cosine contribution")
    (root / "numerical-results.json").write_text(json.dumps(results, indent=2) + "\n")
    return {"status": "passed", "observations": len(results), "scope": "F1/F2 stateless one-bounce, not DDGI or Final"}


def self_test():
    shape = (8, 8, 4)
    values = {name: np.zeros(shape) for name in ("GI.Reference.Position", "GI.Reference.Identity",
              "GI.Receiver.FaceNormal", "GI.Receiver.ShadingNormal")}
    values["GI.Receiver.FaceNormal"][..., 3] = 1
    require(measurement_mask({"fixture": 1}, values, {}).all(), "Plane mask positive test")
    values["GI.Reference.Identity"][..., 1] = 8
    require(not measurement_mask({"fixture": 1}, values, {}).any(), "Wrong primitive cannot enter mask")
    values["GI.Reference.Identity"][..., 1] = 4
    require(measurement_mask({"fixture": 2, "view": "F2_Floor"}, values, {}).all(), "Floor mask positive test")
    values["GI.Reference.Position"][..., 0] = 2
    require(not measurement_mask({"fixture": 2, "view": "F2_Floor"}, values, {}).any(), "World-space mask excludes edge")
    return {"status": "passed", "checks": 4}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    print(json.dumps(self_test() if args.self_test else validate(args.root)))
