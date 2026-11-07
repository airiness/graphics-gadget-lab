"""Validate Native raster receiver records and committed-triangle GI material exports."""
import argparse
import json
from pathlib import Path

import numpy as np

VIEWS = ("Retreat_SeaPassage", "Retreat_Courtyard", "GI_InteriorExterior", "Retreat_Lounge")
MIN_AGREEMENT = 0.995
ALBEDO_TOLERANCE = 0.003
NORMAL_COSINE = 0.999


def check(condition, message):
    if not condition:
        raise ValueError(message)


def unit_normals(value, covered, name):
    length = np.linalg.norm(value[..., :3][covered], axis=-1)
    check((np.abs(length - 1) <= 0.002).all(), f"{name} contains non-unit or missing normals")


def measure(values, shading):
    face, vertex, normal = (values[f"GI.Receiver.{name}"][0] for name in ("FaceNormal", "VertexNormal", "ShadingNormal"))
    depth = values["DisplayView.DepthBuffer"][0, ..., 0]
    covered = depth > 0
    check(covered.any(), "No opaque raster receivers")
    check(np.array_equal(face[..., 3] != 0, covered), "Receiver coverage differs from opaque depth")
    check(np.isin(face[..., 3], [0, 1]).all(), "Unexpected MASK or invalid receiver class in Coastal")
    for name, value in (("face", face), ("vertex", vertex), ("shading", normal)):
        unit_normals(value, covered, name)
    check((np.sum(face[..., :3][covered] * vertex[..., :3][covered], axis=-1) >= -0.0001).all(),
          "Face normals must face the oriented vertex-normal hemisphere")
    check(np.isin(vertex[..., 3][covered], [-1, 1]).all(), "Missing raster front/back-face state")
    check(((normal[..., 3][covered] >= 0) & (normal[..., 3][covered] <= 1)).all(), "Metallic is outside [0,1]")
    expected = values["GI.Receiver.HitAlbedo"][0]
    actual = values["RayScene.HitAlbedo"][0]
    hit_normal = values["RayScene.HitNormal"][0]
    identities = values["RayScene.HitIdentity"][0]
    state = actual[..., 3]
    check(np.isin(state, [-1, 0, 1, 2]).all(), "Invalid hit geometry/material/receiver state")
    check(np.array_equal(state != 0, covered), "Hit state differs from opaque depth coverage")
    check(np.array_equal(expected[..., 3] == 1, covered), "Raster GI albedo coverage is incomplete")
    matched = (state == 1) | (state == 2)
    check(matched.sum() / covered.sum() >= MIN_AGREEMENT, "Too few committed receiver triangles")
    unit_normals(hit_normal, matched, "hit vertex")
    check((identities[matched] >= 0).all() and (identities[matched] == np.floor(identities[matched])).all(),
          "Hit identities are not exact nonnegative indices")
    check((identities[..., 0][matched] < 506).all() and (identities[..., 2][matched] < 28).all() and
          (identities[..., 3][matched] < 59).all(), "Hit identity is outside the locked Coastal snapshot")
    check(np.isin(hit_normal[..., 3][matched], [-1, 1]).all(), "Missing native hit facing state")
    check((actual[..., :3][matched] >= 0).all() and (expected[..., :3][covered] >= 0).all(), "Negative diffuse albedo")
    albedo_error = np.max(np.abs(actual[..., :3] - expected[..., :3]), axis=-1)
    normal_cosine = np.sum(hit_normal[..., :3] * vertex[..., :3], axis=-1)
    good = matched & (albedo_error <= ALBEDO_TOLERANCE) & (normal_cosine >= NORMAL_COSINE)
    rate = good.sum() / covered.sum()
    check(rate >= MIN_AGREEMENT, f"Material/normal agreement {rate:.6%} is below 99.5%")
    shade_covered = shading["GI.Receiver.FaceNormal"][0, ..., 3] != 0
    check(np.array_equal(shade_covered, covered), "Receiver groups have different frozen coverage")
    diffuse = shading["GI.Receiver.DiffuseWeight"][0][covered]
    check(((diffuse >= 0) & (diffuse <= 1)).all(), "Diffuse weight or coat factor is outside [0,1]")
    for name in ("ReflectionRoughness", "ClearcoatNormalRoughness"):
        value = shading[f"GI.Receiver.{name}"][0]
        unit_normals(value, covered, name)
        roughness = value[..., 3][covered]
        check(((roughness > 0) & (roughness <= 1)).all(), "Effective roughness is outside (0,1]")
    base = shading["GI.Receiver.BaseColor"][0][covered]
    check((base >= 0).all() and (base[:, 3] == np.floor(base[:, 3])).all(), "Invalid base color or raster material index")
    return dict(opaquePixels=int(covered.sum()), matchedPixels=int(matched.sum()),
                materialAgreement=float(rate), unmatchedPixels=int((state == -1).sum()),
                singleSidedBackfaces=int((state == 2).sum()),
                albedoAbsoluteErrorP99=float(np.percentile(albedo_error[matched], 99)),
                normalCosineP01=float(np.percentile(normal_cosine[matched], 1)),
                matchedMaterialCount=int(np.unique(identities[..., 2][matched]).size))


