"""Raster/atlas oracle for the own-probe white Lambertian sphere display."""
import json
from pathlib import Path

import numpy as np
from PIL import Image

from ValidateGIFieldSampling import bilinear
from ValidateGIProbeVisualization import load, oracle, require


def raster(summary, selected, full=False):
    """Independent FP64 triangle rasterization, perspective interpolation and D32 ownership."""
    view = summary["receiverView"]
    width, height = view["displayExtent"]
    matrix = np.asarray(view["viewProjection"], np.float64).reshape(4, 4)
    reversed_z = view["reversedDepth"]
    depth = np.full((height, width), 0.0 if reversed_z else 1.0)
    winners = np.full((height, width), -1, dtype=np.int32)
    normals = np.zeros((height, width, 3))
    positions = np.zeros_like(normals)
    interior = np.zeros((height, width), dtype=bool)
    radius = summary["spacing"] * summary["probeVisualization"]["settings"]["radiusInSpacings"]
    nx, _, nz = summary["counts"]
    uv = np.asarray([(x / 16, y / 8) for y in range(9) for x in range(17)])
    longitude, latitude = uv[:, 0] * 2 * np.pi, uv[:, 1] * np.pi
    unit = np.column_stack((np.sin(latitude) * np.cos(longitude), np.cos(latitude),
                            np.sin(latitude) * np.sin(longitude)))
    triangles = []
    for y in range(8):
        for x in range(16):
            a = y * 17 + x
            triangles.extend(((a, a + 17, a + 18), (a, a + 18, a + 1)))
    for probe, category in selected:
        require(category == 1, "Irradiance oracle fixture must select Active probes")
        coordinate = np.asarray((probe % nx, probe // (nx * nz), (probe // nx) % nz))
        center = np.asarray(summary["minimum"]) + coordinate * summary["spacing"]
        world = center + unit * radius
        clip = np.column_stack((world, np.ones(len(world)))) @ matrix
        ndc = clip[:, :3] / clip[:, 3, None]
        screen = (ndc[:, :2] * (1, -1) + 1) * (width / 2, height / 2)
        for indices in triangles:
            ix = np.asarray(indices)
            if np.any(clip[ix, 3] <= 0) or np.any(ndc[ix, 2] < 0) or np.any(ndc[ix, 2] > 1):
                continue
            triangle = screen[ix]
            lo = np.maximum(np.ceil(triangle.min(axis=0) - .5).astype(int), 0)
            hi = np.minimum(np.floor(triangle.max(axis=0) - .5).astype(int), (width - 1, height - 1))
            if np.any(lo > hi):
                continue
            a, b, c = triangle
            determinant = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if abs(determinant) < 1e-10:
                continue
            yy, xx = np.mgrid[lo[1]:hi[1] + 1, lo[0]:hi[0] + 1]
            points = np.stack((xx + .5, yy + .5), axis=-1)
            beta = ((points[..., 0] - a[0]) * (c[1] - a[1]) - (points[..., 1] - a[1]) * (c[0] - a[0])) / determinant
            gamma = ((b[0] - a[0]) * (points[..., 1] - a[1]) - (b[1] - a[1]) * (points[..., 0] - a[0])) / determinant
            barycentric = np.stack((1 - beta - gamma, beta, gamma), axis=-1)
            covered = np.all(barycentric >= 0, axis=-1)
            comparable = (barycentric @ ndc[ix, 2]).astype(np.float32)
            old = depth[yy, xx]
            wins = covered & ((comparable > old) if reversed_z else (comparable < old))
            if not wins.any():
                continue
            weights = barycentric / clip[ix, 3]
            weights /= weights.sum(axis=-1, keepdims=True)
            yi, xi = yy[wins], xx[wins]
            normal = weights[wins] @ unit[ix]
            normals[yi, xi] = normal / np.linalg.norm(normal, axis=-1, keepdims=True)
            positions[yi, xi] = weights[wins] @ world[ix]
            depth[yi, xi] = comparable[wins]
            winners[yi, xi] = probe
            interior[yi, xi] = np.min(barycentric[wins], axis=-1) > .08
    toward_camera = np.asarray(view["cameraPosition"]) - positions
    toward_camera /= np.maximum(np.linalg.norm(toward_camera, axis=-1, keepdims=True), 1e-10)
    facing = np.abs(np.sum(normals * toward_camera, axis=-1))
    # Stay away from the deliberately neutral silhouette and triangle raster edges.
    mask = (winners >= 0) & interior & (facing > .45)
    if full:
        return mask, winners, normals, depth
    return mask, winners[mask], normals[mask]


def display_response(response, ev):
    x = np.clip(response * np.exp2(ev), 0, 65000)
    linear = np.clip(x * (2.51 * x + .03) / (x * (2.43 * x + .59) + .14), 0, 1)
    srgb = np.where(linear < .0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - .055)
    return srgb * 255


def validate(root):
    results = []
    for backend in load(root / "backends.json"):
        previous = None
        for case in load(root / "irradiance-cases.json"):
            directory = root / backend / "irradiance" / case["id"]
            resource = load(next(directory.glob("*.resource-export.json")))
            summary = resource["frame"]["readiness"]["giField"]["summary"]
            display = summary["probeVisualization"]
            require(display["mode"] == ("irradiance" if case["mode"] else "classification") and
                    display["settings"]["previewEV"] == case["ev"], "Effective mode/EV metadata differs from the submitted request")
            arrays = {t["address"]["resource"]: np.load(directory / t["array"], allow_pickle=False) for t in resource["targets"]}
            expected_ids, counts = oracle(summary, arrays["GI.Field.Classification"])
            stats = arrays["GI.Probes.Counts"].reshape(-1)
            ids = arrays["GI.Probes.RenderedIDs"].reshape(2, -1).T
            require(np.array_equal(stats[2:9], counts) and np.array_equal(ids[:len(expected_ids)], expected_ids),
                    "Changing display mode/EV changed deterministic selection")
            require(len(expected_ids) > 0 and summary["irradianceInteriorWidth"] == case["width"], "Angular-profile fixture did not prepare")
            if case["id"] in ("plane-ev2", "plane-scene0", "plane-classification"):
                require(summary["field"] == previous["field"] and np.array_equal(expected_ids, previous["ids"]),
                        "Presentation controls replaced the field or changed its selection")
            if case["id"] == "plane-6":
                previous = dict(field=summary["field"], ids=expected_ids)
            record = dict(backend=backend, case=case["id"], width=case["width"], mode=display["mode"])
            if not case["mode"]:
                results.append(record)
                continue
            atlas = arrays["GI.Field.Irradiance"]
            require(atlas.shape[1:3] == (summary["counts"][2] * (case["width"] + 2), summary["counts"][0] * (case["width"] + 2)),
                    "Atlas dimensions do not match the angular profile")
            mask, probes, normals = raster(summary, expected_ids)
            require(mask.sum() >= 20, "Too few unambiguous sphere pixels for the irradiance oracle")
            nx, _, nz = summary["counts"]
            coordinates = np.column_stack((probes % nx, probes // (nx * nz), (probes // nx) % nz))
            response = bilinear(atlas, coordinates, normals, case["width"])[:, :3]
            expected = display_response(response, case["ev"])
            image = np.asarray(Image.open(next(directory.glob("*.p1.png")))).astype(np.float64)
            error = np.max(np.abs(image[mask, :3] - expected), axis=-1)
            require(np.quantile(error, .99) <= 3 and np.mean(error <= 2) >= .98,
                    f"Own-probe atlas/normal/units/exposure mapping differs: {case['id']} p99={np.quantile(error,.99):.3f}")
            if case["id"].startswith("directional-"):
                require(np.max(np.ptp(response, axis=0)) > .2 and np.max(np.ptp(response, axis=1)) > .1,
                        "Directional HDR fixture has no measurable angular/color variation")
            if case["id"].startswith("plane-"):
                upper = normals[:, 1] > .8
                require(upper.sum() > 20 and np.allclose(response[upper], 1.0, atol=.04),
                        "Unit white environment does not produce the expected white Lambertian response")
            if case["id"] == "black":
                require(np.max(response) == 0 and np.max(image[mask, :3]) == 0, "A valid black Active probe was tinted or reclassified")
                require(np.any(np.all(image[..., :3] == 89, axis=-1)), "Valid black probes lost their neutral silhouette")
            record.update(pixels=int(mask.sum()), p99ByteError=float(np.quantile(error, .99)),
                          responseMinimum=float(response.min()), responseMaximum=float(response.max()),
                          foldSeamPixels=int(np.sum(np.abs(normals[:, 2]) < .15)))
            results.append(record)
    return results
