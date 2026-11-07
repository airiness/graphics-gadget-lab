"""Float64 finite-order diffuse transport oracles, independent of GPU path sampling.

D counts secondary diffuse queries. S and H are direct sun/sky irradiance.
A = F diag(rho), where F includes visibility and the Lambertian 1/pi.
Sun(D) = sum(A**k S, k=1..D); sky(D) = sum(A**k H, k=0..D-1).
No seed, GPU RNG, shader helper or ray-scene data is used by these oracles.
"""
import argparse
from functools import lru_cache
import hashlib
import json
from pathlib import Path

import numpy as np

from ValidateGIReferenceLighting import (require, wall_form_factor, masks, relative_error,
                                        DEFAULTS as LIGHTING_DEFAULTS)

RELATIVE = .01
QUADRATURE = .002


def rectangle(origin, u, v, order):
    nodes, weights = np.polynomial.legendre.leggauss(order)
    a, b = np.meshgrid((nodes + 1) / 2, (nodes + 1) / 2, indexing="ij")
    u, v = np.asarray(u, dtype=float), np.asarray(v, dtype=float)
    points = np.asarray(origin) + a.reshape(-1, 1)*u + b.reshape(-1, 1)*v
    area = np.linalg.norm(np.cross(u, v))
    return points, (weights[:, None]*weights[None, :]*area/4).ravel()


def transfer(receivers, normals, sources, source_normals, weights, field, visible=None):
    """Area quadrature of cos(receiver) cos(source) / (pi r^2) times field."""
    result = np.zeros((len(receivers), field.shape[-1]), dtype=np.float64)
    normals = np.broadcast_to(normals, receivers.shape)
    source_normals = np.broadcast_to(source_normals, sources.shape)
    weighted = field * weights[:, None]
    for begin in range(0, len(receivers), 64):
        p = receivers[begin:begin+64]
        delta = sources[None, :, :] - p[:, None, :]
        squared = np.einsum("ijk,ijk->ij", delta, delta)
        c0 = np.maximum(np.einsum("ijk,ik->ij", delta, normals[begin:begin+len(p)]), 0)
        c1 = np.maximum(-np.einsum("ijk,jk->ij", delta, source_normals), 0)
        kernel = c0*c1/(np.pi*np.maximum(squared, 1e-30)**2)
        if visible is not None:
            kernel *= visible(p, sources)
        result[begin:begin+len(p)] = kernel @ weighted
    return result


def plane_transfer(receivers, normal, sources, source_normal, weights, field, factor):
    # Product quadrature under-resolves the kernel near the shared edge. Its
    # constant mode has an exact rectangle integral: integrate that mode exactly
    # and use the quadrature for the local weighted mean of the varying field.
    augmented = np.column_stack([field, np.ones(len(field))])
    value = transfer(receivers, normal, sources, source_normal, weights, augmented)
    return value[:, :3] * (factor/np.pi/value[:, 3])[:, None]


@lru_cache(maxsize=16)
def plane_fields(order, floor_albedo, wall_albedo, sky, lux):
    floor, fw = rectangle([-2, 0, -2], [4, 0, 0], [0, 0, 4], order)
    wall, ww = rectangle([-2, 0, 2], [4, 0, 0], [0, 4, 0], order)
    factors = [wall_form_factor(floor[:, 0], floor[:, 2]),
               wall_form_factor(wall[:, 0], -wall[:, 1], plane_z=0)]
    rho = [np.asarray(floor_albedo), np.asarray(wall_albedo)]
    sun = [np.full((len(floor), 3), lux/np.sqrt(2)) for _ in range(2)]
    environment = [np.repeat(((np.pi-f)*sky)[:, None], 3, axis=1) for f in factors]
    sun_orders, sky_orders = [sun], [environment]
    # The first sun order is analytic; subsequent orders use area quadrature.
    sun = [factors[i][:, None]/np.pi*rho[1-i]*sun[1-i] for i in range(2)]
    sun_orders.append(sun)
    for index in range(3):
        sky_orders.append([
            plane_transfer(floor, [0, 1, 0], wall, [0, 0, -1], ww, environment[1]*rho[1], factors[0]),
            plane_transfer(wall, [0, 0, -1], floor, [0, 1, 0], fw, environment[0]*rho[0], factors[1])])
        environment = sky_orders[-1]
        if index < 2:
            sun_orders.append([
                plane_transfer(floor, [0, 1, 0], wall, [0, 0, -1], ww, sun[1]*rho[1], factors[0]),
                plane_transfer(wall, [0, 0, -1], floor, [0, 1, 0], fw, sun[0]*rho[0], factors[1])])
            sun = sun_orders[-1]
    return wall, ww, sun_orders, sky_orders