def load_group(directory):
    paths = list(directory.glob("*.resource-export.json"))
    check(len(paths) == 1, f"Expected one resource export in {directory}")
    path = paths[0]
    record = json.loads(path.read_text(encoding="utf-8-sig"))
    frame = record["frame"]
    check(frame["temporal"]["jitterPixels"] == [0, 0], "Receiver samples must be unjittered")
    check(frame["temporal"]["status"] == "disabled", "Receiver capture requires TAA off")
    check(frame["temporal"]["renderExtent"] == frame["temporal"]["displayExtent"], "Receiver domain is not Native")
    values = {}
    for target in record["targets"]:
        name = target["address"]["resource"]
        value = np.load(path.parent / target["array"], allow_pickle=False)
        check(value.dtype == np.dtype("<f4") and value.ndim == 4 and value.shape[0] == 1,
              f"Unexpected float export layout: {name}")
        check(value.shape[-1] == (1 if name == "DisplayView.DepthBuffer" else 4), f"Wrong channel count: {name}")
        check(np.isfinite(value).all(), f"Non-finite receiver data: {name}")
        if name != "DisplayView.DepthBuffer":
            check(target["semantics"]["role"] == "gi.surface-data", f"Missing channel semantics: {name}")
        values[name] = value
    check(len({value.shape[:-1] for value in values.values()}) == 1, "Resource extents differ")
    return values, frame


def matching_inputs(left, right):
    for field in ("camera", "content", "sceneInputs"):
        check(left[field] == right[field], f"Frozen capture inputs differ: {field}")
    for field in ("revision", "contentId", "available", "values", "lighting"):
        check(left["parameters"][field] == right["parameters"][field], f"Capture parameters differ: {field}")
    for field in ("renderExtent", "displayExtent", "jitterPixels", "renderScale"):
        check(left["temporal"][field] == right["temporal"][field], f"Capture domain differs: {field}")


def load_view(root, view):
    values, frame = load_group(root / view / "receivers")
    shading, shading_frame = load_group(root / view / "shading")
    matching_inputs(frame, shading_frame)
    check(frame["camera"]["referenceView"] == view, "Unexpected reference view")
    return values, frame, measure(values, shading)


