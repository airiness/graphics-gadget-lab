"""Independent float64 GGX/geometry/material oracle; never replay shader RNG.

The integration variables are the analytic projected-NDF radial CDF and azimuth.
Gauss-Legendre radial quadrature resolves sharp kernels without uniform spherical
undersampling; an independent closed-form denominator checks normalization.
"""
import argparse
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import struct

import numpy as np
from numpy.polynomial.legendre import leggauss

PREFIX = "GI.SpecularVisibility."
OUTPUTS = [PREFIX + s for s in ("Visibility", "Base", "Coat", "Weights", "Position", "Diagnostics", "F0", "LayerEnergy")]
RECEIVERS = ["GI.Receiver." + s for s in ("FaceNormal", "VertexNormal", "ShadingNormal", "DiffuseWeight", "BaseColor", "ReflectionRoughness", "ClearcoatNormalRoughness", "HitAlbedo")]
COLORS = ((179, 128, 245, 255), (128, 179, 245, 255), (255, 128, 255, 255))


def require(value, message):
    if not value:
        raise ValueError(message)


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    for name, color in zip(("normal.dds", "coat-normal.dds", "anisotropy.dds"), COLORS):
        # Authored constant 2x2 DX10 RGBA8 UNORM, one mip, linear semantics.
        header = [124, 0x100f, 2, 2, 8, 0, 1] + [0] * 11 + [32, 4, 0x30315844, 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0]
        data = b"DDS " + struct.pack("<31I", *header) + struct.pack("<5I", 28, 3, 0, 1, 0) + bytes(color) * 4
        (directory / name).write_bytes(data)
        records.append({"file": name, "rgba": color, "sha256": hashlib.sha256(data).hexdigest()})
    (directory / "manifest.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")