def planes_oracle(position, depth, floor_albedo=(1., 1., 1.),
                  wall_albedo=(.8, .05, .05), sky=1., lux=10000., order=32):
    wall, weights, sun_fields, sky_fields = plane_fields(
        order, tuple(floor_albedo), tuple(wall_albedo), sky, lux)
    factor = wall_form_factor(position[:, 0], position[:, 2])
    sun_orders = [factor[:, None]/np.pi*np.asarray(wall_albedo)*lux/np.sqrt(2)]
    sky_orders = [np.repeat(((np.pi-factor)*sky)[:, None], 3, axis=1)]
    for index in range(1, depth):
        sun_orders.append(transfer(position, [0, 1, 0], wall, [0, 0, -1], weights,
                                   sun_fields[index][1]*wall_albedo))
        sky_orders.append(transfer(position, [0, 1, 0], wall, [0, 0, -1], weights,
                                   sky_fields[index-1][1]*wall_albedo))
    return np.asarray(sun_orders), np.asarray(sky_orders)


# Frozen F4 solids, deliberately authored here instead of reading GPU geometry.
F4_BOXES = np.array([
    [[-2.2, -.2, -2.2], [2.2, 0, 2.2]],
    [[-2.2, 4, -2.2], [2.2, 4.2, 2.2]],
    [[-2.2, 0, -2.2], [-2, 4, 2.2]],
    [[2, 0, -2.2], [2.2, 4, 2.2]],
    [[-2, 0, -2.2], [2, 4, -2]],
    [[-2, 0, 2], [-1, 4, 2.2]],
    [[1, 0, 2], [2, 4, 2.2]],
    [[-1, 3, 2], [1, 4, 2.2]],
], dtype=np.float64)


def f4_visible(receivers, sources):
    delta = sources[None, :, :] - receivers[:, None, :]
    # Segment endpoints lie on surfaces. Only an open interior intersection blocks.
    safe = np.where(np.abs(delta) > 1e-15, delta, 1e-30)
    visible = np.ones(delta.shape[:2], dtype=bool)
    for lo, hi in F4_BOXES:
        t0 = (lo-receivers[:, None, :])/safe
        t1 = (hi-receivers[:, None, :])/safe
        enter = np.minimum(t0, t1).max(-1)
        leave = np.maximum(t0, t1).min(-1)
        visible &= ~(np.maximum(enter, 1e-7) < np.minimum(leave, 1-1e-7))
    return visible