def compare_roots(left, right):
    result = {}
    for view in VIEWS:
        a, frame_a, _ = load_view(left, view)
        b, frame_b, _ = load_view(right, view)
        matching_inputs(frame_a, frame_b)
        covered = (a["GI.Receiver.FaceNormal"][..., 3] != 0) | (b["GI.Receiver.FaceNormal"][..., 3] != 0)
        state_a, state_b = a["RayScene.HitAlbedo"][..., 3], b["RayScene.HitAlbedo"][..., 3]
        good = np.isin(state_a, [1, 2]) & np.isin(state_b, [1, 2])
        good &= np.max(np.abs(a["RayScene.HitAlbedo"][..., :3] - b["RayScene.HitAlbedo"][..., :3]), axis=-1) <= ALBEDO_TOLERANCE
        good &= np.sum(a["RayScene.HitNormal"][..., :3] * b["RayScene.HitNormal"][..., :3], axis=-1) >= NORMAL_COSINE
        rate = np.count_nonzero(good & covered) / np.count_nonzero(covered)
        check(rate >= MIN_AGREEMENT, f"Cross-RHI material agreement {rate:.6%} is below 99.5%")
        result[view] = dict(agreement=float(rate), receiverUnionPixels=int(covered.sum()))
    return result


def self_test():
    values = {}
    shape = (1, 16, 16, 4)
    for name in ("FaceNormal", "VertexNormal", "ShadingNormal", "HitAlbedo"):
        values[f"GI.Receiver.{name}"] = np.zeros(shape, dtype="<f4")
    for name in ("HitAlbedo", "HitNormal", "HitIdentity"):
        values[f"RayScene.{name}"] = np.zeros(shape, dtype="<f4")
    values["DisplayView.DepthBuffer"] = np.full(shape[:-1] + (1,), 0.5, dtype="<f4")
    for name in ("GI.Receiver.FaceNormal", "GI.Receiver.VertexNormal", "GI.Receiver.ShadingNormal", "RayScene.HitNormal"):
        values[name][..., 1] = 1
    for name in ("GI.Receiver.FaceNormal", "GI.Receiver.VertexNormal", "GI.Receiver.HitAlbedo", "RayScene.HitAlbedo", "RayScene.HitNormal"):
        values[name][..., 3] = 1
    values["GI.Receiver.HitAlbedo"][..., :3] = 0.25
    values["RayScene.HitAlbedo"][..., :3] = 0.25
    shading = {"GI.Receiver.FaceNormal": values["GI.Receiver.FaceNormal"].copy()}
    for name in ("DiffuseWeight", "BaseColor", "ReflectionRoughness", "ClearcoatNormalRoughness"):
        shading[f"GI.Receiver.{name}"] = np.full(shape, 0.5, dtype="<f4")
    for name in ("ReflectionRoughness", "ClearcoatNormalRoughness"):
        shading[f"GI.Receiver.{name}"][..., :3] = [0, 1, 0]
    shading["GI.Receiver.BaseColor"][..., 3] = 0
    measure(values, shading)
    adversaries = (("RayScene.HitAlbedo", 0, 0.8), ("RayScene.HitAlbedo", 3, -1),
                   ("RayScene.HitAlbedo", 3, -2), ("RayScene.HitNormal", 1, -1),
                   ("GI.Receiver.FaceNormal", 3, 0), ("RayScene.HitIdentity", 2, 28),
                   ("RayScene.HitIdentity", 0, 0.5), ("GI.Receiver.ShadingNormal", 1, 0))
    for name, channel, replacement in adversaries:
        bad = {key: value.copy() for key, value in values.items()}
        bad[name][..., channel] = replacement
        try:
            measure(bad, shading)
        except ValueError:
            continue
        raise AssertionError(f"Accepted corrupt receiver data: {name}")
    return dict(checks=1 + len(adversaries))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path)
    parser.add_argument("--view", choices=VIEWS)
    parser.add_argument("--left", type=Path)
    parser.add_argument("--right", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = {"ok": True}
    try:
        check(args.self_test or args.root or (args.left and args.right), "Choose self-test, root or cross-RHI roots")
        if args.self_test:
            result["selfTest"] = self_test()
        if args.root:
            result["views"] = {view: load_view(args.root, view)[2] for view in ((args.view,) if args.view else VIEWS)}
        if args.left and args.right:
            result["crossRhi"] = compare_roots(args.left, args.right)
    except Exception as error:
        result = {"ok": False, "error": f"{type(error).__name__}: {error}"}
    encoded = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(encoded + "\n", encoding="utf-8")
    print(encoded)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
