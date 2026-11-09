"""Check the frozen GI inputs and recompute Coastal bounds from actual vertices."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import itertools
import json
import math
from pathlib import Path
import struct

import ValidateAssetBundle as assets


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INPUTS = ROOT / "Tests/Verification/GlobalIllumination/Inputs.json"
IDENTITY = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)


def local_path(root, name):
    path = (root / name).resolve()
    if Path(name).is_absolute() or not path.is_relative_to(root.resolve()):
        raise ValueError("Input path escapes its repository: " + name)
    return path


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def multiply(a, b):
    return tuple(sum(a[row + k * 4] * b[k + column * 4] for k in range(4))
                 for column in range(4) for row in range(4))


def node_matrix(node):
    if "matrix" in node:
        return tuple(node["matrix"])
    x, y, z, w = node.get("rotation", (0, 0, 0, 1))
    sx, sy, sz = node.get("scale", (1, 1, 1))
    tx, ty, tz = node.get("translation", (0, 0, 0))
    return ((1 - 2*y*y - 2*z*z)*sx, (2*x*y + 2*z*w)*sx, (2*x*z - 2*y*w)*sx, 0,
            (2*x*y - 2*z*w)*sy, (1 - 2*x*x - 2*z*z)*sy, (2*y*z + 2*x*w)*sy, 0,
            (2*x*z + 2*y*w)*sz, (2*y*z - 2*x*w)*sz, (1 - 2*x*x - 2*y*y)*sz, 0,
            tx, ty, tz, 1)


def transform(matrix, position):
    p = (*position, 1)
    return tuple(sum(matrix[row + k * 4] * p[k] for k in range(4)) for row in range(3))


def positions(gltf, buffers, index):
    accessor = gltf["accessors"][index]
    if (accessor["componentType"] != 5126 or accessor["type"] != "VEC3" or
            "sparse" in accessor or accessor.get("normalized", False)):
        raise ValueError("The locked bundle requires dense FLOAT VEC3 positions")
    view = gltf["bufferViews"][accessor["bufferView"]]
    stride = view.get("byteStride", 12)
    offset = accessor.get("byteOffset", 0)
    size = (accessor["count"] - 1) * stride + 12
    if stride < 12 or accessor["count"] <= 0 or offset < 0 or offset + size > view["byteLength"]:
        raise ValueError("Position accessor exceeds its buffer view")
    start = view.get("byteOffset", 0) + offset
    buffer = buffers[view["buffer"]]
    if start + size > len(buffer):
        raise ValueError("Position accessor exceeds its buffer")
    result = [struct.unpack_from("<3f", buffer, start + i * stride)
              for i in range(accessor["count"])]
    if not all(math.isfinite(v) for p in result for v in p):
        raise ValueError("Non-finite source position")
    return result


def coastal_geometry(path, excluded_names):
    gltf = json.loads(path.read_text(encoding="utf-8"))
    buffers = [local_path(path.parent, b["uri"]).read_bytes() for b in gltf["buffers"]]
    cache = {}
    core_min, core_max = [math.inf]*3, [-math.inf]*3
    counts, excluded, found = Counter(), {}, set()

    def visit(index, parent, ancestry):
        if index in ancestry:
            raise ValueError("Cyclic glTF node hierarchy")
        node = gltf["nodes"][index]
        world = multiply(parent, node_matrix(node))
        name = node.get("name", "")
        if "mesh" in node:
            low, high = [math.inf]*3, [-math.inf]*3
            for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
                if primitive.get("mode", 4) != 4:
                    raise ValueError("GI geometry inventory requires triangles")
                material = gltf["materials"][primitive["material"]]
                counts[material.get("alphaMode", "OPAQUE")] += 1
                accessor = primitive["attributes"]["POSITION"]
                if accessor not in cache:
                    cache[accessor] = positions(gltf, buffers, accessor)
                for position in cache[accessor]:
                    x, y, z = transform(world, position)
                    # Assimp ConvertToLeftHanded reflects glTF Z. Demo root is identity.
                    for axis, value in enumerate((x, y, -z)):
                        low[axis], high[axis] = min(low[axis], value), max(high[axis], value)
            if name in excluded_names:
                if name in found:
                    raise ValueError("Excluded node name is not unique: " + name)
                found.add(name)
                excluded[name] = {"min": low, "max": high}
            else:
                for axis in range(3):
                    core_min[axis] = min(core_min[axis], low[axis])
                    core_max[axis] = max(core_max[axis], high[axis])
        for child in node.get("children", []):
            visit(child, world, ancestry | {index})

    for root in gltf["scenes"][gltf.get("scene", 0)]["nodes"]:
        visit(root, IDENTITY, set())
    if found != set(excluded_names):
        raise ValueError("An excluded spatial node is missing")
    return {"coreMinMeters": core_min, "coreMaxMeters": core_max,
            "placedPrimitivesByAlphaMode": dict(counts), "excludedBoundsMeters": excluded}


def validate(inputs_path):
    inputs = json.loads(inputs_path.read_text(encoding="utf-8"))
    if inputs["schemaVersion"] != 1:
        raise ValueError("Unsupported GI input lock schema")
    if type(inputs["inputVersion"]) is not int or inputs["inputVersion"] < 1:
        raise ValueError("GI input content requires a positive version")
    bundle = assets.validate(local_path(ROOT, inputs["assetBundle"]["manifest"]))
    if any(bundle[key] != inputs["assetBundle"][key] for key in ("id", "revision")):
        raise ValueError("Installed asset bundle differs from the GI input lock")
    for record in inputs["sourceFiles"]:
        if file_hash(local_path(ROOT, record["path"])) != record["sha256"]:
            raise ValueError("GI source input changed: " + record["path"])
    geometry = coastal_geometry(local_path(ROOT, inputs["coastal"]["gltf"]),
                                inputs["coastal"]["volume"]["excludedFromFit"])
    for key in ("coreMinMeters", "coreMaxMeters"):
        if any(abs(a - b) > 1e-4 for a, b in zip(geometry[key], inputs["coastal"][key])):
            raise ValueError("Coastal vertex bounds changed: " + key)
    if geometry["placedPrimitivesByAlphaMode"] != inputs["coastal"]["placedPrimitivesByAlphaMode"]:
        raise ValueError("Coastal geometry participation changed")
    volume = inputs["coastal"]["volume"]
    for axis in range(3):
        start, end = volume["probeMinMeters"][axis], volume["probeMaxMeters"][axis]
        fitted_min = geometry["coreMinMeters"][axis]
        if axis == 1:
            fitted_min = max(fitted_min, volume["fitFloorMeters"])
        if (volume["spacingMeters"][axis] <= 0 or volume["probeCounts"][axis] < 2 or
                abs(end - start - volume["spacingMeters"][axis] *
                    (volume["probeCounts"][axis] - 1)) > 1e-6 or
                start > fitted_min or end < geometry["coreMaxMeters"][axis]):
            raise ValueError("Coastal grid does not cover its locked core")
    if volume["probeMaxMeters"][1] - geometry["coreMaxMeters"][1] < volume["spacingMeters"][1]:
        raise ValueError("Coastal roof lies inside the top coverage blend")
    field = inputs["coastal"]["field"]
    if field["fullUpdateRayCapacity"] != math.prod(volume["probeCounts"]) * field["raysPerProbeUpdate"]:
        raise ValueError("GI ray-buffer capacity does not cover a full update")
    observations = {v["id"]: v for v in inputs["coastal"]["observations"]}
    seen = set()
    media_root = local_path(ROOT, inputs["baseline"]["directory"])
    if file_hash(media_root / "baseline.json") != inputs["baseline"]["manifestSha256"]:
        raise ValueError("GI baseline manifest identity changed")
    baseline = json.loads((media_root / "baseline.json").read_text(encoding="utf-8"))
    for record in baseline["files"]:
        if file_hash(local_path(media_root, record["path"])) != record["sha256"]:
            raise ValueError("Baseline artifact changed: " + record["path"])
    for capture in baseline["captures"]:
        metadata = json.loads(local_path(media_root, capture["metadata"]).read_text(encoding="utf-8"))
        manifest = json.loads(local_path(media_root, capture["manifest"]).read_text(encoding="utf-8"))
        build = manifest["environment"]["build"]
        if (manifest["consistency"] != "same-frame" or
                manifest["provenance"]["frame"] != metadata["frame"] or
                build["revision"] != inputs["codeBaseline"] or build["dirty"] is not False or
                build["configuration"] != "Develop" or build["pixIntegration"] is not False or
                manifest["environment"]["validation"]["effective"] != "standard"):
            raise ValueError("GI baseline build, validation or frame provenance differs")
        view = observations[capture["observation"]]
        if metadata["camera"] != view["camera"] or metadata["backend"] != capture["rhi"]:
            raise ValueError("Captured GI camera/backend differs from the input lock")
        if (metadata["content"]["assetBundle"] != {k: bundle[k] for k in ("id", "revision")} or
                not metadata["readiness"]["ready"] or metadata["timing"]["settledFrames"] != 64 or
                metadata["sceneInputs"] != inputs["coastal"]["sceneInputs"] or
                metadata["temporal"]["settings"] != inputs["coastal"]["temporalSettings"] or
                metadata["temporal"]["gtao"] != inputs["coastal"]["gtao"] or
                metadata["temporal"]["renderExtent"] != inputs["coastal"]["comparisonExtent"]):
            raise ValueError("Captured GI frame inputs differ from the input lock")
        image = local_path(media_root, capture["image"])
        if image.name != metadata["image"]["file"]:
            raise ValueError("Capture image and metadata disagree")
        key = (capture["rhi"], capture["observation"])
        if key in seen:
            raise ValueError("Duplicate baseline observation")
        seen.add(key)
    if seen != set(itertools.product(("dx12", "vulkan"), observations)):
        raise ValueError("The GI baseline must contain all four views on both RHIs")
    repeats = {(v["rhi"], v["observation"]) for v in baseline["repeatChecks"]
               if v["pngBytesIdentical"] and v["imageSha256"] == v["repeatImageSha256"]}
    if repeats != seen or any(code != 0 for code in baseline["sessionExitCodes"].values()):
        raise ValueError("GI baseline lacks identical repeats or successful shutdown")
    for view in observations.values():
        for mask in view["masks"]:
            x0, y0, x1, y1 = mask["rectPixels"]
            width, height = inputs["coastal"]["comparisonExtent"]
            if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
                raise ValueError("GI observation mask is outside the locked extent")
    return {"ok": True, "bundleRevision": bundle["revision"], "geometry": geometry,
            "verifiedBaselineCaptures": len(seen), "inputVersion": inputs["inputVersion"],
            "inputSchemaVersion": inputs["schemaVersion"]}


def self_test():
    # Independent golden: parent translation + a rotated, non-uniformly scaled child.
    parent = node_matrix({"translation": [10, 20, 30]})
    child = node_matrix({"rotation": [0, 0, math.sqrt(0.5), math.sqrt(0.5)],
                         "scale": [2, 3, 4], "translation": [1, 2, 3]})
    actual = transform(multiply(parent, child), (1, 0, 1))
    if any(abs(a - b) > 1e-9 for a, b in zip(actual, (11, 24, 37))):
        raise AssertionError("Nested TRS transform failed")
    # Actual rotated vertices occupy a smaller AABB than rotated accessor corners.
    turn = node_matrix({"rotation": [0, 0, math.sin(math.pi/8), math.cos(math.pi/8)]})
    triangle = [transform(turn, p) for p in ((0, 0, 0), (1, 0, 0), (0, 1, 0))]
    if abs(max(p[1] for p in triangle) - math.sqrt(0.5)) > 1e-9:
        raise AssertionError("Rotated vertex extent failed")
    fixture = {"accessors": [{"componentType": 5126, "type": "VEC3", "bufferView": 0,
                              "count": 2}], "bufferViews": [{"buffer": 0, "byteLength": 12}]}
    try:
        positions(fixture, [bytes(24)], 0)
    except ValueError:
        return {"ok": True, "checks": ["nested TRS", "rotated vertices", "accessor range"]}
    raise AssertionError("An accessor crossing its buffer view was accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, default=DEFAULT_INPUTS)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--geometry", action="store_true", help="Report vertex bounds before freezing inputs")
    args = parser.parse_args()
    try:
        if args.self_test:
            result = self_test()
        elif args.geometry:
            result = coastal_geometry(ROOT / "Assets/Models/GGLabCoastalRetreat/GGLabCoastalRetreat.gltf",
                                      ["Retreat_SeaSurface", "Retreat_DistantIsland_00",
                                       "Retreat_DistantIsland_01", "Retreat_DistantIsland_02"])
        else:
            result = validate(args.inputs)
    except (ValueError, KeyError, OSError, TypeError, struct.error) as error:
        result = {"ok": False, "error": str(error)}
    print(json.dumps(result, indent=2))
    return 0 if result.get("ok", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