def f4_sun_sources(order):
    nodes, weights = np.polynomial.legendre.leggauss(order)
    points, areas, normals, irradiance = [], [], [], []
    # Exact sunlit floor trapezoids, including the opening-thickness strip.
    for z0, z1 in ((-.8, 2.), (2., 2.2)):
        z = z0+(nodes+1)*(z1-z0)/2
        left = -1+.2*(2.2-z)
        right = 1+.2*((2 if z0 < 2 else 2.2)-z)
        x = (left+right)[:, None]/2+(right-left)[:, None]*nodes[None, :]/2
        points.append(np.stack([x, np.zeros_like(x), np.broadcast_to(z[:, None], x.shape)], -1).reshape(-1, 3))
        areas.append((weights[:, None]*weights[None, :]*(z1-z0)*(right-left)[:, None]/4).ravel())
        normals.append(np.tile([0, 1, 0], (order*order, 1)))
        irradiance.append(np.full(order*order, 10000/np.sqrt(2.04)))
    # Directly lit neutral right jamb.
    z = 2+(nodes+1)*.1
    height = 3-(2.2-z)
    y = height[:, None]*(nodes[None, :]+1)/2
    points.append(np.stack([np.ones_like(y), y, np.broadcast_to(z[:, None], y.shape)], -1).reshape(-1, 3))
    areas.append((weights[:, None]*weights[None, :]*.1*height[:, None]/2).ravel())
    normals.append(np.tile([-1, 0, 0], (order*order, 1)))
    irradiance.append(np.full(order*order, 2000/np.sqrt(2.04)))
    return np.concatenate(points), np.concatenate(areas), np.concatenate(normals), np.concatenate(irradiance)


@lru_cache(maxsize=8)
def f4_secondary_field(order):
    # All inward faces visible from a neutral interior floor, including reveals.
    descriptions = [
        ([-2, 4, -2], [4, 0, 0], [0, 0, 4], [0, -1, 0], [.8]*3),
        ([-2, 0, -2], [0, 4, 0], [0, 0, 4], [1, 0, 0], [.8, .05, .05]),
        ([2, 0, -2], [0, 4, 0], [0, 0, 4], [-1, 0, 0], [.05, .8, .05]),
        ([-2, 0, -2], [4, 0, 0], [0, 4, 0], [0, 0, 1], [.8]*3),
        ([-2, 0, 2], [1, 0, 0], [0, 4, 0], [0, 0, -1], [.8]*3),
        ([1, 0, 2], [1, 0, 0], [0, 4, 0], [0, 0, -1], [.8]*3),
        ([-1, 3, 2], [2, 0, 0], [0, 1, 0], [0, 0, -1], [.8]*3),
        ([-1, 0, 2], [0, 3, 0], [0, 0, .2], [1, 0, 0], [.8]*3),
        ([1, 0, 2], [0, 3, 0], [0, 0, .2], [-1, 0, 0], [.8]*3),
        ([-1, 3, 2], [2, 0, 0], [0, 0, .2], [0, -1, 0], [.8]*3),
    ]
    points, areas, normals, albedos = [], [], [], []
    for origin, u, v, normal, albedo in descriptions:
        p, w = rectangle(origin, u, v, order)
        points.append(p)
        areas.append(w)
        normals.append(np.tile(normal, (len(p), 1)))
        albedos.append(np.tile(albedo, (len(p), 1)))
    p, w, n, rho = map(np.concatenate, (points, areas, normals, albedos))
    source, source_weights, source_normals, direct = f4_sun_sources(order*2)
    incoming = transfer(p, n, source, source_normals, source_weights,
                        np.repeat((direct*.8)[:, None], 3, axis=1), f4_visible)
    return p, w, n, rho*incoming


def f4_second_sun(position, order=20):
    p, w, n, field = f4_secondary_field(order)
    return transfer(position, [0, 1, 0], p, n, w, field, f4_visible)