def normalize(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def reflected(v, normal):
    return normalize(2 * np.sum(v * normal, axis=-1, keepdims=True) * normal - v)


def denominator(roughness):
    q = roughness ** 4
    if abs(1 - q) < 1e-6:
        return 0.25
    return 1 / (1 - q) + 2 * q / (1 - q) ** 2 * np.log(2 * q / (1 + q))


@lru_cache(maxsize=8)
def quadrature(roughness, radial, azimuth):
    nodes, weights = leggauss(radial)
    u = (nodes + 1) / (2 * (1 + roughness ** 4))
    w = weights / (2 * (1 + roughness ** 4))
    slope = roughness ** 2 * np.sqrt(u / (1 - u))
    cosine = (1 - slope * slope) / (1 + slope * slope)
    sine = 2 * slope / (1 + slope * slope)
    phi = (np.arange(azimuth, dtype=np.float64) + 0.5) * (2 * np.pi / azimuth)
    local = np.stack(np.broadcast_arrays(sine[:, None] * np.cos(phi), sine[:, None] * np.sin(phi), cosine[:, None]), axis=-1)
    mass = np.broadcast_to((w * cosine)[:, None] / azimuth, (radial, azimuth)).copy()
    require(abs(mass.sum() - denominator(roughness)) < 0.00005, "CPU GGX denominator normalization failed")
    return local.reshape(-1, 3), mass.ravel()


def box_hit(origin, directions, lo, hi):
    # Independent analytic slab intersection, not an RT triangle/RNG replay.
    with np.errstate(divide="ignore", invalid="ignore"):
        t0 = (np.asarray(lo) - origin) / directions
        t1 = (np.asarray(hi) - origin) / directions
    enter = np.max(np.minimum(t0, t1), axis=-1)
    leave = np.min(np.maximum(t0, t1), axis=-1)
    return (leave >= np.maximum(enter, 0)) & (leave > 1e-7)


def integrate(position, reflection, roughness, geometry, radial, azimuth):
    if geometry == 1:
        return 0.0
    local, mass = quadrature(float(roughness), radial, azimuth)
    # Arbitrary stable CPU frame; intentionally unrelated to shader TangentToWorld.
    axis = np.eye(3)[np.argmin(np.abs(reflection))]
    x = normalize(np.cross(axis, reflection))
    y = np.cross(reflection, x)
    directions = local @ np.stack((x, y, reflection))
    visible = directions[:, 2] < 0
    origin = position + np.array([0, 0, -1 / 65536])
    if geometry in (2, 3):
        lo, hi = ((0.1, -1.5, -1.2), (1.5, 1.5, -1.0)) if geometry == 2 else ((2.7, -4, -1.5), (3.3, 4, -0.5))
        visible &= ~box_hit(origin, directions, lo, hi)
    return float(mass[visible].sum() / mass.sum())


def read_capture(directory):
    receipt = json.loads((directory / "capture.json").read_text(encoding="utf-8-sig"))
    require(receipt["status"] == "completed" and receipt["publication"] == "Published", "Unpublished capture")
    records = list(directory.glob("*.resource-export.json"))
    require(len(records) == 1, f"Expected one export in {directory}")
    record = json.loads(records[0].read_text(encoding="utf-8-sig"))
    values = {}
    for target in record["targets"]:
        value = np.load(directory / target["array"], allow_pickle=False)
        require(value.ndim == 4 and value.shape[0] == 1 and np.isfinite(value).all(), "Invalid/nonfinite payload")
        values[target["address"]["resource"]] = value[0].astype(np.float64)
    return record, values


def receiver_model(camera, extent, material, fine_derivatives=False):
    height, width = extent
    yy, xx = np.indices(extent)
    forward, up, position = (np.asarray(camera[k], dtype=np.float64) for k in ("forward", "up", "position"))
    right = normalize(np.cross(up, forward))
    t = np.tan(np.deg2rad(camera["verticalFovDegrees"]) / 2)
    directions = forward + ((xx + .5) / width * 2 - 1)[..., None] * t * width / height * right + (1 - (yy + .5) / height * 2)[..., None] * t * up
    plane = position + directions * (-position[2] / directions[..., 2])[..., None]
    v = normalize(position - plane)
    n = np.array([0., 0., -1.])
    coat_n = n.copy()
    # The owning importer renormalizes normal-map mip texels, including mip
    # zero, then quantizes RGBA8. Reconstruct that authored input in float64;
    # do not substitute exported GPU normals for this expected answer.
    def imported_normal(color):
        unit = normalize(np.asarray(color[:3]) / 255 * 2 - 1)
        encoded = np.rint((unit * .5 + .5) * 255)
        return normalize((encoded / 255 * 2 - 1) * [1, -1, -1])
    if material == 5:
        n = imported_normal(COLORS[0])
    if material in (5, 10):
        coat_n = imported_normal(COLORS[1])
    roughness = 1. if material == 1 else .045 if material in (2, 8, 11) else .2 if material >= 4 else .5
    if material == 11:
        # Equal-length authored vertex normals, inverse-transpose scale (2,2,1),
        # perspective interpolation on z=0, followed by production normalization.
        n = normalize(np.stack((plane[..., 0] * .5, plane[..., 1] * .5, -np.ones(extent)), axis=-1))
        coat_n = n.copy()
        ex, ey = xx // 2 * 2, yy // 2 * 2
        dx_y = yy if fine_derivatives else ey
        dy_x = xx if fine_derivatives else ex
        dx = n[dx_y, np.minimum(ex + 1, width - 1)] - n[dx_y, ex]
        dy = n[np.minimum(ey + 1, height - 1), dy_x] - n[ey, dy_x]
        kernel = np.minimum(2 * np.sum(dx * dx + dy * dy, axis=-1), .18)
        roughness = np.sqrt(.045 ** 2 + kernel)
    reflection = reflected(v, n)
    if material in (6, 7):
        direction = normalize(np.array([1., 1 / 255]))
        if material == 7:
            direction = np.array([-direction[1], direction[0]])
        tangent = np.array([direction[0], -direction[1], 0])
        bitangent = np.cross(n, tangent)
        projected = normalize(v - np.sum(v * bitangent, axis=-1, keepdims=True) * bitangent)
        alpha_b = roughness ** 2
        alpha_t = alpha_b + (1 - alpha_b) * .8 ** 2
        bent = normalize(n * (alpha_b / alpha_t) + projected * (1 - alpha_b / alpha_t))
        reflection = reflected(v, bent)
    return plane, v, n, coat_n, reflection, reflected(v, coat_n), roughness


def sample_lut(lut, no_v, roughness, error_bound=False):
    h, w = lut.shape[:2]
    xy = np.stack(np.broadcast_arrays(no_v * w - .5, np.asarray(roughness) * h - .5), axis=-1)
    ij = np.floor(xy).astype(np.int64)
    f = xy - ij
    x0, y0 = np.clip(ij[..., 0], 0, w - 1), np.clip(ij[..., 1], 0, h - 1)
    x1, y1 = np.clip(ij[..., 0] + 1, 0, w - 1), np.clip(ij[..., 1] + 1, 0, h - 1)
    if error_bound:
        # Both RHIs permit finite-precision linear-filter fractions. Bound a
        # half-step in 8-bit interpolation weights from authored neighbor values;
        # do not fit an energy tolerance to measured GPU differences.
        dx = np.maximum(np.abs(lut[y0, x1] - lut[y0, x0]), np.abs(lut[y1, x1] - lut[y1, x0]))
        dy = np.maximum(np.abs(lut[y1, x0] - lut[y0, x0]), np.abs(lut[y1, x1] - lut[y0, x1]))
        # The explicit input is an R16G16_FLOAT LUT. Permit one source-format
        # ULP for the format-dependent filtering arithmetic as well as fraction
        # quantization. This bound is determined by the captured texels, not GPU
        # weights, and does not enter the visibility/refinement/seed thresholds.
        half_ulp = np.maximum.reduce([np.spacing(a.astype(np.float16)).astype(np.float64) for a in
                                     (lut[y0, x0], lut[y0, x1], lut[y1, x0], lut[y1, x1])])
        return (dx + dy) / 512 + half_ulp
    a = lut[y0, x0] * (1 - f[..., :1]) + lut[y0, x1] * f[..., :1]
    b = lut[y1, x0] * (1 - f[..., :1]) + lut[y1, x1] * f[..., :1]
    return a * (1 - f[..., 1:]) + b * f[..., 1:]


def energy(f0, lut):
    total = np.clip(lut.sum(axis=-1, keepdims=True), 0, 1)
    compensation = 1 + f0 * (1 / np.maximum(total, 1e-4) - 1)
    compensation = np.where(total <= 1e-4, 1, compensation)
    return np.clip((f0 * lut[..., :1] + lut[..., 1:2]) * compensation, 0, 1)


def energy_filter_bound(f0, lut, delta):
    low_total = lut.sum(-1, keepdims=True) - delta.sum(-1, keepdims=True)
    require(np.all(low_total > 1e-4), "LUT energy bound straddles uninitialized policy")
    common = f0 * (1 - f0)
    da = common * (1 + (lut[..., 1:2] + delta[..., 1:2]) / low_total ** 2)
    db = (1 - f0) + common * (lut[..., :1] + delta[..., :1]) / low_total ** 2
    return da * delta[..., :1] + db * delta[..., 1:2]


def coastal_diagnostics(directory, case, frame, covered, values):
    vis, base, coat, _, positions, diagnostics, _, _ = [values[n] for n in OUTPUTS]
    require(covered.shape == (800, 1280), "Coastal masks require their frozen 1280x800 extent")
    require(frame["content"]["assetBundle"] is not None, "Missing Coastal bundle identity")
    require(np.all(base[..., 3] == 0) and np.all(coat[..., 3] == 0) and np.all(diagnostics[..., 3] == 0), "Invalid Coastal lobe sampling")
    matched = covered & np.isin(vis[..., 3], [1, 2])
    unmatched = covered & (vis[..., 3] == -1)
    require(np.all((matched | unmatched)[covered]) and np.isin(diagnostics[..., 0][unmatched], [-4, -5]).all(), "Unexpected Coastal primary failure")
    # Matching failures are retained, visualized and counted, never deleted to
    # claim a qualified Coastal ROI. The dedicated scenes own numerical gates.
    require(matched.any(), "No matched Coastal receivers")
    inputs = json.loads((Path(__file__).parent / "GlobalIllumination/Inputs.json").read_text(encoding="utf-8-sig"))
    authored = next(o for o in inputs["coastal"]["observations"] if o["referenceView"] == case["view"])
    yy, xx = np.indices(covered.shape)
    masks = []
    from PIL import Image
    overlay = np.zeros((*covered.shape, 3), dtype=np.uint8)
    overlay[covered] = 48
    overlay[unmatched] = [255, 0, 255]
    for roi in authored["masks"]:
        x0, y0, x1, y1 = roi["rectPixels"]
        mask = covered & (xx >= x0) & (xx < x1) & (yy >= y0) & (yy < y1)
        require(mask.any(), "Empty frozen Coastal diagnostic mask")
        overlay[mask & matched] = [255, 100, 30]
        np.save(directory / (roi["id"] + ".mask.npy"), mask, allow_pickle=False)
        masks.append({"id": roi["id"], "covered": int(mask.sum()), "unmatched": int((mask & unmatched).sum()), "qualification": "diagnostic-only-not-reference-certified"})
    Image.fromarray(overlay).save(directory / "oracle-mask.png")
    np.save(directory / "unmatched.mask.npy", unmatched, allow_pickle=False)
    return {"case": case, "qualification": "diagnostic-only-not-reference-certified", "covered": int(covered.sum()), "matched": int(matched.sum()), "unmatched": int(unmatched.sum()), "primaryFailureCounts": {str(code): int((unmatched & (diagnostics[..., 0] == code)).sum()) for code in (-4, -5)}, "masks": masks, "visibilityMean": None}, vis, covered


def inspect(root, case, cache):
    directory = root / case["directory"]
    record, values = read_capture(directory)
    receiver_record, receivers = read_capture(directory / "receivers")
    _, lut_values = read_capture(directory / "lut")
    frame = record["frame"]
    require(frame["camera"] == receiver_record["frame"]["camera"] and frame["camera"]["referenceView"] == case["view"], "Receiver camera drift")
    require(frame["content"]["labId"] == "gglab.lab.gi_reference", "Wrong lab")
    require(frame["temporal"]["status"] == "disabled" and frame["temporal"]["jitterPixels"] == [0, 0], "Temporal receiver")
    gate = [g for g in frame["readiness"]["gates"] if g["name"] == "gi-specular-visibility"]
    require(len(gate) == 1 and gate[0]["state"] == "ready", "Visibility is not GPU-complete")
    params = {p["id"].split(".")[-1]: p["value"] for p in frame["parameters"]["values"] if p["id"].startswith("gi_reference.")}
    for key in ("fixture", "visibility_material", "visibility_geometry", "samples", "seed", "batch_samples", "tile_pixels"):
        require(params.get(key, {"fixture": 0, "visibility_material": 0, "visibility_geometry": 0, "samples": 64, "seed": 61051, "batch_samples": 64, "tile_pixels": 0}[key]) == case[key], f"Unconsumed parameter {key}")
    face = receivers[RECEIVERS[0]]
    covered = face[..., 3] == 1
    require(covered.any() and np.isin(face[..., 3], [0, 1]).all(), "Unsupported/empty receivers")
    vis, base, coat, weights, positions, diagnostics, f0, layer = [values[n] for n in OUTPUTS]
    if case["fixture"] == 11:
        return coastal_diagnostics(directory, case, frame, covered, values)
    # Validate all covered pixels BEFORE the authored mask, including blockers.
    require(np.isin(vis[..., 3][covered], [1, 2]).all() and np.all(diagnostics[..., 3][covered] == 0), "Invalid covered primary/sample")
    require(np.all(base[..., 3] == 0) and np.all(coat[..., 3] == 0), "Invalid lobe samples")
    require(np.all(diagnostics[..., 2][covered] == case["samples"]), "Incomplete sweep published")
    # Independently compensated numerator/denominator sums can differ by one
    # final FP32 ULP at unity. Retain the raw ratio; this storage allowance does
    # not relax any 0/1 control, ROI accuracy, refinement or seed threshold.
    require(np.all((vis[..., :3] >= 0) & (vis[..., :3] <= 1 + 2 * np.finfo(np.float32).eps)) and np.all(weights >= 0), "Visibility/weight range")
    for lobe, sums in enumerate((base, coat)):
        active = covered & ((diagnostics[..., 1].astype(int) & (1 << lobe)) != 0)
        require(np.all(sums[..., 1][active] > 0), "Active lobe has illegal denominator")
        require(np.all(sums[covered & ~active] == 0), "Inactive lobe hid nonzero samples")
        require(np.max(np.abs(vis[..., lobe][active] - sums[..., 0][active] / sums[..., 1][active]), initial=0) < 2e-7, "Per-pixel normalization differs")
    require(np.all(weights[..., 3] <= 1), "Illegal coat transmission")
    y = weights[..., :3] @ np.array([.2126, .7152, .0722])
    total = y + weights[..., 3]
    expected_composite = np.divide(y * vis[..., 0] + weights[..., 3] * vis[..., 1], total, out=np.zeros_like(y), where=total > 0)
    require(np.max(np.abs(vis[..., 2] - expected_composite)) < 3e-7, "Layer composite differs")
    m, geometry = case["visibility_material"], case["visibility_geometry"]
    plane, v, n, coat_n, reflection, coat_reflection, roughness = receiver_model(frame["camera"], covered.shape, m)
    # Authored world/primitive ROI. No value/error-based trimming is allowed.
    mask = covered & (positions[..., 3] == 0) & np.isin(diagnostics[..., 0], [0, 1]) & (np.abs(plane[..., 2]) < 1e-9) & (plane[..., 0] >= -.25) & (plane[..., 0] <= -.05) & (np.abs(plane[..., 1]) <= .08)
    require(mask.sum() >= 4, "Empty authored world/primitive ROI")
    require(np.max(np.abs(positions[..., :3][mask] - plane[mask])) < 0.00005, "Independent receiver mapping failed")
    normal_values = np.broadcast_to(n, (*mask.shape, 3))
    coat_normals = np.broadcast_to(coat_n, (*mask.shape, 3))
    roughness_values = np.broadcast_to(roughness, mask.shape)
    require(np.max(np.abs(receivers[RECEIVERS[2]][..., :3][mask] - normal_values[mask])) < 0.00002, "Primary normal texture/frame differs")
    require(np.max(np.abs(receivers[RECEIVERS[5]][..., :3][mask] - reflection[mask])) < 0.00003, "Independent bent-reflection reconstruction failed")
    derivative_mode = "zero-variance"
    if m == 11:
        # Unqualified HLSL ddx/ddy permits implementation-dependent derivative
        # granularity. Independently reconstruct BOTH legal authored fields;
        # accept a whole-ROI model only, never an arbitrary exported roughness.
        # No other RHI's values enter either expected field.
        derivative_mode = "coarse"
        if np.max(np.abs(receivers[RECEIVERS[5]][..., 3][mask] - roughness_values[mask])) >= 3e-6:
            roughness = receiver_model(frame["camera"], covered.shape, m, True)[-1]
            roughness_values = np.broadcast_to(roughness, mask.shape)
            derivative_mode = "fine"
    require(np.max(np.abs(receivers[RECEIVERS[5]][..., 3][mask] - roughness_values[mask])) < (3e-6 if m == 11 else 2e-7), "Effective roughness/specular AA differs")
    require(np.max(np.abs(receivers[RECEIVERS[6]][..., :3][mask] - coat_normals[mask])) < 0.00002, "Coat normal differs")
    coat_roughness = .75 * 128 / 255 if m == 10 else .75
    require(np.max(np.abs(receivers[RECEIVERS[6]][..., 3][mask] - coat_roughness)) < 2e-7, "Coat roughness texture differs")
    if m == 11:
        require(np.min(roughness_values[mask]) > .0451, "Specular-AA fixture has no derivative energy")
    ior = 1. if m == 8 else 0. if m == 9 else 2.4 if m == 10 else 1.5
    f0_expected = np.array([.8, .2, .05]) if m == 3 else np.full(3, ((ior - 1) / (ior + 1)) ** 2)
    require(np.max(np.abs(f0[mask, :3] - f0_expected)) < 1e-7 and np.max(np.abs(f0[..., 3][mask] - ior)) < 2e-7, "Full IOR/F0 identity differs")
    no_v = np.clip(np.sum(v * n, axis=-1), 0, 1)
    coat_no_v = np.clip(np.sum(v * coat_n, axis=-1), 0, 1)
    lut = lut_values["IBL.BrdfLut"]
    coat_factor = .8 if m in (4, 5, 10) else 0.
    base_lut = sample_lut(lut, no_v, roughness)
    coat_lut = sample_lut(lut, coat_no_v, coat_roughness)
    base_energy = energy(f0_expected, base_lut)
    coat_energy = energy(np.full(3, .04), coat_lut)[..., 0] if coat_factor else np.zeros_like(no_v)
    base_bound = energy_filter_bound(f0_expected, base_lut, sample_lut(lut, no_v, roughness, True))
    coat_bound = coat_factor * energy_filter_bound(np.full(3, .04), coat_lut, sample_lut(lut, coat_no_v, coat_roughness, True))[..., 0]
    cw = coat_factor * coat_energy
    expected_weights = np.concatenate((base_energy * (1 - cw[..., None]) ** 2, cw[..., None]), axis=-1)
    weight_bound = np.concatenate((base_bound + 2 * (base_energy + base_bound) * coat_bound[..., None], coat_bound[..., None]), axis=-1)
    require(np.all(np.abs(weights[mask] - expected_weights[mask]) <= weight_bound[mask] + 4e-6), "Independent layer-energy oracle failed")
    key = (case["view"], m, geometry, tuple(covered.shape), derivative_mode)
    if key not in cache:
        active = [bool(np.any(expected_weights[mask, :3] > 0)), bool(coat_factor)]
        estimates = []
        refinement = 1.
        for radial in (64, 128, 256, 512):
            pixels = []
            for p, r, cr, rough in zip(plane[mask], reflection[mask], coat_reflection[mask], roughness_values[mask]):
                pixels.append([integrate(p, r, rough, geometry, radial, radial * 4) if active[0] else 0., integrate(p, cr, coat_roughness, geometry, radial, radial * 4) if active[1] else 0.])
            result = np.asarray(pixels)
            estimates.append(result.mean(0))
            if len(estimates) > 1:
                refinement = float(np.max(np.abs(estimates[-1] - estimates[-2])))
                if refinement < .001:
                    break
        require(refinement < .001, f"CPU oracle refinement exhausted: {refinement}")
        cache[key] = (result, refinement, radial)
    expected, refinement, radial = cache[key]
    cpu_weights = expected_weights[mask]
    base_y = cpu_weights[:, :3] @ np.array([.2126, .7152, .0722])
    combined = np.divide(base_y * expected[:, 0] + cpu_weights[:, 3] * expected[:, 1], base_y + cpu_weights[:, 3], out=np.zeros_like(base_y), where=base_y + cpu_weights[:, 3] > 0)
    expected_mean = np.r_[expected.mean(0), combined.mean()]
    mean = vis[mask, :3].mean(0)
    error = float(np.max(np.abs(mean - expected_mean)))
    require(error < .01, f"GPU/CPU error {error}: {case}")
    if geometry == 1:
        require(np.max(np.abs(vis[mask, :3])) <= 1e-6, "Closed control is not exact zero")
    # The odd-sized normal view's central pixel is a genuine N=V=R open control.
    if geometry == 0 and case["view"].endswith("Normal") and m not in (5, 6, 7):
        center = tuple(i // 2 for i in covered.shape)
        require(abs(vis[center][0] - 1) <= 1e-6, "Normal-incidence open control is not exact one")
    overlay = np.zeros((*mask.shape, 3), dtype=np.uint8)
    overlay[covered] = 48
    overlay[mask] = [255, 80, 30]
    from PIL import Image
    Image.fromarray(overlay).save(directory / "oracle-mask.png")
    np.save(directory / "oracle-mask.npy", mask, allow_pickle=False)
    return {"case": case, "qualification": "sampling-qualified", "pixels": int(mask.sum()), "mean": mean.tolist(), "cpuMean": expected_mean.tolist(), "absoluteError": error, "cpuRefinement": refinement, "cpuRadialNodes": radial, "derivativeMode": derivative_mode, "denominatorRange": [float(denominator(float(roughness_values[mask].min()))), float(denominator(float(roughness_values[mask].max())))]}, vis, mask


def validate(root):
    cases = json.loads((root / "cases.json").read_text(encoding="utf-8-sig"))
    cache, results, groups, partitions, cross_rhi = {}, [], {}, {}, {}
    for case in cases:
        result, vis, mask = inspect(root, case, cache)
        results.append(result)
        if case["fixture"] == 11:
            continue
        key = (case["rhi"], case["view"], case["visibility_material"], case["visibility_geometry"], case.get("lighting", "baseline"))
        groups.setdefault(key, []).append(result)
        identity = (case["rhi"], case["view"], case["visibility_material"], case["visibility_geometry"], case["samples"], case["seed"])
        _, payload = read_capture(root / case["directory"])
        geometric = np.concatenate((payload[PREFIX + "Base"], payload[PREFIX + "Coat"], vis[..., :2]), axis=-1)
        if identity in partitions:
            require(np.array_equal(geometric, partitions[identity]), "Batch/tile or pure-lighting changes altered numerator, denominator or lobe visibility")
        partitions[identity] = geometric
        cross_identity = (case["view"], case["visibility_material"], case["visibility_geometry"], case["samples"], case["seed"])
        if cross_identity in cross_rhi:
            require(np.max(np.abs(np.asarray(result["mean"]) - cross_rhi[cross_identity])) < .01, "Cross-RHI visibility differs")
        cross_rhi[cross_identity] = np.asarray(result["mean"])
    sampling = []
    for key, records in groups.items():
        if key[-1] != "baseline":
            continue
        observations = {(r["case"]["samples"], r["case"]["seed"]): np.array(r["mean"]) for r in records}
        counts = sorted(set(n for n, _ in observations))
        seeds = sorted(set(s for _, s in observations))
        require(len(counts) >= 2 and counts[-1] == 2 * counts[0] and len(seeds) >= 2, "Missing N/2N or independent-seed qualification")
        seed_error = float(np.max(np.abs(observations[counts[-1], seeds[0]] - observations[counts[-1], seeds[1]])))
        sample_error = float(np.max(np.abs(observations[counts[0], seeds[0]] - observations[counts[-1], seeds[0]])))
        require(seed_error < .01 and sample_error < .01, f"Unconverged sampling budget: {key}, {seed_error}, {sample_error}")
        sampling.append({"identity": key, "independentSeedDifference": seed_error, "doublingDifference": sample_error})
    return {"status": "passed", "contract": "continuous-GGX-geometric-environment-visibility", "observations": results, "sampling": sampling, "coastalQualified": False, "productionS1": False}


def self_test():
    for r in (.045, .2, .5, .75, 1.):
        local, mass = quadrature(r, 256, 1024)
        require(abs(np.linalg.norm(local, axis=-1).min() - 1) < 1e-12, "Oracle directions not normalized")
        require(abs(mass.sum() - denominator(r)) < 5e-5, "Denominator identity")
        require(abs(integrate(np.zeros(3), np.array([0., 0., -1.]), r, 0, 128, 512) - 1) < 1e-12, "Open oracle")
        require(integrate(np.zeros(3), np.array([0., 0., -1.]), r, 1, 128, 512) == 0, "Closed oracle")
    require(box_hit(np.zeros(3), np.array([[0., 0., -1.], [1., 0., 0.]]), (-1, -1, -2), (1, 1, -1)).tolist() == [True, False], "Slab oracle")
    require(np.array_equal(energy(np.zeros(3), np.array([1., 0.])), np.zeros(3)), "Zero-energy activity")
    print(json.dumps({"status": "passed", "oracle": "float64 radial/azimuth quadrature + analytic slab geometry + independent layer energy"}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--generate", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.generate:
        generate(args.generate)
    if args.self_test:
        self_test()
    if args.root:
        try:
            result = validate(args.root)
        except Exception as error:
            result = {"status": "failed", "qualification": "unconverged-or-invalid", "reason": str(error)}
            (args.root / "validation.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            raise
        (args.root / "validation.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"status": result["status"], "observations": len(result["observations"]), "samplingGroups": len(result["sampling"])}))


if __name__ == "__main__":
    main()