def read_case(root, case, components=True):
    directory = root/case["directory"]
    exports = list(directory.glob("*.resource-export.json"))
    require(len(exports) == 1, "Expected one bounded transport export")
    record = json.loads(exports[0].read_text(encoding="utf-8-sig"))
    frame = record["frame"]
    require(frame["readiness"]["ready"] and frame["content"]["labId"] == "gglab.lab.gi_reference" and
            frame["camera"]["referenceView"] == case["view"], "Unready/wrong transport content")
    gate = [g for g in frame["readiness"]["gates"] if g["name"] == "gi-reference"]
    require(len(gate) == 1 and gate[0]["state"] == "ready" and
            f"D={case['depth']}," in gate[0]["detail"], "Actual observation depth is missing/stale")
    actual = dict(LIGHTING_DEFAULTS, depth=1)
    actual.update({p["id"].split(".", 1)[1]: p["value"] for p in frame["parameters"]["values"]
                   if p["id"].startswith("gi_reference.")})
    require(all(actual[k] == case[k] for k in ("fixture", "depth", "samples", "batch_samples", "seed",
                                               "albedo", "secondary_albedo", "environment")),
            "Transport parameter provenance differs")
    require(frame["temporal"]["status"] == "disabled" and frame["temporal"]["jitterPixels"] == [0, 0],
            "Reference transport cannot use temporal sampling")
    inputs = frame["sceneInputs"]
    require(inputs["atmosphere"] is None and inputs["environment"]["intensity"] == 0,
            "Transport contracts require the declared direct constant sky and no atmosphere")
    if case["fixture"] in (4, 8):
        sun = inputs["sun"]
        direction = np.array([.2, -1, -1] if case["fixture"] == 4 else [0, -1, 1], dtype=float)
        direction /= np.linalg.norm(direction)
        require(sun is not None and sun["toaIlluminanceLux"] == 10000 and
                np.max(np.abs(np.asarray(sun["photonDirection"])-direction)) < 1e-6,
                "Wrong WorldSun units/direction")
    else:
        require(inputs["sun"] is None, "Sealed room must remain sun-off")
    values = {}
    for target in record["targets"]:
        a = np.load(directory/target["array"], allow_pickle=False)
        require(a.dtype == np.dtype("<f4") and a.ndim == 4 and a.shape[0] == 1 and
                a.shape[-1] == 4 and np.isfinite(a).all(), "Invalid/non-finite transport export")
        values[target["address"]["resource"]] = a[0]
    e = values["GI.Reference.Irradiance"]
    covered = values["GI.Receiver.HitAlbedo"][..., 3] == 1
    require(covered.any() and np.all(e[..., 3][covered] == 1) and np.all(e[..., 3][~covered] == 0),
            "Invalid primary or deep path cannot be excluded from statistics")
    check_path_diagnostics(values["GI.Reference.PathDiagnostics"], covered, case["depth"])
    diag = values["GI.Reference.Diagnostics"]
    require(np.all(diag >= 0) and np.all(diag.sum(-1) <= 1.00001) and np.all(diag[..., 3] == 0),
            "First-query outcome contract changed")
    require(np.all(e[..., :3] >= 0), "Negative reference irradiance")
    if components:
        require(np.allclose(e[..., :3], values["GI.Reference.SunIrradiance"][..., :3] +
                values["GI.Reference.EnvironmentIrradiance"][..., :3], rtol=1e-6, atol=1e-6),
                "Transport components do not sum to E")
    graph = json.loads(next(directory.glob("*.graph.json")).read_text(encoding="utf-8-sig"))
    passes = {p["name"]: p for p in graph["compile"]["passes"] if not p["culled"]}
    require("GI.Reference.PathTrace" in passes and "GI.Receiver" in passes and
            passes["GI.Receiver"]["executionOrder"] < passes["GI.Reference.PathTrace"]["executionOrder"],
            "Wrong Runtime transport composition")
    return values, frame


def check_path_diagnostics(path, covered, depth):
    require(np.all(path[..., 0] == 0), "Any-bounce invalid/non-finite path must fail validation")
    require(np.all((path[..., 1] >= 0) & (path[..., 1] <= 1)) and
            np.all(path[..., 2][covered] == depth) and np.all(path[..., 3][covered] == 1),
            "Wrong depth/truncation diagnostics")


def transport_masks(case, values, frame):
    if case["fixture"] not in (2, 3):
        return masks(case, values, frame["camera"])
    from ValidateGIReference import ROOM_FACES
    identity = values["GI.Reference.Identity"]
    p = values["GI.Reference.Position"]
    instance, triangles = ROOM_FACES[case["view"].replace("F3_", "F2_")]
    selected = (p[..., 3] == 1) & (identity[..., 0] == instance) & np.isin(identity[..., 1], triangles)
    if case["view"].endswith(("Floor", "Ceiling", "Roof")):
        selected &= (np.abs(p[..., 0]) <= 1.5) & (np.abs(p[..., 2]) <= 1.5)
    else:
        selected &= (p[..., 1] >= .5) & (p[..., 1] <= 3.5)
    return {"room": selected}


@lru_cache(maxsize=8)
def plane_order_means(positions, floor, wall):
    p = np.frombuffer(positions, dtype=np.float64).reshape(-1, 3)
    factor = wall_form_factor(p[:, 0], p[:, 2]).mean()
    results = []
    for order in (40, 64):
        sources, weights, sun, sky = plane_fields(order, floor, wall, 1., 10000.)
        # ROI averaging and the linear surface integral commute. Compute the
        # averaged geometric kernel once, then apply every finite-order field.
        kernel_mean = np.zeros(len(sources))
        for begin in range(0, len(p), 64):
            delta = sources[None, :, :]-p[begin:begin+64, None, :]
            squared = np.einsum("ijk,ijk->ij", delta, delta)
            kernel_mean += (delta[..., 1]*delta[..., 2]/(np.pi*squared*squared)).sum(0)
        kernel_mean *= weights/len(p)
        sun_means = [factor/np.pi*np.asarray(wall)*10000/np.sqrt(2)]
        sky_means = [np.full(3, np.pi-factor)]
        for index in range(1, 4):
            sun_means.append(kernel_mean @ (sun[index][1]*wall))
            sky_means.append(kernel_mean @ (sky[index-1][1]*wall))
        results.append((np.asarray(sun_means), np.asarray(sky_means)))
    return tuple(results)


def validate(root, cases):
    results, arrays, selectors = {}, {}, {}
    for case in cases:
        values, frame = read_case(root, case)
        name = case["directory"]
        keep = (case["fixture"] == 8 and (case.get("variant") == "batch" or
                "/transport-planes-d" in name and not case.get("variant") or
                name.endswith("/transport-receiver-d1"))) or (
                case["fixture"] == 4 and case["depth"] <= 2 and not case.get("variant"))
        if keep:
            arrays[name] = values
        selectors[name] = transport_masks(case, values, frame)
        rows = {}
        for mask_name, mask in selectors[name].items():
            require(mask.sum() >= 32, f"Empty transport ROI: {name}/{mask_name}")
            p = values["GI.Reference.Position"][mask, :3].astype(float)
            sun = values["GI.Reference.SunIrradiance"][mask, :3].mean(0, dtype=float)
            sky = values["GI.Reference.EnvironmentIrradiance"][mask, :3].mean(0, dtype=float)
            row = dict(pixels=int(mask.sum()), maskSha256=hashlib.sha256(mask.tobytes()).hexdigest(),
                       sun=sun.tolist(), environment=sky.tolist())
            if case["fixture"] == 4:
                require(np.max(np.abs(values["GI.Receiver.HitAlbedo"][mask, :3]-.8)) < 1e-6,
                        "Frozen F4 measurement receiver must remain neutral")
            if case["fixture"] == 8 and case.get("variant") != "batch":
                floor = (.2, .5, .8) if case["albedo"] else (1., 1., 1.)
                wall = (.8, .05, .05) if case["secondary_albedo"] else (.8, .8, .8)
                low, high = plane_order_means(p.tobytes(), floor, wall)
                refined = max(relative_error(a, b) for x, y in zip(low, high)
                              for a, b in zip(x[:case["depth"]], y[:case["depth"]]))
                require(refined < QUADRATURE, f"Unresolved CPU quadrature: {name}: {refined}")
                expected = [component[:case["depth"]].sum(0) for component in high]
                error = max(relative_error(sun, expected[0]), relative_error(sky, expected[1]))
                require(error < RELATIVE, f"Independent finite-order oracle: {name}: {error}")
                row.update(oracleSun=expected[0].tolist(), oracleEnvironment=expected[1].tolist(),
                           sunOrders=high[0][:case["depth"]].tolist(), environmentOrders=high[1][:case["depth"]].tolist(),
                           oracleError=error, quadratureRelative=refined)
            elif case["fixture"] in (2, 3) and not case["view"].endswith("Roof"):
                require(np.all(values["GI.Reference.Irradiance"][mask, :3] == 0) and
                        np.all(values["GI.Reference.Diagnostics"][mask, 0] == 0), "False sky in sealed room")
                # The whole visible interior also catches the seams outside the central ROI.
                covered = values["GI.Reference.Position"][..., 3] == 1
                require(np.all(values["GI.Reference.Irradiance"][covered, :3] == 0), "Sealed-room seam energy")
                # Coincident solid boundaries can terminate on a single-sided
                # backface. That is a hit with zero future transport, never sky;
                # the fixed-depth contract does not require every path to truncate.
                row["depthTruncatedFraction"] = float(values["GI.Reference.PathDiagnostics"][mask, 1].mean())
            elif case["fixture"] in (2, 3):
                require(np.all(sun == 0) and np.max(np.abs(sky-np.pi)) < 1e-6 and
                        np.all(values["GI.Reference.PathDiagnostics"][mask, 1] == 0),
                        "Exterior roof must retain true sky misses at every depth")
            rows[mask_name] = row
        results[name] = dict(depth=case["depth"], masks=rows, referenceStatus="ReferenceUnconverged",
                             cameraSha256=hashlib.sha256(json.dumps(frame["camera"], sort_keys=True).encode()).hexdigest())
        print(f"Checked {name}", flush=True)

    backends = sorted({c["directory"].split("/")[0] for c in cases})
    for backend in backends:
        # Shared path prefixes: every first-query outcome array must be unchanged.
        base = f"{backend}/transport-planes-d1"
        for depth in (2, 3, 4):
            name = f"{backend}/transport-planes-d{depth}"
            require(np.array_equal(arrays[base]["GI.Reference.Diagnostics"], arrays[name]["GI.Reference.Diagnostics"]),
                    "Continuation perturbed the primary sample sequence")
            previous = results[f"{backend}/transport-planes-d{depth-1}"]["masks"]["floor"]
            row = results[name]["masks"]["floor"]
            require(row["maskSha256"] == previous["maskSha256"], "Adjacent-depth receiver masks differ")
            errors = []
            for component, order_key in (("sun", "sunOrders"), ("environment", "environmentOrders")):
                increment = np.asarray(row[component])-previous[component]
                expected = np.asarray(row[order_key])[-1]
                errors.append(relative_error(increment, expected))
            require(max(errors) < RELATIVE, f"Wrong finite-order increment at D={depth}: {errors}")
            row["incrementErrors"] = errors
        for batch in (7, 31):
            left = arrays[f"{backend}/transport-batch-1"]
            right = arrays[f"{backend}/transport-batch-{batch}"]
            require(all(np.array_equal(left[key], right[key]) for key in left),
                    "Batch partition changed the transport samples or sums")
        for depth in (1, 2, 3):
            name = f"{backend}/transport-receiver-d{depth}"
            if depth == 1:
                mask = selectors[name]["floor"]
                require(np.array_equal(arrays[name]["GI.Reference.Irradiance"][mask],
                                       arrays[base]["GI.Reference.Irradiance"][mask]), "D=1 local receiver response leaked into E")
        # Frozen F4's D=2 Sun increment is independently integrated over actual
        # coloured walls, neutral faces and sunlit floor/jamb trapezoids.
        name = f"{backend}/transport-f4-d2"
        previous_name = f"{backend}/transport-f4-d1"
        floor_mask = selectors[name]["floor"]
        p = arrays[name]["GI.Reference.Position"][floor_mask, :3].astype(float)
        low, high = f4_second_sun(p, 12), f4_second_sun(p, 20)
        for mask_name, mask in selectors[name].items():
            require(results[name]["masks"][mask_name]["maskSha256"] ==
                    results[previous_name]["masks"][mask_name]["maskSha256"] and
                    results[name]["cameraSha256"] == results[previous_name]["cameraSha256"],
                    "F4 adjacent-depth masks/cameras differ")
            selected = mask[floor_mask]
            expected = high[selected].mean(0)
            refined = relative_error(low[selected].mean(0), expected)
            require(refined < QUADRATURE, f"F4 quadrature unresolved: {refined}")
            increment = (arrays[name]["GI.Reference.SunIrradiance"][mask, :3].astype(float)-
                         arrays[previous_name]["GI.Reference.SunIrradiance"][mask, :3]).mean(0)
            error = relative_error(increment, expected)
            require(error < RELATIVE, f"F4 coloured second-order Sun: {mask_name}: {error}")
            results[name]["masks"][mask_name].update(secondSunOracle=expected.tolist(),
                secondSunIncrement=increment.tolist(), secondSunError=error, quadratureRelative=refined)
        red = results[name]["masks"]["red-adjacent"]["secondSunIncrement"]
        green = results[name]["masks"]["green-adjacent"]["secondSunIncrement"]
        require(red[0] > red[1] and green[1] > green[0] and min(red[:2]) > red[2],
                "Frozen F4 Sun colour does not follow the traversed wall")
        for case in cases:
            if case["directory"].startswith(backend+"/") and case.get("variant") in ("independent", "double"):
                base_name = case["directory"].rsplit("-", 1)[0]
                errors = []
                for mask_name, row in results[case["directory"]]["masks"].items():
                    base_row = results[base_name]["masks"][mask_name]
                    require(row["maskSha256"] == base_row["maskSha256"] and
                            results[case["directory"]]["cameraSha256"] == results[base_name]["cameraSha256"],
                            "Sampling qualification masks/cameras differ")
                    errors.extend(relative_error(row[c], base_row[c]) for c in ("sun", "environment"))
                require(max(errors) < RELATIVE, f"Fixed-seed/sample qualification: {case['directory']}: {errors}")
                results[case["directory"]]["samplingChanges"] = errors
    if len(backends) == 2:
        for name, result in results.items():
            other = name.replace("dx12/", "vulkan/", 1)
            if name.startswith("dx12/"):
                require(other in results, "Missing paired RHI transport observation")
                for mask_name, row in result["masks"].items():
                    peer = results[other]["masks"][mask_name]
                    require(max(relative_error(row[c], peer[c]) for c in ("sun", "environment")) < RELATIVE,
                            f"Cross-RHI transport disagreement: {name}/{mask_name}")
    report = dict(status="passed", observations=results,
                  rules=dict(relative=RELATIVE, cpuQuadrature=QUADRATURE, absolute=1e-6,
                             signal="unexposed linear RGB E, components and adjacent-depth increments",
                             batch="bit-identical complete arrays on each RHI",
                             convergence="not certified; ReferenceUnconverged for DDGI"))
    (root/"transport-results.json").write_text(json.dumps(report, indent=2)+"\n")
    return report


def self_test():
    positions = np.array([[0, 0, 0], [-.75, 0, -.5], [.75, 0, .5]])
    low = planes_oracle(positions, 4, order=40)
    high = planes_oracle(positions, 4, order=64)
    error = max(float(np.max(np.abs(a-b)/np.maximum(np.abs(b), 1e-6))) for a, b in zip(low, high))
    require(error < .002, "Plane quadrature refinement exceeds 0.2%")
    sun, sky = high
    require(np.all(sun > 0) and np.all(sky > 0), "Every finite order must carry energy")
    require(np.max(np.ptp(sky[0], axis=-1)) == 0, "Direct sky cannot inherit receiver albedo")
    require(np.all(sun[0, :, 0] > sun[0, :, 1]*10), "First Sun order uses the red wall")
    changed, _ = planes_oracle(positions, 3, floor_albedo=(.2, .5, .8), order=64)
    require(np.array_equal(changed[0], sun[0]), "Local primary albedo must not weight E")
    require(np.allclose(changed[1]/sun[1], [.2, .5, .8]), "Revisited floor material must weight the second Sun order")
    require(np.array_equal(sun[:2], planes_oracle(positions, 2, order=64)[0]), "Depths share identical finite-order prefixes")
    _, means = plane_order_means(positions.tobytes(), (1., 1., 1.), (.8, .05, .05))
    require(all(np.allclose(a, b.mean(1), rtol=1e-12, atol=1e-12) for a, b in zip(means, high)),
            "Averaged-kernel ROI integration differs from pointwise quadrature")
    f4_low, f4_high = f4_second_sun(positions, 12), f4_second_sun(positions, 20)
    f4_error = float(np.max(np.abs(f4_low-f4_high)/f4_high))
    require(f4_error < .002, "F4 second-order Sun quadrature exceeds 0.2%")
    require(f4_high[1, 0] > f4_high[1, 1] and f4_high[2, 1] > f4_high[2, 0],
            "Frozen F4 coloured transport must follow the adjacent wall")
    try:
        check_path_diagnostics(np.array([[[1., 0., 3., 1.]]]), np.ones((1, 1), dtype=bool), 3)
        raise AssertionError("A deep invalid path was accepted")
    except ValueError:
        pass
    return dict(status="passed", checks=11, planeQuadratureRelative=error, f4QuadratureRelative=f4_error,
                scope="finite-depth integration only; ReferenceUnconverged for DDGI")


def validate_transactions(root):
    results = {}
    for backend in (p for p in root.iterdir() if p.is_dir()):
        for name, depth in (("control", 3), ("reset", 2)):
            case = dict(directory=name, fixture=8, depth=depth, samples=64, batch_samples=7,
                        seed=61051, albedo=0, secondary_albedo=1, environment=0, view="Contract_Sun")
            read_case(backend, case, components=False)
        control = json.loads((backend/"control-status.json").read_text(encoding="utf-8-sig"))
        require(control["watch"]["report"]["production"] == "Passed", "Clean counter variant did not pass")
        pending = json.loads((backend/"pending-status.json").read_text(encoding="utf-8-sig"))
        require(any(g["name"] == "gi-reference" and g["state"] == "pending" and "D=4," in g["detail"]
                    for g in pending["frame"]["gates"]), "Cancellation did not exercise pending reference work")
        directory = backend/"injected"
        record = json.loads(next(directory.glob("*.resource-export.json")).read_text(encoding="utf-8-sig"))
        values = {t["address"]["resource"]: np.load(directory/t["array"], allow_pickle=False)[0]
                  for t in record["targets"]}
        require(all(np.isfinite(a).all() for a in values.values()), "Rejected operand escaped into stored RGB")
        pixel = (100, 160)
        require(values["GI.Reference.Irradiance"][pixel][3] == -3 and
                values["GI.Reference.PathDiagnostics"][pixel][0] > 0 and
                values["GI.Reference.Diagnostics"][pixel][3] == 0 and
                values["GI.Reference.Diagnostics"][pixel][1] > 0,
                "Deep injected error must fail despite valid first-query outcomes")
        status = json.loads((backend/"injected-status.json").read_text(encoding="utf-8-sig"))
        totals = status["watch"]["report"]["siteTotals"]
        require(any(s["site"] == "pass-gi-reference.gipathfinite.isfinite" and
                    s["nonFiniteReplacements"] > 0 for s in totals), "Deep invalidity lacks production counter evidence")
        covered = values["GI.Receiver.HitAlbedo"][..., 3] == 1
        try:
            check_path_diagnostics(values["GI.Reference.PathDiagnostics"], covered, 3)
            raise AssertionError("Numerical validator accepted an injected deep path")
        except ValueError:
            pass
        results[backend.name] = dict(cleanCounterVariant="passed", cancellation="passed", depthReset="passed",
                                     deepInvalid="rejected", primaryInvalidFraction=0)
    report = dict(status="passed", backends=results)
    (root/"transaction-results.json").write_text(json.dumps(report, indent=2)+"\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--transactions", type=Path)
    args = parser.parse_args()
    print(json.dumps(validate_transactions(args.transactions) if args.transactions else self_test()))
